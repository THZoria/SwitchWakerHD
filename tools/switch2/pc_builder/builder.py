"""Offline Windows builder: bundled tools only, never CMake or a system SDK."""
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
import hashlib
import importlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import time

VERSION = "0.0.1"
NRO_NAME = "wwhd_switch2.nro"
UPSTREAM = "https://github.com/centollOS/SwitchWakerHD"
CREDIT = "SwitchWakerHD / Zoria"
COPY_INSTRUCTIONS_NAME = "COPY_TO_MICROSD.txt"

# The shared console frontend retains its original labels. Translate only at
# the PC boundary so the console installer and other targets are unchanged.
FRONTEND_STAGES = {
    "Identification du RPX PowerPC": "Identifying the PowerPC RPX",
    "Lecture et decodage du RPX": "Reading and decoding the RPX",
    "Analyse sauvegardes PowerPC": "Analyzing PowerPC register preservation",
    "Analyse precision PowerPC": "Analyzing PowerPC precision",
    "Propagation precision PowerPC": "Propagating PowerPC precision",
    "Analyse registres PowerPC": "Analyzing PowerPC registers",
    "Propagation registres PowerPC": "Propagating PowerPC registers",
    "Generation des fonctions C": "Generating C functions",
    "Ecriture des fichiers C": "Writing C files",
    "Ecriture des tables et en-tetes": "Writing tables and headers",
    "Verification des sources C": "Validating C sources",
}


def english_stage(stage):
    name, separator, batch = stage.partition(" - lot ")
    return FRONTEND_STAGES.get(name, name) + (" - batch " + batch if separator else "")


class Cancelled(RuntimeError):
    pass


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def relative_path(text):
    path = Path(text)
    if not text or path.is_absolute() or any(part in ("", "..", ".") for part in text.split("/")) \
            or any(char in text for char in (":", "\\", "\0")):
        raise ValueError("Invalid resource path: " + text)
    return path


def copy_instructions():
    return (f"Build complete - Wind Waker HD Switch 2 {VERSION}\n\n"
            f"1. Copy {NRO_NAME} to /switch/ on your microSD card.\n"
            "2. Copy the CONTENTS of the romfs folder (code, content, meta) to\n"
            "   /switch/wwhd-switch2/game/ on your microSD card.\n"
            "   Do not create a game/romfs/ subfolder.\n\n"
            "Expected layout:\n"
            f"/switch/{NRO_NAME}\n"
            "/switch/wwhd-switch2/game/code/cking.rpx\n"
            "/switch/wwhd-switch2/game/content/\n"
            "/switch/wwhd-switch2/game/meta/meta.xml\n\n"
            "Do not overwrite existing saves.\n"
            "Launch the NRO through hbmenu in application mode (full memory).\n\n"
            f"Original project: {UPSTREAM}\n{CREDIT}\n"
            "Private output: contains code derived from your dump. Do not redistribute it.\n")


@dataclass
class BuildResult:
    directory: Path
    nro: Path
    romfs: Path


class Builder:
    def __init__(self, assets, progress=None, cancel=None, jobs=2, activity=None):
        self.assets = Path(assets).resolve()
        self.progress = progress or (lambda *args: None)
        self.cancel = cancel or threading.Event()
        self.jobs = max(1, min(4, jobs))
        self.activity = activity or (lambda text: None)
        self.work = None
        self._log = None
        self._lock = threading.Lock()

    def notify(self, text):
        self.activity(text)
        if self._log:
            with self._lock:
                self._log.write(text + "\n")
                self._log.flush()

    def report(self, stage, current=0, total=1):
        if self.cancel.is_set():
            raise Cancelled("Build cancelled; your original dump is preserved.")
        self.progress(english_stage(stage), current, max(1, total))

    def run_tool(self, arguments, log_path=None):
        # Exact absolute executable paths. No shell, PATH lookup, installer,
        # SDK discovery or network access on the user's machine.
        env = os.environ.copy()
        for name in ("CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "LIBRARY_PATH", "COMPILER_PATH",
                     "SDKROOT", "CL", "_CL_", "CCC_OVERRIDE_OPTIONS", "INCLUDE", "LIB"):
            env.pop(name, None)
        flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
        self.notify("Running " + Path(arguments[0]).name)
        with subprocess.Popen([str(arg) for arg in arguments], stdin=subprocess.DEVNULL,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env,
                              creationflags=flags) as process:
            while True:
                try:
                    output, _ = process.communicate(timeout=0.25)
                    break
                except subprocess.TimeoutExpired:
                    if self.cancel.is_set():
                        process.terminate()
                        process.communicate()
                        raise Cancelled("Build cancelled; diagnostics have been preserved.")
        text = output.decode("utf-8", errors="replace")
        if log_path:
            Path(log_path).write_text(text, encoding="utf-8")
        if self._log:
            with self._lock:
                self._log.write("\n" + " ".join(map(str, arguments)) + "\n" + text)
                self._log.flush()
        if process.returncode:
            raise RuntimeError(f"{Path(arguments[0]).name} failed ({process.returncode}).\n{text[-6000:]}")
        return text

    def verify_assets(self):
        manifest = json.loads((self.assets / "builder-manifest.json").read_text(encoding="utf-8"))
        if manifest.get("schema") != 1 or manifest.get("version") != VERSION or manifest.get("dump_input") is not None:
            raise ValueError("Incompatible builder package")
        expected = [record["path"] for record in manifest["files"]]
        actual = {path.relative_to(self.assets).as_posix() for path in self.assets.rglob("*") if path.is_file()}
        if len(set(expected)) != len(expected) or actual != set(expected) | {"builder-manifest.json"}:
            raise ValueError("Builder package is incomplete or contains unaudited files")
        for index, record in enumerate(manifest["files"]):
            self.report("Verifying bundled tools", index, len(manifest["files"]))
            self.notify("Verifying bundled resource: " + record["path"])
            path = self.assets / relative_path(record["path"])
            if not path.is_file() or path.is_symlink() or digest(path) != record["sha256"]:
                raise ValueError("Missing or corrupted resource: " + record["path"])
        self.report("Verifying bundled tools", len(manifest["files"]), len(manifest["files"]))
        return manifest

    def frontend(self):
        resources = self.assets / "resources"
        sys.dont_write_bytecode = True
        # The portable app uses the SDK's normal frontend defaults, never a
        # developer's inherited experimental pass overrides.
        for name in list(os.environ):
            if name.startswith("WWHD_RECOMP_"):
                os.environ.pop(name)
        for folder in (resources / "tools", resources / "tools/recomp", resources / "tools/switch2/python"):
            value = str(folder)
            if value not in sys.path:
                sys.path.insert(0, value)
        return importlib.import_module("console_recompile")

    def self_test(self):
        self.verify_assets()
        tools = self.assets / "tools"
        clang = self.run_tool([tools / "clang.exe", "--version"])
        linker = self.run_tool([tools / "ld.lld.exe", "--version"])
        if "19.1.5" not in clang:
            raise ValueError("Unexpected Clang version")
        self.frontend()
        return {"version": VERSION, "clang": clang.splitlines()[0], "linker": linker.splitlines()[0],
                "developer_tools_required": False}

    def compile_arguments(self, source, output, generated):
        resources = self.assets / "resources"
        return [self.assets / "tools/clang.exe", "--target=aarch64-none-elf", "-mcpu=cortex-a78c+crypto",
                "-x", "c", "-std=gnu11", "-O3", "-fPIC", "-ffunction-sections", "-fdata-sections",
                "-fsigned-char", "-ffp-contract=off", "-fno-strict-aliasing", "-fwrapv",
                "-fomit-frame-pointer", "-fexceptions", "-funwind-tables", "-D__SWITCH__", "-D__SWITCH2__",
                "-Werror=unknown-attributes", "-ferror-limit=8", "-resource-dir", resources / "clang",
                "-nostdinc", "-isystem", resources / "clang/include", "-isystem", resources / "libc/include",
                "-I", resources / "runtime/include", "-I", generated, "-c", source, "-o", output]

    @staticmethod
    def game_files(game):
        files = []
        for folder in ("code", "content", "meta"):
            root = game / folder
            if root.is_symlink():
                raise ValueError("The dump must not contain symbolic links")
            for directory, children, names in os.walk(root, followlinks=False):
                for child in children + names:
                    if (Path(directory) / child).is_symlink():
                        raise ValueError("The dump must not contain symbolic links")
                for name in sorted(names):
                    path = Path(directory) / name
                    files.append((path, path.relative_to(game), path.stat().st_size))
        return files

    def copy_game(self, game, destination, files):
        for folder in ("code", "content", "meta"):
            (destination / folder).mkdir(parents=True)
            for directory, children, _ in os.walk(game / folder, followlinks=False):
                for child in children:
                    source = Path(directory) / child
                    if source.is_symlink():
                        raise ValueError("The dump changed: a symbolic link was added")
                    (destination / source.relative_to(game)).mkdir(parents=True, exist_ok=True)
        total = max(1, sum(size for _, _, size in files))
        done = 0
        for source, relative, expected_size in files:
            if source.is_symlink():
                raise ValueError("The dump changed: a symbolic link was added")
            target = destination / relative
            self.notify("Copying " + relative.as_posix())
            target.parent.mkdir(parents=True, exist_ok=True)
            size = 0
            with source.open("rb") as input_file, target.open("xb") as output_file:
                while True:
                    self.report("Preparing the romfs folder", done, total)
                    chunk = input_file.read(1 << 20)
                    if not chunk:
                        break
                    output_file.write(chunk)
                    size += len(chunk)
                    done += len(chunk)
            if size != expected_size:
                raise ValueError("The dump changed while copying: " + str(relative))
        self.report("Preparing the romfs folder", total, total)

    def build(self, game, output_parent):
        game, output_parent = Path(game).resolve(), Path(output_parent).resolve()
        if output_parent == game or game in output_parent.parents:
            raise ValueError("Choose an output folder outside the game dump")
        if self.assets == output_parent or self.assets in output_parent.parents:
            raise ValueError("Do not build inside the application's resources")
        self.verify_assets()
        frontend = self.frontend()
        self.report("Verifying the game dump")
        self.notify("Checking code/cking.rpx and meta/meta.xml")
        _, identity = frontend.identify(game)
        files = self.game_files(game)
        output_parent.mkdir(parents=True, exist_ok=True)
        required = sum(size for _, _, size in files) + (2 << 30)
        if shutil.disk_usage(output_parent).free < required:
            raise ValueError(f"Not enough free space: at least {required / (1 << 30):.1f} GiB required")
        self.report("Verifying the game dump", 1)
        self.work = Path(tempfile.mkdtemp(prefix=".wwhd-build-", dir=output_parent))
        product = self.work / "product"
        product.mkdir()
        generated, objects = self.work / "generated", self.work / "objects"
        objects.mkdir()
        with (self.work / "build.log").open("x", encoding="utf-8", buffering=1) as log:
            self._log = log
            log.write(f"WWHD Switch 2 PC Builder {VERSION}\n{UPSTREAM}\n{CREDIT}\n")
            try:
                self.notify("Translating PowerPC code from code/cking.rpx into C sources")
                translated = frontend.run(str(game), str(generated), self.report)
                if translated["rpx_sha256"] != identity["rpx_sha256"]:
                    raise ValueError("The RPX changed during translation")
                sources = translated["sources"]
                def compile_unit(name):
                    relative = relative_path(name)
                    if relative.name != name or relative.suffix != ".c":
                        raise ValueError("Invalid translation unit")
                    target = objects / (name + ".o")
                    self.notify("Compiling " + name + " -> " + target.name)
                    self.run_tool(self.compile_arguments(generated / name, target, generated),
                                  objects / (name + ".log"))
                    if not target.is_file() or not target.stat().st_size:
                        raise ValueError("The compiler did not produce an object file")
                    self.notify("Compiled " + name)
                    return target
                self.report("Compiling ARM64 code", 0, len(sources))
                compiled = {}
                with ThreadPoolExecutor(max_workers=self.jobs) as pool:
                    futures = {pool.submit(compile_unit, name): name for name in sources}
                    try:
                        for future in as_completed(futures):
                            compiled[futures[future]] = future.result()
                            self.report("Compiling ARM64 code", len(compiled), len(sources))
                    except BaseException:
                        self.cancel.set()
                        for future in futures:
                            future.cancel()
                        raise
                resources = self.assets / "resources"
                sdk = resources / "sdk"
                recipe = json.loads((sdk / "link-recipe.json").read_text(encoding="utf-8"))
                elf = self.work / "game.elf"
                arguments, slots = [], 0
                for value in recipe["arguments"]:
                    if value == "{guest_objects}":
                        slots += 1
                        arguments.extend(compiled[name].as_posix() for name in sources)
                    else:
                        arguments.append(value.replace("{sdk}", sdk.as_posix()).replace("{output}", elf.as_posix()))
                if slots != 1 or any("{" in value or "}" in value for value in arguments):
                    raise ValueError("Invalid linker recipe")
                # Response files keep long installation paths below Windows'
                # command-line limit. LLVM tokenizes quoted forward-slash paths.
                response = self.work / "link.rsp"
                response.write_text("\n".join(json.dumps(value, ensure_ascii=False) for value in arguments), encoding="utf-8")
                self.report("Linking the NRO")
                self.notify(f"Linking {len(sources)} ARM64 objects with the Switch 2 runtime")
                self.run_tool([self.assets / "tools/ld.lld.exe", "@" + str(response)])
                self.report("Linking the NRO", 1)
                self.report("Creating and validating the NRO")
                self.notify("Creating and validating " + NRO_NAME)
                nro = product / NRO_NAME
                self.run_tool([self.assets / "tools/wwhd-nro-pack.exe", elf, nro,
                               resources / "branding/icon.jpg", resources / "branding/game.nacp"])
                self.report("Creating and validating the NRO", 1)
                if digest(game / "code/cking.rpx") != identity["rpx_sha256"]:
                    raise ValueError("The original RPX changed during the build")
                self.copy_game(game, product / "romfs", files)
                if digest(product / "romfs/code/cking.rpx") != identity["rpx_sha256"]:
                    raise ValueError("The copied RPX does not match the NRO")
                (product / COPY_INSTRUCTIONS_NAME).write_text(copy_instructions(), encoding="utf-8")
                (product / "build-info.json").write_text(json.dumps({
                    "version": VERSION, "upstream": UPSTREAM, "credit": CREDIT,
                    "rpx_sha256": identity["rpx_sha256"], "nro": NRO_NAME,
                    "nro_sha256": digest(nro)}, indent=2) + "\n", encoding="utf-8")
                shutil.copyfile(generated / "recompiler.log", product / "recompiler.log")
                self.report("Build complete", 1)
            except BaseException as error:
                log.write(f"\nFAILED: {error}\nDiagnostics saved in {self.work}\n")
                raise
            finally:
                self._log = None
        shutil.copyfile(self.work / "build.log", product / "build.log")
        final = output_parent / (f"WWHD-Switch2-{VERSION}-" + self.work.name.removeprefix(".wwhd-build-"))
        product.rename(final)
        shutil.rmtree(self.work)  # Only this uniquely created, owned transaction.
        self.notify("Output ready: " + str(final))
        return BuildResult(final, final / NRO_NAME, final / "romfs")
