"""Pack a raw Cortex-M application binary with the project boot image header.

The binary must begin with the application's vector table.  The linker for a
real split image must place that table at 0x02010040; the current CAN demo is
still linked at 0x02000000 and is intentionally not silently relocated.
"""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path

MAGIC = 0x4155544F
HEADER_SIZE = 64
FORMAT_VERSION = 1


def pack_image(source: Path, destination: Path, version: int, stack: int | None,
               reset: int | None, flags: int) -> None:
    payload = source.read_bytes()
    if len(payload) < 8:
        raise ValueError("application binary is shorter than a vector table")
    inferred_stack, inferred_reset = struct.unpack_from("<II", payload, 0)
    stack = inferred_stack if stack is None else stack
    reset = inferred_reset if reset is None else reset
    crc = zlib.crc32(payload) & 0xFFFFFFFF
    header = struct.pack(
        "<IHHIIIIII8I",
        MAGIC,
        HEADER_SIZE,
        FORMAT_VERSION,
        len(payload),
        version,
        crc,
        stack,
        reset,
        flags,
        *([0] * 8),
    )
    assert len(header) == HEADER_SIZE
    destination.write_bytes(header + payload)
    print(f"wrote {destination} ({len(payload)} bytes payload, CRC32=0x{crc:08X})")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--version", type=lambda value: int(value, 0), default=1)
    parser.add_argument("--stack", type=lambda value: int(value, 0))
    parser.add_argument("--reset", type=lambda value: int(value, 0))
    parser.add_argument("--flags", type=lambda value: int(value, 0), default=0)
    args = parser.parse_args()
    pack_image(args.source, args.destination, args.version, args.stack, args.reset, args.flags)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
