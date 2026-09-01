#!/usr/bin/env python3
"""Capture a privacy-safe TONEX USB interface inventory from macOS IORegistry."""

from __future__ import annotations

import json
import plistlib
import subprocess
import sys
from typing import Iterator


TONEX_USB_VID = 0x1963
TONEX_USB_PID = 0x0068


class InventoryError(RuntimeError):
    pass


def walk_registry(entry) -> Iterator[dict]:
    if isinstance(entry, list):
        for child in entry:
            yield from walk_registry(child)
        return
    if not isinstance(entry, dict):
        return
    yield entry
    yield from walk_registry(entry.get("IORegistryEntryChildren", []))


def infer_role(interface: dict) -> str:
    interface_class = interface.get("bInterfaceClass")
    interface_subclass = interface.get("bInterfaceSubClass")
    name = str(interface.get("kUSBString") or interface.get("IORegistryEntryName") or "")
    if interface_class == 0x02:
        return "cdc_control"
    if interface_class == 0x0A:
        return "cdc_data"
    if interface_class == 0x01 and interface_subclass == 0x03:
        return "midi_streaming"
    if interface_class == 0x01 and interface_subclass == 0x01:
        return "midi_control" if "MIDI" in name.upper() else "audio_control"
    if interface_class == 0x01 and interface_subclass == 0x02:
        return "audio_streaming"
    return "unknown"


def extract_inventory(registry_root) -> dict:
    device = next(
        (
            entry
            for entry in walk_registry(registry_root)
            if entry.get("IOObjectClass") == "IOUSBHostDevice"
            and entry.get("idVendor") == TONEX_USB_VID
            and entry.get("idProduct") == TONEX_USB_PID
        ),
        None,
    )
    if device is None:
        raise InventoryError("A full-size TONEX Pedal was not found in IORegistry")

    interfaces = []
    for entry in walk_registry(device.get("IORegistryEntryChildren", [])):
        if entry.get("IOObjectClass") != "IOUSBHostInterface":
            continue
        interfaces.append(
            {
                "number": entry.get("bInterfaceNumber"),
                "alternate_setting": entry.get("bAlternateSetting"),
                "class": entry.get("bInterfaceClass"),
                "subclass": entry.get("bInterfaceSubClass"),
                "protocol": entry.get("bInterfaceProtocol"),
                "endpoint_count": entry.get("bNumEndpoints"),
                "role": infer_role(entry),
                "name": entry.get("kUSBString") or None,
            }
        )
    interfaces.sort(key=lambda interface: (interface["number"], interface["alternate_setting"]))

    return {
        "schema_version": 1,
        "source": "macos_ioreg",
        "privacy": "serial, location, session, and raw descriptor signature omitted",
        "device": {
            "vendor_id": device.get("idVendor"),
            "product_id": device.get("idProduct"),
            "device_class": device.get("bDeviceClass"),
            "device_subclass": device.get("bDeviceSubClass"),
            "device_protocol": device.get("bDeviceProtocol"),
            "usb_speed": device.get("USBSpeed"),
            "link_speed_bps": device.get("UsbLinkSpeed"),
        },
        "interfaces": interfaces,
        "endpoint_details_available": False,
        "endpoint_note": "IORegistry reports endpoint counts but not addresses/types; capture those from ESP32 descriptor logs.",
    }


def capture_inventory() -> dict:
    if sys.platform != "darwin":
        raise InventoryError("USB inventory capture currently requires macOS")
    try:
        completed = subprocess.run(
            ["ioreg", "-p", "IOService", "-l", "-a"],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        raise InventoryError(f"Could not read IORegistry: {exc}") from exc
    try:
        registry = plistlib.loads(completed.stdout)
    except plistlib.InvalidFileException as exc:
        raise InventoryError("IORegistry returned an invalid property list") from exc
    return extract_inventory(registry)


def main() -> int:
    try:
        inventory = capture_inventory()
    except InventoryError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    print(json.dumps(inventory, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
