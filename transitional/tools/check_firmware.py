#!/usr/bin/env python3
"""Validate the OTA artifact, without connecting to or changing any device."""
import argparse
import hashlib
import struct
from pathlib import Path


def check_image(path):
    image = path.read_bytes()
    if not 288 <= len(image) <= 0x140000:
        raise ValueError("Application does not fit the unchanged 0x140000-byte OTA slot")
    if image[0] != 0xE9 or not 1 <= image[1] <= 16:
        raise ValueError("Not an ESP application image")
    if struct.unpack_from("<H", image, 12)[0] != 0:
        raise ValueError("Image is not for the original ESP32")
    if struct.unpack_from("<I", image, 32)[0] != 0xABCD5432:
        raise ValueError("Application descriptor missing; do not upload a merged/bootloader/filesystem image")
    project = image[80:112].split(b"\0", 1)[0]
    if project != b"electrodacus-esp32":
        raise ValueError("Unexpected application project name")
    # The firmware SDK also verifies the full image before selecting its slot.
    # Here validate the ordinary ESP image checksum and appended SHA-256.
    offset = 24
    checksum = 0xEF
    for _ in range(image[1]):
        if offset + 8 > len(image):
            raise ValueError("Truncated segment header")
        _, length = struct.unpack_from("<II", image, offset)
        offset += 8
        if offset + length > len(image):
            raise ValueError("Truncated segment payload")
        for byte in image[offset:offset + length]:
            checksum ^= byte
        offset += length
    checksum_offset = offset + (15 - offset % 16)
    if checksum_offset >= len(image) or image[checksum_offset] != checksum:
        raise ValueError("Image checksum mismatch")
    end = checksum_offset + 1
    if image[23] == 1:
        if len(image) != end + 32 or hashlib.sha256(image[:end]).digest() != image[end:]:
            raise ValueError("Appended image SHA-256 mismatch or unexpected trailing bytes")
    elif len(image) != end:
        raise ValueError("Unexpected trailing bytes after application image")
    digest = hashlib.sha256(image).hexdigest()
    path.with_suffix(path.suffix + ".sha256").write_text(f"{digest}  {path.name}\n")
    print(f"Verified application: {len(image)} bytes, sha256={digest}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    check_image(parser.parse_args().image)
