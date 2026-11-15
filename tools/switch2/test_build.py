"""Host-only tests; no console, compiler or game dump required."""
import hashlib
from pathlib import Path
import shlex
import tempfile
import unittest
from unittest.mock import patch

import build


class BuildTests(unittest.TestCase):
    def test_msys_drive_path(self):
        self.assertEqual(build.msys_path(Path("D:/SDK folder/ounce")), "/d/SDK folder/ounce")

    def test_msys_posix_path(self):
        self.assertEqual(build.msys_path(Path("/opt/devkitpro")), "/opt/devkitpro")

    def test_command_paths_with_spaces_remain_single_arguments(self):
        for command in build.commands("/d/build dir", "/d/SDK dir", "/d/source dir"):
            self.assertEqual(shlex.split(shlex.join(command)), command)
        configure, compile_command = build.commands("/build", "/sdk", "/source")
        self.assertIn("-DOUNCE_SDK_ROOT=/sdk", configure)
        self.assertIn("wwhd_switch2_probe_nro", compile_command)

    def test_archive_checksum(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "sdk.tar.xz"
            path.write_bytes(b"test SDK")
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                build.verify_archive(path)
            with patch.object(build, "SDK_SHA256", hashlib.sha256(b"test SDK").hexdigest()):
                build.verify_archive(path)

    def test_rebuilt_libounce_override_is_explicit(self):
        default, _ = build.commands("/build", "/sdk", "/source")
        local, _ = build.commands("/build", "/sdk", "/source", "/local libs")
        self.assertIn("-DOUNCE_LIBRARY_DIR=/sdk/libounce/lib", default)
        self.assertIn("-DOUNCE_LIBRARY_DIR=/local libs", local)
        self.assertEqual(shlex.split(shlex.join(local)), local)

    def test_gamecode_and_runtime_targets_are_opt_in(self):
        plain, plain_build = build.commands("/build", "/sdk", "/source")
        game, game_build = build.commands("/build", "/sdk", "/source", manifest="/private/game.json",
                                         clang="/Program Files/clang.exe", jobs=2)
        self.assertIn("-DWWHD_BUILD_GAMECODE=OFF", plain)
        self.assertNotIn("wwhd_switch2_gamecode", plain_build)
        self.assertIn("-DWWHD_BUILD_GAMECODE=ON", game)
        self.assertIn("wwhd_switch2_gamecode", game_build)
        self.assertIn("wwhd_switch2_runtime_probe_nro", game_build)
        self.assertEqual(game_build[-2:], ["--parallel", "2"])
        self.assertEqual(shlex.split(shlex.join(game)), game)


if __name__ == "__main__":
    unittest.main()
