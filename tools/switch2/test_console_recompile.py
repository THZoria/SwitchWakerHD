import importlib.util
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from build import ROOT

sys.path.insert(0, str(ROOT / "tools/recomp"))
from recomp_progress import tracked, WorkProgress
spec = importlib.util.spec_from_file_location("console_recompile", ROOT / "tools/switch2/python/console_recompile.py")
frontend = importlib.util.module_from_spec(spec)
spec.loader.exec_module(frontend)


class ConsoleRecompileTests(unittest.TestCase):
    def test_progress_counts_completed_work_and_preserves_order(self):
        events = []
        owner = SimpleNamespace(progress_callback=lambda *args: events.append(args))
        self.assertEqual(list(tracked(owner, "functions", [3, 1, 2])), [3, 1, 2])
        self.assertEqual(events, [("functions", 0, 3), ("functions", 1, 3),
                                  ("functions", 2, 3), ("functions", 3, 3)])
        # No callback means no changes to ordinary desktop output/state.
        self.assertEqual(list(tracked(object(), "functions", [1])), [1])

    def test_dynamic_progress_uses_explicit_batches(self):
        events = []
        owner = SimpleNamespace(progress_callback=lambda *args: events.append(args))
        work = WorkProgress(owner, "fixpoint")
        work.begin(2)
        work.advance(3)  # re-analysis added work: do not pretend it is finished
        work.begin(3)
        work.advance(2)
        work.begin(2)
        work.advance(1)
        work.begin(1)
        work.advance(0)
        self.assertEqual(events[0], ("fixpoint - lot 1", 0, 2))
        self.assertIn(("fixpoint - lot 2", 0, 2), events)
        self.assertEqual(events[-1], ("fixpoint - lot 2", 2, 2))

    def test_progress_exception_cancels_without_false_completion(self):
        def cancel(*args):
            if args[1]:
                raise RuntimeError("cancelled")
        owner = SimpleNamespace(progress_callback=cancel)
        with self.assertRaisesRegex(RuntimeError, "cancelled"):
            list(tracked(owner, "functions", [1, 2]))

    def test_missing_layout_is_rejected(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as directory:
            with self.assertRaisesRegex(ValueError, "code"):
                frontend.identify(directory)

    def test_unknown_rpx_is_rejected_before_parsing(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as directory:
            root = Path(directory)
            for child in ("code", "content", "meta"):
                (root / child).mkdir()
            (root / "code/cking.rpx").write_bytes(b"not a game")
            with self.assertRaisesRegex(ValueError, "unmodified"):
                frontend.identify(root)

    def test_partial_recompiler_output_is_not_completed(self):
        identity = {"rpx_sha256": "a" * 64}
        fake = SimpleNamespace(run=lambda *args: None)
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as directory:
            with patch.object(frontend, "identify", return_value=(object(), identity)), \
                    patch.object(frontend, "Recompiler", return_value=fake):
                with self.assertRaisesRegex(RuntimeError, "table.c"):
                    frontend.run("synthetic-game", directory)
                self.assertFalse((Path(directory) / "compilation.json").exists())

    def test_nonempty_output_is_preserved(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as directory:
            user_file = Path(directory) / "user-save.dat"
            user_file.write_bytes(b"preserve")
            with patch.object(frontend, "identify", return_value=(object(), {})):
                with self.assertRaisesRegex(ValueError, "empty"):
                    frontend.run("synthetic-game", directory)
            self.assertEqual(user_file.read_bytes(), b"preserve")

    def test_missing_standard_streams_are_logged_and_restored(self):
        def generate(directory, instructions):
            sys.stderr.write("single-precision summaries: synthetic\n")
            print("synthetic translation complete")
            for name in ("table.c", "imports.c", "funcs.h", "report.txt", "code_0.c"):
                (Path(directory) / name).write_text("synthetic test fixture\n")

        with tempfile.TemporaryDirectory(dir=ROOT / "build") as directory:
            with patch.object(frontend, "identify", return_value=(object(), {"rpx_sha256": "a" * 64})), \
                    patch.object(frontend, "Recompiler", return_value=SimpleNamespace(run=generate)), \
                    patch.object(sys, "stdout", None), patch.object(sys, "stderr", None):
                result = frontend.run("synthetic-game", directory)
                self.assertIsNone(sys.stdout)
                self.assertIsNone(sys.stderr)
            self.assertEqual(result["sources"], ["code_0.c", "table.c", "imports.c"])
            log = (Path(directory) / "recompiler.log").read_text(encoding="utf-8")
            self.assertIn("single-precision summaries: synthetic", log)
            self.assertIn("synthetic translation complete", log)

    def test_missing_standard_streams_preserve_original_exception(self):
        def fail(directory, instructions):
            sys.stderr.write("synthetic diagnostic before failure\n")
            raise ValueError("synthetic recompiler failure")

        with tempfile.TemporaryDirectory(dir=ROOT / "build") as directory:
            with patch.object(frontend, "identify", return_value=(object(), {})), \
                    patch.object(frontend, "Recompiler", return_value=SimpleNamespace(run=fail)), \
                    patch.object(sys, "stdout", None), patch.object(sys, "stderr", None):
                with self.assertRaisesRegex(ValueError, "synthetic recompiler failure"):
                    frontend.run("synthetic-game", directory)
                self.assertIsNone(sys.stdout)
                self.assertIsNone(sys.stderr)
            log = (Path(directory) / "recompiler.log").read_text(encoding="utf-8")
            self.assertIn("synthetic diagnostic before failure", log)
            self.assertIn("ValueError: synthetic recompiler failure", log)
            self.assertFalse((Path(directory) / "compilation.json").exists())


if __name__ == "__main__":
    unittest.main()
