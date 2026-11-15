#!/usr/bin/env python3
"""Build prepared SDL/glslang sources locally; never install into devkitPro."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess

from build import ROOT, msys_path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--devkitpro", type=Path, default=os.environ.get("DEVKITPRO", "C:/devkitPro"))
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    devkit = args.devkitpro.resolve()
    steps = []
    for name, options in (
        ("glslang", ["-DENABLE_GLSLANG_BINARIES=OFF", "-DENABLE_OPT=OFF", "-DGLSLANG_TESTS=OFF"]),
        ("sdl", ["-DSDL_SHARED=OFF", "-DSDL_STATIC=ON", "-DSDL_INSTALL=ON",
                 "-DSDL_TESTS=OFF", "-DSDL_EXAMPLES=OFF", "-DSDL_TEST_LIBRARY=OFF",
                 "-DSDL_GPU=OFF", "-DSDL_RENDER_GPU=OFF", "-DSDL_VULKAN=OFF",
                 "-DSDL_CAMERA=OFF", "-DSDL_HIDAPI=OFF", "-DSDL_DIALOG=OFF",
                 # The Switch video files test this before including SDL_internal.h;
                 # disabling PCH otherwise silently compiles those files empty.
                 "-DCMAKE_C_FLAGS=-mcpu=cortex-a78c+crypto -mtp=soft -ftls-model=local-exec "
                 "-ffunction-sections -fdata-sections -D__SWITCH__ -D__SWITCH2__ -DSDL_VIDEO_DRIVER_SWITCH=1"]),
    ):
        source = ROOT / f"build/switch2-{name}"
        if not (source / "CMakeLists.txt").is_file():
            parser.error(f"Prepare sources first (docs/switch2-port.md): {source}")
        build = msys_path(ROOT / f"build/switch2-deps/{name}")
        steps.append(["cmake", "-S", msys_path(source), "-B", build, "-G", "Ninja",
                      "-DCMAKE_TOOLCHAIN_FILE=" + msys_path(ROOT / "cmake/switch2/Switch2.cmake"),
                      "-DOUNCE_SDK_ROOT=" + msys_path(ROOT / "build/switch2-sdk"),
                      "-DOUNCE_LIBRARY_DIR=" + msys_path(ROOT / "build/switch2-libounce-local"),
                      "-DCMAKE_INSTALL_PREFIX=" + msys_path(ROOT / "build/switch2-deps/prefix"),
                      "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON", *options])
        steps.append(["cmake", "--build", build, "--parallel", str(args.jobs)])
        steps.append(["cmake", "--install", build])
    env = os.environ.copy()
    env["DEVKITPRO"] = str(devkit)
    if os.name == "nt":
        script = "set -eu\nexport DEVKITPRO=" + shlex.quote(msys_path(devkit)) + "\n"
        script += "\n".join(shlex.join(step) for step in steps)
        subprocess.run([str(devkit / "msys2/usr/bin/bash.exe"), "-lc", script], env=env, check=True)
    else:
        for step in steps:
            subprocess.run(step, env=env, check=True)


if __name__ == "__main__":
    main()
