#!/usr/bin/env python3
"""Generate repeatable normal and anomalous CAN traffic for the RA8D1 edge IDS demo."""

from __future__ import annotations

import argparse
import signal
import sys
import time
from dataclasses import dataclass

try:
    import can
except ImportError as exc:  # pragma: no cover - exercised on a development PC
    raise SystemExit("python-can is required: python -m pip install python-can") from exc


@dataclass
class Stream:
    arbitration_id: int
    period_s: float
    next_send: float
    counter: int = 0


def payload(counter: int) -> bytearray:
    return bytearray(
        (
            counter & 0xFF,
            (counter >> 8) & 0xFF,
            20 + counter % 7,
            80 + counter % 5,
            0x34,
            0x12,
            0x5A,
            0xA5,
        )
    )


def make_message(stream: Stream, attack: str, in_attack: bool, fd: bool) -> can.Message:
    data = payload(stream.counter)
    arbitration_id = stream.arbitration_id

    if in_attack and attack == "unknown" and stream.arbitration_id == 0x100:
        arbitration_id = 0x666
    elif in_attack and attack == "dlc" and stream.arbitration_id == 0x100:
        data = data[:4]
    elif in_attack and attack == "payload" and stream.arbitration_id == 0x100:
        data[2] = 0xF2
        data[3] = 0x04
        data[6] ^= 0xFF

    return can.Message(
        arbitration_id=arbitration_id,
        is_extended_id=False,
        is_fd=fd,
        bitrate_switch=fd,
        data=data,
    )


def open_bus(args: argparse.Namespace) -> can.BusABC:
    kwargs: dict[str, object] = {
        "interface": args.interface,
        "channel": args.channel,
        "bitrate": args.bitrate,
        "fd": args.fd,
    }
    if args.fd:
        kwargs["data_bitrate"] = args.data_bitrate
    return can.Bus(**kwargs)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Normal traffic for 10 s, then inject one CAN anomaly for 2 s."
    )
    parser.add_argument("--interface", default="socketcan", help="python-can interface backend")
    parser.add_argument("--channel", default="can0", help="backend channel/device")
    parser.add_argument("--bitrate", type=int, default=500_000)
    parser.add_argument("--data-bitrate", type=int, default=2_000_000)
    parser.add_argument("--fd", action="store_true", help="send CAN-FD+BRS instead of Classical CAN")
    parser.add_argument(
        "--attack",
        choices=("cycle", "unknown", "dlc", "payload", "flood"),
        default="cycle",
    )
    parser.add_argument("--normal-seconds", type=float, default=10.0)
    parser.add_argument("--attack-seconds", type=float, default=2.0)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    stop = False

    def request_stop(_signum: int, _frame: object) -> None:
        nonlocal stop
        stop = True

    signal.signal(signal.SIGINT, request_stop)
    signal.signal(signal.SIGTERM, request_stop)

    attacks = ("unknown", "dlc", "payload", "flood")
    attack_index = 0
    attack = attacks[attack_index] if args.attack == "cycle" else args.attack
    phase_started = time.monotonic()
    in_attack = False
    now = phase_started
    streams = [
        Stream(0x100, 0.010, now),
        Stream(0x180, 0.020, now),
        Stream(0x240, 0.100, now),
    ]

    print(
        f"Opening {args.interface}:{args.channel} at {args.bitrate} bps"
        + (f", data {args.data_bitrate} bps" if args.fd else "")
    )
    with open_bus(args) as bus:
        while not stop:
            now = time.monotonic()
            duration = args.attack_seconds if in_attack else args.normal_seconds
            if now - phase_started >= duration:
                in_attack = not in_attack
                phase_started = now
                if in_attack:
                    attack = attacks[attack_index] if args.attack == "cycle" else args.attack
                    print(f"ATTACK: {attack}")
                else:
                    print("NORMAL: relearning-safe traffic")
                    if args.attack == "cycle":
                        attack_index = (attack_index + 1) % len(attacks)

            for stream in streams:
                period = 0.0008 if (in_attack and attack == "flood" and stream.arbitration_id == 0x100) else stream.period_s
                if now >= stream.next_send:
                    message = make_message(stream, attack, in_attack, args.fd)
                    try:
                        bus.send(message, timeout=0.1)
                    except can.CanError as exc:
                        print(f"send failed: {exc}", file=sys.stderr)
                    stream.counter = (stream.counter + 1) & 0xFFFF
                    stream.next_send = max(stream.next_send + period, now)

            time.sleep(0.0002)

    print("Stopped")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
