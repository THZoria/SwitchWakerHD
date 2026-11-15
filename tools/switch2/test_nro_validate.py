import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

from build import ROOT


def synthetic_static_nro():
    data = bytearray(0x3000)
    struct.pack_into("<II", data, 0, 0x14000020, 0x80)
    struct.pack_into("<4I", data, 0x10, 0x304f524e, 0, len(data), 0)
    struct.pack_into("<8I", data, 0x20, 0, 0x1000, 0x1000, 0x1000, 0x2000, 0x1000, 0x1000, 0)
    struct.pack_into("<7I", data, 0x80, 0x30444f4d, 0x2000-0x80, 0x3000-0x80, 0x4000-0x80, 0, 0, 0)
    struct.pack_into("<3I", data, 0x9c, 0x304e554f, 2, 1)
    struct.pack_into("<4I", data, 0x1000, 1, 1, 0, 0)
    struct.pack_into("<Q", data, 0x1100, 0x2100)
    struct.pack_into("<Q", data, 0x2100, 0x1120)
    tags = [(4, 0x1000), (6, 0x1040), (5, 0x1080), (10, 1), (11, 24),
            (35, 8), (36, 0x1100), (37, 8), (0, 0)]
    for i, pair in enumerate(tags):
        struct.pack_into("<QQ", data, 0x2000 + i * 16, *pair)
    return data


class NroValidateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.binary = Path(os.environ.get("WWHD_NRO_PACK_CHECK", ROOT / "build/switch2-nro-pack-check.exe"))
        if not cls.binary.is_file():
            raise unittest.SkipTest("Build the optional native NRO host test CLI")
        cls.env = os.environ.copy()
        if os.name == "nt":
            cls.env["PATH"] = "C:/devkitPro/msys2/usr/bin" + os.pathsep + cls.env.get("PATH", "")

    def validate(self, data):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as directory:
            path = Path(directory) / "synthetic.nro"
            path.write_bytes(data)
            return subprocess.run([str(self.binary), "--validate", str(path)], env=self.env, capture_output=True).returncode == 0

    def test_valid_static_switch2_profile(self):
        self.assertTrue(self.validate(synthetic_static_nro()))

    def test_rejects_platform_imports_text_writes_and_bad_ranges(self):
        for offset, fmt, value in ((0xa0, "I", 1), (0x2000, "Q", 1), (0x2010, "Q", 4),
                                   (0x1100, "Q", 0x800), (0x1100, "Q", 0x4000), (0x1100, "Q", 1),
                                   (0x2008, "Q", 2**64-1), (0x24, "I", 0x2000), (0x18, "I", 2**32-1)):
            with self.subTest(offset=offset, value=value):
                data = synthetic_static_nro()
                struct.pack_into("<" + fmt, data, offset, value)
                self.assertFalse(self.validate(data))

    def test_rejects_unresolved_dynamic_symbol(self):
        data = synthetic_static_nro()
        struct.pack_into("<I", data, 0x1004, 2)
        self.assertFalse(self.validate(data))


if __name__ == "__main__":
    unittest.main()
