#!/usr/bin/env python3
"""Build-PC LLD compatibility check; NEVER copies guest objects into the SDK."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from build import ROOT


def expand_recipe(arguments, sdk, guests, output):
    expanded = []
    for value in arguments:
        if value == "{guest_objects}":
            expanded.extend(str(path) for path in guests)
        else:
            expanded.append(value.replace("{sdk}", sdk.as_posix()).replace("{output}", output.as_posix()))
    return expanded


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, default=ROOT / "build/switch2-install-sdk")
    parser.add_argument("--guest-archive", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--lld", type=Path, default=Path("C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin/ld.lld.exe"))
    args = parser.parse_args()
    sdk = args.sdk.resolve()
    output = args.output.resolve()
    if output.parent == sdk or sdk in output.parents:
        parser.error("Private verification output must remain OUTSIDE the runtime SDK")
    manifest = json.loads((sdk / "link-recipe.json").read_text())
    if manifest["dump_input"] is not None:
        parser.error("Runtime SDK unexpectedly declares game input")
    for record in manifest["files"]:
        if hashlib.sha256((sdk / record["file"]).read_bytes()).hexdigest() != record["sha256"]:
            parser.error("Runtime SDK file hash mismatch: " + record["file"])
    output.parent.mkdir(parents=True, exist_ok=True)
    arguments = expand_recipe(manifest["arguments"], sdk, [args.guest_archive.resolve()], output)
    subprocess.run([str(args.lld), *arguments], check=True)
    print(f"Host LLD compatibility link passed: {output}")
    print("This does not validate console compilation, execution, or a complete installer.")


if __name__ == "__main__":
    main()
