import shlex
import unittest

import build_compiler


class CompilerBuildTests(unittest.TestCase):
    def test_compiler_is_separate_from_private_game(self):
        steps = build_compiler.commands("/d/private project", 2)
        self.assertEqual(len(steps), 4)
        self.assertIn("clang-tblgen", steps[1])
        self.assertIn("llvm-tblgen", steps[1])
        self.assertIn("-DWWHD_NATIVE_TOOLS=/d/private project/build/switch2-llvm-native/bin", steps[2])
        self.assertIn("wwhd_console_clang", steps[3])
        for command in steps:
            self.assertEqual(shlex.split(shlex.join(command)), command)
            self.assertNotIn("gamecode", shlex.join(command))
            self.assertNotIn("gen-switch2", shlex.join(command))
            self.assertNotIn("--game-manifest", command)

    def test_bounded_compile_parallelism_and_pinned_revision(self):
        steps = build_compiler.commands("/project", 3)
        for step in (steps[1], steps[3]):
            self.assertEqual(step[-2:], ["--parallel", "3"])
        self.assertEqual(len(build_compiler.LLVM_COMMIT), 40)


if __name__ == "__main__":
    unittest.main()
