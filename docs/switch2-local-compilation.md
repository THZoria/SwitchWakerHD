# Switch 2: desired game-free launcher / local compilation

> Historical console-installer experiment. The selected user-facing direction
> is now the [standalone PC builder](switch2-pc-builder.md): compilation happens
> on the PC, and the completed private NRO plus game resources are copied to SD.
> The sources below are retained separately; do not confuse its installer NRO
> with the playable `wwhd_switch2.nro` produced by the PC program.

## Current status: implemented in source, console validation pending

The complete installer path is now connected in
`tools/switch2/compiler/install_pipeline.cpp` and `installer_main.cpp`:
embedded CPython runs the original RPX-to-C frontend, embedded Clang compiles
the resulting translation units, embedded LLD links the original runtime SDK,
and the native converter produces a strictly validated private NRO. A validated
transactional cache is reused before requesting `envSetNextLoad`.

Build the separate installer with:

```powershell
python tools/switch2/build_installer.py --jobs 2
```

Built output: `build/switch2-compiler/wwhd_switch2_installer.nro`
(268,318,407 bytes; 101,105,664-byte executable image, including the exclusive
file-copy and missing-standard-stream corrections).
The complete installer builds and passes its host artifact audit, but is
**not yet confirmed functional on console**. The older implementation notes below describe the
earlier intermediate milestones, not the current integration boundary.

### Checks completed on the build computer

- Console-targeted CPython and the Clang/LLD bridges link in a development ELF.
- The packaged original frontend translates the user's supported RPX into
  39,713 functions and 81 translation units.
- The original-only runtime SDK exports 26 link inputs. Host LLD links both
  placeholder-only compatibility inputs and, separately, the private real game.
- The real-game host LLD result converts to a 76,800,467-byte NRO and passes
  strict Switch 2 platform/import/relocation validation.
- The converter reproduces the existing private playable NRO byte for byte.
- The actual installer ELF has no undefined symbols or generated guest-function
  symbols. Its NRO has the Switch 2 OUN0 record, 0.0.1/Zoria NACP, requested
  icon and 2,231 RomFS files (2,230 resources plus the manifest). All embedded
  resource hashes match; no unaudited extra files are present.
- The Python checks pass: 43 tests, with two optional Pillow skips.

`python tools/switch2/verify_installer.py` repeats the actual artifact audit.
Installer linking strips development DWARF while retaining symbols: otherwise
the LLVM debug payload produces a 2.97 GB ELF that `elf2nro` cannot read.

None of these checks proves embedded Python/Clang/LLD execution on hardware.

### First console execution

The first hardware log confirms installer startup, dump/cache checks and entry
into resource extraction. Extraction failed at the first resource because
libstdc++'s `std::filesystem::copy_file` returned `ENOSYS` for RomFS -> SD.
`compiler/file_copy.h` now uses ordinary `open/read/write/close` with exclusive
destination creation, interrupted/partial-I/O handling and cleanup of its own
incomplete output. Both resource extraction and native-cache publication use
this path. Host checks cover multi-chunk binary copying, empty files, refusal
to overwrite, missing sources and cache publication/recovery. Target compilation
passes. The corrected installer has been rebuilt and passed its full embedded
resource audit (manifest `217261a830f54f61c96dadbfdf4c6e1bf7597182452320f55d7683930461ea93`).
Another console run is required to confirm this correction on hardware.

The next hardware log confirms that all resources extract and CPython reaches
RPX function analysis. The frontend then failed when writing progress to
`sys.stderr`, which CPython correctly leaves as `None` when standard descriptors
are unavailable. The console frontend now redirects both Python output streams
to a line-buffered UTF-8 `generated/recompiler.log` inside the private build
transaction, recording tracebacks before restoring the original streams. Tests
cover successful translation and original-exception preservation with both
standard streams set to `None`. The bridge reports the diagnostic log location
on failure. The updated installer builds and passes the full artifact audit
(manifest `c52967c653f2dcb87218aff7e521ae571698f3d473235f9bb927c808f4409e1b`).
It needs another hardware run; no successful
native game compilation or chainload has yet been observed.

### Console installation contract

The installation UI now reports a percentage and bar for the **current stage**,
also written to `installer.log`. Extraction counts validated files, PowerPC
analysis/generation counts completed functions, and AArch64 compilation counts
completed translation units. Fixpoint propagation announces explicit work
batches because its final iteration count is unknown; percentages reset only
when entering a named new batch/stage. Python reports through an in-process
callback to the existing joined worker/UI, with cancellation at safe boundaries.
Monolithic operations (RPX loading, linking, packaging, final validation) remain
at 0% with an activity indicator until they succeed, then report 100%. These are
work-unit percentages, not a fabricated time estimate or a global ETA. Progress
is optional in the original frontend, preserving desktop and Switch 1 callers.

- Dump: `/switch/wwhd-switch2/game/{code,content,meta}`; supported unmodified
  USA/EUR version-0 RPX and matching metadata are required.
- Cache: `/switch/wwhd-switch2/cache/`; at least 2 GiB free SD space for initial
  compilation. The executable is stored as `guest.module` and chainloaded as
  NRO content, despite its cache filename.
- Log: `/switch/wwhd-switch2/installer.log` (early loader/RomFS failures currently
  precede log creation).
- The loader must support next-load requests and expose the bundled RomFS.
- Compiler work uses one joined 16 MiB-stack worker; LLVM/LLD parallelism is
  disabled. Python is finalized before native compilation; LLVM is shut down
  before returning to the loader. The game's 4 GiB guest heap is allocated only
  by the subsequently launched private game.
- Cache identity binds the RPX, resource manifest, runtime ABI and target flags;
  an installation-success marker never substitutes for a validated executable.
- Failed compilation preserves diagnostic staging, the dump and saves. Corrupt
  extracted tool resources currently cause an explicit error rather than an
  automatic reconstruction.

Acceptance still requires a cold installation, a second cached launch and
failure/cancellation testing on console. Third-party dependencies and artwork
also require redistribution-rights review before any public release.

## Private playable artifact (kept separate)

The current `build/switch2-game/wwhd_switch2.nro` is a **private statically linked
game build**, including `libwwhd_switch2_gamecode.a`. Changing its version or
icon does not turn it into a game-free public launcher. No dummy "compiled"
marker, copied game archive or installer success message may stand in for a real
local compiler. There is currently no playable game-free NRO in this port.

The requested layout already matches the host's automatic paths:

```
/switch/wwhd-switch2/game/
    code/cking.rpx
    content/
    meta/meta.xml
```

`switch2_host.cpp` selects that path and validates the RPX against the private
compiled build. `tools/switch2/prepare_game.py` validates USA/EUR v0 dumps and
matching title metadata **on the build computer**, not on the console. The
current full-game archive is compiled on the computer by Clang and linked into
the runtime by `cmake/switch2/RuntimePort.cmake`.

## Work necessary for the requested console-only first launch

### Implementation started: integrated native compiler and real cache storage

`tools/switch2/compiler/console_compile.cpp` now implements a real in-process
Clang driver/frontend bridge. It derives a single cc1 compilation from the same
AArch64 guest flags as the private build, then calls `EmitObjAction` directly;
it never executes a desktop compiler, subprocess or downloaded game object.
Its result is an **object**, not yet a playable executable.
`console_link.cpp` adds the in-process LLD ELF bridge, with file mapping disabled
and single-threaded linking. Neither bridge is yet validated in a console NRO.

LLVM/Clang 19.1.5 source is pinned to
`ab4b5a2db582958af1ee308a790cfdb42bd24720` under ignored
`build/switch2-llvm-source/`. `tools/switch2/build_compiler.py --jobs 2` first
builds PC-hosted `llvm-tblgen` / `clang-tblgen` (build utilities), then configures
the separate Switch-2-hosted Clang/AArch64 library. These utilities are not
confused with a console compiler. The compiler target consumes no game archive,
generated guest body or game manifest. Full Clang, the AArch64 backend, LLD ELF
and both embedding bridges now compile as console-targeted static libraries.
The development ELF linking these components and the packager also succeeds;
this does not yet validate a running console compiler or an installer NRO.

LLVM Support now builds successfully for the console. The compiler-only target
exposes newlib's POSIX declarations, omits unsupported Unix-domain socket streams,
uses 4096-byte pages, and explicitly disables subprocess execution and POSIX
signal installation. Reproducible patches are tested against pristine pinned
LLVM files and checked for idempotence. Successful archive creation alone does
not establish console link/run support. Use
`build_compiler.py --console-only --link-check --jobs 2` for the development-only
ELF that forces a real link of both bridges and the NRO packager. It has no RPX
installer, is not converted to a deliverable NRO, and is never a substitute for
the requested first-launch chain.
The first link exposed newlib declarations without implementations for POSIX
user lookups. A dedicated patch now leaves `~username` unchanged on Switch 2 and
uses only an explicitly provided `HOME`, never `getuid`, `getpwuid_r`,
`getpwnam_r` or `sysconf` for a nonexistent password database. The development
ELF now links successfully with `--no-undefined`; `aarch64-none-elf-nm -u`
reports no unresolved symbols. `readelf` confirms an AArch64 ELF64 ET_DYN,
entry zero, RX/R/RW load segments and PT_DYNAMIC. It remains a development-only
artifact under `build/switch2-compiler/wwhd_compiler_link_check.elf`, not a
playable NRO. Its large file size includes development debug sections; the
compiler still needs console execution tests, resource headers and the actual
RPX frontend/installation orchestration.

`tools/switch2/compiler/native_cache.h` implements versioned, digest-checked
native module cache lookup/publication. RPX digest, recompiler revision, runtime
ABI and target flags all affect the key. Valid completed entries are reused
without rewriting; incomplete staging directories are ignored; corrupt entries
are replaced transactionally while preserving the invalid version. A module
validator is mandatory: an ELF object or success marker alone cannot constitute
an installed game. This library is not yet wired into the playable NRO.
`compiler_cache_check.cpp` passes host checks for reuse, identity changes,
corruption, interrupted staging and mandatory validation. Build-command tests
also confirm the compiler bootstrap has no private-game input.

**Still required:** complete the console-hosted compiler port/link validation,
embed/port the Python RPX-to-C recompiler (or a functionally equivalent backend),
complete the runtime link recipe and ELF-to-NRO packaging, then connect
the first-launch UI and cache to a game-free runtime target. The current playable
NRO remains private and unchanged by this separate compiler build.

### Remaining integration requirements

The selected architecture is an **installer NRO that builds a private game NRO
on the SD card and chainloads it through the homebrew loader**. It reuses the
working static runtime/guest ABI and ordinary unwind metadata; a new in-process
dynamic-module ABI or JIT loader is not required for this path. The compiler must
finish and release its threads/resources before returning to the loader.

`tools/switch2/build_runtime_sdk.py --jobs 2` prepares the original runtime in
isolated placeholder-only build directories. It exports only `wwhd` runtime
objects and the original dispatch trampoline, never placeholder guest bodies or
the game-code archive. Exported archive members have unique names and a checked
member list. The current export has 417 unique members and matching archive and
per-object SHA-256 hashes (930,729,834 bytes including development debug data).
The source/hash audit is not proof of a complete installer package:
dependency archives, target headers, startup objects and the exact LLD-compatible
link recipe still need packaging and licensing review.

`compiler/nro_pack.cpp` implements console-native ELF-to-NRO conversion without
spawning elf2nro. It checks ELF64/AArch64/ET_DYN identity, bounded RX/R/RW segments,
MOD0 presence, note bounds, image/BSS budgets and asset sizes. It exclusively
creates a new staging output, never truncating an existing destination. This is
format conversion, **not** the required full import/relocation/ABI validator.
Synthetic tests cover malformed bounds, existing-file preservation and byte-level
agreement with the installed reference converter. The same source compiles as an
AArch64 object; its host test CLI also reproduced the existing private NRO
**byte for byte (68,288,979 bytes)** with its original NACP/icon. The existing
playable NRO was not overwritten. Console execution of this component is not
yet validated, and no installer target calls it yet.

1. **Separate game code from the runtime.** The public target must not import
   the private game-code archive or include generated function bodies. Audit
   direct runtime-to-guest calls, imports, dispatch tables and build identities;
   package the original runtime and its static-link dependencies separately.
   Version that runtime SDK ABI and retain GCC/newlib/libounce compatibility.
2. **Port the real compilation chain.** The recompiler is Python and emits C;
   generated calls require Clang's mandatory-tail behavior and exception/unwind
   metadata. No console-hosted Python + Clang/LLVM + linker chain is integrated
   into this port. Running desktop compiler binaries on Switch 2 is not possible.
   A console-native embedded toolchain, or a different native recompiler backend,
   must first be implemented and validated. This is not a CMake switch.
3. **Package and launch the linked private NRO safely.** Validate the linked
   AArch64 ELF, segment bounds, dynamic relocations and unresolved symbols, then
   convert it into a valid NRO with the required Switch 2 metadata. Validate the
   finished NRO and use the supported homebrew-loader next-load API, not a jump
   into file bytes or a subprocess. GuestExit must retain normal unwinding.
   Compare the converter against the existing elf2nro tool without distributing
   the user's private game output. A dynamic/JIT loader is an alternative design,
   not a prerequisite for this static-NRO architecture.
4. **Compile before allocating the 4 GiB guest heap.** Budget compiler peak RAM,
   temporary disk space, cancellation and progress display. Avoid unavailable
   POSIX subprocess assumptions; current console support is not a Linux shell.
5. **Persist a validated cache transactionally.** Proposed location:
   `/switch/wwhd-switch2/cache/gamecode/<cache-key>/`. Bind the key to the RPX
   SHA-256, region/build, module ABI, recompiler/backend version, target and
   compile flags (signed char, float behavior, mandatory tails, unwind support).
   Write temporary output, validate module bounds/relocations and digest, then
   publish atomically. Interrupted/corrupt output is never considered installed.
   Preserve the dump and saves on failure.
6. **Reuse without recompiling on ordinary launches.** The same dump and
   compatible ABI reuse the completed module. A changed dump, incompatible
   compiler/runtime ABI or corrupt cache requires reconstruction: "once" means
   once per compatible validated cache, not trusting an arbitrary forever flag.
7. **Audit actual public artifacts and dependencies.** A launcher without the
   private archive is necessary but not a legal guarantee. Do not bundle game
   files, generated bodies, sealed manifests, gameplay shader caches, SDK content
   without redistribution rights, or the user's compiled cache.

Acceptance requires an actual cold-console installation from only the launcher
and the user's dump, a second launch without recompilation, interrupted-install
recovery, and a distribution artifact with no game-code input. Until then, do not
label the private playable NRO as distributable.

## Version, icon and credit implemented independently

The full Switch 2 homebrew now uses version **0.0.1** in both NACP metadata and
runtime build information. The hbmenu author field and Settings > About credit
**Zoria**, retaining the upstream SwitchWakerHD contributors. About also
identifies this as a private build without on-console compilation.

The user-requested Nintendo logo is formatted as a **256x256 RGB JPEG**, with
aspect ratio/transparency preserved, for the NRO's hbmenu icon. It is kept in
ignored `build/switch2-branding/`, not committed as project-owned artwork.
`provenance.json` records its original URL and hashes. Downloading/resizing a
copyrighted logo does **not** grant redistribution rights; use an original or
appropriately licensed icon for a future public release unless rights are
otherwise established.

Preparation (Pillow in a project-local virtual environment):

```
python -m venv build/switch2-branding-venv
build/switch2-branding-venv/Scripts/python.exe -m pip install Pillow
build/switch2-branding-venv/Scripts/python.exe tools/switch2/prepare_branding.py --download
```

If the local Python certificate store rejects the source server, download that
same URL using a normally TLS-validated client and use `--source PATH`; do not
disable certificate verification. CMake accepts `WWHD_SWITCH2_ICON` and otherwise
uses the prepared private icon if present. It does not download artwork during
configuration. Synthetic tests live in `tools/switch2/test_branding.py`.
