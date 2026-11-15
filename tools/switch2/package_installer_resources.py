#!/usr/bin/env python3
"""Stage only original compiler/runtime resources for the installer RomFS."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from build import ROOT
from build_compiler import LLVM_COMMIT
from build_python import PYTHON_COMMIT


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--devkitpro", type=Path, default=Path("C:/devkitPro"))
    args = parser.parse_args()
    base = ROOT / "build/switch2-installer-resources"
    base.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="staging-", dir=base) as scratch:
        stage = Path(scratch)

        def copy(source, destination):
            target = stage / destination
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)

        python_lib = ROOT / "build/switch2-python-source/Lib"
        for path in python_lib.rglob("*.py"):
            relative = path.relative_to(python_lib)
            if any(part in ("test", "tests", "idlelib", "tkinter", "ensurepip", "venv", "__pycache__") for part in relative.parts):
                continue
            copy(path, Path("python/Lib") / relative)
        for path in (ROOT / "tools/recomp").rglob("*"):
            if path.is_file() and path.suffix in (".py", ".txt", ".json", ".h") and not path.name.startswith("test_"):
                copy(path, path.relative_to(ROOT))
        copy(ROOT / "tools/rpx.py", "tools/rpx.py")
        copy(ROOT / "tools/switch2/python/console_recompile.py", "tools/switch2/python/console_recompile.py")
        for folder in (ROOT / "runtime/src", ROOT / "runtime/include"):
            for path in folder.rglob("*"):
                if path.is_file() and path.suffix in (".c", ".cpp", ".h", ".inc"):
                    copy(path, path.relative_to(ROOT))
        clang = ROOT / "build/switch2-llvm-source/clang/lib/Headers"
        for path in clang.rglob("*.h"):
            copy(path, Path("clang/include") / path.relative_to(clang))
        libc = args.devkitpro / "devkitA64/aarch64-none-elf/include"
        for path in libc.rglob("*"):
            relative = path.relative_to(libc)
            if path.is_file() and relative.parts[0] != "c++":
                copy(path, Path("libc/include") / relative)
        sdk = ROOT / "build/switch2-install-sdk"
        recipe = json.loads((sdk / "link-recipe.json").read_text())
        if recipe["dump_input"] is not None:
            raise ValueError("Refusing game-derived SDK resources")
        copy(sdk / "link-recipe.json", "sdk/link-recipe.json")
        for record in recipe["files"]:
            source = sdk / record["file"]
            if digest(source) != record["sha256"]:
                raise ValueError("SDK input hash mismatch")
            copy(source, Path("sdk") / record["file"])
        copy(ROOT / "build/switch2-branding/icon-256.jpg", "branding/icon.jpg")
        subprocess.run([str(args.devkitpro / "tools/bin/nacptool.exe"), "--create", "Wind Waker HD",
                        "Zoria / SwitchWakerHD contributors", "0.0.1", str(stage / "branding/game.nacp")], check=True)
        files = [{"path": path.relative_to(stage).as_posix(), "size": path.stat().st_size, "sha256": digest(path)}
                 for path in sorted(stage.rglob("*")) if path.is_file()]
        # Bind the cache to the original installer implementation as well as
        # all packaged assets; never to a build-PC path or a dummy marker.
        implementation = [{"path": path.name, "sha256": digest(path)}
                          for path in sorted((ROOT / "tools/switch2/compiler").glob("*")) if path.is_file()]
        import sys
        sys.path.insert(0, str(ROOT / "tools/recomp"))
        import builds
        manifest = {"schema": 1, "llvm": LLVM_COMMIT, "python": PYTHON_COMMIT,
                    "supported_rpx": [build.sha256 for build in builds.all_builds()],
                    "files": files, "implementation": implementation, "dump_input": None}
        data = (json.dumps(manifest, sort_keys=True, separators=(",", ":")) + "\n").encode()
        identity = hashlib.sha256(data).hexdigest()
        final = base / identity
        if not final.exists():
            shutil.copytree(stage, final)
            (final / "resources.json").write_bytes(data)
        elif (final / "resources.json").read_bytes() != data:
            raise ValueError("Unexpected preexisting resources at content-addressed destination")
        info = {"directory": str(final), "manifest_sha256": identity,
                "files": len(files), "bytes": sum(record["size"] for record in files)}
        (base / "current.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(info, indent=2))


if __name__ == "__main__":
    main()
