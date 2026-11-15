#!/usr/bin/env python3
"""Build the game-free Switch 2 probe without changing the system devkitPro tree."""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
SDK_VERSION = "20261114"
SDK_SHA256 = "f24d5773b1ee1c2b19317f57e8ae54a60fcdcefe493a0e495297c241c4912f59"
SDK_URL = ("https://github.com/ouncebrew/ounce-devkit/releases/download/"
           f"devkit-{SDK_VERSION}/ounce-devkit-{SDK_VERSION}.tar.xz")
LIBOUNCE_COMMIT = "9382b1ec48ac5dabdaf52f1f99e0c0aacd02cd2f"


def verify_archive(path: Path) -> None:
    with path.open("rb") as source:
        actual = hashlib.file_digest(source, "sha256").hexdigest()
    if actual != SDK_SHA256:
        raise ValueError(f"SDK SHA-256 mismatch: {actual}; expected {SDK_SHA256}")


def fetch_sdk(sdk: Path) -> None:
    sdk.mkdir(parents=True, exist_ok=True)
    archive = sdk / f"ounce-devkit-{SDK_VERSION}.tar.xz"
    if not archive.exists():
        print(f"Downloading {SDK_URL}", flush=True)
        # A failed transfer must not become a seemingly complete archive.
        part = archive.with_suffix(".part")
        with urllib.request.urlopen(SDK_URL, timeout=120) as response, part.open("wb") as out:
            while block := response.read(1024 * 1024):
                out.write(block)
        verify_archive(part)
        part.replace(archive)
    else:
        verify_archive(archive)
    with tarfile.open(archive, "r:xz") as source:
        # Python's data filter rejects traversal, unsafe links and special files.
        source.extractall(sdk, filter="data")


def msys_path(path: Path) -> str:
    text = path.as_posix().replace("\\", "/")
    if len(text) >= 3 and text[1:3] == ":/":
        return f"/{text[0].lower()}/{text[3:]}"
    return text


def commands(build: str, sdk: str, root: str, libraries: str | None = None, *,
             manifest: str | None = None, clang: str | None = None, jobs: int = 2) -> list[list[str]]:
    steps = [
        ["cmake", "-S", f"{root}/tools/switch2", "-B", build, "-G", "Ninja", "-U", "CMAKE_EXE_LINKER_FLAGS",
         f"-DCMAKE_TOOLCHAIN_FILE={root}/cmake/switch2/Switch2.cmake",
         f"-DOUNCE_SDK_ROOT={sdk}", f"-DOUNCE_LIBRARY_DIR={libraries or sdk + '/libounce/lib'}",
         "-DCMAKE_BUILD_TYPE=Release"],
        ["cmake", "--build", build, "--target", "wwhd_switch2_probe_nro",
          "wwhd_switch2_boot_nro", "wwhd_switch2_boot_core_nro", "wwhd_switch2_boot_posix_nro"],
    ]
    steps[0] += [f"-DWWHD_GAME_MANIFEST={manifest or ''}", f"-DWWHD_BUILD_GAMECODE={'ON' if clang else 'OFF'}"]
    if manifest:
        steps[1].append("wwhd_switch2_runtime_probe_nro")
    if clang:
        steps[0].append(f"-DWWHD_CLANG_C_COMPILER={clang}")
        steps[1].append("wwhd_switch2_gamecode")
    steps[1] += ["--parallel", str(jobs)]
    return steps


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--devkitpro", type=Path, default=os.environ.get("DEVKITPRO"))
    parser.add_argument("--sdk", type=Path, default=ROOT / "build/switch2-sdk")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/switch2-first-frame")
    parser.add_argument("--fetch-sdk", action="store_true", help="download and verify the pinned SDK into --sdk")
    parser.add_argument("--rebuild-libounce", action="store_true",
                        help="rebuild pinned libounce with the installed compiler/newlib in build/, without system installation")
    parser.add_argument("--game-manifest", type=Path, help="metadata produced by prepare_game.py; also builds the RPX loader probe")
    parser.add_argument("--build-gamecode", action="store_true", help="also compile the private C game code to an AArch64 static library")
    parser.add_argument("--clang", type=Path, help="Clang executable for generated C only (no runtime ABI change)")
    parser.add_argument("--jobs", type=int, default=2, help="parallel compile jobs (generated functions use much RAM)")
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if args.game_manifest and not args.game_manifest.is_file():
        parser.error("--game-manifest must be produced by prepare_game.py first")
    clang = None
    if args.build_gamecode:
        if not args.game_manifest:
            parser.error("--build-gamecode requires --game-manifest")
        clang = args.clang
        if not clang:
            found = shutil.which("clang")
            if found:
                clang = Path(found)
            elif os.name == "nt":
                vs = Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "Microsoft Visual Studio"
                candidates = sorted(vs.glob("*/Community/VC/Tools/Llvm/x64/bin/clang.exe"))
                if candidates:
                    clang = candidates[-1]
        if not clang or not clang.is_file():
            parser.error("Clang not found; supply --clang PATH (AArch64 musttail support required)")
        clang = clang.resolve()
    if not args.devkitpro:
        parser.error("Set DEVKITPRO or pass --devkitpro")
    devkit = args.devkitpro.resolve()
    sdk = args.sdk.resolve()
    build = args.build_dir.resolve()
    if not (devkit / "cmake/devkitA64.cmake").is_file():
        parser.error(f"No devkitPro toolchain in {devkit}")
    if args.fetch_sdk:
        # Only extract in an empty SDK directory (or alongside our own archive).
        # Do not overwrite an existing installation or an unpacked user's SDK.
        if (sdk / "libounce").exists():
            parser.error(f"SDK already unpacked in {sdk}; omit --fetch-sdk")
        fetch_sdk(sdk)
    if not (sdk / "libounce/include/switch.h").is_file():
        parser.error(f"No ounce-devkit in {sdk}; use --fetch-sdk or --sdk PATH")
    env = os.environ.copy()
    env["DEVKITPRO"] = str(devkit)
    bash = devkit / "msys2/usr/bin/bash.exe"
    if os.name == "nt" and not bash.is_file():
        parser.error(f"MSYS2 bash not found at {bash}")

    def run_steps(steps: list[list[str]]) -> None:
        if os.name == "nt":
            script = f"export DEVKITPRO={shlex.quote(msys_path(devkit))}\nset -eu\n"
            script += "\n".join(shlex.join(step) for step in steps)
            subprocess.run([str(bash), "-lc", script], env=env, check=True)
        else:
            for step in steps:
                subprocess.run(step, env=env, check=True)

    path_arg = msys_path if os.name == "nt" else str
    libraries = None
    if args.rebuild_libounce:
        source = ROOT / "build/libounce-source"
        if not source.exists():
            subprocess.run(["git", "clone", "--no-checkout", "https://github.com/ouncebrew/libounce.git", str(source)], check=True)
            subprocess.run(["git", "-C", str(source), "switch", "--detach", LIBOUNCE_COMMIT], check=True)
        revision = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
        if revision != LIBOUNCE_COMMIT:
            parser.error(f"{source} must be at pinned commit {LIBOUNCE_COMMIT}; refusing to change an existing checkout")
        run_steps([["make", "-C", path_arg(source / "nx"), "-j8", "SWITCH2=1", "lib/libounce.a", "lib/libounce_core.a"]])
        libraries = ROOT / "build/switch2-libounce-local"
        libraries.mkdir(parents=True, exist_ok=True)
        for name in ("libounce.a", "libounce_core.a"):
            shutil.copy2(source / "nx/lib" / name, libraries / name)
        (libraries / "libnx.a").write_text("INPUT(libounce.a)\n", encoding="ascii")
        (libraries / "source-commit.txt").write_text(revision + "\n", encoding="ascii")
        print(f"Using rebuilt libounce: {libraries}", flush=True)

    run_steps(commands(path_arg(build), path_arg(sdk), path_arg(ROOT), path_arg(libraries) if libraries else None,
                       manifest=path_arg(args.game_manifest.resolve()) if args.game_manifest else None,
                       clang=path_arg(clang) if clang else None, jobs=args.jobs))
    print(f"NRO (diagnostic only, not the game): {build / 'wwhd_switch2_probe.nro'}")
    print(f"Minimal startup test: {build / 'wwhd_switch2_boot.nro'}")
    print(f"Startup test without mimalloc: {build / 'wwhd_switch2_boot_core.nro'}")
    print(f"Opt-in POSIX isolation test: {build / 'wwhd_switch2_boot_posix.nro'}")
    if args.game_manifest:
        print(f"Personal RPX loader test (not the game): {build / 'wwhd_switch2_runtime_probe.nro'}")
    if clang:
        print(f"Private AArch64 game-code archive (not a playable NRO): {build / 'libwwhd_switch2_gamecode.a'}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError, tarfile.TarError) as error:
        raise SystemExit(str(error)) from error
