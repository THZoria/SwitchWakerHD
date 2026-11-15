#!/usr/bin/env python3
"""Prepare the requested copyrighted logo for a PRIVATE build, outside source control.

Requires Pillow. This is asset formatting, not a licence or a game-free release.
Network access is explicit (--download); --source accepts an existing PNG.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
LOGO_URL = ("https://upload.wikimedia.org/wikipedia/fr/1/11/"
            "The_Legend_of_Zelda_The_Wind_Waker_HD_Logo.png")
DEFAULT_OUT = ROOT / "build/switch2-branding"
MAX_DOWNLOAD = 16 * 1024 * 1024


def render_icon(source: Path, output: Path) -> None:
    from PIL import Image
    with Image.open(source) as original:
        if original.width * original.height > 16_000_000:
            raise ValueError("Logo exceeds the 16-megapixel input limit")
        logo = original.convert("RGBA")
    logo.thumbnail((232, 232), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (256, 256), (20, 31, 42, 255))
    canvas.alpha_composite(logo, ((256 - logo.width) // 2, (256 - logo.height) // 2))
    output.parent.mkdir(parents=True, exist_ok=True)
    canvas.convert("RGB").save(output, "JPEG", quality=95, optimize=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    choice = parser.add_mutually_exclusive_group(required=True)
    choice.add_argument("--download", action="store_true")
    choice.add_argument("--source", type=Path)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    source = args.source
    if args.download:
        source = args.out_dir / "wind-waker-hd-logo.png"
        request = urllib.request.Request(LOGO_URL, headers={"User-Agent": "WWHD-Switch2-PrivateBranding/0.0.1"})
        with urllib.request.urlopen(request, timeout=60) as response:
            data = response.read(MAX_DOWNLOAD + 1)
        if len(data) > MAX_DOWNLOAD:
            raise ValueError("Logo download exceeds 16 MiB")
        if not data.startswith(b"\x89PNG\r\n\x1a\n"):
            raise ValueError("The source did not return a PNG")
        source.write_bytes(data)
    output = args.out_dir / "icon-256.jpg"
    render_icon(source, output)
    provenance = {
        "source_url": LOGO_URL,
        "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "output_sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
        "format": "JPEG RGB 256x256; aspect ratio preserved",
        "notice": "Nintendo game logo; copyrighted. Downloading/resizing grants no redistribution rights.",
    }
    (args.out_dir / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    print(f"Prepared private-build icon: {output}")


if __name__ == "__main__":
    main()
