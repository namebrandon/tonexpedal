#!/usr/bin/env python3
"""Stress TONEX preset changes while validating unsolicited CDC events."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tempfile
import time
from collections import Counter
from pathlib import Path
from typing import Optional, Sequence

import tonex_hardware_probe as protocol


MIN_RATE = 0.5
MAX_RATE = 10.0
MAX_DURATION = 120.0
DEFAULT_RATE = 5.0
DEFAULT_DURATION = 30.0
DEFAULT_DRAIN_SECONDS = 2.0
MIDI_PROBE = Path(__file__).with_name("tonex_midi_probe.swift")
TRANSMISSION_PATTERN = re.compile(r"^(sent|restored|cleanup_restore) index=(\d+)\b")


def bounded_float(value: str, minimum: float, maximum: float, label: str) -> float:
    parsed = float(value)
    if not minimum <= parsed <= maximum:
        raise argparse.ArgumentTypeError(
            f"{label} must be between {minimum:g} and {maximum:g}"
        )
    return parsed


def deterministic_preset_sequence(count: int) -> list[int]:
    """Cover presets 1..149 in a repeatable, non-adjacent permutation."""
    if count <= 0:
        raise ValueError("change count must be greater than zero")
    return [1 + ((position * 37) % (protocol.TOTAL_PRESETS - 1)) for position in range(count)]


def parse_transmitted_indices(output: str) -> list[int]:
    indices = []
    for line in output.splitlines():
        match = TRANSMISSION_PATTERN.match(line)
        if match:
            indices.append(int(match.group(2)))
    return indices


def analyze_event_sequence(expected: Sequence[int], observed: Sequence[int]) -> dict:
    expected_list = list(expected)
    observed_list = list(observed)
    expected_counts = Counter(expected_list)
    observed_counts = Counter(observed_list)
    missing = expected_counts - observed_counts
    excess = observed_counts - expected_counts
    first_mismatch = None
    for position in range(max(len(expected_list), len(observed_list))):
        expected_index = expected_list[position] if position < len(expected_list) else None
        observed_index = observed_list[position] if position < len(observed_list) else None
        if expected_index != observed_index:
            first_mismatch = {
                "position": position,
                "expected": expected_index,
                "observed": observed_index,
            }
            break

    exact_match = expected_list == observed_list
    return {
        "exact_match": exact_match,
        "expected_count": len(expected_list),
        "observed_count": len(observed_list),
        "missing_count": sum(missing.values()),
        "excess_count": sum(excess.values()),
        "missing_index_counts": dict(sorted(missing.items())),
        "excess_index_counts": dict(sorted(excess.items())),
        "reordered": not exact_match and not missing and not excess,
        "first_mismatch": first_mismatch,
    }


def collect_while_running(
    transport: protocol.PosixSerialTransport,
    process: subprocess.Popen,
    hard_timeout: float,
    drain_seconds: float,
) -> list[bytes]:
    payloads = []
    hard_deadline = time.monotonic() + hard_timeout
    finished_at = None
    while time.monotonic() < hard_deadline:
        payloads.extend(transport.read_frames_for(0.1))
        if process.poll() is not None:
            if finished_at is None:
                finished_at = time.monotonic()
            if time.monotonic() - finished_at >= drain_seconds:
                return payloads
    raise protocol.ProbeError("MIDI sender did not finish before the stress-test timeout")


def best_effort_restore(restore_index: int) -> None:
    command = [
        "swift",
        str(MIDI_PROBE),
        "--restore-index",
        str(restore_index),
        "--indices",
        str(restore_index),
        "--delay",
        "0.1",
    ]
    try:
        subprocess.run(
            command,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=10,
            check=False,
        )
    except (OSError, subprocess.SubprocessError):
        pass


def run_stress(
    port: str,
    restore_index: int,
    rate: float,
    duration: float,
    drain_seconds: float,
) -> dict:
    changes = max(1, round(rate * duration))
    targets = deterministic_preset_sequence(changes)
    expected_events = targets + [restore_index]
    delay = 1.0 / rate
    command = [
        "swift",
        str(MIDI_PROBE),
        "--restore-index",
        str(restore_index),
        "--indices",
        ",".join(str(index) for index in targets),
        "--delay",
        f"{delay:.6f}",
    ]
    hard_timeout = 10.0 + (len(expected_events) * delay) + drain_seconds
    process = None
    payloads = []
    sender_output = ""
    sender_error = ""
    sender_exit_code = None
    started_at = time.monotonic()

    try:
        with tempfile.TemporaryFile(mode="w+", encoding="utf-8") as sender_log:
            with protocol.PosixSerialTransport(port, timeout=3.0) as transport:
                process = subprocess.Popen(
                    command,
                    stdout=sender_log,
                    stderr=subprocess.PIPE,
                    text=True,
                )
                payloads = collect_while_running(
                    transport,
                    process,
                    hard_timeout=hard_timeout,
                    drain_seconds=drain_seconds,
                )
                _, sender_error = process.communicate(timeout=1.0)
                sender_exit_code = process.returncode
                sender_log.seek(0)
                sender_output = sender_log.read()
                crc_errors = transport.decoder.protocol_error_count
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            best_effort_restore(restore_index)

    active_events = []
    malformed_active_events = 0
    other_frames = 0
    for payload in payloads:
        if len(payload) >= 12 and payload[7:12] == protocol.ACTIVE_PRESET_EVENT_PREFIX:
            try:
                active_events.append(protocol.parse_active_preset_event_index(payload))
            except protocol.ProtocolError:
                malformed_active_events += 1
        else:
            other_frames += 1

    transmitted = parse_transmitted_indices(sender_output)
    analysis = analyze_event_sequence(expected_events, active_events)
    sender_complete = sender_exit_code == 0 and transmitted == expected_events
    final_index_confirmed = bool(active_events) and active_events[-1] == restore_index
    passed = (
        sender_complete
        and analysis["exact_match"]
        and crc_errors == 0
        and malformed_active_events == 0
        and other_frames == 0
        and final_index_confirmed
    )
    return {
        "status": "ok" if passed else "failed",
        "port": port,
        "baud_rate": 115200,
        "rate_hz": rate,
        "requested_duration_seconds": duration,
        "actual_duration_ms": round((time.monotonic() - started_at) * 1000),
        "changes_sent": len(targets),
        "restore_index": restore_index,
        "sender_exit_code": sender_exit_code,
        "sender_complete": sender_complete,
        "sender_error": sender_error.strip() or None,
        "transmitted_indices": transmitted,
        "observed_indices": active_events,
        "event_analysis": analysis,
        "crc_errors": crc_errors,
        "malformed_active_events": malformed_active_events,
        "other_frames": other_frames,
        "final_index_confirmed": final_index_confirmed,
    }


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Rapidly change TONEX presets over MIDI and verify every CDC event. "
            "This changes the connected pedal and restores --restore-index afterward."
        )
    )
    parser.add_argument("--port", help="USB serial device; auto-detected when exactly one is present")
    parser.add_argument(
        "--restore-index",
        required=True,
        type=protocol.preset_index,
        help="known current preset index to restore after the test",
    )
    parser.add_argument(
        "--rate",
        type=lambda value: bounded_float(value, MIN_RATE, MAX_RATE, "rate"),
        default=DEFAULT_RATE,
        help=f"preset changes per second ({MIN_RATE:g}..{MAX_RATE:g}; default: {DEFAULT_RATE:g})",
    )
    parser.add_argument(
        "--duration",
        type=lambda value: bounded_float(value, 1.0, MAX_DURATION, "duration"),
        default=DEFAULT_DURATION,
        help=f"send duration in seconds (1..{MAX_DURATION:g}; default: {DEFAULT_DURATION:g})",
    )
    parser.add_argument(
        "--drain-seconds",
        type=lambda value: bounded_float(value, 0.5, 10.0, "drain time"),
        default=DEFAULT_DRAIN_SECONDS,
        help=f"CDC drain time after MIDI completes (default: {DEFAULT_DRAIN_SECONDS:g})",
    )
    parser.add_argument("--json", action="store_true", help="emit JSON instead of human-readable output")
    return parser.parse_args(argv)


def print_human(result: dict) -> None:
    analysis = result["event_analysis"]
    print(
        f"port={result['port']} baud={result['baud_rate']} rate_hz={result['rate_hz']:g} "
        f"changes_sent={result['changes_sent']} restore_index={result['restore_index']}"
    )
    print(
        f"events_expected={analysis['expected_count']} events_observed={analysis['observed_count']} "
        f"missing={analysis['missing_count']} excess={analysis['excess_count']} "
        f"reordered={str(analysis['reordered']).lower()} crc_errors={result['crc_errors']} "
        f"malformed={result['malformed_active_events']} other_frames={result['other_frames']}"
    )
    if analysis["first_mismatch"] is not None:
        mismatch = analysis["first_mismatch"]
        print(
            f"first_mismatch_position={mismatch['position']} expected={mismatch['expected']} "
            f"observed={mismatch['observed']}"
        )
    print(
        f"final_index_confirmed={str(result['final_index_confirmed']).lower()} "
        f"sender_complete={str(result['sender_complete']).lower()}"
    )
    if result["sender_error"]:
        print(f"sender_error={result['sender_error']}", file=sys.stderr)
    print(f"result={result['status']} duration_ms={result['actual_duration_ms']}")


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    if sys.platform != "darwin":
        print("error: the rapid-change MIDI sender currently requires macOS CoreMIDI", file=sys.stderr)
        return 1
    try:
        port = args.port or protocol.discover_port()
        result = run_stress(
            port=port,
            restore_index=args.restore_index,
            rate=args.rate,
            duration=args.duration,
            drain_seconds=args.drain_seconds,
        )
    except (OSError, protocol.ProbeError, subprocess.SubprocessError, ValueError) as exc:
        if args.json:
            print(json.dumps({"status": "error", "message": str(exc)}))
        else:
            print(f"error: {exc}", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps(result, sort_keys=True))
    else:
        print_human(result)
    return 0 if result["status"] == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())
