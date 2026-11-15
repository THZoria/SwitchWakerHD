#!/usr/bin/env python3
"""Build the actual console-hosted Clang backend (not yet a playable installer).

Native tablegen runs on the build PC; the resulting Clang libraries target
Switch 2. No game dump or generated game function is an input to this build.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess

from build import ROOT, msys_path

LLVM_COMMIT = "ab4b5a2db582958af1ee308a790cfdb42bd24720"


def apply_port_patches(source: Path):
    for patch in sorted((ROOT / "tools/switch2/compiler/patches").glob("*.patch")):
        command = ["git", "-C", str(source), "apply"]
        already_applied = subprocess.run([*command, "--reverse", "--check", str(patch)],
                                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if already_applied.returncode == 0:
            continue
        subprocess.run([*command, "--check", str(patch)], check=True)
        subprocess.run([*command, str(patch)], check=True)


def commands(root: str, jobs: int):
    source = root + "/build/switch2-llvm-source"
    native = root + "/build/switch2-llvm-native"
    console = root + "/build/switch2-compiler"
    minimal = ["-DCMAKE_BUILD_TYPE=Release", "-DLLVM_TARGETS_TO_BUILD=AArch64", "-DLLVM_ENABLE_PROJECTS=clang",
               "-DLLVM_INCLUDE_TESTS=OFF", "-DLLVM_INCLUDE_BENCHMARKS=OFF", "-DLLVM_INCLUDE_EXAMPLES=OFF",
               "-DLLVM_INCLUDE_DOCS=OFF", "-DLLVM_ENABLE_ZLIB=OFF", "-DLLVM_ENABLE_ZSTD=OFF",
               "-DLLVM_ENABLE_LIBXML2=OFF", "-DLLVM_ENABLE_TERMINFO=OFF", "-DLLVM_ENABLE_LIBEDIT=OFF"]
    return [
        ["cmake", "-S", source + "/llvm", "-B", native, "-G", "Ninja",
         "-DCMAKE_C_COMPILER=/usr/bin/gcc", "-DCMAKE_CXX_COMPILER=/usr/bin/g++", *minimal],
        ["cmake", "--build", native, "--target", "llvm-tblgen", "clang-tblgen", "--parallel", str(jobs)],
        ["cmake", "-S", root + "/tools/switch2/compiler", "-B", console, "-G", "Ninja",
         "-DCMAKE_TOOLCHAIN_FILE=" + root + "/cmake/switch2/Switch2.cmake",
         "-DOUNCE_SDK_ROOT=" + root + "/build/switch2-sdk",
         "-DOUNCE_LIBRARY_DIR=" + root + "/build/switch2-libounce-local",
         "-DWWHD_LLVM_SOURCE=" + source, "-DWWHD_NATIVE_TOOLS=" + native + "/bin",
         "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON"],
        ["cmake", "--build", console, "--target", "wwhd_console_clang", "--parallel", str(jobs)],
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--devkitpro", type=Path, default=os.environ.get("DEVKITPRO", "C:/devkitPro"))
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--console-only", action="store_true", help="reuse completed host tablegen tools")
    parser.add_argument("--support-only", action="store_true", help="port LLVM Support first and collect its platform errors")
    parser.add_argument("--link-check", action="store_true", help="link a development ELF against the full compiler; NOT an installer NRO")
    parser.add_argument("--with-python", action="store_true", help="include the built console CPython RPX frontend")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if args.support_only and args.link_check:
        parser.error("--support-only and --link-check are mutually exclusive")
    if os.name != "nt":
        parser.error("This initial bootstrap uses the validated Windows/MSYS host toolchain")
    source = ROOT / "build/switch2-llvm-source"
    revision = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if revision != LLVM_COMMIT:
        parser.error("LLVM checkout differs from the pinned compiler; refusing to modify it")
    if not (source / "clang/CMakeLists.txt").is_file():
        parser.error("Sparse checkout must include llvm, clang, cmake and third-party")
    apply_port_patches(source)
    env = os.environ.copy()
    env["DEVKITPRO"] = str(args.devkitpro.resolve())
    script = "set -eu\nexport DEVKITPRO=" + shlex.quote(msys_path(args.devkitpro.resolve())) + "\n"
    steps = commands(msys_path(ROOT), args.jobs)
    steps[2].append("-DWWHD_COMPILER_PYTHON=" + ("ON" if args.with_python else "OFF"))
    if args.console_only:
        native = ROOT / "build/switch2-llvm-native/bin"
        if not all((native / name).is_file() for name in ("llvm-tblgen.exe", "clang-tblgen.exe")):
            parser.error("Build the native tablegen tools first")
        steps = steps[2:]
    if args.support_only:
        steps[-1][4] = "LLVMSupport"
        steps[-1].extend(["--", "-k", "0"])
    elif args.link_check:
        steps[-1][4] = "wwhd_compiler_link_check"
    script += "\n".join(shlex.join(command) for command in steps)
    subprocess.run([str(args.devkitpro / "msys2/usr/bin/bash.exe"), "-lc", script], env=env, check=True)
    print("Development compiler ELF linked; this is NOT an RPX installer." if args.link_check else
          "LLVM support library built." if args.support_only else
          "Console-hosted compiler library built. This is not a game-free playable NRO yet.")


if __name__ == "__main__":
    main()
