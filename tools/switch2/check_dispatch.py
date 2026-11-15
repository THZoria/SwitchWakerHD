#!/usr/bin/env python3
"""Check the actual dispatch trampoline's AArch64 tail edge at -O0 and -O3."""
import argparse
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang", required=True, type=Path)
    parser.add_argument("--devkitpro", type=Path, default=Path("C:/devkitPro"))
    args = parser.parse_args()
    for optimization in ("-O0", "-O3"):
        ir = subprocess.check_output([
            str(args.clang), "--target=aarch64-none-elf", "-mcpu=cortex-a78c+crypto",
            "-D__SWITCH__", "-D__SWITCH2__", "-fexceptions", "-funwind-tables",
            "-isystem", str(args.devkitpro / "devkitA64/aarch64-none-elf/include"),
            "-I" + str(ROOT / "runtime/include"), optimization, "-S", "-emit-llvm",
            str(ROOT / "runtime/src/platform/switch2_dispatch.c"), "-o", "-",
        ], text=True)
        assert re.search(r"musttail call void .*\n\s*ret void", ir), "mandatory guest tail edge missing"
        assert "@ppc_dispatch_resolve_switch2(" in ir, "C++ resolver call missing"
        definition = re.search(r"define .*@ppc_dispatch\(.*?\)[^\n#]*#(\d+)", ir)
        assert definition, "dispatch entry missing"
        attributes = re.search(r"attributes #" + definition[1] + r" = \{([^}]*)\}", ir)[1]
        assert "uwtable" in attributes, "unwind information missing"
        assert "nounwind" not in attributes, "HLE exceptions cannot propagate"
        assert "icmp eq" in ir or "icmp ne" in ir, "completed-HLE/null-target branch missing"
        print(f"PASS {optimization}: musttail, HLE completion branch, exception-unwind attributes")


if __name__ == "__main__":
    main()
