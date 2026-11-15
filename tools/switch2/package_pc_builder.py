#!/usr/bin/env python3
"""Developer-side packaging only: produce an offline, dependency-free PC ZIP.

Run with build/pc-builder-venv/Scripts/python.exe after build_runtime_sdk.py
and package_link_sdk.py. End users only extract the ZIP and run the EXE.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

from PIL import Image, ImageOps
import pefile

from build import ROOT

sys.path.insert(0, str(ROOT / "tools/switch2/pc_builder"))
from builder import VERSION, UPSTREAM, CREDIT, digest, copy_instructions

SYSTEM_DLLS = {"kernel32.dll", "advapi32.dll", "ntdll.dll", "version.dll", "oleaut32.dll",
               "user32.dll", "shell32.dll", "ws2_32.dll", "ole32.dll", "bcrypt.dll", "secur32.dll",
               "gdi32.dll", "comdlg32.dll", "winmm.dll", "msvcrt.dll"}


def audit_native_tool(path):
    pe = pefile.PE(str(path))
    try:
        imports = {entry.dll.decode("ascii").lower() for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", [])}
        imports.update(entry.dll.decode("ascii").lower() for entry in getattr(pe, "DIRECTORY_ENTRY_DELAY_IMPORT", []))
        extra = imports - SYSTEM_DLLS
        if extra:
            raise ValueError(f"{path.name} requires unbundled runtime libraries: {sorted(extra)}")
        return sorted(imports)
    finally:
        pe.close()


def build_native_packer(output, work):
    vcvars = Path("C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat")
    script = work / "compiler-env.cmd"
    script.write_text(f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\nset\n', encoding="utf-8")
    values = subprocess.check_output([os.environ.get("COMSPEC", "cmd.exe"), "/d", "/c", str(script)], text=True)
    env = os.environ.copy()
    env.update(line.split("=", 1) for line in values.splitlines() if "=" in line and not line.startswith("="))
    compiler = shutil.which("cl.exe", path=env.get("Path", env.get("PATH")))
    if not compiler:
        raise ValueError("Developer-side MSVC compiler missing")
    subprocess.run([compiler, "/nologo", "/std:c++17", "/EHsc", "/MT", "/O2", "/DNDEBUG",
                    "/D_CRT_SECURE_NO_WARNINGS", "/Fe" + str(output),
                    str(ROOT / "tools/switch2/pc_builder/native_pack.cpp"),
                    str(ROOT / "tools/switch2/compiler/nro_pack.cpp"),
                    str(ROOT / "tools/switch2/compiler/nro_validate.cpp")], cwd=work, env=env, check=True)


def audit_runtime_symbols(archive, audit):
    # Two upstream original no-op fallbacks deliberately use guest-style names.
    # They are not generated bodies, and real translations override them.
    fallbacks = {"f_025F9448_orig": "runtime/src/language_region.cpp",
                 "f_027F4D5C_orig": "runtime/src/model_probe.cpp"}
    import re
    result = subprocess.run(["C:/devkitPro/devkitA64/bin/aarch64-none-elf-nm.exe", "-g", "--defined-only", str(archive)],
                            capture_output=True, text=True, check=True)
    member = None
    found = []
    for line in result.stdout.splitlines():
        match = re.fullmatch(r"runtime_(\d+)\.o:", line)
        if match:
            member = int(match[1])
        symbol = re.search(r"\b([A-Za-z]) (f_[0-9A-Fa-f]{8}(?:_orig)?)$", line)
        if symbol:
            kind, name = symbol.groups()
            source = audit["objects"][member]["source"].replace("\\", "/") if member is not None else ""
            if kind != "W" or name not in fallbacks or not source.endswith("/" + fallbacks[name]):
                raise ValueError("Generated or unaudited guest symbol in runtime SDK: " + line)
            found.append({"symbol": name, "source": fallbacks[name], "kind": "original weak fallback"})
    return found


def stage_resources(assets, llvm):
    resources = assets / "resources"
    def copy(source, relative):
        destination = resources / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)

    # These source directories are original project code. Never copy build/gen*,
    # a private archive, a user's cache, game manifests or shader recordings.
    for folder in (ROOT / "tools/recomp", ROOT / "runtime/src", ROOT / "runtime/include"):
        for path in folder.rglob("*"):
            if path.is_file() and path.suffix in (".py", ".txt", ".json", ".c", ".cpp", ".h", ".inc") \
                    and not path.name.startswith("test_") and "__pycache__" not in path.parts:
                copy(path, path.relative_to(ROOT))
    copy(ROOT / "tools/rpx.py", Path("tools/rpx.py"))
    copy(ROOT / "tools/switch2/python/console_recompile.py", Path("tools/switch2/python/console_recompile.py"))
    for source, target in ((ROOT / "build/switch2-llvm-source/clang/lib/Headers", "clang/include"),
                            (Path("C:/devkitPro/devkitA64/aarch64-none-elf/include"), "libc/include")):
        for path in source.rglob("*"):
            if path.is_file() and "c++" not in path.relative_to(source).parts:
                copy(path, Path(target) / path.relative_to(source))
    sdk = ROOT / "build/switch2-install-sdk"
    recipe = json.loads((sdk / "link-recipe.json").read_text())
    audit = json.loads((ROOT / "build/switch2-runtime-sdk/runtime-audit.json").read_text())
    if recipe["dump_input"] is not None or audit["dump_input"] is not None:
        raise ValueError("The portable builder must not contain a game archive")
    for record in recipe["files"]:
        path = sdk / record["file"]
        if digest(path) != record["sha256"]:
            raise ValueError("SDK hash mismatch: " + record["file"])
        copy(path, Path("sdk") / record["file"])
        record.pop("source", None)
    fallbacks = audit_runtime_symbols(resources / "sdk/runtime.a", audit)
    (resources / "sdk/runtime-origin.json").write_text(json.dumps({
        "dump_input": None, "original_objects": len(audit["objects"]),
        "generated_guest_inputs": [], "original_weak_fallbacks": fallbacks,
        "runtime_audit_sha256": digest(ROOT / "build/switch2-runtime-sdk/runtime-audit.json")}, indent=2) + "\n", encoding="utf-8")
    (resources / "sdk/link-recipe.json").write_text(json.dumps(recipe, indent=2) + "\n", encoding="utf-8")
    branding = resources / "branding"
    branding.mkdir()
    shutil.copy2(ROOT / "build/switch2-branding/icon-256.jpg", branding / "icon.jpg")
    shutil.copy2(ROOT / "build/switch2-branding/provenance.json", branding / "provenance.json")
    with Image.open(ROOT / "build/switch2-branding/wind-waker-hd-logo.png") as image:
        logo = ImageOps.contain(image.convert("RGBA"), (220, 110))
        logo.save(branding / "logo.png")
        icon = Image.new("RGBA", (256, 256))
        small = ImageOps.contain(image.convert("RGBA"), (248, 248))
        icon.alpha_composite(small, ((256 - small.width) // 2, (256 - small.height) // 2))
        icon.save(branding / "builder.ico", sizes=[(16, 16), (32, 32), (48, 48), (256, 256)])
    # A fresh NACP, not metadata copied from a user's playable game artifact.
    nacp = bytearray(0x4000)
    for language in range(16):
        base = language * 0x300
        title = b"Wind Waker HD"
        author = CREDIT.encode("utf-8")
        nacp[base:base + len(title)] = title
        nacp[base + 0x200:base + 0x200 + len(author)] = author
    nacp[0x3060:0x3060 + len(VERSION)] = VERSION.encode("ascii")
    # Use nacptool defaults for non-text policy bytes, then set exact credits.
    seed = ROOT / "build/switch2-installer-resources" / "current.json"
    if seed.is_file():
        default = Path(json.loads(seed.read_text())["directory"]) / "branding/game.nacp"
        if default.is_file():
            original = default.read_bytes()
            nacp[0x3000:] = original[0x3000:]
            nacp[0x3060:0x3070] = VERSION.encode().ljust(16, b"\0")
    (branding / "game.nacp").write_bytes(nacp)
    license_dir = assets / "licenses"
    license_dir.mkdir()
    shutil.copy2(ROOT / "LICENSE", license_dir / "SwitchWakerHD-LICENSE.txt")
    shutil.copy2(ROOT / "build/switch2-llvm-source/llvm/LICENSE.TXT", license_dir / "LLVM-LICENSE.txt")
    python_license = Path(sys.base_prefix) / "LICENSE.txt"
    if python_license.is_file():
        shutil.copy2(python_license, license_dir / "Python-LICENSE.txt")
    for path in (ROOT / "runtime/third_party").rglob("*"):
        if path.is_file() and path.name.lower().startswith(("license", "copying")):
            destination = license_dir / "runtime" / path.relative_to(ROOT / "runtime/third_party")
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, destination)
    (license_dir / "NOTICE.txt").write_text(
        f"Wind Waker HD Switch 2 {VERSION}\nOriginal project: {UPSTREAM}\n{CREDIT}\n\n"
        "The package contains original runtime/link libraries, not game code.\n"
        "LLVM/Clang/LLD 19.1.5: https://github.com/llvm/llvm-project/tree/llvmorg-19.1.5\n"
        "Python and Tk are bundled by PyInstaller.\n"
        "Target libraries: devkitA64 GCC/newlib, libounce, SDL3, OunceVK and their dependencies.\n"
        "Review all third-party license/source obligations before a public release.\n"
        "Nintendo artwork is supplied at the owner's request for local use; resizing it grants no redistribution rights.\n"
        "Generated NROs and copied game resources are private and must not be included in a public builder ZIP.\n",
        encoding="utf-8")
    # Ship the modified builder/converter sources as well as their license.
    source_root = assets / "builder-source"
    sources = [*list((ROOT / "tools/switch2/pc_builder").glob("*.py")),
               ROOT / "tools/switch2/pc_builder/native_pack.cpp", Path(__file__),
               *list((ROOT / "tools/switch2/compiler").glob("nro_*.cpp")),
               *list((ROOT / "tools/switch2/compiler").glob("nro_*.h"))]
    for path in sources:
        destination = source_root / path.relative_to(ROOT)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, destination)
    return resources


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--llvm-bin", type=Path, default=Path("C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin"))
    args = parser.parse_args()
    base = ROOT / "build/pc-builder-release"
    base.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="package-", dir=base))
    assets = work / "assets"
    tools = assets / "tools"
    tools.mkdir(parents=True)
    for name in ("clang.exe", "ld.lld.exe"):
        shutil.copy2(args.llvm_bin / name, tools / name)
    version = subprocess.check_output([str(tools / "clang.exe"), "--version"], text=True)
    if "19.1.5" not in version:
        parser.error("Use the validated native Windows Clang 19.1.5")
    build_native_packer(tools / "wwhd-nro-pack.exe", work)
    imports = {path.name: audit_native_tool(path) for path in tools.glob("*.exe")}
    resources = stage_resources(assets, args.llvm_bin)
    files = [{"path": path.relative_to(assets).as_posix(), "size": path.stat().st_size, "sha256": digest(path)}
             for path in sorted(assets.rglob("*")) if path.is_file()]
    for record in files:
        path = Path(record["path"])
        if path.suffix.lower() in (".rpx", ".nro", ".elf") or "gamecode" in path.name.lower():
            raise ValueError("Refusing private game input in the portable bundle")
    (assets / "builder-manifest.json").write_text(json.dumps({
        "schema": 1, "version": VERSION, "language": "en", "upstream": UPSTREAM, "credit": CREDIT,
        "dump_input": None, "native_imports": imports, "files": files}, indent=2) + "\n", encoding="utf-8")
    command = [sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean", "--onedir", "--windowed",
               "--name", "WWHD-Switch2-Builder", "--distpath", str(work / "frozen"),
               "--workpath", str(work / "pyinstaller"), "--specpath", str(work),
               "--icon", str(resources / "branding/builder.ico"),
               "--paths", str(ROOT / "tools/switch2/pc_builder")]
    for name in ("argparse", "bisect", "glob", "xml.etree.ElementTree", "xml.parsers.expat", "zlib", "copy"):
        command += ["--hidden-import", name]
    command.append(str(ROOT / "tools/switch2/pc_builder/app.py"))
    subprocess.run(command, check=True)
    package = work / "frozen/WWHD-Switch2-Builder"
    shutil.copytree(assets, package / "assets")
    (package / "README_FIRST.txt").write_text(
        "Extract the ENTIRE ZIP to a Windows 10/11 x64 folder, then open WWHD-Switch2-Builder.exe.\n"
        "Do not run the application directly from the ZIP.\n"
        "No CMake, Python, Visual Studio, devkitPro or other developer tools need to be installed.\n"
        "Choose your own USA/EUR v0 Loadiine dump and an output folder with several GiB of free space.\n"
        "All processing is local and offline.\n\n" + copy_instructions(), encoding="utf-8")
    # PATH deliberately excludes installed Python, CMake, SDKs and Visual Studio.
    env = os.environ.copy()
    env["PATH"] = str(Path(os.environ.get("SystemRoot", "C:/Windows")) / "System32")
    report = work / "portable-self-test.json"
    subprocess.run([str(package / "WWHD-Switch2-Builder.exe"), "--self-test", "--report", str(report)],
                   env=env, check=True)
    result = json.loads(report.read_text())
    if not result.get("success"):
        raise ValueError(result)
    archive = work / f"WWHD-Switch2-PC-Builder-{VERSION}-Windows-x64-EN.zip"
    with zipfile.ZipFile(archive, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as output:
        for path in sorted(package.rglob("*")):
            if path.is_file():
                output.write(path, Path("WWHD-Switch2-Builder") / path.relative_to(package))
    info = {"language": "en", "executable": str(package / "WWHD-Switch2-Builder.exe"), "zip": str(archive),
            "zip_sha256": digest(archive), "self_test": result}
    (base / "current.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(info, indent=2))
    print("Portable app built; a complete private dump-to-NRO build still needs verification.")


if __name__ == "__main__":
    main()
