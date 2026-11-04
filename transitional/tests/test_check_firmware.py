import hashlib
import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path

SPEC = importlib.util.spec_from_file_location(
    "check_firmware", Path(__file__).parents[1] / "tools" / "check_firmware.py")
CHECK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK)


def application():
    header = bytearray(24)
    header[0:2] = bytes([0xE9, 1])
    header[23] = 1
    descriptor = bytearray(256)
    struct.pack_into("<I", descriptor, 0, 0xABCD5432)
    descriptor[48:66] = b"electrodacus-esp32\0"
    data = header + struct.pack("<II", 0x3F400020, len(descriptor)) + descriptor
    checksum = 0xEF
    for byte in descriptor:
        checksum ^= byte
    data += bytes(15 - len(data) % 16) + bytes([checksum])
    return data + hashlib.sha256(data).digest()


class ArtifactTests(unittest.TestCase):
    def check(self, data):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "firmware.bin"
            path.write_bytes(data)
            CHECK.check_image(path)
            self.assertTrue(path.with_suffix(".bin.sha256").exists())

    def test_valid_image(self):
        self.check(application())

    def test_truncated_payload(self):
        with self.assertRaises(ValueError):
            self.check(application()[:289])

    def test_wrong_project(self):
        image = application()
        image[80] = ord("x")
        with self.assertRaisesRegex(ValueError, "project"):
            self.check(image)

    def test_corrupt_checksum(self):
        image = application()
        image[-33] ^= 1
        with self.assertRaisesRegex(ValueError, "checksum"):
            self.check(image)

    def test_corrupt_sha256(self):
        image = application()
        image[-1] ^= 1
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            self.check(image)

    def test_merged_flash(self):
        with self.assertRaises(ValueError):
            self.check(bytes(4096) + application())

    def test_extra_trailing_data(self):
        with self.assertRaises(ValueError):
            self.check(application() + b"extra")


if __name__ == "__main__":
    unittest.main()
