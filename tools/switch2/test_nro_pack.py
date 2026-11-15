"""Synthetic checks for the console-native converter via its host test CLI."""
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

from build import ROOT


def synthetic_elf():
    image = bytearray(0x4000)
    image[:7] = b"\x7fELF\x02\x01\x01"
    struct.pack_into("<HHIQQQIHHHHHH", image, 16, 3, 183, 1, 0, 64, 0, 0, 64, 56, 3, 0, 0, 0)
    for i, flags in enumerate((5, 4, 6)):
        size = 0x100 if i == 0 else 0x80
        struct.pack_into("<II6Q", image, 64 + i * 56, 1, flags, 0x1000 * (i + 1), 0x1000 * i,
                         0x1000 * i, size, size + (0x2000 if i == 2 else 0), 0x1000)
    struct.pack_into("<II", image, 0x1000, 0x14000020, 0x80)
    image[0x1080:0x1084] = b"MOD0"
    return image


class NroPackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.binary = Path(os.environ.get("WWHD_NRO_PACK_CHECK", ROOT / "build/switch2-nro-pack-check.exe"))
        if not cls.binary.is_file():
            raise unittest.SkipTest("Build the optional C++ nro_pack_check host CLI first")
        cls.env = os.environ.copy()
        msys = Path("C:/devkitPro/msys2/usr/bin")
        if os.name == "nt" and msys.is_dir():
            cls.env["PATH"] = str(msys) + os.pathsep + cls.env.get("PATH", "")

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="nro-pack-test-", dir=ROOT / "build")
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.elf = self.directory / "synthetic.elf"
        self.output = self.directory / "synthetic.nro"
        self.elf.write_bytes(synthetic_elf())

    def run_pack(self, *assets):
        return subprocess.run([str(self.binary), str(self.elf), str(self.output), *map(str, assets)],
                              env=self.env, capture_output=True, text=True)

    def test_segments_bss_and_header(self):
        result = self.run_pack()
        self.assertEqual(result.returncode, 0, result.stderr)
        image = self.output.read_bytes()
        self.assertEqual(len(image), 0x3000)
        self.assertEqual(image[16:20], b"NRO0")
        self.assertEqual(struct.unpack_from("<8I", image, 0x20),
                         (0, 0x1000, 0x1000, 0x1000, 0x2000, 0x1000, 0x2000, 0))
        self.assertEqual(image[0x80:0x84], b"MOD0")

    def test_matches_reference_converter_with_assets(self):
        reference = Path("C:/devkitPro/tools/bin/elf2nro.exe")
        if not reference.is_file():
            self.skipTest("Optional reference elf2nro tool is not installed")
        icon = self.directory / "icon.jpg"
        nacp = self.directory / "control.nacp"
        icon.write_bytes(b"synthetic asset")
        nacp.write_bytes(bytes(0x4000))
        result = self.run_pack(icon, nacp)
        self.assertEqual(result.returncode, 0, result.stderr)
        expected = self.directory / "reference.nro"
        subprocess.run([str(reference), str(self.elf), str(expected), f"--icon={icon}", f"--nacp={nacp}"],
                       env=self.env, check=True, capture_output=True)
        self.assertEqual(self.output.read_bytes(), expected.read_bytes())

    def test_existing_output_is_not_overwritten(self):
        self.output.write_bytes(b"existing user data")
        self.assertNotEqual(self.run_pack().returncode, 0)
        self.assertEqual(self.output.read_bytes(), b"existing user data")

    def test_malformed_inputs_never_create_output(self):
        original = synthetic_elf()
        mutations = [b"ELF", bytes(64)]
        for offset, value, fmt in ((32, 2**64 - 32, "Q"), (64 + 16, 4096, "Q"),
                                   (64 + 4, 7, "I"), (64 + 32, 2**64 - 1, "Q"),
                                   (0x1000 + 4, 0xfffffffc, "I"), (0x1080, 0, "I")):
            image = bytearray(original)
            struct.pack_into("<" + fmt, image, offset, value)
            mutations.append(image)
        for image in mutations:
            with self.subTest(size=len(image)):
                self.elf.write_bytes(image)
                self.assertNotEqual(self.run_pack().returncode, 0)
                self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
