#!/usr/bin/env python3
"""Read-only TONEX Pedal CDC protocol probe for macOS and Linux."""

from __future__ import annotations

import argparse
import copy
import glob
import json
import os
import select
import struct
import sys
import time
from collections import Counter, deque
from dataclasses import asdict, dataclass
from typing import Callable, Iterable, Optional, Sequence


HELLO_CMD = bytes.fromhex("b9 03 00 82 04 00 80 10 01 b9 02 02 10")
REQUEST_STATE_CMD = bytes.fromhex("b9 03 00 82 06 00 80 10 03 b9 02 81 01 02 10")
NAME_MARKER = bytes.fromhex("b9 04 b9 02 bc 21")
PARAM_MARKER = bytes.fromhex("ba 03 ba 29")
AMP_ENABLE_INDEX = 17
CAB_TYPE_INDEX = 23
FLOAT_SIZE = 5
TOTAL_PRESETS = 150
DEFAULT_PRESETS = (0, 127, 128, 149)
MAX_FRAME_BYTES = 16384
PRESET_RESPONSE_PREFIX = bytes.fromhex("04 10 b9 03 01")
ACTIVE_PRESET_EVENT_PREFIX = bytes.fromhex("04 02 b9 03 00")


class ProbeError(RuntimeError):
    """Raised when the hardware or protocol probe cannot continue safely."""


class ProtocolError(ProbeError):
    """Raised when a pedal response does not match the expected protocol."""


@dataclass
class PresetResult:
    index: int
    payload_bytes: int
    name_present: bool
    amp: bool
    cab: bool
    cab_type: str
    name: Optional[str] = None


def calculate_crc(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc >> 1) ^ 0x8408) if crc & 1 else crc >> 1
    return (~crc) & 0xFFFF


def build_frame(payload: bytes) -> bytes:
    body = payload + calculate_crc(payload).to_bytes(2, "little")
    framed = bytearray([0x7E])
    for byte in body:
        if byte in (0x7D, 0x7E):
            framed.extend((0x7D, byte ^ 0x20))
        else:
            framed.append(byte)
    framed.append(0x7E)
    return bytes(framed)


def deframe(frame: bytes) -> bytes:
    if len(frame) < 4 or frame[0] != 0x7E or frame[-1] != 0x7E:
        raise ProtocolError("Invalid HDLC frame delimiters")

    body = bytearray()
    escaped = False
    for byte in frame[1:-1]:
        if escaped:
            body.append(byte ^ 0x20)
            escaped = False
        elif byte == 0x7D:
            escaped = True
        else:
            body.append(byte)

    if escaped or len(body) < 2:
        raise ProtocolError("Truncated HDLC frame")

    payload = bytes(body[:-2])
    received_crc = int.from_bytes(body[-2:], "little")
    expected_crc = calculate_crc(payload)
    if received_crc != expected_crc:
        raise ProtocolError(
            f"HDLC CRC mismatch: received 0x{received_crc:04x}, expected 0x{expected_crc:04x}"
        )
    return payload


class HdlcStreamDecoder:
    """Recover complete HDLC payloads from arbitrarily chunked serial input."""

    def __init__(self, max_frame_bytes: int = MAX_FRAME_BYTES):
        self.max_frame_bytes = max_frame_bytes
        self.pending = bytearray()
        self.last_protocol_error: Optional[ProtocolError] = None
        self.protocol_error_count = 0

    def feed(self, chunk: bytes) -> list[bytes]:
        payloads = []
        for byte in chunk:
            if byte == 0x7E:
                if len(self.pending) > 1:
                    self.pending.append(byte)
                    try:
                        payloads.append(deframe(bytes(self.pending)))
                    except ProtocolError as exc:
                        self.last_protocol_error = exc
                        self.protocol_error_count += 1
                self.pending = bytearray([0x7E])
            elif self.pending:
                self.pending.append(byte)
                if len(self.pending) > self.max_frame_bytes:
                    self.pending.clear()
        return payloads


def create_preset_request(index: int) -> bytes:
    if not 0 <= index < TOTAL_PRESETS:
        raise ValueError(f"Preset index must be between 0 and {TOTAL_PRESETS - 1}")
    request = bytearray.fromhex("b9 03 81 00 02 82 06 00 80 10 03 b9 04 10 01")
    if index >= 128:
        request.append(0x80)
    request.extend((index, 0x00))
    return bytes(request)


def parse_preset_index(payload: bytes) -> int:
    prefix = bytes.fromhex("b9 03 81 04 02 81")
    body_prefixes = {
        PRESET_RESPONSE_PREFIX,
        ACTIVE_PRESET_EVENT_PREFIX,
    }
    if len(payload) < 14 or not payload.startswith(prefix):
        raise ProtocolError("Preset response has an invalid header")
    if payload[7:12] not in body_prefixes:
        raise ProtocolError("Preset response has an invalid body prefix")

    if payload[12] == 0x80:
        index = payload[13]
        expected_name_offset = 14
        if index < 128:
            raise ProtocolError(f"Preset response has non-canonical extended index {index}")
    else:
        index = payload[12]
        expected_name_offset = 13
    if not 0 <= index < TOTAL_PRESETS:
        raise ProtocolError(f"Preset response has out-of-range index {index}")
    if payload[expected_name_offset : expected_name_offset + len(NAME_MARKER)] != NAME_MARKER:
        raise ProtocolError("Preset response index is not followed by the name marker")
    return index


def parse_preset_response_index(payload: bytes) -> int:
    if len(payload) < 12 or payload[7:12] != PRESET_RESPONSE_PREFIX:
        raise ProtocolError("Payload is not a solicited preset response")
    return parse_preset_index(payload)


def parse_active_preset_event_index(payload: bytes) -> int:
    if len(payload) < 12 or payload[7:12] != ACTIVE_PRESET_EVENT_PREFIX:
        raise ProtocolError("Payload is not an active-preset event")
    return parse_preset_index(payload)


def parse_preset_response(index: int, payload: bytes, include_name: bool = False) -> PresetResult:
    response_index = parse_preset_response_index(payload)
    if response_index != index:
        raise ProtocolError(
            f"Preset response index {response_index} does not match request index {index}"
        )
    name_marker_offset = payload.find(NAME_MARKER)
    if name_marker_offset < 0:
        raise ProtocolError(f"Preset {index} response is missing the name marker")

    name_offset = name_marker_offset + len(NAME_MARKER)
    name_bytes = payload[name_offset : name_offset + 32]
    if len(name_bytes) != 32:
        raise ProtocolError(f"Preset {index} response has a truncated name field")
    name = name_bytes.split(b"\0", 1)[0].decode("utf-8", errors="replace").rstrip(" \r\n")
    if not name:
        raise ProtocolError(f"Preset {index} response has an empty name field")

    parameter_marker_offset = payload.find(PARAM_MARKER)
    if parameter_marker_offset < 0:
        raise ProtocolError(f"Preset {index} response is missing the parameter marker")
    parameter_offset = parameter_marker_offset + len(PARAM_MARKER)

    def read_tagged_float(parameter_index: int, label: str) -> float:
        offset = parameter_offset + parameter_index * FLOAT_SIZE
        encoded = payload[offset : offset + FLOAT_SIZE]
        if len(encoded) != FLOAT_SIZE or encoded[0] != 0x88:
            raise ProtocolError(f"Preset {index} response has an invalid {label} field")
        return struct.unpack("<f", encoded[1:])[0]

    amp_value = read_tagged_float(AMP_ENABLE_INDEX, "AMP")
    cab_value = read_tagged_float(CAB_TYPE_INDEX, "CAB")
    cab_types = {0.0: "tone_model", 1.0: "vir", 2.0: "disabled"}
    if cab_value not in cab_types:
        raise ProtocolError(f"Preset {index} response has unknown CAB type {cab_value}")
    return PresetResult(
        index=index,
        payload_bytes=len(payload),
        name_present=True,
        amp=amp_value > 0.5,
        cab=cab_value != 2.0,
        cab_type=cab_types[cab_value],
        name=name if include_name else None,
    )


def discover_port() -> str:
    candidates = sorted(
        set(
            glob.glob("/dev/cu.usbmodem*")
            + glob.glob("/dev/ttyACM*")
            + glob.glob("/dev/ttyUSB*")
        )
    )
    if not candidates:
        raise ProbeError("No USB serial device found; pass --port explicitly")
    if len(candidates) > 1:
        joined = ", ".join(candidates)
        raise ProbeError(f"Multiple USB serial devices found ({joined}); pass --port explicitly")
    return candidates[0]


class PosixSerialTransport:
    def __init__(self, port: str, timeout: float):
        self.port = port
        self.timeout = timeout
        self.fd: Optional[int] = None
        self.original_attributes = None
        self.decoder = HdlcStreamDecoder()
        self.decoded_payloads = deque()

    def __enter__(self) -> "PosixSerialTransport":
        if os.name != "posix":
            raise ProbeError("The command-line probe currently supports macOS and Linux")

        import termios

        try:
            self.fd = os.open(self.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        except OSError as exc:
            raise ProbeError(f"Could not open {self.port}: {exc}") from exc

        try:
            attributes = termios.tcgetattr(self.fd)
            self.original_attributes = copy.deepcopy(attributes)
            attributes[0] = 0
            attributes[1] = 0
            attributes[2] &= ~(termios.PARENB | termios.CSTOPB | termios.CSIZE)
            if hasattr(termios, "CRTSCTS"):
                attributes[2] &= ~termios.CRTSCTS
            attributes[2] |= termios.CS8 | termios.CLOCAL | termios.CREAD
            attributes[3] = 0
            attributes[4] = termios.B115200
            attributes[5] = termios.B115200
            attributes[6][termios.VMIN] = 0
            attributes[6][termios.VTIME] = 0
            termios.tcsetattr(self.fd, termios.TCSANOW, attributes)
            termios.tcflush(self.fd, termios.TCIOFLUSH)
            self.decoder = HdlcStreamDecoder()
            self.decoded_payloads.clear()
            time.sleep(0.5)
        except Exception:
            os.close(self.fd)
            self.fd = None
            raise
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> None:
        if self.fd is None:
            return
        import termios

        if self.original_attributes is not None:
            try:
                termios.tcsetattr(self.fd, termios.TCSANOW, self.original_attributes)
            except termios.error:
                pass
        os.close(self.fd)
        self.fd = None

    def exchange(
        self,
        payload: bytes,
        label: str,
        response_matcher: Optional[Callable[[bytes], bool]] = None,
        active_event_handler: Optional[Callable[[int], None]] = None,
    ) -> bytes:
        if self.fd is None:
            raise ProbeError("Serial transport is not open")

        import termios

        try:
            os.write(self.fd, build_frame(payload))
            termios.tcdrain(self.fd)
        except OSError as exc:
            raise ProbeError(f"Could not send {label}: {exc}") from exc
        return self._read_matching_frame(label, response_matcher, active_event_handler)

    def _read_matching_frame(
        self,
        label: str,
        response_matcher: Optional[Callable[[bytes], bool]],
        active_event_handler: Optional[Callable[[int], None]],
    ) -> bytes:
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline:
            remaining = max(0.0, deadline - time.monotonic())
            candidate = self._read_frame(label, remaining)
            if len(candidate) >= 12 and candidate[7:12] == ACTIVE_PRESET_EVENT_PREFIX:
                active_index = parse_active_preset_event_index(candidate)
                if active_event_handler is not None:
                    active_event_handler(active_index)
                continue
            if response_matcher is not None and not response_matcher(candidate):
                raise ProtocolError(f"Received an unexpected response to {label}")
            return candidate
        raise ProbeError(f"Timed out waiting for {label}")

    def _read_frame(self, label: str, timeout: Optional[float] = None) -> bytes:
        if self.fd is None:
            raise ProbeError("Serial transport is not open")

        if self.decoded_payloads:
            return self.decoded_payloads.popleft()

        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        while time.monotonic() < deadline:
            remaining = max(0.0, deadline - time.monotonic())
            readable, _, _ = select.select([self.fd], [], [], remaining)
            if not readable:
                break
            try:
                chunk = os.read(self.fd, 4096)
            except BlockingIOError:
                continue
            except OSError as exc:
                raise ProbeError(f"Could not read {label}: {exc}") from exc

            self.decoded_payloads.extend(self.decoder.feed(chunk))
            if self.decoded_payloads:
                return self.decoded_payloads.popleft()

        if self.decoder.last_protocol_error is not None:
            raise ProtocolError(
                f"No valid response to {label}: {self.decoder.last_protocol_error}"
            )
        raise ProbeError(f"Timed out waiting for {label}")

    def read_frames_for(self, duration: float) -> list[bytes]:
        """Collect unsolicited frames without writing another command."""
        if self.fd is None:
            raise ProbeError("Serial transport is not open")

        payloads = list(self.decoded_payloads)
        self.decoded_payloads.clear()
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            remaining = max(0.0, deadline - time.monotonic())
            readable, _, _ = select.select([self.fd], [], [], remaining)
            if not readable:
                break
            try:
                chunk = os.read(self.fd, 4096)
            except BlockingIOError:
                continue
            except OSError as exc:
                raise ProbeError(f"Could not read unsolicited response: {exc}") from exc
            payloads.extend(self.decoder.feed(chunk))
        return payloads


def run_probe(
    port: str,
    preset_indices: Iterable[int],
    timeout: float,
    include_names: bool,
) -> dict:
    results = []
    active_preset_events = []
    with PosixSerialTransport(port, timeout) as transport:
        hello = transport.exchange(
            HELLO_CMD,
            "hello response",
            active_event_handler=active_preset_events.append,
        )
        time.sleep(0.2)
        state = transport.exchange(
            REQUEST_STATE_CMD,
            "state response",
            active_event_handler=active_preset_events.append,
        )
        time.sleep(0.2)

        for index in preset_indices:
            def matches_requested_preset(candidate: bytes, expected: int = index) -> bool:
                response_index = parse_preset_response_index(candidate)
                if response_index != expected:
                    raise ProtocolError(
                        f"Preset response index {response_index} does not match request index {expected}"
                    )
                return True

            payload = transport.exchange(
                create_preset_request(index),
                f"preset {index}",
                response_matcher=matches_requested_preset,
                active_event_handler=active_preset_events.append,
            )
            results.append(asdict(parse_preset_response(index, payload, include_names)))
            time.sleep(0.04)

    if not include_names:
        for result in results:
            result.pop("name", None)
    return {
        "status": "ok",
        "read_only": True,
        "port": port,
        "baud_rate": 115200,
        "hello_payload_bytes": len(hello),
        "state_payload_bytes": len(state),
        "presets_checked": len(results),
        "presets": results,
        "active_preset_events": active_preset_events,
    }


def summarize_unsolicited_payload(payload: bytes) -> dict:
    summary = {"payload_bytes": len(payload)}
    try:
        summary["preset_index"] = parse_preset_index(payload)
    except ProtocolError:
        pass
    if len(payload) <= 64:
        summary["payload_hex"] = payload.hex(" ")
    else:
        summary["payload_prefix_hex"] = payload[:12].hex(" ")
    return summary


def run_passive_listener(port: str, timeout: float, listen_seconds: float) -> dict:
    with PosixSerialTransport(port, timeout) as transport:
        hello = transport.exchange(HELLO_CMD, "hello response")
        time.sleep(0.2)
        state = transport.exchange(REQUEST_STATE_CMD, "state response")
        events = transport.read_frames_for(listen_seconds)
    return {
        "status": "ok",
        "read_only": True,
        "port": port,
        "baud_rate": 115200,
        "hello_payload_bytes": len(hello),
        "state_payload_bytes": len(state),
        "listen_seconds": listen_seconds,
        "event_count": len(events),
        "events": [summarize_unsolicited_payload(payload) for payload in events],
    }


def summarize_cycle(cycle: int, duration_seconds: float, result: dict) -> dict:
    presets = result["presets"]
    return {
        "cycle": cycle,
        "duration_ms": round(duration_seconds * 1000),
        "hello_payload_bytes": result["hello_payload_bytes"],
        "state_payload_bytes": result["state_payload_bytes"],
        "presets_checked": result["presets_checked"],
        "payload_byte_counts": dict(
            sorted(Counter(preset["payload_bytes"] for preset in presets).items())
        ),
        "amp_enabled": sum(1 for preset in presets if preset["amp"]),
        "cab_enabled": sum(1 for preset in presets if preset["cab"]),
        "cab_type_counts": dict(sorted(Counter(preset["cab_type"] for preset in presets).items())),
        "active_preset_events": result.get("active_preset_events", []),
    }


def run_repeated_probe(
    port: str,
    preset_indices: Iterable[int],
    timeout: float,
    repeat: int,
) -> dict:
    indices = tuple(preset_indices)
    cycles = []
    started_at = time.monotonic()
    for cycle in range(1, repeat + 1):
        cycle_started_at = time.monotonic()
        result = run_probe(port, indices, timeout, include_names=False)
        cycles.append(summarize_cycle(cycle, time.monotonic() - cycle_started_at, result))

    durations = [cycle["duration_ms"] for cycle in cycles]
    return {
        "status": "ok",
        "read_only": True,
        "port": port,
        "baud_rate": 115200,
        "cycles_completed": len(cycles),
        "presets_checked": sum(cycle["presets_checked"] for cycle in cycles),
        "total_duration_ms": round((time.monotonic() - started_at) * 1000),
        "cycle_duration_ms": {
            "min": min(durations),
            "max": max(durations),
            "average": round(sum(durations) / len(durations)),
        },
        "cycles": cycles,
    }


def positive_float(value: str) -> float:
    parsed = float(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("timeout must be greater than zero")
    return parsed


def positive_int(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("value must be greater than zero")
    return parsed


def preset_index(value: str) -> int:
    parsed = int(value)
    if not 0 <= parsed < TOTAL_PRESETS:
        raise argparse.ArgumentTypeError(f"preset must be between 0 and {TOTAL_PRESETS - 1}")
    return parsed


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run read-only CDC protocol checks against a connected TONEX Pedal."
    )
    parser.add_argument("--port", help="USB serial device; auto-detected when exactly one is present")
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument(
        "--preset",
        action="append",
        type=preset_index,
        dest="presets",
        help="preset index to validate; repeat for multiple presets",
    )
    selection.add_argument(
        "--all",
        action="store_true",
        help=f"validate all {TOTAL_PRESETS} presets",
    )
    selection.add_argument(
        "--listen-seconds",
        type=positive_float,
        help="handshake, then passively collect unsolicited CDC frames",
    )
    parser.add_argument("--timeout", type=positive_float, default=3.0, help="response timeout in seconds")
    parser.add_argument(
        "--repeat",
        type=positive_int,
        default=1,
        help="open, probe, and close the pedal this many times",
    )
    parser.add_argument("--show-names", action="store_true", help="include preset names in output")
    parser.add_argument("--json", action="store_true", help="emit JSON instead of human-readable output")
    return parser.parse_args(argv)


def print_human(result: dict) -> None:
    print(f"port={result['port']} baud={result['baud_rate']} read_only=true")
    print(f"hello=ok payload_bytes={result['hello_payload_bytes']}")
    print(f"state=ok payload_bytes={result['state_payload_bytes']}")
    for preset in result["presets"]:
        fields = [
            f"preset={preset['index']}",
            f"payload_bytes={preset['payload_bytes']}",
            "name_present=true",
            f"amp={str(preset['amp']).lower()}",
            f"cab={str(preset['cab']).lower()}",
            f"cab_type={preset['cab_type']}",
        ]
        if "name" in preset:
            fields.append(f"name={json.dumps(preset['name'], ensure_ascii=False)}")
        print(" ".join(fields))
    if result.get("active_preset_events"):
        print(f"active_preset_events={result['active_preset_events']}")
    print(f"result=ok presets_checked={result['presets_checked']} port_closed=true")


def print_soak_human(result: dict) -> None:
    print(f"port={result['port']} baud={result['baud_rate']} read_only=true")
    for cycle in result["cycles"]:
        print(
            "cycle={cycle} result=ok duration_ms={duration_ms} presets_checked={presets_checked} "
            "payload_byte_counts={payload_byte_counts} cab_type_counts={cab_type_counts} "
            "active_preset_events={active_preset_events} "
            "port_closed=true".format(**cycle)
        )
    print(
        f"result=ok cycles_completed={result['cycles_completed']} "
        f"presets_checked={result['presets_checked']} total_duration_ms={result['total_duration_ms']}"
    )


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    try:
        port = args.port or discover_port()
        indices = range(TOTAL_PRESETS) if args.all else (args.presets or DEFAULT_PRESETS)
        if args.listen_seconds is not None:
            if args.repeat != 1 or args.show_names:
                raise ProbeError("--listen-seconds cannot be combined with --repeat or --show-names")
            result = run_passive_listener(port, args.timeout, args.listen_seconds)
        elif args.repeat == 1:
            result = run_probe(port, indices, args.timeout, args.show_names)
        else:
            if args.show_names:
                raise ProbeError("--show-names cannot be combined with --repeat")
            result = run_repeated_probe(port, indices, args.timeout, args.repeat)
    except (OSError, ProbeError, ValueError) as exc:
        if args.json:
            print(json.dumps({"status": "error", "message": str(exc)}))
        else:
            print(f"error: {exc}", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    elif args.listen_seconds is not None:
        print(f"port={result['port']} baud={result['baud_rate']} read_only=true")
        print(f"hello=ok payload_bytes={result['hello_payload_bytes']}")
        print(f"state=ok payload_bytes={result['state_payload_bytes']}")
        for index, event in enumerate(result["events"], start=1):
            fields = [f"event={index}", f"payload_bytes={event['payload_bytes']}"]
            if "preset_index" in event:
                fields.append(f"preset_index={event['preset_index']}")
            if "payload_hex" in event:
                fields.append(f"payload_hex={event['payload_hex']}")
            else:
                fields.append(f"payload_prefix_hex={event['payload_prefix_hex']}")
            print(" ".join(fields))
        print(f"result=ok events={result['event_count']} port_closed=true")
    elif args.repeat > 1:
        print_soak_human(result)
    else:
        print_human(result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
