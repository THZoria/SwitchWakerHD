# Switch 2 native port: bring-up

**Current status:** the native Switch 2 runtime and private game NRO are
implemented, and the earlier private build was played on hardware. Version
0.0.1 uses the credit **SwitchWakerHD / Zoria** and defaults to native 30 FPS
without interpolation or True 60, preserving saved player choices. The
[standalone Windows PC builder](switch2-pc-builder.md) is the selected player
workflow; its generated NRO still needs hardware validation. Generated game
code and resources are private and must not be redistributed. The
[console-installer experiment](switch2-local-compilation.md) is retained separately.
The milestone sections below record the earlier bring-up history, not current
missing features unless explicitly identified as remaining limitations.

## Status

**Milestone 0: game-free Vulkan probe, not a playable WWHD port.**

The probe builds a native `.nro` with libounce and OunceVK. It compiles the actual
WWHD Vulkan function loader (`runtime/src/gfx/vulkan/loader.cpp`) and a small
native surface bridge (`platform/switch2_vulkan.cpp`). It clears/presents an
animated blue/green image and destroys the Vulkan objects before returning from
`main`, including on initialization failures. It never changes clocks, starts
a network server, writes to NAND, or opens game data.

Cross-compilation/linking verified on Windows/MSYS2 with devkitA64 GCC 14.2.0
and ounce-devkit 20261114. The prebuilt libounce was subsequently found ABI
incompatible with that installation's older newlib; use rebuilt libounce below.
**Hardware result reported by the tester:** animated image works; the trace
reaches `First frame submitted and presented`, followed on exit by
`Vulkan destroyed; returning to hbmenu` and libounce's `userAppExit` handoff.
Observed: loader Vulkan 1.4.354, device Vulkan 1.4.0 (`Switch 2 GPU (NVK model
0x808)`), graphics/present queue 0, 1920x1080 swapchain with three images.
This validates the tested native presentation path, not WWHD's shaders,
guest memory, threading, audio or complete GPU-feature requirements. Seamless
docking, sleep, repeated launches and all shutdown edge cases remain untested.
There is no Switch 2 game target yet; the existing Switch 1/deko3d and desktop
targets have deliberately not been changed during this bring-up milestone.

## Reference

The platform reference is [Dusklight v2.0.3-switch2-1](https://github.com/wowilookgood/dusklight/tree/v2.0.3-switch2-1),
commit `ad781a8fcd25e17597dcf906217e2d4079d2aa93`:

- `hen/cmake/Switch2.cmake`: libounce instead of libnx; Switch 2 portlibs first.
- `hen/platform/hen_vulkan.c`: statically linked Vulkan entry points.
- `hen/platform/hen_app.c`: destroy GPU resources before returning to hbmenu,
  since the next NRO can share the process. Never rely on static destruction.
- `hen/patches/sdl3-switch2.patch`: future SDL3 host adaptation.

Dusklight uses the GameCube TP decompilation and Aurora/Dawn; WWHD uses Wii U
static recompilation and a GX2/Vulkan backend. We do not transplant TP's engine,
ISO loader or Dawn. The bridge and probe are written for this project. The
toolchain also follows the public ounce-devkit toolchain and supports a local,
unpacked SDK without replacing system libnx.

## Build on Windows, Linux or macOS

Prerequisites: Python 3.12+, devkitPro/devkitA64, CMake 3.20+, Ninja and Switch
packaging tools. On Windows use devkitPro's **MSYS2** CMake/Ninja, not native
Windows CMake (the Python helper selects MSYS2 automatically).

From the repository root, with `DEVKITPRO` set:

```sh
# Once: download ounce-devkit into build/switch2-sdk (SHA-256 checked).
python tools/switch2/build.py --fetch-sdk
# Afterwards:
python tools/switch2/build.py
# Or use an existing installed/unpacked SDK:
python tools/switch2/build.py --sdk /path/to/ounce-devkit-root
```

**For the installed GCC 14/older newlib, rebuild libounce instead of using the
archive's prebuilt library:**

```sh
python tools/switch2/build.py --rebuild-libounce
```

This checks out ounce-devkit's pinned libounce commit
`9382b1ec48ac5dabdaf52f1f99e0c0aacd02cd2f` in `build/libounce-source`, builds it
with the installed compiler/newlib, and uses `build/switch2-libounce-local` as
the library override. No system installation or Switch 1 libnx is modified.
Repeat the flag on subsequent builds; omitting it selects the original SDK
archives again. CMake refuses the known pinned-SDK `_reent.deviceData` mismatch.
The Vulkan library remains prebuilt; rebuilding libounce establishes neither
its ABI compatibility nor gameplay correctness.

The diagnosed failure is concrete: old libc's `_open_r` stored `deviceData`
at byte 344; prebuilt libounce's `fsdev_open` read it at byte 568. After the
libounce rebuild both use 344, verified in the linked ELF's disassembly.
`boot net-v4` reports the compiler's `_reent` size and deviceData offset.
**Hardware result reported by the tester:** `_reent` size 352, deviceData at
344; POSIX open returned fd 4 / errno 0, write returned 25 / errno 0, close
returned 0 / errno 0, followed by normal main return and __appExit. This validates
the tested C file path after the libounce rebuild, not the entire Vulkan stack.

`--devkitpro PATH` overrides `DEVKITPRO`; `--build-dir PATH` chooses the output
directory. No system packages or SDK directories are installed/replaced.
The pinned SDK is 20261114, archive SHA-256
`f24d5773b1ee1c2b19317f57e8ae54a60fcdcefe493a0e495297c241c4912f59`.

Output: `build/switch2-first-frame/wwhd_switch2_probe.nro` (plus the matching ELF).

Equivalent manual configure/build, in MSYS2 on Windows:

```sh
cmake -S tools/switch2 -B build/switch2-first-frame -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/switch2/Switch2.cmake" \
  -DOUNCE_SDK_ROOT="$PWD/build/switch2-sdk" -DCMAKE_BUILD_TYPE=Release
cmake --build build/switch2-first-frame --target wwhd_switch2_probe_nro
```

Host helper tests: `python -m unittest discover -s tools/switch2 -p 'test_*.py'`.

## Hardware test

### If the Vulkan probe crashes before creating a log

The absence of `first-frame.log` does **not** prove where the crash occurred:
loading/relocation, libounce startup (including window initialization), C++
constructors, SD access, and the start of `main` all precede its first marker.
Do not infer a GPU failure or a fixed bug without a trace.

Small C-only diagnostics isolate this path. They link no OunceVK or C++
runtime and replace default app initialization with only SM, FS and an SD mount.
They request no window and return to hbmenu automatically after half a second:

**Updated boot probes (`boot net-v4`) also connect back to nxlink before FS
initialization**, when launched by nxlink. They report FS initialization, SD
mount and POSIX open/write errors even when no SD log can be created. This
requires the sender's `-s` option and permission for nxlink through the host
firewall. SD launches do not initialize networking. If SM or the C runtime
fails before this connection, no network output is possible either.

From Windows CMD (not PowerShell), with hbmenu NetLoader active:

```bat
"C:\devkitPro\tools\bin\nxlink.exe" -a CONSOLE_IP -s "PATH_TO\wwhd_switch2_boot.nro"
```

Use the newly built NRO at the build path, not the older copy on the card.
The first expected line is `[boot net-v4] __appInit reached`.

The net-v2 hardware trace reached successful SM, sockets, FS and SD mount
initialization, but stopped before the POSIX open result. The exact failing
instruction is not yet known (even `errno = 0` was between those markers).
**net-v3 writes its logs through native `fsFsOpenFile`/`fsFileWrite`, not POSIX
open/write**, so the baseline can be tested independently of that failure.

1. Launch `wwhd_switch2_boot.nro`. **No image is expected.** Check the SD root
   for `wwhd-switch2-boot.log`.
2. If that crashes or produces no log, try `wwhd_switch2_boot_core.nro` and
   check `wwhd-switch2-boot-core.log`. This variant uses libounce_core/newlib
   malloc instead of libounce/mimalloc.
3. Report which variant returns to a usable hbmenu, any log contents, the exact
   kaiHEN version, and whether Dusklight's published NRO runs on the same setup.

If native logging and baseline shutdown work, `wwhd_switch2_boot_posix.nro`
is a separate opt-in test. It logs each step before errno access, `open`,
`write`, and `close`; it can reproduce the previous crash. Its trace goes to
`wwhd-switch2-boot-posix.log` via native FS and nxlink. The only POSIX test file
it creates/truncates is `/wwhd-switch2-posix-test.txt`. Do not treat a successful
native log as a fix for WWHD's normal POSIX filesystem operations.

A complete boot log has `__appInit`, `constructor reached`, `main reached`,
`main returning normally`, and `__appExit`. If neither writes anything, the
loader/runtime ABI/toolchain or early service setup still needs investigation;
no fix is claimed for that failure mode. The prebuilt-runtime ABI mismatch is
fixed by rebuilding libounce as described above. Keep the first-frame ELF for analysis.

### Vulkan presentation test

The updated Vulkan probe (`Vulkan net-v5`) uses the shared early logger in
`tools/switch2/probe_runtime.c`: nxlink plus a native-FS log at the SD root,
**`/wwhd-switch2-first-frame.log`**. No stdout redirection is required. It logs
applet/HID/time/default-window initialization before C++ constructors and main,
then instance/device/surface/swapchain milestones. Unlike the minimal C probes,
it initializes those display/input services. libounce's default shutdown keeps
the GPU/window alive until their own cleanup, before closing FS and services.

Send the freshly rebuilt `wwhd_switch2_probe.nro` with nxlink `-s` to see those
markers, rather than launching the original copy from the SD card.

1. On a Switch 2 already running a compatible kaiHEN setup, copy the probe to
   `sdmc:/switch/wwhd_switch2_probe.nro`. No game dump is needed for this probe.
2. Launch it from hbmenu through a native title (not the album).
3. Expected: animated blue/green image, requested output 1920x1080 in either
   mode. Press **Plus**; hbmenu should display and remain usable.
4. Relaunch it to verify GPU resources were released. Test separately in
   handheld and docked mode. This milestone exits on dock changes; seamless
   switching, sleep and HOME behavior are **not implemented/validated**.
5. Return `sdmc:/wwhd-switch2-first-frame.log` or the nxlink output. Expected markers:
   `WWHD Vulkan entry points loaded`, `First frame submitted and presented`,
   `Vulkan destroyed; returning to hbmenu`. A failed call logs `FAIL` and its
   Vulkan result. If it crashes, keep the ELF of that build for symbolization.

## Next milestones (not implemented)

1. Add a distinct Switch 2 game target: existing GX2/Vulkan backend plus SDL3
   host, linked statically to OunceVK, with one NWindow surface. Do not route it
   through `cmake/SwitchPort.cmake`, which forces deko3d for any `NX_ROOT`.
2. Adapt SDL3 using Dusklight as reference (external-graphics-context window,
   controller hotplug, single-screen GamePad presentation, dock/undock).
3. Audit the Wii U 4 GiB guest address window mappings against libounce/kaiHEN,
   worker stacks/cores, POSIX semantics, logging and orderly thread shutdown.
4. Compile from the user's own legal version-0 USA/EUR `cking.rpx`. The
   recompiled game's `musttail` requirement needs **GCC 15+** (GCC 14 suffices
   only for this game-free probe). No generated game code is committed.
5. Validate first title screen, sound, controls, save load, then gameplay.
   Regenerate Vulkan shader/pipeline caches; Switch 1 DKSH caches are not used.

No 60 fps/4K claim follows from Dusklight's performance; these are different
games and different renderers. Keep 30 fps and conservative resolution for the
first WWHD hardware tests.

## Personal dump and CPU/loader bring-up

The tester's dump has been identified as USA version 0 with the expected RPX
SHA-256 and entry `028EA120`. Static translation produced 39,713 functions in
79 C translation units, under ignored `build/gen-switch2/`. This is **translation
to C**, not evidence that the game runs. Nothing in the dump is modified or
copied to tracked source files.

Those units, the import/dispatch tables and the compiler check have also been
compiled successfully with Visual Studio's Clang 19.1.5 for AArch64, producing
the private `libwwhd_switch2_gamecode.a` (55,230,240 bytes). No TLS-runtime
references were found in the archive. This is a static archive, not a linked
game executable or proof of execution on hardware.

```sh
python tools/switch2/prepare_game.py --game-dir "/path/to/extracted game"
python tools/switch2/build.py --rebuild-libounce \
  --game-manifest build/switch2/game-manifest.json
```

`--no-recompile` on prepare_game.py validates identity and writes the private
metadata without repeating translation. The checked game version, hash and
entry are compiled into the optional **loader probe**, not the game code.

### Full-runtime dependency preparation (build-only validation)

Local dependencies install to `build/switch2-deps/prefix`, never the system SDK.
Prepared sources used here:

- glslang `16.0.0`, commit `a57276bf558f5cf94d3a9854ebdf5a2236849a5a`.
- devkitPro SDL `switch-sdl-3.4`, original tip
  `c329016c0e338ab4397ad930f83330c9fe058348`. Its 24 Switch commits
  after `a962f40` were rebased onto official SDL `release-3.4.18`
  (`829a65d769d935c4852f8159e964312c0957260a`). Fetch enough history
  to include that base, fetch the official release tag, then run
  `git rebase --onto release-3.4.18 a962f40 switch-sdl-3.4` in this
  **local dependency checkout**, not the WWHD repository.
- Apply Dusklight's published `hen/patches/sdl3-switch2.patch` unchanged
  from tag `v2.0.3-switch2-1` after checking it with `git apply --check`.
  No SDL contribution or generated source patch is required.

With these prepared checkouts at `build/switch2-glslang` and
`build/switch2-sdl`, run `python tools/switch2/build_deps.py`.
Both static-library builds and local installs passed. PCH is disabled because
MSYS CMake embeds `/d/...` includes that native GCC cannot open. Without PCH,
the Switch video files test `SDL_VIDEO_DRIVER_SWITCH` before loading the
configuration header; the build script defines it explicitly so those files
are not silently empty. SDL's platform summary still says `NintendoSwitch-1`;
the compiler flags select `__SWITCH2__` and the local libounce SDK.

The GCC C++ runtime's Switch 2 dispatch path now resolves targets in C++, then
returns to `runtime/src/platform/switch2_dispatch.c`, compiled by Clang with
mandatory tail calls, exceptions and unwind tables enabled. Completed HLE
calls return a null target. Other platforms retain their original dispatch.
`tools/switch2/check_dispatch.py --clang PATH` checks the actual trampoline's
LLVM IR at both `-O0` and `-O3`: mandatory tail edge, HLE completion branch,
unwind tables and absence of `nounwind`. Both checks passed, and the private
game-code archive rebuild includes the trampoline. This is **not** a linked
or hardware-validated full runtime; C++ resolver integration and exception
propagation still need full-link/runtime tests. Safe thread shutdown and
SDL/OunceVK integration remain prerequisites for a full-game NRO.

### Experimental full-runtime NRO (gameplay milestone validated)

**Hardware update:** after the `paths-v2` fix, the tester confirmed gameplay
and subsequently confirmed saving/loading works. The trace shows the real
guest threads running, GX2 initialization, NVK/OunceVK dynamic rendering,
SDL controller detection and 48 kHz stereo output, on-screen name entry, and
an automatic state written in the sea. A 5.2-second pause recovered without
process termination. This is a successful boot/gameplay milestone, not a
long-session stability or frame-rate measurement.

#### Opt-in NetLoader performance capture

The tester reports stable 60 fps facing the sea, but a persistent 25-30 fps
facing the Outset tower/village/forest path, even with the camera stationary.
This is scene-dependent load, not proof of shader compilation or GPU saturation.
The first hardware captures below identify render-thread CPU work as the primary
measured limit in these stationary scenes; no optimization gain is claimed yet.

Switch 2 adds **Dev > Show developer options** (off by default, session-only),
then `Measure sea (15s)`, `Measure village (15s)` and `Measure forest path (15s)`.
Point the camera first, select a label, then leave the controls untouched. The
menu closes, waits 3 seconds for release/menu settling, and collects 15 seconds.
Opening Settings cancels rather than measuring a scene with the overlay open.
No worker/listener or persistent profiling default is added, and no clocks,
resolution, interpolation mode, caches or save files are changed.

`[measure sw2]` reports appear through the existing LOG/nxlink sink directly in
CMD (the normal session log still mirrors them). They contain window FPS and
logic-step rate, new shader/SPIR-V/pipeline creation counts and wall durations,
disk hits, upload-arena bytes/frame, normal CPU metrics and the latest complete
120-swap render-profiler section. That detailed section includes sampled draw
phases, texture checks/hashing, uploads by class, CPU render time, and GPU /
acquire / present / guest-render synchronization waits. It is explicitly the
latest 120-swap window, **not** a 15-second aggregate. Existing periodic profiler
windows also reach nxlink while capture is enabled. Profiling has overhead.

Performance-metric demand is enabled only during the session and restored to
the normal overlay demand on finish/cancel. Existing optional GPU query logic
is reused; GPU submission intervals and CPU wait times are **not GPU load**,
and all-zero/emulated or unsupported timestamps must not be interpreted as a
fast GPU. The prior detailed-profiler request is restored without overwriting
settings. Environment-forced profiling disables the capture buttons to avoid
silently producing an unreset or uninstrumented sample.

`tools/switch2/profile_window_check.cpp` is the host-only lifecycle test: opt-in
start, nonreplacement, 3s settling, 15s measurement, single completion,
cancellation, backwards time and long stalls during settling. Compile/run in
MSYS with `g++ -std=c++20 -Iruntime/src tools/switch2/profile_window_check.cpp -o build/switch2-profile-window-check.exe`
then `build/switch2-profile-window-check.exe`. It passes. Console collection was
validated by the tester through nxlink; GPU intervals remain non-utilization data.

##### Stationary hardware baseline and render A/B tests

The tester's first captures used 60 fps presentation, unchanged clocks and detailed
profiling (which adds overhead). Whole-capture rates and trailing CPU metrics:

| Scene | Duration | Output FPS | Logic steps/s | Render CPU avg | Game CPU avg | GPU interval avg |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Sea | 15.002 s | 55.73 | 29.80 | 16.99 ms | 9.64 ms | 8.47 ms |
| Village | 15.019 s | 22.17 | 22.17 | 44.37 ms | 15.44 ms | 11.76 ms |

Both captures created **zero** shader variants, SPIR-V compiles and pipelines.
The latest village 120-swap window had 6,144 draws/frame, render CPU 44.50 ms,
idle 0.08 ms, guest DrawDone waits 30.05 ms, GPU waits 0, acquire 0.07 ms and
present 0.04 ms. Uploads were 32.43 MiB/frame: vertex 26.57, index 3.70, UBO
0.47 and uniforms 1.65; sampled unique guest bytes were only 2.61 MiB/frame
(vertex 2.22, index 0.31, UBO 0.08). No texture reuploads occurred. The 68.8%
buffer-pointer/ALU-only draw class and repeated uploads motivate preparation
tests, not shader prewarming. Sampled draw phases estimate index 7.90 ms,
uniforms 6.87 ms and vertices 6.34 ms; independently sampled/wall timings must
not be summed as exact CPU frame totals. GPU intervals are not GPU busy time.

Two **session-only, initially off** controls in Dev expose existing renderer
paths on Switch 2 only:

- `Test vertex snapshot reuse`: exact fresh-byte comparison against a CPU
  shadow (or an immutable HOST_CACHED/HOST_COHERENT upload slice), per binding
  and submission generation. No address-only assumption, write-watch enablement,
  cross-submission reuse or extra worker. The optional second history entry stays
  disabled unless explicitly enabled by the existing environment diagnostic.
- `Test specialized index conversion`: dispatch width/endian/restart once outside
  the index loop and fill a pre-sized vector. Existing native-index and wrapped
  guest-address fallbacks stay intact. No persistent index cache is enabled.

Each change logs its frame; capture BEGIN logs both session choices, and END adds
reuse checks/hits and avoided upload bytes per output frame. These are experiments,
not player defaults. Test the same stationary village view with both off, vertex
only, indices only, then both off again; report graphics glitches as well as rates.
Repeat useful candidates in interleaved runs before concluding there is a gain.
The existing persistent buffer cache remains disabled without write-watch.

Host validation: `runtime/tools/snapshot_cache_test.cpp` passes in shadow and
direct modes, including modified bytes and reset behavior. The new
`tools/switch2/index_conversion_check.cpp` passes 40,896 equivalence cases against
the original conversion loops (all supported widths/endian modes, primitives,
restart settings/markers, short/empty counts, nonindexed and unaligned data).
Build either with `g++ -std=c++20 -O2 -Iruntime/src <source> -o <output>`.
Hardware A/B effectiveness and visual correctness remain unverified.

##### Follow-up hardware results and CPU uniform copy plans

The tester subsequently compared specialized indices alone (vertex reuse off)
against both off in the same village viewpoint. Capture #5: 15.028 s, 383 output
frames, 25.49 FPS, trailing render CPU average 37.99 ms; latest window 5,308
draws/frame and sampled index phase 2.57 ms. Capture #6, indices off: 15.041 s,
333 frames, 22.14 FPS, trailing render CPU average 44.07 ms; latest window 5,260
draws/frame and index phase 7.99 ms. The observed output-rate difference is
15.1%, not a statistical confidence interval or a release-wide performance claim.
Both had zero new shader/SPIR-V/pipeline creations. Source uploads differed
(30.864 vs 31.866 MiB/frame), so the loads are close, not identical. Sea with
indices alone reached 58.52 FPS (render CPU average 15.90 ms).

Vertex reuse plus indices had previously reached only 53.85 FPS at sea, with
0.293 MiB/frame avoided uploads and a sampled vertex phase around 4.66 ms.
The village with both enabled reached 24.59 FPS but its draw count varied from
5,207 to 3,894/frame during the capture. Do not treat that as an isolated vertex
reuse measurement. Vertex reuse remains off.

The next Switch 2 build promotes specialized indices to the normal CPU path;
`Disable specialized indices (baseline)` is now the initially-unchecked Dev
control (the old positive-enable test checkbox is replaced). No setting-file
migration, clocks, worker thread or additional profiling is enabled.

The build also adds a CPU **uniform copy plan**, enabled normally on Switch 2
only. Each shader lazily compiles its immutable remapped-uniform metadata into
contiguous source/destination spans. Every draw still clears padding and rereads
the actual registers/guest/submitted-block bytes. Only the interpretation and
per-vec4 copy-loop overhead is reduced: no guest payload is cached, and no
write-watch is required. Invalid destinations, entry order, overlapping writes,
missing block addresses, submitted/guest boundaries and 32-bit guest address
wrapping retain the reference semantics. Stereo eye-row rewriting retains the
original per-entry path. Desktop and Switch 1 paths are unchanged.

Dev provides two initially-unchecked session-only controls:
`Disable CPU uniform copy plan (baseline)` and `Verify CPU uniform packing (slow)`.
The latter computes both paths, compares every output byte and substitutes the
reference payload on mismatch; it is a correctness diagnostic, **not** a speed
test. NetLoader captures log plan/verification state and cumulative capture
deltas for planned packs, copy spans, verification checks and mismatches.
Hardware uniform-plan speed and visual correctness are not yet validated.

`tools/switch2/uniform_copy_plan_check.cpp` passes 53,000 exact-byte cases with
randomized changed payloads, contiguous/disjoint/overlapping copies, invalid
destinations, submitted-block edge sizes, unaligned sources and guest-address
wrapping; it also asserts contiguous entries merge and wrapped offsets do not.
Compile with `g++ -std=c++20 -O2 -Wall -Wextra -Iruntime/src tools/switch2/uniform_copy_plan_check.cpp -o build/switch2-uniform-copy-plan-check.exe`.

For hardware timing, keep specialized indices enabled and vertex reuse off.
Measure a stationary village with the uniform plan enabled, disable just that
plan for a second capture, then reenable it for a third. Check verification in
a separate short run (nonzero checks and zero mismatches), then turn it off
before any timing comparison. No 60 FPS village claim is made.

##### CPU command compaction (normal Switch 2 path)

Follow-up uniform-plan captures did **not** demonstrate a village FPS gain:
off 26.15 FPS / trailing render CPU average 37.23 ms, on 26.20 FPS / 37.37 ms.
The sampled pack phase decreased from 3.50 to 3.22 ms, at 5,165 versus 5,198
draws/frame; the frame-wide difference is not evidence of a speedup. Sea was
57.98 FPS / 16.17 ms off versus 58.66 FPS / 15.65 ms on, with somewhat different
draw counts. All verification checks remained zero: these are timing captures,
not an exact-output validation on console.

The player confirmed stable 30 FPS already works and requested an implementation
to improve 60 FPS, **not more manual A/B runs**. The next build adds these normal,
automatically enabled Switch 2 paths without new test switches or automatic
profiling:

- **Vertex binding compaction:** defer vertex buffer binding writes until all
  streams for the current draw are prepared, then emit each contiguous run with
  one `vkCmdBindVertexBuffers`. Sparse holes are untouched; repeated writes keep
  their last value. CPU-decoded streams and stereo replay use the same grouping.
  Scratch is bounded to 32 slots; higher slots preserve the original individual
  calls after flushing pending bindings. No draws, copies or barriers are moved
  across draw boundaries. The existing owned deferred-command paths copy arrays
  before this per-draw scratch dies.
- **Validated index memoization:** 128 bounded CPU entries for nonwrapping BE16 /
  BE32 indexed draws, each at most 16 KiB source and 16K output indices. Every
  lookup checks the full conversion key AND every fresh source byte. Misses first
  snapshot the source, then convert that same snapshot. Hits reuse conversion and
  raw min/max metadata, applying the current signed base vertex per draw. GPU
  uploads can be reused only within the same device/submission generation. A
  changed byte, collision, renderer recreation or generation change cannot make
  a stale GPU slice a valid hit. Empty, large, wrapped, native and write-watch
  cache paths retain the previous handling. The persistent write-watch buffer
  cache is still not enabled on the dense backend.
- **Small uniform snapshot reuse:** enable the existing exact-byte-validation
  cache for uniforms, not for large vertex streams. Unchanged payloads retain
  their immutable aligned upload slice within the submission. Enable the existing
  exact descriptor bind-state comparison to omit unchanged sets/offsets; pass,
  command-buffer, generation and pipeline-layout boundaries remain in the key.

These reduce repeated CPU preparation and binding calls; they do **not** merge
5,000 draws into one or omit objects. Logic timing, player settings, resolution,
clocks, shader math and drawing order are unchanged. Existing environment
diagnostics remain available internally; the user need not edit any file or
operate a new menu control. Desktop and Switch 1 defaults stay unchanged.

If a NetLoader capture is explicitly requested later, its existing report also
includes vertex binding inputs/emitted API calls, validated index hits and
avoided index upload bytes. Nothing is collected/logged periodically by default.

`tools/switch2/command_compaction_check.cpp` checks 20,000 randomized binding
sequences against the original final binding state (including sparse holes,
duplicate bindings, full 32-slot runs and out-of-scratch fallback), and tests
index source mutation, collisions, incomplete conversions, immutable GPU payloads,
device/generation changes, reset and size bypass. Its synthetic command reduction
is **not** a hardware performance result. A further 10,000 mixed primitive,
BE16/BE32 and restart draws with mutations/collisions/resets compare cached
conversion and uploaded payloads against a freshly converted reference (passes).
Existing index-conversion, uniform
snapshot and submitted-uniform host checks also remain applicable. Build:
`python tools/switch2/compile_runtime.py --nro --jobs 4`. Hardware effectiveness
and visual correctness of this combined change remain unverified; no new manual
measurement campaign is required from the player.

##### Multi-entry vertex range reuse enabled normally

The player explicitly requested `vertex reuse=on` to target repeated geometry
uploads and the 16.667 ms budget for 60 FPS. Simply restoring the old last-entry
cache would ignore its earlier sea regression. Switch 2 now uses a different
normal path for bounded full-prefix vertex snapshots:

- 512 sets x 4 ways (2,048 immutable snapshots) indexed by the guest base address,
  shared across vertex binding numbers. Interleaved meshes no longer replace a
  single per-binding entry on every draw.
- The requested prefix size is **not** part of the key. A larger snapshot serves
  a smaller proven fetch prefix after comparing every requested byte. Growing
  prefixes or changed bytes produce a new immutable upload; no in-flight slice
  is patched. Small payload padding remains zero-defined.
- Fresh source bytes are always validated against a CPU shadow, or against the
  immutable upload only when host-cached/coherent direct reads are allowed.
  No address-only reuse, guest write-watch assumption or cross-submission GPU
  reuse. Device/submission changes and renderer resets invalidate all entries.
- Source ranges above 1 MiB, wrapped ranges and unbounded fetches keep the fresh
  upload path. Retained CPU shadow capacities have a 32 MiB budget; budget
  pressure drops metadata/shadows, never GPU-owned arena slices. A replacement
  shadow can temporarily add up to one cacheable range during admission.
- Enable the existing exact vertex-binding-state comparison normally on Switch
  2: when a validated snapshot preserves the same buffer+offset, no redundant
  bind is emitted. The preceding contiguous-binding compaction remains active.

The Dev control is now the initially-unchecked
`Disable vertex range reuse (baseline)`; normal capture state reports
`vertex reuse=on` without user setup. Diagnostic/profiling defaults stay off.
The old per-binding history code stays available for other platforms; the
separate partial-window diagnostic retains its distinct cache so uninitialized
prefix bytes cannot masquerade as a complete snapshot.

`tools/switch2/vertex_range_cache_check.cpp` covers interleaved addresses, prefix
hits, outside-prefix mutations, changed prefixes, growth, small zero padding,
LRU collisions, generation/device/reset, shadow budget reclamation and stable
shadow-to-upload bytes when the guest changes during the factory call. It also
checks 20,000 randomized requests in each of shadow/poisoned-upload and direct
modes against immutable expected GPU bytes. These host checks pass.

This directly targets repeated upload allocations and writes, but validation
still reads guest bytes. Neither sampled unique-byte counts nor sampled phase
timings prove that removing all duplicate copies would bring total render CPU
below 16.667 ms; no constant-60-FPS or measured speedup claim is made for this
build without a hardware result. The player is not asked to run further manual
A/B captures.

#### Player defaults: settings chord and 60 fps presentation

Switch 2 no longer enqueues the new-controller setup offer on connection.
The existing controls/setup UI remains accessible manually. Its settings
shortcut is **L + R + Minus**, registered through the overlay's platform
extension before the first input pump. All three must be released before it
arms at startup and after every toggle; holding the chord or only releasing
Minus cannot repeatedly open/close the menu. Overlay input-release gates keep
the recognized chord from leaking into the game. Minus alone no longer opens
Settings after a half-second hold; it remains available to the game's existing
single-screen controls. The controller shortcut row and About hints show the
fixed chord; other platforms keep their existing shortcuts and setup offer.

The current `presentation_defaults()` uses **native 30 FPS** for fresh settings:
`fps60=0` disables interpolation and True 60. `interpFps=60` only remembers the
rate for a later opt-in to interpolation; it does not enable that mode. Existing
saved values are preserved, including choices from the earlier 60 FPS prototype.
Explicit FPS environment overrides defer default initialization. These are
targets, not measured performance, and no clocks are changed. Game data and saves
are untouched. Use **L + R + Minus** to change an existing saved FPS choice.

Host regression check (MSYS, no game/settings files needed):
`g++ -std=c++20 -Iruntime/src tools/switch2/player_defaults_check.cpp -o build/switch2-player-defaults-check.exe`
then `build/switch2-player-defaults-check.exe`. It covers startup-held buttons,
single buttons, chord latching/rearm, release-gate behavior, one-time defaults,
subsequent player choices and environment overrides. The new NRO still needs
hardware verification for menu behavior, frame pacing and actual performance.

The first full-runtime NRO now builds and links successfully:
`build/switch2-game/wwhd_switch2.nro` (68,241,605 bytes). This is the actual
WWHD C++ runtime, HLE/GX2, Vulkan/SDL host and private ARM64 game code, not a
clear-only or RPX-loader diagnostic. The initial artifact was untested when
first built; gameplay was subsequently validated after the path fix above.

Rebuild after preparing dependencies and the private archive:

```text
python tools/switch2/build.py --rebuild-libounce --game-manifest build/switch2/game-manifest.json --build-gamecode --jobs 2
python tools/switch2/build_deps.py
python tools/switch2/compile_runtime.py --nro --jobs 4
```

The output ELF is AArch64 PIE with no undefined symbols. Its dispatch trampoline
restores SP/LR before an indirect `br` into guest code (checked in the linked
disassembly). All Clang guest C files now use `-fexceptions -funwind-tables`,
not just the trampoline: `GuestExit`/HLE exceptions must be able to unwind
ordinary guest calls too. The linked ELF contains `.eh_frame` and exception
tables. Thirteen host-only Switch 2 tests and both dispatch IR checks pass.

The host checks the RPX SHA-256 against the compiled identity, verifies the
content/meta layout, selects the validated dense guest-memory implementation
when sparse mapping is unavailable, and uses separate game/save/config paths
under `sdmc:/switch/wwhd-switch2`. The startup logger is shared with the probes,
now mutex-serialized; game output goes to nxlink and
`sdmc:/switch/wwhd-switch2/wwhd.log`. No log-writer thread or clock changes.

**Prototype exit policy:** the main entry and `exit`, `_Exit`, `_exit`, `abort`
are link-wrapped to end the entire title with `svcExitProcess`, without running
static destructors or tearing resources out from under running threads. It
does **not** return to hbmenu. This avoids unloading live guest/service code;
it does not implement orderly per-thread cancellation/join or GPU shutdown.
Detached pthread resources are retained until process termination because
libounce/newlib lacks detach support; default-attribute pthreads receive 4 MiB
stacks. A reaper and orderly shutdown remain release-quality work. Treat this
as an initial console-test build, not a finished playable release.

SDL's Switch driver requires EGL symbols even with an external graphics
context. The SDK's EGL script selects the combined `libouncevk-gl` provider,
which exports Vulkan as well. Only this provider is linked in the full game;
adding the Vulkan-only `libouncevk.a` as well causes duplicate definitions.
This does not create an EGL context/window: presentation still uses VI Vulkan.
Switch 1-only preparation/benchmark/shader-wait hooks are excluded, as is its
profiler; unavailable thread CPU/peak RSS metrics report unavailable rather
than pretending that wall time or heap size is a CPU/RSS measurement.

On-console launch (Windows CMD, with NetLoader active):

```cmd
"C:\devkitPro\tools\bin\nxlink.exe" -a CONSOLE_IP -s "build\switch2-game\wwhd_switch2.nro"
```

### Full-runtime source compilation (compile-only default)

#### First full-runtime hardware test: device-prefix path failure

The first full NRO reached application/window/service initialization, runtime
constructors, and verification of the RPX hash and content/meta layout. It
reported 8,924 MiB free before guest allocation. Startup then failed **before
guest execution** in save-backup path resolution:
`sdmc:/switch/wwhd-switch2/sdmc:/switch/wwhd-switch2/save-backups/restore-pending.json`.
The logged `status=1` whole-title termination was intentional after that error;
it was not a Vulkan initialization failure (renderer selection alone had run).

Fix: expose `/switch/wwhd-switch2/...` POSIX absolute paths to C++ for the
config/game/save roots. libounce accepts these on the default SD device.
Link-wrap `getcwd` as well: strip its `sdmc:` prefix and trailing slash so
`std::filesystem::absolute(relative)` cannot reintroduce a device-prefixed
relative component. Reject unsupported device prefixes rather than silently
redirecting them. Native FS logger paths remain volume-relative and unchanged.
The boot marker `paths-v2` prints and validates the C++ current/save paths
before any save/backup operations. No old save/config files are deleted.

`tools/switch2/path_check.cpp` tests the actual shared path helper, both explicit
absolute and relative-save joins, and the pending-restore regression with the
host MSYS C++ compiler. It passes without reading/writing any game/save files:
`g++ -std=c++20 -Iruntime/src tools/switch2/path_check.cpp -o build/switch2-path-check.exe`
then `build/switch2-path-check.exe` (run inside devkitPro's MSYS shell).
The `paths-v2` NRO subsequently passed the console test above.

`python tools/switch2/compile_runtime.py --jobs 4` configures the root project
in `build/switch2-runtime`. `NINTENDO_SWITCH2` selects
`cmake/switch2/RuntimePort.cmake` before the Switch 1/deko3d configuration.
The private Clang game archive is imported; the actual C++ runtime, GX2,
Vulkan backend, SDL input and overlay are compiled with GCC. At this milestone
`wwhd` is deliberately an **OBJECT library**: no ELF or NRO is produced, so an
unfinished host cannot unload into hbmenu with guest/service threads alive.
LZ4 1.10.0 is built from the same checksum-pinned source as desktop bundles.

Switch 2's Vulkan path uses SDL's external-graphics-context window, the static
OunceVK resolver and native VI surface creation (including surface retry), not
SDL's disabled Vulkan loader or EGL window creation. A second GamePad window
is never offered/created. Switch 2 uses the SDL audio path instead of the
Switch 1 detached audout thread, and its default config directory is
`sdmc:/switch/wwhd-switch2`. Switch 1's screen hooks are disabled specifically
on Switch 2; the SDL host keeps upstream `mods/one_screen.cpp` instead, avoiding
duplicate game hooks. Startup selection of the single-screen mode still needs
the Switch 2 host integration.

The complete runtime and Vulkan source compilation now passes. The opt-in
`--nro` path above additionally links the experimental whole-process-exit host
in a separate `build/switch2-game` directory. Neither build success validates
SDL/OunceVK presentation on hardware or guest execution; orderly shutdown/join
coverage and hardware runtime-service tests remain outstanding.

### Runtime loader test (not gameplay)

Output: `build/switch2-first-frame/wwhd_switch2_runtime_probe.nro`.

This uses the actual WWHD `load_rpx` implementation, extracted unchanged from
`core.cpp` to `runtime/src/rpx_loader.cpp`. The ordinary runtime source glob
continues to include it. The new Switch 2 memory layer uses the same guest
layout as Switch 1, with explicit unmapping/freeing before returning to hbmenu:
a reserved 4 GiB address window, only the six used ranges physically backed
(about 1.4 GiB). It checks all map/permission/unmap results and never frees
backing that still has aliases. It does not start guest threads, call the RPX
entry, or initialize graphics/audio. These mappings are not yet validated on
Switch 2 hardware.

The loader probe cross-build/link succeeds (858,309-byte NRO, no undefined ELF
symbols). Its dense-heap loading/shutdown has now passed the console test below;
the optional sparse mapping backend remains untested on Switch 2.

**Hardware update:** the personal RPX hash matches, but kaiHEN does not hint all
of the required process-code mapping SVCs. The old probe stopped before making
any mapping. That is consistent with kaiHEN's documented game policy
(`libkaihen/JIT.md`), not proof the tester launched outside title mode.

The updated `memory net-v6` reports each SVC hint and available heap. When
sparse mapping is unavailable, it uses a **normal 4 GiB aligned data allocation
from libounce's heap**, requiring at least another 256 MiB of free host memory
before attempting it. It zeroes the allocation and frees it before returning.
The actual RPX and guest layout are unchanged; no executable pages, JIT calls
or privileged SVCs are needed for this backend. The cost is 4 GiB instead of
about 1.4 GiB. Its null area and address gaps are zero-filled **but not guarded
or unmapped**. This is logged explicitly. If the heap is insufficient or
fragmented, the test fails with a diagnostic rather than changing permissions
or modifying kaiHEN.

**Dense-backend hardware PASS reported by the tester:** all three process-code
SVC hints were zero, with an own-process handle present. libounce reported
9,061 MiB total heap and 9,034 MiB free before allocation. The 4,096 MiB data
window was allocated and zeroed; WWHD's loader returned entry `028EA120`, SDA
`104E21E0`, SDA2 `10008000`, data end `104DA1C8`, stack size `10000`. The scratch
endian check passed, the dense allocation was freed, and the trace reached
`PASS: checked RPX loaded; guest window released; returning to hbmenu`, followed
by normal app exit. This validates loading/guest data memory, **not execution of
the recompiled game or the full Wii U runtime**.

1. Copy **your own** `code/cking.rpx` to
   `sdmc:/switch/wwhd-switch2/game/code/cking.rpx`.
2. Run the loader probe through nxlink `-s` (same command as the other probes,
   but with `wwhd_switch2_runtime_probe.nro`). No game image is expected.
3. Return the complete network output or `/wwhd-switch2-runtime.log`.
4. Expected endpoint: `PASS: checked RPX loaded; guest window released` followed
   by the normal return to hbmenu. SVC hints, selected backend, failed
   allocations and mappings are logged; launch through native-title kaiHEN
   to make the application's full RAM available.

The RPX hash is verified **before** passing data to the original loader; a
different or updated dump is refused. No game code is embedded in this probe.

### Optional game-code archive

```sh
python tools/switch2/build.py --rebuild-libounce \
  --game-manifest build/switch2/game-manifest.json --build-gamecode --jobs 2
```

This compiles the personal generated C into `libwwhd_switch2_gamecode.a`, not
a playable NRO. Clang with AArch64/musttail support is selected via `--clang`
(the helper can discover Visual Studio Community's Clang 19 on Windows).
The C++ runtime, libc and rebuilt libounce remain on devkitA64. This avoids
switching their ABI just to compile the C game code. Generated C has no TLS;
GCC's unsupported `-mtp=soft` is not passed to Clang. The optional compiler
probe checks both function-entry preemption and a genuine musttail dispatch.

Clang enforces equal parameter counts for musttail. On Switch 2/Clang only,
`PPC_ENTER_FN` therefore uses the original call-and-resume `ppc_preempt` check
instead of the Switch 1/GCC-only two-argument tail-restart optimization. Actual
guest tail calls still use `musttail`; no generated game file is edited.
This path requires runtime/gameplay validation and makes no performance claim.

The **full game executable remains unimplemented**: the GX2/Vulkan host, SDL3,
HLE services, guest dispatch/thread execution, audio/input and clean shutdown
still need integration. In particular, the full C++ dispatcher also uses
musttail and cannot simply be compiled by the installed GCC 14. Generated
objects/archive contain private game-derived code and must not be distributed.
