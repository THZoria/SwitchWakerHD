from pathlib import Path
import unittest

from build_runtime_sdk import select_runtime_objects


class RuntimeSdkTests(unittest.TestCase):
    def test_only_runtime_target_objects_are_selected(self):
        entries = [
            {"output": "CMakeFiles/wwhd.dir/runtime/src/core.o", "file": "/src/runtime/src/core.cpp"},
            {"output": "CMakeFiles/gamecode.dir/code_000.o", "file": "/src/build/gen-switch2-sdk-stub/code_000.c"},
            {"output": "CMakeFiles/cemu_latte.dir/decompiler.o", "file": "/src/runtime/third_party/decompiler.cpp"},
        ]
        selected = select_runtime_objects(entries, Path("sdk-build"))
        self.assertEqual(selected, [(Path("sdk-build/CMakeFiles/wwhd.dir/runtime/src/core.o"), "/src/runtime/src/core.cpp")])

    def test_generated_guest_body_in_runtime_target_is_rejected(self):
        entries = [{"output": "CMakeFiles/wwhd.dir/code_000.o", "file": "/src/build/gen-switch2/code_000.c"}]
        with self.assertRaises(ValueError):
            select_runtime_objects(entries, Path("sdk-build"))

    def test_empty_sdk_is_rejected(self):
        with self.assertRaises(ValueError):
            select_runtime_objects([], Path("sdk-build"))


if __name__ == "__main__":
    unittest.main()
