import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import threading
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from build import ROOT

sys.path.insert(0, str(ROOT / "tools/switch2/pc_builder"))
from builder import (Builder, Cancelled, NRO_NAME, VERSION, UPSTREAM, COPY_INSTRUCTIONS_NAME,
                     FRONTEND_STAGES, english_stage, copy_instructions, digest, relative_path)


class PortableBuilderTests(unittest.TestCase):
    def test_paths_and_copy_instructions(self):
        for value in ("../private", "/absolute", "C:/private", "x/../y", "x/./y", "x\\y", "", "x//y"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                relative_path(value)
        self.assertEqual(relative_path("sdk/runtime.a"), Path("sdk/runtime.a"))
        text = copy_instructions()
        for required in (NRO_NAME, "/switch/", "/switch/wwhd-switch2/game/", "SwitchWakerHD / Zoria", UPSTREAM, VERSION):
            self.assertIn(required, text)
        self.assertIn("Build complete", text)
        self.assertIn("Copy the CONTENTS", text)

    def test_shared_frontend_progress_is_translated_only_for_pc(self):
        events = []
        builder = Builder(ROOT, progress=lambda *args: events.append(args))
        for french, english in FRONTEND_STAGES.items():
            builder.report(french, 1, 2)
            self.assertEqual(events[-1], (english, 1, 2))
        builder.report("Propagation precision PowerPC - lot 3", 4, 10)
        self.assertEqual(events[-1], ("Propagating PowerPC precision - batch 3", 4, 10))
        self.assertEqual(english_stage("Linking the NRO"), "Linking the NRO")

    def test_compiler_uses_only_bundled_paths_and_guest_abi_flags(self):
        builder = Builder(ROOT / "build/synthetic portable assets")
        arguments = list(map(str, builder.compile_arguments(Path("a.c"), Path("a.o"), Path("generated"))))
        self.assertEqual(Path(arguments[0]), builder.assets / "tools/clang.exe")
        for flag in ("--target=aarch64-none-elf", "-fsigned-char", "-ffp-contract=off", "-funwind-tables", "-fexceptions", "-nostdinc"):
            self.assertIn(flag, arguments)
        self.assertNotIn("cmake", " ".join(arguments).lower())

    def test_cancel_is_explicit(self):
        cancel = threading.Event()
        cancel.set()
        with self.assertRaises(Cancelled):
            Builder(ROOT, cancel=cancel).report("test")

    def test_asset_hash_and_unaudited_extra_detection(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as temporary:
            assets = Path(temporary)
            (assets / "original.txt").write_bytes(b"original")
            manifest = {"schema": 1, "version": VERSION, "dump_input": None,
                        "files": [{"path": "original.txt", "sha256": digest(assets / "original.txt")}]}
            (assets / "builder-manifest.json").write_text(json.dumps(manifest))
            Builder(assets).verify_assets()
            (assets / "original.txt").write_bytes(b"corrupt")
            with self.assertRaisesRegex(ValueError, "corrupted"):
                Builder(assets).verify_assets()
            (assets / "unlisted.py").write_text("not allowed")
            with self.assertRaisesRegex(ValueError, "unaudited"):
                Builder(assets).verify_assets()

    def test_output_inside_dump_is_rejected_before_any_build(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as temporary:
            with self.assertRaisesRegex(ValueError, "outside the game dump"):
                Builder(ROOT).build(temporary, Path(temporary) / "output")

    def test_transaction_and_romfs_layout_with_synthetic_tools(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as temporary:
            root = Path(temporary)
            game, output, assets = root / "dump", root / "output", root / "assets"
            for folder in ("code", "content/empty", "meta"):
                (game / folder).mkdir(parents=True)
            (game / "code/cking.rpx").write_bytes(b"synthetic dump fixture, not a game")
            (game / "content/data.bin").write_bytes(bytes(range(256)))
            (game / "meta/meta.xml").write_text("synthetic metadata")
            identity = {"rpx_sha256": digest(game / "code/cking.rpx")}
            sdk = assets / "resources/sdk"
            sdk.mkdir(parents=True)
            (sdk / "link-recipe.json").write_text(json.dumps({"arguments": ["{guest_objects}", "-o", "{output}"]}))
            def translate(source, destination, progress):
                generated = Path(destination)
                generated.mkdir()
                (generated / "code_0.c").write_text("/* synthetic */")
                (generated / "recompiler.log").write_text("synthetic progress")
                return {**identity, "sources": ["code_0.c"]}
            frontend = SimpleNamespace(identify=lambda game: (None, identity), run=translate)
            commands = []
            def tool(arguments, log_path=None):
                commands.append(list(map(str, arguments)))
                name = Path(arguments[0]).name
                if name == "clang.exe":
                    Path(arguments[arguments.index("-o") + 1]).write_bytes(b"synthetic object")
                elif name == "wwhd-nro-pack.exe":
                    Path(arguments[2]).write_bytes(b"synthetic result - not an actual NRO")
                return ""
            activities = []
            builder = Builder(assets, activity=activities.append)
            with patch.object(builder, "verify_assets"), patch.object(builder, "frontend", return_value=frontend), \
                    patch.object(builder, "run_tool", side_effect=tool):
                result = builder.build(game, output)
            self.assertEqual(result.nro.name, NRO_NAME)
            self.assertEqual((result.romfs / "code/cking.rpx").read_bytes(), (game / "code/cking.rpx").read_bytes())
            self.assertTrue((result.romfs / "content/empty").is_dir())
            instructions = result.directory / COPY_INSTRUCTIONS_NAME
            self.assertTrue(instructions.is_file())
            self.assertIn("Copy the CONTENTS", instructions.read_text(encoding="utf-8"))
            self.assertFalse(builder.work.exists())
            self.assertEqual(len(commands), 3)
            self.assertTrue(all(Path(command[0]).is_relative_to(assets) for command in commands))
            self.assertIn("Compiling code_0.c -> code_0.c.o", activities)
            self.assertIn("Compiled code_0.c", activities)
            self.assertIn("Copying content/data.bin", activities)
            self.assertIn("Creating and validating " + NRO_NAME, activities)
            self.assertTrue(activities[-1].startswith("Output ready: "))

    def test_gui_removes_static_build_banner(self):
        source = (ROOT / "tools/switch2/pc_builder/app.py").read_text(encoding="utf-8")
        self.assertNotIn("Standalone and offline:", source)
        self.assertNotIn("Do not move the game dump during this operation.", source)
        self.assertIn('elif kind == "activity":', source)


if __name__ == "__main__":
    unittest.main()
