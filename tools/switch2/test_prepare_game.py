"""Preparation tests use a synthetic header/metadata, never game bytes."""
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import prepare_game


class PrepareGameTests(unittest.TestCase):
    def fixture(self, root: Path, *, title="0005000010143500", version=0):
        for name in ("code", "content", "meta"):
            (root / name).mkdir(parents=True)
        header = bytearray(64)
        header[:6] = b"\x7fELF\x01\x02"
        struct.pack_into(">I", header, 0x18, 0x02001234)
        (root / "code/cking.rpx").write_bytes(header)
        (root / "meta/meta.xml").write_text(
            f"<menu><title_id>{title}</title_id><title_version>{version}</title_version></menu>", encoding="utf-8")
        return SimpleNamespace(name="USA", title_id="0005000010143500", sha256="a" * 64)

    def test_identification_preserves_dump_and_handles_special_path(self):
        with tempfile.TemporaryDirectory() as tmp:
            game = Path(tmp) / "Jeux & Emulateurs [USA]"
            identity = self.fixture(game)
            before = (game / "code/cking.rpx").read_bytes()
            with patch.object(prepare_game.builds, "identify", return_value=identity):
                info = prepare_game.identify_game(game)
            self.assertEqual(info["entry"], 0x02001234)
            self.assertEqual(info["rpx_sha256"], "a" * 64)
            self.assertEqual((game / "code/cking.rpx").read_bytes(), before)

    def test_wrong_title_or_updated_game_is_rejected(self):
        for kwargs in ({"title": "0005000010143600"}, {"version": 16}):
            with self.subTest(kwargs=kwargs), tempfile.TemporaryDirectory() as tmp:
                game = Path(tmp)
                identity = self.fixture(game, **kwargs)
                with patch.object(prepare_game.builds, "identify", return_value=identity):
                    with self.assertRaisesRegex(ValueError, "does not match"):
                        prepare_game.identify_game(game)

    def test_unrecognized_rpx_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            game = Path(tmp)
            self.fixture(game)
            with patch.object(prepare_game.builds, "identify", return_value=None):
                with self.assertRaisesRegex(ValueError, "Unsupported"):
                    prepare_game.identify_game(game)

    def test_wrong_elf_endianness_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            game = Path(tmp)
            identity = self.fixture(game)
            (game / "code/cking.rpx").write_bytes(b"\x7fELF\x01\x01" + bytes(58))
            with patch.object(prepare_game.builds, "identify", return_value=identity):
                with self.assertRaisesRegex(ValueError, "big-endian"):
                    prepare_game.identify_game(game)


if __name__ == "__main__":
    unittest.main()
