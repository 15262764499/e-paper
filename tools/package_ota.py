#!/usr/bin/env python3
"""Create a hardware-targeted EPO2 BLE OTA package for the STM32 application."""

from __future__ import annotations

import argparse
import hashlib
import hmac
import os
from pathlib import Path
import struct


APP_BASE = 0x08006000
APP_MAX_SIZE = 0x00012000
SRAM_BASE = 0x20000000
SRAM_END = 0x20009000
STM32_DEVICE_ID = 0x0460
PRODUCT_ID = 0x00000001
HARDWARE_REVISION = 0x0001
DOMAIN = b"EPD-OTA-RELEASE-V2"
PACKAGE_MAGIC = b"EPO2"
PACKAGE_VERSION = 2
PACKAGE_HEADER_SIZE = 88
DEVELOPMENT_KEY_HEX = (
    "6db5359a48c2710fe423b8619cd7045e"
    "17a93cf0826bd13459ee0ac7b2459813"
)


def parse_version(text: str) -> int:
    if "." not in text:
        value = int(text, 0)
    else:
        parts = [int(part, 10) for part in text.split(".")]
        if len(parts) != 3 or any(part < 0 or part > 255 for part in parts):
            raise argparse.ArgumentTypeError("version must be MAJOR.MINOR.PATCH")
        value = (parts[0] << 16) | (parts[1] << 8) | parts[2]
    if value <= 0 or value > 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("version must be in 1..0xffffffff")
    return value


def parse_hex_bytes(text: str, size: int, name: str) -> bytes:
    normalized = text.strip().replace(":", "").replace("-", "")
    try:
        value = bytes.fromhex(normalized)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"{name} is not valid hexadecimal") from exc
    if len(value) != size:
        raise argparse.ArgumentTypeError(f"{name} must contain {size} bytes")
    return value


def parse_uint(text: str, bits: int, name: str) -> int:
    try:
        value = int(text, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"{name} is not an integer") from exc
    if value < 0 or value >= (1 << bits):
        raise argparse.ArgumentTypeError(f"{name} must fit in {bits} bits")
    return value


def validate_application(image: bytes) -> None:
    if len(image) < 8 or len(image) > APP_MAX_SIZE:
        raise ValueError(f"application size must be 8..{APP_MAX_SIZE} bytes")
    stack, reset = struct.unpack_from("<II", image, 0)
    reset_address = reset & ~1
    if not (SRAM_BASE <= stack <= SRAM_END and stack % 4 == 0):
        raise ValueError(f"invalid initial MSP 0x{stack:08x}")
    if not (reset & 1):
        raise ValueError(f"reset vector 0x{reset:08x} is not Thumb code")
    if not (APP_BASE <= reset_address < APP_BASE + len(image)):
        raise ValueError(
            f"reset vector 0x{reset:08x} is outside the relocated application"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="relocated application .bin")
    parser.add_argument("--output", "-o", type=Path)
    parser.add_argument("--version", required=True, type=parse_version)
    parser.add_argument(
        "--product-id",
        default=PRODUCT_ID,
        type=lambda value: parse_uint(value, 32, "product-id"),
        help=f"target product class (default: 0x{PRODUCT_ID:08x})",
    )
    parser.add_argument(
        "--hardware-revision",
        default=HARDWARE_REVISION,
        type=lambda value: parse_uint(value, 16, "hardware-revision"),
        help=f"target hardware revision (default: {HARDWARE_REVISION})",
    )
    key_group = parser.add_mutually_exclusive_group()
    key_group.add_argument(
        "--key-hex",
        help="32-byte release key; alternatively EPAPER_OTA_RELEASE_KEY",
    )
    key_group.add_argument(
        "--development-key",
        action="store_true",
        help="use the repository development key (never for production)",
    )
    args = parser.parse_args()

    key_text = args.key_hex or os.environ.get("EPAPER_OTA_RELEASE_KEY")
    if args.development_key:
        key_text = DEVELOPMENT_KEY_HEX
    if not key_text:
        parser.error("provide --key-hex, --development-key, or EPAPER_OTA_RELEASE_KEY")
    release_key = parse_hex_bytes(key_text, 32, "release key")

    image = args.input.read_bytes()
    validate_application(image)
    digest = hashlib.sha256(image).digest()
    version_le = struct.pack("<I", args.version)
    size_le = struct.pack("<I", len(image))
    target = struct.pack(
        "<HHI", STM32_DEVICE_ID, args.hardware_revision, args.product_id
    )
    release_tag = hmac.new(
        release_key, DOMAIN + target + version_le + size_le + digest,
        hashlib.sha256,
    ).digest()
    header = (
        PACKAGE_MAGIC
        + bytes((PACKAGE_VERSION, 0))
        + struct.pack("<H", PACKAGE_HEADER_SIZE)
        + target
        + version_le
        + size_le
        + digest
        + release_tag
    )
    if len(header) != PACKAGE_HEADER_SIZE:
        raise RuntimeError("internal EPO2 header size mismatch")
    output = args.output or args.input.with_suffix(".epota")
    output.write_bytes(header + image)
    print(f"output={output}")
    print(f"version=0x{args.version:08x}")
    print(f"stm32_device_id=0x{STM32_DEVICE_ID:04x}")
    print(f"product_id=0x{args.product_id:08x}")
    print(f"hardware_revision={args.hardware_revision}")
    print(f"firmware_size={len(image)}")
    print(f"sha256={digest.hex()}")
    print(f"release_tag={release_tag.hex()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
