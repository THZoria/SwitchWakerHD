"""Run the optional native copy/cache regression CLI on synthetic files only."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from build import ROOT, msys_path


class FileCopyTests(unittest.TestCase):
    def test_exclusive_binary_copy_and_cache_transactions(self):
        binary = ROOT / "build/switch2-compiler-cache-check.exe"
        if not binary.is_file():
            self.skipTest("Build the optional compiler_cache_check.cpp host CLI")
        env = os.environ.copy()
        if os.name == "nt":
            env["PATH"] = "C:/devkitPro/msys2/usr/bin" + os.pathsep + env.get("PATH", "")
        with tempfile.TemporaryDirectory(prefix="file-copy-test-", dir=ROOT / "build") as scratch:
            # The Windows test executable is MSYS-hosted: filesystem::path must
            # receive an absolute POSIX path, not a drive-prefixed relative one.
            path = msys_path(Path(scratch)) if os.name == "nt" else scratch
            subprocess.run([str(binary), path], env=env, check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()
