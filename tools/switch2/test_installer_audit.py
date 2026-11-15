import io
import struct
import unittest

from verify_installer import audit_romfs


class InstallerAuditTests(unittest.TestCase):
    def fixture(self):
        # One root directory and one file, using the standard RomFS tables.
        data = bytearray(0x104)
        struct.pack_into("<10Q", data, 0, 80, 80, 0, 80, 24, 104, 0, 104, 40, 0x100)
        struct.pack_into("<6I", data, 80, 0, 0xffffffff, 0xffffffff, 0, 0xffffffff, 0)
        struct.pack_into("<IIQQII", data, 104, 0, 0xffffffff, 0, 4, 0xffffffff, 5)
        data[136:141] = b"a.txt"
        data[0x100:0x104] = b"test"
        return data

    def parse(self, data):
        stream = io.BytesIO(data)

        def read(offset, length):
            stream.seek(offset)
            result = stream.read(length)
            if len(result) != length:
                raise ValueError("Short fixture read")
            return result

        return audit_romfs(read, 0, len(data))

    def test_file_extent(self):
        self.assertEqual(self.parse(self.fixture()), {"a.txt": (0x100, 4)})

    def test_rejects_cycle(self):
        data = self.fixture()
        struct.pack_into("<I", data, 108, 0)
        with self.assertRaisesRegex(ValueError, "Cyclic"):
            self.parse(data)

    def test_rejects_out_of_bounds_file(self):
        data = self.fixture()
        struct.pack_into("<Q", data, 120, 5)
        with self.assertRaisesRegex(ValueError, "outside asset"):
            self.parse(data)

    def test_rejects_traversal(self):
        data = self.fixture()
        data[136:141] = b"../xx"
        with self.assertRaisesRegex(ValueError, "entry name"):
            self.parse(data)


if __name__ == "__main__":
    unittest.main()
