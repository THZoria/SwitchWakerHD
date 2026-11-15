#!/usr/bin/env python3
"""Build the real first-launch installer separately from the private game NRO."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

from build import ROOT, msys_path
from build_compiler import commands, apply_port_patches


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    for name in ("package_link_sdk.py", "package_installer_resources.py"):
        subprocess.run([sys.executable, str(ROOT / "tools/switch2" / name)], check=True)
    resources = json.loads((ROOT / "build/switch2-installer-resources/current.json").read_text())
    apply_port_patches(ROOT / "build/switch2-llvm-source")
    steps = commands(msys_path(ROOT), args.jobs)[2:]
    steps[0] += ["-DWWHD_COMPILER_PYTHON=ON", "-DWWHD_BUILD_INSTALLER=ON",
                 "-DWWHD_INSTALLER_RESOURCES_DIR=" + msys_path(Path(resources["directory"]))]
    steps[1][4] = "wwhd_switch2_installer_nro"
    env = os.environ.copy()
    env["DEVKITPRO"] = "C:/devkitPro"
    script = "set -eu\nexport DEVKITPRO=/c/devkitPro\n" + "\n".join(shlex.join(step) for step in steps)
    subprocess.run(["C:/devkitPro/msys2/usr/bin/bash.exe", "-lc", script], env=env, check=True)
    subprocess.run([sys.executable, str(ROOT / "tools/switch2/verify_installer.py")], check=True)
    print("Installer NRO built: build/switch2-compiler/wwhd_switch2_installer.nro")
    print("On-console first-install/cache/chainload validation is still required.")


if __name__ == "__main__":
    main()
