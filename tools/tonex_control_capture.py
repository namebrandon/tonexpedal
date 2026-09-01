#!/usr/bin/env python3
"""Capture privacy-safe TONEX CDC fingerprints while physical controls are used."""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path
from typing import Callable, Optional, Sequence

import tonex_hardware_probe as protocol


DEFAULT_SECONDS = 20.0
POLL_SECONDS = 0.25


def capture_controls(
    port: str,
    seconds: float,
    label: str,
    event_handler: Optional[Callable[[dict], None]] = None,
    ready_handler: Optional[Callable[[], None]] = None,
    raw_event_handler: Optional[Callable[[dict], None]] = None,
) -> dict:
    events = []
    with protocol.PosixSerialTransport(port, timeout=3.0) as transport:
        hello = transport.exchange(protocol.HELLO_CMD, "hello response")
        time.sleep(0.2)
        state = transport.exchange(protocol.REQUEST_STATE_CMD, "state response")
        if ready_handler is not None:
            ready_handler()

        started_at = time.monotonic()
        deadline = started_at + seconds
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            for payload in transport.read_frames_for(min(POLL_SECONDS, remaining)):
                summary = protocol.summarize_unsolicited_payload(payload)
                summary["elapsed_ms"] = round((time.monotonic() - started_at) * 1000)
                events.append(summary)
                if raw_event_handler is not None:
                    raw_event_handler({
                        "elapsed_ms": summary["elapsed_ms"],
                        "payload_hex": payload.hex(" "),
                    })
                if event_handler is not None:
                    event_handler(summary)
        crc_errors = transport.decoder.protocol_error_count

    return {
        "status": "ok" if crc_errors == 0 else "failed",
        "read_only": True,
        "privacy_safe": True,
        "port": port,
        "baud_rate": 115200,
        "label": label,
        "capture_seconds": seconds,
        "hello_payload_bytes": len(hello),
        "state_payload_bytes": len(state),
        "event_count": len(events),
        "crc_errors": crc_errors,
        "events": events,
    }


def positive_float(value: str) -> float:
    parsed = float(value)
    if parsed <= 0 or parsed > 300:
        raise argparse.ArgumentTypeError("seconds must be greater than 0 and at most 300")
    return parsed


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Capture timestamped, privacy-safe CDC event fingerprints while operating "
            "physical controls on a connected TONEX Pedal."
        )
    )
    parser.add_argument("--port", help="USB serial device; auto-detected when exactly one is present")
    parser.add_argument("--seconds", type=positive_float, default=DEFAULT_SECONDS)
    parser.add_argument("--label", default="physical_control", help="short label for this capture phase")
    parser.add_argument(
        "--raw-output",
        type=Path,
        help=(
            "opt-in local JSON export of complete unsolicited payloads; these can contain "
            "preset data and must not be committed"
        ),
    )
    parser.add_argument("--json", action="store_true", help="emit one JSON document after capture")
    return parser.parse_args(argv)


def event_line(number: int, event: dict) -> str:
    fields = [
        f"event={number}",
        f"elapsed_ms={event['elapsed_ms']}",
        f"type={event['event_type']}",
        f"payload_bytes={event['payload_bytes']}",
        f"fingerprint={event['payload_fingerprint']}",
    ]
    if "preset_index" in event:
        fields.append(f"preset_index={event['preset_index']}")
    if "payload_hex" in event:
        fields.append(f"payload_hex={event['payload_hex']}")
    else:
        fields.append(f"payload_prefix_hex={event['payload_prefix_hex']}")
    return " ".join(fields)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    try:
        port = args.port or protocol.discover_port()
        event_number = 0
        raw_events = []

        def ready() -> None:
            if not args.json:
                print(
                    f"capture_ready=true label={args.label} port={port} "
                    f"seconds={args.seconds:g} read_only=true privacy_safe=true",
                    flush=True,
                )

        def show_event(event: dict) -> None:
            nonlocal event_number
            event_number += 1
            if not args.json:
                print(event_line(event_number, event), flush=True)

        def retain_raw_event(event: dict) -> None:
            raw_events.append(event)

        result = capture_controls(
            port=port,
            seconds=args.seconds,
            label=args.label,
            event_handler=show_event,
            ready_handler=ready,
            raw_event_handler=retain_raw_event if args.raw_output else None,
        )
        if args.raw_output:
            args.raw_output.parent.mkdir(parents=True, exist_ok=True)
            args.raw_output.write_text(
                json.dumps(
                    {
                        "schema_version": 1,
                        "read_only": True,
                        "label": args.label,
                        "capture_seconds": args.seconds,
                        "events": raw_events,
                    },
                    indent=2,
                ) + "\n",
                encoding="utf-8",
            )
    except (OSError, protocol.ProbeError, ValueError) as exc:
        if args.json:
            print(json.dumps({"status": "error", "message": str(exc)}))
        else:
            print(f"error: {exc}", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps(result, sort_keys=True))
    else:
        print(
            f"result={result['status']} label={result['label']} events={result['event_count']} "
            f"crc_errors={result['crc_errors']} port_closed=true"
        )
    return 0 if result["status"] == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())
