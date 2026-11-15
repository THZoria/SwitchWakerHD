#!/usr/bin/env python3
"""Export original runtime dependencies and an embedded-LLD link recipe.

No dump or placeholder guest archive enters this package. A caller supplies real
locally compiled guest objects when replaying the recipe. Licensing review is
still required before distributing third-party toolchain/dependency archives.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess

from build import ROOT, msys_path


def native_path(value, base):
    if len(value) > 2 and value[0] == "/" and value[2] == "/" and value[1].isalpha():
        value = value[1] + ":" + value[2:]
    path = Path(value)
    return path if path.is_absolute() else base / path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--devkitpro", type=Path, default=os.environ.get("DEVKITPRO", "C:/devkitPro"))
    args = parser.parse_args()
    build = ROOT / "build/switch2-runtime-sdk-build"
    runtime = ROOT / "build/switch2-runtime-sdk"
    output = ROOT / "build/switch2-install-sdk"
    audit = json.loads((runtime / "runtime-audit.json").read_text())
    archive = runtime / "libwwhd_switch2_runtime.a"
    if audit["dump_input"] is not None or hashlib.sha256(archive.read_bytes()).hexdigest() != audit["archive_sha256"]:
        raise ValueError("Runtime SDK audit/hash mismatch")
    command = ["ninja", "-C", msys_path(build), "-t", "commands", "wwhd"]
    shell = args.devkitpro / "msys2/usr/bin/bash.exe"
    lines = subprocess.check_output([str(shell), "-lc", shlex.join(command)], text=True).splitlines()
    links = [line for line in lines if " -o wwhd_switch2.elf " in line]
    if len(links) != 1:
        raise ValueError("Cannot identify isolated runtime link command")
    arguments = shlex.split(links[0])
    libraries = []
    # The CMake link line's explicit dependency archives, not its guest archive.
    for value in arguments[arguments.index("-o") + 2:]:
        if not value.endswith(".a"):
            continue
        if value.endswith("/libwwhd_switch2_gamecode.a"):
            continue
        path = native_path(value, build).resolve()
        if path not in libraries:
            libraries.append(path)
    gcc = args.devkitpro / "devkitA64/bin/aarch64-none-elf-gcc.exe"
    flags = ["-fPIE", "-mcpu=cortex-a78c+crypto", "-mtp=soft", "-ftls-model=local-exec"]

    def toolchain_file(name):
        path = Path(subprocess.check_output([str(gcc), *flags, "-print-file-name=" + name], text=True).strip()).resolve()
        if not path.is_file():
            raise ValueError(f"Missing exact target toolchain file: {name}")
        return path

    # nx is rebuilt for this GCC/newlib, not the prebuilt archive from an old ABI.
    libraries += [toolchain_file("libstdc++.a"), toolchain_file("libm.a"),
                  ROOT / "build/switch2-libounce-local/libnx.a", toolchain_file("libgcc.a"),
                  toolchain_file("libc.a"), toolchain_file("libsysbase.a")]
    expanded = []

    def expand(path, visiting=()):
        if path in visiting:
            raise ValueError(f"Recursive SDK linker script: {path}")
        with path.open("rb") as stream:
            signature = stream.read(8)
        if signature == b"!<arch>\n":
            if path not in expanded:
                expanded.append(path)
            return
        # Some SDK *.a names are GNU INPUT scripts. Expand only this narrow
        # audited syntax, never arbitrary linker instructions from the SD card.
        text = path.read_text().strip()
        match = re.fullmatch(r"INPUT\(([-\w+ .]+)\)", text)
        if not match:
            raise ValueError(f"Unsupported SDK linker input: {path}")
        for token in shlex.split(match[1]):
            if re.fullmatch(r"-l[\w+.-]+", token):
                name = "lib" + token[2:] + ".a"
            elif re.fullmatch(r"[\w+-]+\.a", token):
                name = token
            else:
                raise ValueError(f"Unsupported SDK linker script token: {token}")
            candidates = [path.parent / name, ROOT / "build/switch2-sdk/portlibs/switch2/lib" / name]
            dependency = next((item for item in candidates if item.is_file()), None)
            expand((dependency or toolchain_file(name)).resolve(), (*visiting, path))

    for library in libraries:
        expand(library)
    libraries = expanded
    if not libraries:
        raise ValueError("No runtime link dependencies")
    output.mkdir(parents=True, exist_ok=True)
    strip = args.devkitpro / "devkitA64/bin/aarch64-none-elf-strip.exe"
    records = []

    def copy_input(source, name, debug=False):
        destination = output / name
        if debug:
            subprocess.run([str(strip), "--strip-debug", "-o", str(destination), str(source)], check=True)
        else:
            shutil.copy2(source, destination)
        records.append({"file": name, "source": str(source), "sha256": hashlib.sha256(destination.read_bytes()).hexdigest()})
        return "{sdk}/" + name

    runtime_argument = copy_input(archive, "runtime.a", True)
    library_arguments = [copy_input(path, f"dependency_{i:03d}.a", True) for i, path in enumerate(libraries)]
    startup = {name: copy_input(toolchain_file(name), name) for name in ("crti.o", "crtbegin.o", "crtend.o", "crtn.o")}
    script = ROOT / "build/switch2-sdk/libounce/switch.ld"
    # GNU CONSTRUCTORS only supports legacy a.out; ELF constructors are retained
    # explicitly in .init_array/.fini_array. LLD rejects this no-op directive.
    text = script.read_text().replace("\t\tSORT(CONSTRUCTORS)\n", "")
    # Do not merge distinct authenticated RELR section types emitted by newer
    # host LLD versions into the ordinary Horizon RELR output section.
    text = text.replace("*(.relr.*)", "*(.relr.dyn)")
    # Unlike GNU ld, LLD requires an explicit PT_TLS when a PHDRS block is
    # present, to resolve local-exec STT_TLS offsets. NRO conversion still uses
    # only the three PT_LOADs; libounce consumes the unchanged __tls_* symbols.
    text = text.replace("\tdyn    PT_DYNAMIC;", "\tdyn    PT_DYNAMIC;\n\ttls    PT_TLS FLAGS(4);")
    text = text.replace("\t} :data\n\n\tPROVIDE_HIDDEN( __tdata_lma", "\t} :data :tls\n\n\tPROVIDE_HIDDEN( __tdata_lma")
    text = text.replace("\t} :data\n\n\t.bss", "\t} :data :tls\n\n\t.bss")
    (output / "switch.ld").write_text(text, encoding="utf-8")
    module = output / "module_name.o"
    subprocess.run([str(gcc), *flags, "-c", str(ROOT / "tools/switch2/compiler/runtime_module_name.c"),
                    "-o", str(module)], check=True)
    for path in (output / "switch.ld", module):
        records.append({"file": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    wraps = ("main", "exit", "_Exit", "_exit", "abort", "pthread_create", "pthread_detach", "getcwd")
    recipe = ["-m", "aarch64elf", "-pie", "--no-dynamic-linker", "--gc-sections", "-z", "text", "-z", "now",
              "-z", "pack-relative-relocs", "--build-id=sha1", "--hash-style=sysv", "--no-undefined",
              "--threads=1", "--no-mmap-output-file", "--undefined=_start", "-T", "{sdk}/switch.ld",
              *["--wrap=" + name for name in wraps], startup["crti.o"], startup["crtbegin.o"], "{sdk}/module_name.o",
              "--start-group", "--whole-archive", runtime_argument, "--no-whole-archive", "{guest_objects}",
              *library_arguments, "--end-group", startup["crtend.o"], startup["crtn.o"], "-o", "{output}"]
    manifest = {"schema": 1, "kind": "original-runtime-link-sdk", "dump_input": None,
                "runtime_audit_sha256": hashlib.sha256((runtime / "runtime-audit.json").read_bytes()).hexdigest(),
                "arguments": recipe, "files": records}
    (output / "link-recipe.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Exported {len(records)} original runtime/link inputs; no guest archive: {output}")


if __name__ == "__main__":
    main()
