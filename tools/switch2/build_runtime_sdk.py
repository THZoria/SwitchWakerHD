#!/usr/bin/env python3
"""Build the runtime archive for the future console installer, WITHOUT a dump.

Only stubgen's original placeholder bodies are used for the development link.
They and the placeholder game archive are excluded from the exported runtime.
This archive alone is not a playable NRO or a complete redistribution package.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

from build import ROOT, msys_path


def select_runtime_objects(database, build: Path):
    selected = []
    for entry in database:
        output = entry.get("output", "").replace("\\", "/")
        if not output.startswith("CMakeFiles/wwhd.dir/"):
            continue
        source = entry["file"].replace("\\", "/")
        if "/gen-switch2" in source or "/gamecode-objects/" in output:
            raise ValueError("A generated guest translation cannot enter the runtime SDK")
        selected.append((build / output, source))
    if not selected:
        raise ValueError("No runtime objects in the isolated build")
    return selected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--devkitpro", type=Path, default=os.environ.get("DEVKITPRO", "C:/devkitPro"))
    parser.add_argument("--clang", type=Path)
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if os.name != "nt":
        parser.error("Initial SDK exporter uses the validated Windows/MSYS build")
    clang = args.clang
    if not clang:
        visual_studio = Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "Microsoft Visual Studio"
        candidates = sorted(visual_studio.glob("*/Community/VC/Tools/Llvm/x64/bin/clang.exe"))
        if candidates:
            clang = candidates[-1]
    if not clang or not clang.is_file():
        parser.error("Supply the build-PC Clang used ONLY for the original dispatch trampoline and placeholders")
    generated = ROOT / "build/gen-switch2-sdk-stub"
    guest_build = ROOT / "build/switch2-sdk-stub-build"
    runtime_build = ROOT / "build/switch2-runtime-sdk-build"
    output = ROOT / "build/switch2-runtime-sdk"
    subprocess.run([sys.executable, str(ROOT / "tools/recomp/stubgen.py"), str(generated)], check=True)
    report = (generated / "report.txt").read_text()
    if not report.startswith("placeholder from stubgen.py:"):
        raise ValueError("Runtime SDK must be built against dump-free placeholders")
    manifest = ROOT / "build/switch2-sdk-stub-manifest.json"
    manifest.write_text(json.dumps({"build": "USA", "rpx_sha256": "0" * 64, "entry": 0,
                                    "generated_dir": str(generated)}) + "\n", encoding="utf-8")
    p = msys_path
    sdk_options = ["-DCMAKE_TOOLCHAIN_FILE=" + p(ROOT / "cmake/switch2/Switch2.cmake"),
                   "-DOUNCE_SDK_ROOT=" + p(ROOT / "build/switch2-sdk"),
                   "-DOUNCE_LIBRARY_DIR=" + p(ROOT / "build/switch2-libounce-local"),
                   "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON"]
    steps = [
        ["cmake", "-S", p(ROOT / "tools/switch2"), "-B", p(guest_build), "-G", "Ninja", *sdk_options,
         "-DWWHD_GAME_MANIFEST=" + p(manifest), "-DWWHD_BUILD_GAMECODE=ON", "-DWWHD_CLANG_C_COMPILER=" + p(clang)],
        ["cmake", "--build", p(guest_build), "--target", "wwhd_switch2_gamecode", "--parallel", str(args.jobs)],
        ["cmake", "-S", p(ROOT), "-B", p(runtime_build), "-G", "Ninja", *sdk_options,
         "-DGEN_DIR=" + p(generated), "-DWWHD_SWITCH2_BUILD_NRO=ON",
         "-DWWHD_SWITCH2_GAMECODE=" + p(guest_build / "libwwhd_switch2_gamecode.a")],
        ["cmake", "--build", p(runtime_build), "--target", "wwhd", "--parallel", str(args.jobs)],
    ]
    env = os.environ.copy()
    env["DEVKITPRO"] = str(args.devkitpro.resolve())
    script = "set -eu\nexport DEVKITPRO=" + shlex.quote(p(args.devkitpro.resolve())) + "\n"
    script += "\n".join(shlex.join(step) for step in steps)
    subprocess.run([str(args.devkitpro / "msys2/usr/bin/bash.exe"), "-lc", script], env=env, check=True)
    database = json.loads((runtime_build / "compile_commands.json").read_text())
    objects = select_runtime_objects(database, runtime_build)
    # The required tail trampoline is our own source, not a generated game body.
    objects.append((guest_build / "gamecode-objects/switch2_dispatch.o", "runtime/src/platform/switch2_dispatch.c"))
    for object_file, _ in objects:
        if not object_file.is_file():
            raise ValueError(f"Missing runtime SDK input: {object_file}")
    output.mkdir(parents=True, exist_ok=True)
    members = output / "objects"
    members.mkdir(exist_ok=True)
    member_paths = []
    for index, (object_file, _) in enumerate(objects):
        # GNU ar normally stores basenames: Path.o, shaders.o, ... can collide
        # across directories. Give every member an unambiguous original name.
        member = members / f"runtime_{index:05d}.o"
        shutil.copy2(object_file, member)
        member_paths.append(member)
    response = output / "runtime-inputs.rsp"
    response.write_text("\n".join('"' + obj.as_posix() + '"' for obj in member_paths) + "\n", encoding="utf-8")
    archive = output / "libwwhd_switch2_runtime.a"
    # New output name avoids ar retaining obsolete members from an earlier build.
    temporary = output / "libwwhd_switch2_runtime.building.a"
    if temporary.exists():
        raise ValueError(f"A previous SDK transaction remains: {temporary}; inspect it before retrying")
    subprocess.run([str(args.devkitpro / "devkitA64/bin/aarch64-none-elf-ar.exe"), "rcs", str(temporary),
                    "@" + str(response)], check=True)
    archived = subprocess.check_output([str(args.devkitpro / "devkitA64/bin/aarch64-none-elf-ar.exe"),
                                       "t", str(temporary)], text=True).splitlines()
    if archived != [path.name for path in member_paths]:
        raise ValueError("Runtime archive member list does not match the audited objects")
    temporary.replace(archive)
    audit = {"schema": 1, "kind": "runtime-only; NOT a complete installer", "dump_input": None,
             "placeholder_report": report.strip(), "excluded": ["placeholder gamecode archive", "generated guest bodies"],
             "archive_sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
             "objects": [{"source": source, "sha256": hashlib.sha256(obj.read_bytes()).hexdigest()}
                         for obj, source in objects]}
    (output / "runtime-audit.json").write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8")
    print(f"Runtime-only archive built: {archive}")
    print("Target libraries, link recipe, console compiler and installer are still required; do not distribute this as a complete SDK.")


if __name__ == "__main__":
    main()
