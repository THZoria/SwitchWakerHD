"""Offline RPX frontend called by embedded CPython, never via a subprocess."""
from contextlib import redirect_stderr, redirect_stdout
import json
from pathlib import Path
import struct
import traceback
import xml.etree.ElementTree as ET

import builds
from recomp import Recompiler


def identify(game_directory):
    game = Path(game_directory)
    for directory in ("code", "content", "meta"):
        if not (game / directory).is_dir():
            raise ValueError(f"Missing game directory: {directory}")
    rpx = game / "code/cking.rpx"
    build = builds.identify(rpx)
    if build is None:
        raise ValueError("An unmodified USA/EUR version-0 dump is required")
    metadata = game / "meta/meta.xml"
    if metadata.stat().st_size > 1024 * 1024:
        raise ValueError("Oversized meta.xml")
    meta = ET.parse(metadata).getroot()
    title = (meta.findtext("title_id") or "").strip().lower()
    version = int((meta.findtext("title_version") or "-1").strip())
    if title != build.title_id or version != 0:
        raise ValueError("meta.xml does not match the supported RPX")
    with rpx.open("rb") as source:
        header = source.read(64)
    if len(header) != 64 or header[:6] != b"\x7fELF\x01\x02":
        raise ValueError("Expected an ELF32 big-endian RPX")
    return build, {"build": build.name, "title_id": title, "version": version,
                   "rpx_sha256": build.sha256, "entry": struct.unpack_from(">I", header, 24)[0]}


def run(game_directory, output_directory, progress=None):
    """Only generates real C translations; compilation and publication are native."""
    if progress:
        progress("Identification du RPX PowerPC", 0, 1)
    build, identity = identify(game_directory)
    if progress:
        progress("Identification du RPX PowerPC", 1, 1)
    output = Path(output_directory)
    if output.exists() and any(output.iterdir()):
        raise ValueError("Recompilation requires an empty transaction directory")
    output.mkdir(parents=True, exist_ok=True)
    # CPython legitimately sets sys.stdout/sys.stderr to None when Horizon
    # provides no valid standard descriptors. The original frontend writes
    # progress to stderr and print(), so supply real UTF-8 streams independently
    # of those descriptors. Keep the log in this private transaction, not RomFS.
    with (output / "recompiler.log").open("x", encoding="utf-8", errors="backslashreplace",
                                          buffering=1) as log:
        with redirect_stdout(log), redirect_stderr(log):
            try:
                if progress:
                    progress("Lecture et decodage du RPX", 0, 1)
                recompiler = Recompiler(str(Path(game_directory) / "code/cking.rpx"), build)
                recompiler.progress_callback = progress
                if progress:
                    progress("Lecture et decodage du RPX", 1, 1)
                recompiler.run(str(output), 30000)
            except BaseException:
                traceback.print_exc(file=log)
                raise
    if progress:
        progress("Verification des sources C", 0, 1)
    for required in ("table.c", "imports.c", "funcs.h", "report.txt"):
        if not (output / required).is_file():
            raise RuntimeError(f"Recompiler produced no {required}")
    sources = sorted(path.name for path in output.glob("code_*.c"))
    if not sources:
        raise RuntimeError("Recompiler produced no guest translation units")
    identity["sources"] = [*sources, "table.c", "imports.c"]
    (output / "compilation.json").write_text(json.dumps(identity, indent=2) + "\n", encoding="utf-8")
    if progress:
        progress("Verification des sources C", 1, 1)
    return identity
