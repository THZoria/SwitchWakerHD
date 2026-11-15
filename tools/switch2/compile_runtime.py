#!/usr/bin/env python3
"""Compile the real Switch 2 runtime/GX2/Vulkan sources; --nro packages the prototype."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess

from build import ROOT, msys_path


def commands(root: str, jobs: int = 4, nro: bool = False) -> list[list[str]]:
    build = root + ("/build/switch2-game" if nro else "/build/switch2-runtime")
    return [["cmake", "-S", root, "-B", build, "-G", "Ninja",
             "-DCMAKE_TOOLCHAIN_FILE=" + root + "/cmake/switch2/Switch2.cmake",
             "-DOUNCE_SDK_ROOT=" + root + "/build/switch2-sdk",
             "-DOUNCE_LIBRARY_DIR=" + root + "/build/switch2-libounce-local",
             "-DGEN_DIR=" + root + "/build/gen-switch2", "-DCMAKE_BUILD_TYPE=Release",
             "-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON", f"-DWWHD_SWITCH2_BUILD_NRO={'ON' if nro else 'OFF'}"],
            ["cmake", "--build", build, "--target", "wwhd_nro" if nro else "wwhd", "--parallel", str(jobs)]]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--devkitpro", type=Path, default=os.environ.get("DEVKITPRO", "C:/devkitPro"))
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--nro", action="store_true", help="link the experimental runtime and package an NRO (whole-title exit)")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    steps = commands(msys_path(ROOT), args.jobs, args.nro)
    env = os.environ.copy()
    env["DEVKITPRO"] = str(args.devkitpro.resolve())
    if os.name == "nt":
        script = "set -eu\nexport DEVKITPRO=" + shlex.quote(msys_path(args.devkitpro.resolve())) + "\n"
        script += "\n".join(shlex.join(step) for step in steps)
        subprocess.run([str(args.devkitpro / "msys2/usr/bin/bash.exe"), "-lc", script], env=env, check=True)
    else:
        for step in steps:
            subprocess.run(step, env=env, check=True)


if __name__ == "__main__":
    main()
