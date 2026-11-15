#!/usr/bin/env python3
"""Cross-build a static embedded CPython for the offline console RPX frontend."""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

from build import ROOT, msys_path

PYTHON_COMMIT = "3d6d2a63a23ee4d340ed8df6de0bd70ef175c4cf"


def apply_patches(source):
    for patch in sorted((ROOT / "tools/switch2/python/patches").glob("*.patch")):
        command = ["git", "-C", str(source), "apply"]
        if subprocess.run([*command, "--reverse", "--check", str(patch)],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
            continue
        subprocess.run([*command, "--check", str(patch)], check=True)
        subprocess.run([*command, str(patch)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--configure-only", action="store_true")
    parser.add_argument("--build-only", action="store_true", help="reuse an already configured console build")
    parser.add_argument("--devkitpro", type=Path, default=os.environ.get("DEVKITPRO", "C:/devkitPro"))
    args = parser.parse_args()
    if args.jobs < 1 or sys.version_info[:2] != (3, 12):
        parser.error("Use build-PC Python 3.12 and a positive --jobs value")
    if args.configure_only and args.build_only:
        parser.error("--configure-only and --build-only are mutually exclusive")
    source = ROOT / "build/switch2-python-source"
    revision = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if revision != PYTHON_COMMIT:
        parser.error("CPython checkout differs from pinned revision")
    apply_patches(source)
    output = ROOT / "build/switch2-python"
    (output / "Modules").mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / "tools/switch2/python/Setup.local", output / "Modules/Setup.local")
    sdk = ROOT / "build/switch2-sdk"
    compiler = args.devkitpro / "devkitA64/bin/aarch64-none-elf-"
    p = msys_path
    env = os.environ.copy()
    env["DEVKITPRO"] = str(args.devkitpro.resolve())
    values = {
        "CC": p(Path(str(compiler) + "gcc.exe")), "CXX": p(Path(str(compiler) + "g++.exe")),
        "AR": p(Path(str(compiler) + "ar.exe")), "RANLIB": p(Path(str(compiler) + "ranlib.exe")),
        "READELF": p(Path(str(compiler) + "readelf.exe")),
        "CFLAGS": "-O2 -fPIC -fsigned-char -ffunction-sections -fdata-sections -mcpu=cortex-a78c+crypto -mtp=soft -ftls-model=local-exec -D__SWITCH__ -D__SWITCH2__ -D_GNU_SOURCE",
        "CPPFLAGS": f"-I{p(sdk / 'libounce/include')} -I{p(args.devkitpro / 'portlibs/switch/include')}",
        "LDFLAGS": f"-L{p(ROOT / 'build/switch2-libounce-local')} -L{p(args.devkitpro / 'portlibs/switch/lib')} -specs={p(ROOT / 'build/switch2-compiler/switch2.specs')} -Wl,-T,{p(sdk / 'libounce/switch.ld')}",
        "LIBS": "-lnx -lm", "CONFIG_SITE": p(ROOT / "tools/switch2/python/config.site"),
        "ZLIB_CFLAGS": f"-I{p(args.devkitpro / 'portlibs/switch/include')}", "ZLIB_LIBS": "-lz",
    }
    configure = ["bash", p(source / "configure"), "--host=aarch64-none-elf", "--build=x86_64-pc-msys",
                 "--with-build-python=" + p(Path(sys.executable)), "--disable-shared", "--disable-ipv6",
                 "--disable-test-modules", "--without-ensurepip", "--without-readline", "--with-pkg-config=no",
                 "--with-builtin-hashlib-hashes=sha2", "--prefix=/switch/wwhd-switch2/compiler/python"]
    script = "set -eu\n" + "\n".join("export " + name + "=" + shlex.quote(value) for name, value in values.items())
    script += "\ncd " + shlex.quote(p(output))
    if not args.build_only:
        script += "\n" + shlex.join(configure)
    if not args.configure_only:
        script += "\n" + shlex.join(["make", "-k", "-j", str(args.jobs), "libpython3.12.a"])
    subprocess.run([str(args.devkitpro / "msys2/usr/bin/bash.exe"), "-lc", script], env=env, check=True)
    print("CPython configured." if args.configure_only else "Console-targeted CPython archive built; console link/run checks still required.")


if __name__ == "__main__":
    main()
