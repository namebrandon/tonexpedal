#!/usr/bin/env python3
"""Poll and fingerprint the TONEX State response while controls are operated."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import time
from typing import Callable, Optional, Sequence

import tonex_hardware_probe as protocol


DEFAULT_SECONDS = 30.0
DEFAULT_INTERVAL = 0.5


def bounded_float(value: str, minimum: float, maximum: float, label: str) -> float:
    parsed = float(value)
    if not minimum <= parsed <= maximum:
        raise argparse.ArgumentTypeError(
            f"{label} must be between {minimum:g} and {maximum:g}"
        )
    return parsed


def state_sample(payload: bytes, elapsed_seconds: float) -> dict:
    return {
        "elapsed_ms": round(elapsed_seconds * 1000),
        "state_code": protocol.parse_state_response(payload),
        "payload_bytes": len(payload),
        "payload_fingerprint": hashlib.sha256(payload).hexdigest()[:16],
        "payload_hex": payload.hex(" "),
    }


def watch_state(
    port: str,
    seconds: float,
    interval: float,
    sample_handler: Optional[Callable[[dict], None]] = None,
    ready_handler: Optional[Callable[[], None]] = None,
) -> dict:
    samples = []
    active_preset_events = []
    started_at = time.monotonic()
    deadline = started_at + seconds
    with protocol.PosixSerialTransport(port, timeout=3.0) as transport:
        sample_number = 0
        while time.monotonic() < deadline:
            payload = transport.exchange(
                protocol.REQUEST_STATE_CMD,
                "state response",
                response_matcher=lambda candidate: protocol.parse_state_response(candidate) >= 0,
                active_event_handler=active_preset_events.append,
            )
            sample = state_sample(payload, time.monotonic() - started_at)
            samples.append(sample)
            sample_number += 1
            if sample_handler is not None:
                sample_handler(sample)
            if sample_number == 1 and ready_handler is not None:
                ready_handler()
            remaining = deadline - time.monotonic()
            if remaining > 0:
                time.sleep(min(interval, remaining))
        crc_errors = transport.decoder.protocol_error_count

    distinct_codes = sorted({sample["state_code"] for sample in samples})
    distinct_fingerprints = sorted({sample["payload_fingerprint"] for sample in samples})
    return {
        "status": "ok" if crc_errors == 0 else "failed",
        "read_only": True,
        "port": port,
        "baud_rate": 115200,
        "capture_seconds": seconds,
        "interval_seconds": interval,
        "sample_count": len(samples),
        "distinct_state_codes": distinct_codes,
        "distinct_payload_fingerprints": distinct_fingerprints,
        "active_preset_events": active_preset_events,
        "crc_errors": crc_errors,
        "samples": samples,
    }


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Poll the read-only TONEX State command while toggling a physical control. "
            "Responses are short and printed in full for protocol comparison."
        )
    )
    parser.add_argument("--port", help="USB serial device; auto-detected when exactly one is present")
    parser.add_argument(
        "--seconds",
        type=lambda value: bounded_float(value, 1.0, 300.0, "seconds"),
        default=DEFAULT_SECONDS,
    )
    parser.add_argument(
        "--interval",
        type=lambda value: bounded_float(value, 0.1, 10.0, "interval"),
        default=DEFAULT_INTERVAL,
    )
    parser.add_argument("--json", action="store_true", help="emit one JSON document after capture")
    return parser.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    try:
        port = args.port or protocol.discover_port()
        sample_number = 0

        def show_sample(sample: dict) -> None:
            nonlocal sample_number
            sample_number += 1
            if not args.json:
                print(
                    f"sample={sample_number} elapsed_ms={sample['elapsed_ms']} "
                    f"state_code={sample['state_code']} fingerprint={sample['payload_fingerprint']} "
                    f"payload_hex={sample['payload_hex']}",
                    flush=True,
                )

        def ready() -> None:
            if not args.json:
                print(
                    f"watch_ready=true port={port} seconds={args.seconds:g} "
                    f"interval={args.interval:g} read_only=true",
                    flush=True,
                )

        result = watch_state(
            port=port,
            seconds=args.seconds,
            interval=args.interval,
            sample_handler=show_sample,
            ready_handler=ready,
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
            f"result={result['status']} samples={result['sample_count']} "
            f"distinct_state_codes={result['distinct_state_codes']} "
            f"distinct_fingerprints={len(result['distinct_payload_fingerprints'])} "
            f"active_preset_events={result['active_preset_events']} "
            f"crc_errors={result['crc_errors']} port_closed=true"
        )
    return 0 if result["status"] == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())
