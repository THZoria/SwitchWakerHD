"""Host-only checks for the isolated runtime compilation command."""
import shlex
import unittest

import compile_runtime


class RuntimeBuildTests(unittest.TestCase):
    def test_isolated_sdk_and_output(self):
        configure, compile_command = compile_runtime.commands("/d/private project", 2)
        self.assertIn("/d/private project/build/switch2-runtime", configure)
        self.assertIn("-DOUNCE_LIBRARY_DIR=/d/private project/build/switch2-libounce-local", configure)
        self.assertIn("-DGEN_DIR=/d/private project/build/gen-switch2", configure)
        self.assertIn("-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON", configure)
        self.assertEqual(compile_command[-2:], ["--parallel", "2"])
        self.assertEqual(compile_command[3:5], ["--target", "wwhd"])

    def test_spaces_survive_shell_roundtrip(self):
        for command in compile_runtime.commands("/d/project with spaces"):
            self.assertEqual(shlex.split(shlex.join(command)), command)

    def test_nro_is_explicit_and_uses_separate_output(self):
        configure, compile_command = compile_runtime.commands("/private", 2, nro=True)
        self.assertIn("-DWWHD_SWITCH2_BUILD_NRO=ON", configure)
        self.assertIn("/private/build/switch2-game", configure)
        self.assertIn("wwhd_nro", compile_command)
        default, _ = compile_runtime.commands("/private")
        self.assertIn("-DWWHD_SWITCH2_BUILD_NRO=OFF", default)


if __name__ == "__main__":
    unittest.main()
