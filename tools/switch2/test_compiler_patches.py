"""Check reproducibility against pristine pinned LLVM files, when available."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from build_compiler import ROOT, LLVM_COMMIT, apply_port_patches


class CompilerPatchTests(unittest.TestCase):
    def test_patches_apply_to_pristine_sources_and_are_idempotent(self):
        source = ROOT / "build/switch2-llvm-source"
        if not shutil.which("git") or not (source / ".git").exists():
            self.skipTest("Optional pinned LLVM checkout is not available")
        revision = subprocess.check_output(
            ["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
        self.assertEqual(revision, LLVM_COMMIT)
        patches = sorted((ROOT / "tools/switch2/compiler/patches").glob("*.patch"))
        self.assertTrue(patches)
        paths = set()
        for patch in patches:
            for line in patch.read_text().splitlines():
                if line.startswith("+++ b/"):
                    paths.add(line[6:])
        with tempfile.TemporaryDirectory(prefix="llvm-patch-test-", dir=ROOT / "build") as scratch:
            checkout = Path(scratch)
            subprocess.run(["git", "-C", scratch, "init", "--quiet"], check=True)
            for path in paths:
                pristine = subprocess.check_output(["git", "-C", str(source), "show", f"{LLVM_COMMIT}:{path}"])
                destination = checkout / path
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(pristine)
            apply_port_patches(checkout)
            before = {path: (checkout / path).read_bytes() for path in paths}
            apply_port_patches(checkout)
            self.assertEqual(before, {path: (checkout / path).read_bytes() for path in paths})


if __name__ == "__main__":
    unittest.main()
