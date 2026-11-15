#!/usr/bin/env python3
"""Audit the built installer, including the actual bundled RomFS bytes.

This is a host-side artifact check, not a claim of console execution success.
The installer has a larger file budget than the private cached game NRO.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

from build import ROOT


def require(condition, message):
    if not condition:
        raise ValueError(message)


def audit_romfs(read, base, size):
    header = struct.unpack("<10Q", read(base, 80))
    require(header[0] == 80, "Invalid RomFS header")
    for offset, length in zip(header[1:9:2], header[2:9:2]):
        require(offset <= size and length <= size - offset, "RomFS table exceeds asset")
    directory_base, directory_size = header[3:5]
    file_base, file_size = header[7:9]
    data_base = header[9]
    require(data_base <= size, "RomFS data exceeds asset")
    files, directories_seen, files_seen = {}, set(), set()
    absent = 0xffffffff

    def table(offset, length, start, limit):
        require(offset <= limit and length <= limit - offset, "RomFS entry outside table")
        return read(base + start + offset, length)

    def name(raw):
        text = raw.decode("utf-8")
        require(text and text not in (".", "..") and not any(c in text for c in "/\\:\0"),
                "Invalid RomFS entry name")
        return text

    pending = [(0, "")]
    while pending:
        offset, parent = pending.pop()
        require(offset not in directories_seen and len(directories_seen) < 10000,
                "Cyclic or excessive RomFS directories")
        directories_seen.add(offset)
        _, sibling, child, first_file, _, length = struct.unpack(
            "<6I", table(offset, 24, directory_base, directory_size))
        relative = name(table(offset + 24, length, directory_base, directory_size)) if offset else ""
        current = parent + relative
        if sibling != absent:
            pending.append((sibling, parent))
        if child != absent:
            pending.append((child, current + "/" if current else ""))
        offset = first_file
        while offset != absent:
            require(offset not in files_seen and len(files_seen) < 20000, "Cyclic or excessive RomFS files")
            files_seen.add(offset)
            _, sibling, data_offset, length, _, name_length = struct.unpack(
                "<IIQQII", table(offset, 32, file_base, file_size))
            filename = name(table(offset + 32, name_length, file_base, file_size))
            path = (current + "/" if current else "") + filename
            require(path not in files, "Duplicate RomFS path")
            require(data_offset <= size - data_base and length <= size - data_base - data_offset,
                    "RomFS file outside asset")
            files[path] = (base + data_base + data_offset, length)
            offset = sibling
    return files


def audit_nro(nro, resources):
    with nro.open("rb") as stream:
        size = nro.stat().st_size

        def read(offset, length):
            require(offset >= 0 and length >= 0 and offset <= size and length <= size - offset,
                    "NRO field outside file")
            stream.seek(offset)
            result = stream.read(length)
            require(len(result) == length, "Short NRO read")
            return result

        header = read(0, 0x80)
        require(header[0x10:0x14] == b"NRO0", "Missing NRO0")
        image = struct.unpack_from("<I", header, 0x18)[0]
        end = 0
        for index in range(3):
            offset, length = struct.unpack_from("<II", header, 0x20 + 8 * index)
            require(offset == end and length > 0 and length % 4096 == 0, "Invalid NRO segments")
            end += length
        require(end == image, "Segment/image mismatch")
        mod = struct.unpack_from("<I", header, 4)[0]
        require(read(mod, 4) == b"MOD0", "Missing MOD0")
        extensions = read(mod + 28, 96)
        found = False
        for offset in range(0, 96, 12):
            magic, platform, version = struct.unpack_from("<III", extensions, offset)
            if magic == 0x304e554f:
                found = platform == 2 and version == 1
                break
            if magic & 0xffffff != 0x594e4c:
                break
        require(found, "Missing supported Switch 2 OUN0")
        aset = read(image, 56)
        require(aset[:8] == b"ASET\0\0\0\0", "Invalid ASET")
        assets = [struct.unpack_from("<QQ", aset, 8 + 16 * index) for index in range(3)]
        for offset, length in assets:
            require(offset >= 56 and length > 0, "Missing installer asset")
            read(image + offset + length - 1, 1)
        icon = read(image + assets[0][0], assets[0][1])
        require(icon == (resources / "branding/icon.jpg").read_bytes(), "Icon mismatch")
        nacp = read(image + assets[1][0], assets[1][1])
        require(len(nacp) == 0x4000, "Invalid NACP size")
        require(nacp[0x3060:0x3070].split(b"\0")[0] == b"0.0.1", "NACP version mismatch")
        require(b"Zoria" in nacp[0x200:0x300], "Missing Zoria author credit")
        romfs_offset, romfs_size = assets[2]
        files = audit_romfs(read, image + romfs_offset, romfs_size)
        manifest_bytes = (resources / "resources.json").read_bytes()
        manifest = json.loads(manifest_bytes)
        require(manifest["dump_input"] is None, "Manifest declares game input")
        records = {record["path"]: record for record in manifest["files"]}
        require(set(files) == set(records) | {"resources.json"}, "RomFS has missing or unaudited files")
        offset, length = files["resources.json"]
        require(read(offset, length) == manifest_bytes, "Embedded manifest mismatch")
        for path, record in records.items():
            offset, length = files[path]
            require(length == record["size"], "Resource size mismatch: " + path)
            digest = hashlib.sha256()
            for start in range(0, length, 1 << 20):
                digest.update(read(offset + start, min(1 << 20, length - start)))
            require(digest.hexdigest() == record["sha256"], "Resource hash mismatch: " + path)
        return {"bytes": size, "image_bytes": image, "romfs_files": len(files),
                "manifest_sha256": hashlib.sha256(manifest_bytes).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nro", type=Path, default=ROOT / "build/switch2-compiler/wwhd_switch2_installer.nro")
    parser.add_argument("--elf", type=Path, default=ROOT / "build/switch2-compiler/wwhd_switch2_installer.elf")
    parser.add_argument("--resources", type=Path)
    args = parser.parse_args()
    resources = args.resources or Path(json.loads(
        (ROOT / "build/switch2-installer-resources/current.json").read_text())["directory"])
    nm = "C:/devkitPro/devkitA64/bin/aarch64-none-elf-nm.exe"
    require(not subprocess.check_output([nm, "-u", str(args.elf)], text=True).strip(),
            "Installer ELF has undefined symbols")
    symbols = subprocess.check_output([nm, "--defined-only", str(args.elf)], text=True)
    require(not re.search(r"\b[fF]_[0-9a-fA-F]{8}(?:_orig)?\b", symbols),
            "Installer contains generated guest functions")
    print(json.dumps(audit_nro(args.nro, resources), indent=2))
    print("Host artifact audit passed; console execution is still unverified.")


if __name__ == "__main__":
    main()
