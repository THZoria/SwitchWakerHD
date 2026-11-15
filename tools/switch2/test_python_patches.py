from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from build_python import ROOT, PYTHON_COMMIT, apply_patches


class PythonPatchTests(unittest.TestCase):
    def test_pristine_pinned_source_and_idempotence(self):
        source = ROOT / "build/switch2-python-source"
        if not shutil.which("git") or not (source / ".git").exists():
            self.skipTest("Optional pinned CPython checkout is unavailable")
        revision = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
        self.assertEqual(revision, PYTHON_COMMIT)
        paths = set()
        for patch in (ROOT / "tools/switch2/python/patches").glob("*.patch"):
            paths.update(line[6:] for line in patch.read_text().splitlines() if line.startswith("+++ b/"))
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as scratch:
            checkout = Path(scratch)
            subprocess.run(["git", "-C", scratch, "init", "--quiet"], check=True)
            for path in paths:
                pristine = subprocess.check_output(["git", "-C", str(source), "show", f"{PYTHON_COMMIT}:{path}"])
                target = checkout / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(pristine)
            apply_patches(checkout)
            expected = {path: (checkout / path).read_bytes() for path in paths}
            apply_patches(checkout)
            self.assertEqual(expected, {path: (checkout / path).read_bytes() for path in paths})


if __name__ == "__main__":
    unittest.main()
