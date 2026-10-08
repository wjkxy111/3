"""Create and verify the project secure manifest image.

The private key is used only on the host. The RA8D1 bootloader verifies the
raw ECDSA-P256 signature with RSIP-E51A; it never stores the private key.
Node.js is used only because this Windows setup has no Python crypto package.
S-record input is reduced to the RA8D1 code-flash address window; ELF/HEX input
is rejected to avoid silently signing load gaps or non-vector bytes.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import subprocess
from pathlib import Path

MAGIC = 0x53454349
HEADER_SIZE = 256
FORMAT_VERSION = 2
KEY_ID = 1
STRUCT = "<IHH7I32s64s31I"
SECURE_SLOT_START = 0x02010000
SECURE_SLOT_SIZE = 0x001E8000
SECURE_APP_START = SECURE_SLOT_START + HEADER_SIZE


def node_path() -> Path:
    return Path(r"C:\ti\ccs2031\ccs\tools\node\node.exe")


def helper_path() -> Path:
    return Path(__file__).with_name("secure_sign_node.js")


def load_payload(source: Path) -> bytes:
    suffix = source.suffix.lower()
    if suffix != ".srec":
        if suffix in {".hex", ".elf"}:
            raise ValueError("use a raw .bin or an RA code-flash .srec; ELF/HEX load gaps are ambiguous")
        return source.read_bytes()
    records: list[tuple[int, bytes]] = []
    for line_number, line in enumerate(source.read_text(encoding="ascii").splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        if (len(line) < 10) or (line[0] != "S"):
            raise ValueError(f"invalid S-record line {line_number}")
        record_type = line[1]
        count = int(line[2:4], 16)
        raw = bytes.fromhex(line[4:])
        if (len(raw) != count) or ((sum(raw) + count) & 0xFF) != 0xFF:
            raise ValueError(f"bad S-record checksum/length at line {line_number}")
        address_bytes = {"1": 2, "2": 3, "3": 4}.get(record_type)
        if address_bytes is None:
            continue
        address = int.from_bytes(raw[:address_bytes], "big")
        records.append((address, raw[address_bytes:-1]))
    code_records = [
        (address, data) for address, data in records
        if 0x02000000 <= address < 0x02200000
    ]
    if not code_records:
        raise ValueError("S-record contains no RA8D1 code-flash data")
    base = min(address for address, _data in code_records)
    end = max(address + len(data) for address, data in code_records)
    image = bytearray([0xFF]) * (end - base)
    for address, data in code_records:
        image[address - base:address - base + len(data)] = data
    return bytes(image)


def signing_domain(header_without_signature: bytes) -> bytes:
    return header_without_signature[:68]


def node_sign(private_key: Path, domain: bytes) -> bytes:
    result = subprocess.run(
        [str(node_path()), str(helper_path()), "sign", str(private_key), domain.hex()],
        check=True,
        capture_output=True,
        text=True,
    )
    signature = bytes.fromhex(result.stdout.strip())
    if len(signature) != 64:
        raise ValueError("Node did not return a raw 64-byte P-256 signature")
    return signature


def generate_key(private_key: Path) -> None:
    subprocess.run(
        [str(node_path()), str(helper_path()), "generate", str(private_key)],
        check=True,
    )
    print(f"private key: {private_key}")
    print(f"public key:  {private_key}.pub.pem")


def pack(source: Path, destination: Path, private_key: Path, version: int,
         security_counter: int, stack: int | None, reset: int | None) -> None:
    payload = load_payload(source)
    if len(payload) < 8:
        raise ValueError("payload must contain a Cortex-M vector table")
    inferred_stack, inferred_reset = struct.unpack_from("<II", payload)
    stack = inferred_stack if stack is None else stack
    reset = inferred_reset if reset is None else reset
    if not ((SECURE_APP_START | 1) <= reset < SECURE_SLOT_START + SECURE_SLOT_SIZE):
        raise ValueError(
            "reset vector is outside the secure application slot; rebuild the application "
            "with bootloader/project/memory_regions_app_ext.lld"
        )
    image_hash = hashlib.sha256(payload).digest()
    signature = bytes(64)
    header = struct.pack(
        STRUCT, MAGIC, HEADER_SIZE, FORMAT_VERSION, len(payload), version,
        security_counter, stack, reset, 0, KEY_ID, image_hash, signature,
        *([0] * 31),
    )
    signature = node_sign(private_key, signing_domain(header))
    header = struct.pack(
        STRUCT, MAGIC, HEADER_SIZE, FORMAT_VERSION, len(payload), version,
        security_counter, stack, reset, 0, KEY_ID, image_hash, signature,
        *([0] * 31),
    )
    assert len(header) == HEADER_SIZE
    destination.write_bytes(header + payload)
    print(f"wrote {destination} ({len(payload)} byte payload, P-256 signature)")


def verify(image: Path, public_key: Path) -> None:
    raw = image.read_bytes()
    if len(raw) < HEADER_SIZE:
        raise ValueError("image is shorter than secure header")
    fields = struct.unpack_from(STRUCT, raw, 0)
    magic, header_size, fmt, size, _version, _counter, _sp, _reset, _flags, _key = fields[:10]
    image_hash = fields[10]
    signature = fields[11]
    if (magic, header_size, fmt) != (MAGIC, HEADER_SIZE, FORMAT_VERSION):
        raise ValueError("bad secure header")
    payload = raw[HEADER_SIZE:HEADER_SIZE + size]
    if hashlib.sha256(payload).digest() != image_hash:
        raise ValueError("payload hash mismatch")
    domain = raw[:68]
    result = subprocess.run(
        [str(node_path()), str(helper_path()), "verify", str(public_key), domain.hex()],
        env={**__import__("os").environ, "AUTOSAR_SIGNATURE_HEX": signature.hex()},
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise ValueError("ECDSA signature verification failed")
    print("secure image: VALID (payload SHA-256 + ECDSA P-256)")


def main() -> int:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    gen = sub.add_parser("generate-key")
    gen.add_argument("private_key", type=Path)
    packer = sub.add_parser("pack")
    packer.add_argument("source", type=Path)
    packer.add_argument("destination", type=Path)
    packer.add_argument("private_key", type=Path)
    packer.add_argument("--version", type=lambda x: int(x, 0), default=1)
    packer.add_argument("--security-counter", type=lambda x: int(x, 0), default=1)
    packer.add_argument("--stack", type=lambda x: int(x, 0))
    packer.add_argument("--reset", type=lambda x: int(x, 0))
    checker = sub.add_parser("verify")
    checker.add_argument("image", type=Path)
    checker.add_argument("public_key", type=Path)
    args = parser.parse_args()
    if args.command == "generate-key":
        generate_key(args.private_key)
    elif args.command == "pack":
        pack(args.source, args.destination, args.private_key, args.version,
             args.security_counter, args.stack, args.reset)
    else:
        verify(args.image, args.public_key)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
