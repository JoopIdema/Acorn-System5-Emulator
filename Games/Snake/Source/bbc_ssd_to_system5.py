#!/usr/bin/env python3
"""Convert a standard BBC Micro Acorn DFS .ssd image to Acorn System DFS.

The data sectors are copied unchanged. Only catalogue fields that differ are
normalised:
  * all catalogue entries are moved to directory ' ' (space)
  * the catalogue cycle number is cleared
  * bits 16 and 17 of load and execution addresses are cleared

Usage:
    python3 bbc_ssd_to_system5.py input.ssd output.ssd
    python3 bbc_ssd_to_system5.py --force input.ssd output.ssd
"""

from __future__ import annotations
import argparse
from pathlib import Path
import sys

SECTOR_SIZE = 256
CAT0 = 0
CAT1 = SECTOR_SIZE


class ConversionError(ValueError):
    pass


def validate_catalogue(image: bytearray) -> tuple[int, int]:
    if len(image) < 2 * SECTOR_SIZE:
        raise ConversionError("image is smaller than the two DFS catalogue sectors")

    file_offset = image[CAT1 + 5]
    if file_offset % 8 != 0 or file_offset > 31 * 8:
        raise ConversionError(
            f"invalid DFS file offset 0x{file_offset:02X}; expected a multiple of 8 up to 0xF8"
        )

    file_count = file_offset // 8
    size_hi = image[CAT1 + 6] & 0x03
    sector_count = (size_hi << 8) | image[CAT1 + 7]
    if not 2 <= sector_count <= 1023:
        raise ConversionError(f"invalid DFS sector count: {sector_count}")

    if len(image) % SECTOR_SIZE:
        raise ConversionError(
            f"image size {len(image)} is not a multiple of {SECTOR_SIZE} bytes"
        )

    # SSD files are often truncated after the last used sector. The catalogue
    # may therefore describe an 800-sector disc while the host file is much
    # smaller. Validate occupied extents rather than demanding a fully padded
    # image.
    image_sectors = len(image) // SECTOR_SIZE
    for index in range(file_count):
        meta_offset = CAT1 + 8 + index * 8
        packed = image[meta_offset + 6]
        length = (image[meta_offset + 4]
                  | (image[meta_offset + 5] << 8)
                  | (((packed >> 4) & 0x03) << 16))
        start = image[meta_offset + 7] | ((packed & 0x03) << 8)
        end = start + (length + SECTOR_SIZE - 1) // SECTOR_SIZE
        if start < 2 or end > sector_count:
            raise ConversionError(
                f"file entry {index + 1} has invalid sector extent {start}..{end - 1}"
            )
        if length and end > image_sectors:
            raise ConversionError(
                f"file entry {index + 1} extends through sector {end - 1}, "
                f"but the image contains only {image_sectors} sectors"
            )
    return file_count, sector_count


def visible_name(raw: bytes) -> str:
    return raw.decode("ascii", errors="replace").rstrip(" ")


def convert(image: bytes, force: bool = False) -> tuple[bytes, list[str]]:
    out = bytearray(image)
    try:
        file_count, sector_count = validate_catalogue(out)
    except ConversionError:
        if not force:
            raise
        # In force mode we still need a safe catalogue entry count.
        file_offset = out[CAT1 + 5] if len(out) > CAT1 + 5 else 0
        if file_offset % 8 or file_offset > 31 * 8:
            raise ConversionError("cannot safely determine catalogue entry count, even with --force")
        file_count = file_offset // 8
        sector_count = len(out) // SECTOR_SIZE

    changes: list[str] = []

    old_cycle = out[CAT1 + 4]
    out[CAT1 + 4] = 0
    if old_cycle:
        changes.append(f"cycle number: 0x{old_cycle:02X} -> 0x00")

    for index in range(file_count):
        name_offset = CAT0 + 8 + index * 8
        meta_offset = CAT1 + 8 + index * 8
        name = visible_name(bytes(out[name_offset:name_offset + 7])) or f"entry {index + 1}"

        old_dir_attr = out[name_offset + 7]
        locked = old_dir_attr & 0x80
        old_dir = old_dir_attr & 0x7F
        out[name_offset + 7] = locked | 0x20
        if old_dir != 0x20:
            display_dir = chr(old_dir) if 0x20 <= old_dir <= 0x7E else f"0x{old_dir:02X}"
            changes.append(f"{display_dir}.{name}: directory -> space")

        # Packed high bits byte:
        # bits 0-1 start sector; 2-3 load; 4-5 length; 6-7 execution.
        packed_offset = meta_offset + 6
        old_packed = out[packed_offset]
        out[packed_offset] = old_packed & 0x33
        if old_packed != out[packed_offset]:
            changes.append(
                f"{name}: cleared load/exec bits 16-17 "
                f"(0x{old_packed:02X} -> 0x{out[packed_offset]:02X})"
            )

    changes.insert(0, f"catalogue: {file_count} file(s), {sector_count} sector(s)")
    return bytes(out), changes


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="BBC DFS .ssd input image")
    parser.add_argument("output", type=Path, help="Acorn System DFS .ssd output image")
    parser.add_argument("--force", action="store_true", help="continue past some size validation errors")
    args = parser.parse_args()

    if args.input.resolve() == args.output.resolve():
        parser.error("input and output must be different files")

    try:
        source = args.input.read_bytes()
        converted, changes = convert(source, force=args.force)
        args.output.write_bytes(converted)
    except (OSError, ConversionError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    for line in changes:
        print(line)
    print(f"written: {args.output} ({len(converted)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
