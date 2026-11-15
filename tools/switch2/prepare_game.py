#!/usr/bin/env python3
"""Validate a local game dump and prepare private Switch 2 recompilation output."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/recomp"))
import builds


def identify_game(game: Path) -> dict:
    for directory in ("code", "content", "meta"):
        if not (game / directory).is_dir():
            raise ValueError(f"Missing {directory}/ in {game}")
    rpx = game / "code/cking.rpx"
    build = builds.identify(rpx)
    if build is None:
        raise ValueError("Unsupported cking.rpx: an unmodified USA/EUR version-0 dump is required")
    meta = ET.parse(game / "meta/meta.xml").getroot()
    title = (meta.findtext("title_id") or "").strip().lower()
    version = int((meta.findtext("title_version") or "-1").strip())
    if title != build.title_id or version != 0:
        raise ValueError("meta.xml does not match the identified version-0 RPX")
    with rpx.open("rb") as source:
        header = source.read(0x40)
    if len(header) < 0x40 or header[:6] != b"\x7fELF\x01\x02":
        raise ValueError("Expected a big-endian ELF32 RPX")
    return {"build": build.name, "title_id": title, "version": version,
            "rpx_sha256": build.sha256, "entry": struct.unpack_from(">I", header, 0x18)[0],
            "game_dir": str(game.resolve())}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", required=True, type=Path)
    parser.add_argument("--out-dir", type=Path, default=ROOT / "build/gen-switch2")
    parser.add_argument("--manifest", type=Path, default=ROOT / "build/switch2/game-manifest.json")
    parser.add_argument("--no-recompile", action="store_true", help="validate and write metadata only")
    args = parser.parse_args()
    info = identify_game(args.game_dir)
    info["generated_dir"] = str(args.out_dir.resolve())
    print(f"Recognized {info['build']} v0, title {info['title_id']}, entry {info['entry']:08X}", flush=True)
    print(f"RPX SHA-256: {info['rpx_sha256']}", flush=True)
    if not args.no_recompile:
        subprocess.run([sys.executable, str(ROOT / "tools/recomp/recomp.py"),
                        str(args.game_dir / "code/cking.rpx"), str(args.out_dir)], check=True)
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
    print(f"Private build metadata: {args.manifest}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, ET.ParseError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from error
