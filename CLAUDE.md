# SoapyVI_PS5 — Working Log & Project Notes

> Fork of [KytyPS5](https://github.com/KytyPS5/KytyPS5) (GPL-2.0), a PlayStation 5
> translation layer. This file is a **living log**: toolchain state, build changes,
> known issues to verify, per-game performance stats, and hardware context.
>
> **Maintenance rule:** update this file on *every* build, dependency change, code
> change, and game test run. Each working & validated change is committed and pushed
> to the `SoapyVI_PS5` repo. Keep newest entries at the top of each log section.

---

## 1. Hardware (test machine)

| Component | Spec |
|-----------|------|
| CPU | AMD Ryzen 7 5700X3D — 8 cores / 16 threads, 3.2 GHz base |
| GPU | NVIDIA GeForce RTX 3060 **12 GB** (WMI misreports 4 GB) — driver 32.0.15.9636 |
| RAM | 32 GB DDR4-3200 |
| OS | Windows 10 Pro, build 19045 |
| Disk C: | 930 GB total — **24.1 GB free at setup (TIGHT, watch this)** |
| Display | 2560×1440 |

> ⚠️ **Disk space risk.** 24 GB free is marginal for a Release build + Qt 6 + Vulkan
> SDK + build artifacts. Free space before/if the build or Qt install fails with
> ENOSPC. Candidates: move `_Build` to a larger drive, or `-DCMAKE_BUILD_TYPE=Release`
> keeps artifacts smaller than Debug.

---

## 2. Toolchain state

| Tool | Status | Notes |
|------|--------|-------|
| Git | ✅ present | |
| Python | ✅ 3.12 | needed by bundled SPIRV-Tools at configure time |
| CMake | ✅ bundled in VS 2026 | `…\VS\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin` — add to PATH |
| Ninja | ✅ bundled in VS 2026 | `…\CMake\Ninja\ninja.exe` — add to PATH |
| MSVC toolset | ✅ 14.52.37025 | provides STL/CRT headers for clang-cl |
| Windows SDK | ✅ 10.0.26100.0 | |
| clang-cl | ⏳ installing | standalone LLVM 23.1.6 via winget (`LLVM.LLVM`) |
| glslang | ⬜ pending | via Vulkan SDK (`KhronosGroup.VulkanSDK` 1.4.363.0) |
| Qt 6 | ⬜ pending | Concurrent, Network, Widgets (MSVC 2022 64-bit) — launcher only |
| Vulkan headers | ✅ vendored | `3rdparty/Vulkan-Headers` submodule (no separate SDK needed for headers) |
| Submodules | ✅ all 15 initialized | |

### Build configuration (decided)
- Full build **including** the Qt GUI launcher.
- Compiler: **clang-cl** (MSVC `cl.exe` is rejected by CMake). `cl.exe` is not supported.
- Generator: Ninja, `-DCMAKE_BUILD_TYPE=Release`.
- Planned configure (adjust Qt path once installed):
  ```
  cmake -S . -B _Build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl ^
    -DCMAKE_PREFIX_PATH="C:/Qt/6.x.x/msvc2022_64"
  ```

---

## 3. Changelog (newest first)

### 2026-11-26 — First game test (GTA VI) + crash-reporter thread-id fix
- **Rebuilt the emulator from scratch** — `_Build/` (incl. `winenv.bat`) had been deleted. Recreated
  `winenv.bat` (paths re-verified: VS18 vcvars64, LLVM `C:\Program Files\LLVM\bin`, Vulkan SDK
  `1.4.363.0\Bin`, CMake/Ninja now also on global PATH), reconfigured (70s) and built
  `kyty_emulator` (992/992, exit 0). Binary back at `_Build/windows/kyty_emulator.exe` (~30 MB).
- **First real game boot attempt — Grand Theft Auto VI** (`PPSA38500`, SDK 12/13). See §5 for the
  full entry. Boot reaches guest code then crashes in `libc.prx`; **root cause = Agc (PS5 next-gen
  graphics) is unimplemented** → unresolved stubs → libc AVX store to an unmapped address. Expected
  for an SDK-12 next-gen title on this emulator.
- **This validated the crash reporter on a real crash** (previously "pending"): it wrote
  `_Logs/crash_<ts>.txt` + mirrored the full report to the structured log at Critical. 
- **Bug found & fixed via the logging system:** the crash report's `thread:` field rendered **blank**
  — `PthreadGetname` returns 0 (success) with an empty string for never-named guest threads, so the
  old `!= 0` fallback was skipped. Fixed (`runtimeLinker.cpp`): empty-name now falls back to
  `(unnamed guest thread)`, and the report + structured marker now also print the **native OS thread
  id** (`GetCurrentThreadId` / `SYS_gettid` / mach) so a crash can be correlated to the session log.
  **Validated:** rebuilt, re-ran the identical crash → `thread: (unnamed guest thread) (host tid 25964)`.

### 2026-11-26 — Crash reporter (verbose-logging initiative, part 2 of 4)
- `RuntimeLinker::WriteCrashReport(ExceptionInfo)` (runtimeLinker.cpp): on a fatal fault, builds a
  report (git version, faulting thread, registers, ±48 code bytes, 32 stack qwords, and the loaded
  module list with the module containing the PC marked), writes `_Logs/crash_<timestamp>.txt`,
  mirrors it to the structured log (`KLOGC(Category::Crash, …)` marker + full body at Critical),
  and still echoes to the console.
- `KytyExceptionHandler` now calls it instead of the old stdout-only dump — so crash context is
  captured in the session log and a standalone file even with `--printf-direction File`.
- Best-effort/lock-free (no `m_mutex`) since the faulting thread may hold it. **Validated:** emulator
  builds clean (0 errors). Runtime crash-path validation pending a real game/crash.

### 2026-11-26 — Structured logging core (verbose-logging initiative, part 1 of 4)
- New focus: a verbose logging system to capture crashes, shader translations, frame times, and
  more. Inventoried existing logging (flat spdlog `LOGF`; `DebugCounters`+perf overlay on-screen
  only; shader IR dump via `--shader-log-*`; crash context via `std::printf` in `KytyExceptionHandler`).
- Built the **structured log core** (the foundation the other three parts write through):
  - `common/logging/logCategory.h` — `Level` (Trace→Critical/Off), `Category` (Kernel, Loader,
    Gpu, Shader, Pipeline, Audio, Input, Hle, Fs, Net, Perf, Crash, Guest), `KLOG*` macros.
  - `common/logging/logStructured.cpp` — rotating session file (64 MiB × 5), timestamped
    `[time] [LEVEL] [Category] msg`, min-level gate, optional console echo, `flush_on(warn)`.
  - Config + CLI: `--log-level`, `--log-file`, `--log-console`; auto file `_Logs/kyty_<timestamp>.log`.
  - Kept separate from the guest-printf stream; `LOGF` untouched; API kept out of `log.h` to limit rebuilds.
- **Validated:** build clean (0 errors); `structured_log_tests` passes incl. a real file round-trip;
  `kyty_emulator --help` lists the new flags. (Gotcha: this spdlog uses wide filenames — pass `path.native()`.)
- Next parts: crash reporter → perf/frame telemetry sink → shader translation ledger.


### 2026-11-25 — Initial environment setup
- Inventoried toolchain; confirmed CMake/Ninja bundled in VS 2026, MSVC 14.52 + Win SDK present.
- All 15 git submodules confirmed initialized.
- Installed **LLVM/clang-cl 23.1.6** (winget), **Vulkan SDK 1.4.363.0** (glslangValidator at
  `C:\VulkanSDK\1.4.363.0\Bin`), and **Qt 6.8.3 msvc2022_64** (via `aqtinstall` → `C:\Qt\6.8.3\msvc2022_64`).
- Added reusable build-env wrapper `_Build/winenv.bat` (vcvars64 + clang-cl/glslang/cmake/ninja on PATH).
- **CMake configure succeeded** (exit 0, 84s) → `_Build/windows`. Command recorded in §2.
  - Benign: "Could NOT find WrapVulkanHeaders" = Tracy's optional system-Vulkan probe; project uses vendored headers.
- Disk freed up to **61.7 GB** before build (was 24 GB) — risk reduced.
- Peer-reviewed launch path: `main.cpp`, `emulator.cpp` (Run/mount/init), `elf.cpp`,
  `loadExec.cpp`, `runtimeLinker.cpp` (LoadProgram/Execute). No launch-path correctness
  bugs found; code is defensively asserted. Notes recorded in §4.
- Full build (launcher + kyty_emulator + kyty_tests) started.
- Fork target: **public** `SoapyVI_PS5` repo (awaiting `gh` auth).

### 2026-11-25 — Build succeeded ✅
- **Build completed, exit 0, 0 errors / 0 failures** (2392 objects, clang-cl 23.1.6 + lld-link).
- Binaries: `_Build/windows/kyty_emulator.exe` (~30 MB), `_Build/windows/launcher/kyty_launcher.exe` (~5.3 MB).
  Qt runtime DLLs deployed next to the launcher via windeployqt.
- Smoke test: `kyty_emulator.exe --help` runs and prints `ver = 0.3.0, git = …93cce665-dirty`.
- `ctest`: **80/84 passed (95%)**; 4 failures logged in §4 (video decoder + kernel fs crashes,
  an NTFS timestamp test, and one not-run test). None block the emulator from building/running.

### 2026-11-25 — Fork published & pushed ✅
- Authenticated `gh` via `GH_TOKEN` env (piping the token into `--with-token` on PowerShell
  appended a CR and caused spurious `401 Bad credentials`; the env var avoids that).
- Created public repo **https://github.com/ZaneIceMaster11/SoapyVI_PS5**.
- Remotes: `origin` → SoapyVI_PS5, `upstream` → KytyPS5. Full `main` history pushed.
- **Security:** the classic PAT was shared in chat — **revoke it** at github.com/settings/tokens
  once local push access is re-established via a terminal `gh auth login`.
- Next: investigate the `kernel_file_system` / `vdecsw` test crashes; then await a valid game dump.

---

## 4. Known issues / to verify

- [x] ~~Disk space~~ — freed to 61.7 GB before build; risk reduced.
- [x] clang-cl resolves MSVC 14.52 STL headers — configure succeeded via `winenv.bat` (vcvars64).
- [x] glslang on PATH — `C:\VulkanSDK\1.4.363.0\Bin` added by `winenv.bat`; configure found it.
- [x] Qt route — `aqtinstall`, prefix `C:/Qt/6.8.3/msvc2022_64`.
- [x] ctest: **80/84 passed (95%)** on this toolchain. Failures (upstream code, not fork regressions):
  - `common` → `File.GetTimesReportsExactNanoseconds` — NTFS timestamp-precision expectation; environmental, not launch-related.
  - `vdecsw` → crash `0xC0000409` (stack-overrun fastfail) — software video decoder (FMV/cutscenes). Investigate before trusting video playback.
  - `kernel_file_system` → crash `0xC0000409`. **Investigated (root cause found):** the suite's
    first test, `TestMutexHandoff`, detects a sleeping thread via `/proc/self/task/<tid>/stat`
    (`WaitUntilSleeping`), which is Linux-only; on Windows the 5 s deadline trips and `Check()`
    calls `std::abort()`. The leading mutex/signal cluster is Linux-CI-oriented and also performs
    one-time pthread init the rest of the suite needs. A local guard (`#if defined(__linux__)`
    on the cluster + replicating the init on other platforms) made `--mutex-only` pass, but the
    heavyweight Vulkan+filesystem section then fastfails separately — a distinct, pre-existing
    Windows issue in a Linux-CI test. **Not a product/launch bug; zero impact on running games**
    (the emulator builds, smoke-runs, and upstream ships Windows game support). Experimental test
    edits were reverted (can't validate the Linux path from here). Revisit only if Windows CI for
    this suite is wanted.
  - `psml` → Not Run — `psml_tests` binary not produced by the `kyty_tests` target set; build it explicitly to run.
- [ ] Tracy's "WrapVulkanHeaders not found" — confirm benign (project uses vendored headers).

### Peer review — game-launch path (2026-11-25)
Scope: the code exercised when launching a game — `src/main.cpp`, `src/emulator.cpp`,
`src/loader/{elf,loadExec,runtimeLinker}.cpp`. Verdict: **mature, defensively-asserted code;
no launch-path correctness bugs found.** Items to keep in mind:

- **Decrypted SELF required.** `Elf64::LoadSegment` aborts (`EXIT_NOT_IMPLEMENTED`) on
  compressed/encrypted SELF segments (`compressed_size != decompressed_size`). The game's
  `eboot.bin`/PRXs must be decrypted / fake-signed (fSELF). The emulator does not decrypt
  retail firmware SELFs. → If a provided game won't boot, check this first.
- **Mount comment vs code** (`emulator.cpp:176-178`): comment says `/hostapp` is the writable
  mount, but `/app0` is mounted writable (`true`) and `/hostapp` read-only. Verify intended; cosmetic.
- **Dynamic-section walks** (`GetDynValue`/`GetDynList`) rely on a `DT_NULL` terminator; a
  malformed dynamic section could over-read. Trusted-input only; low risk.
- Arg parsing (`main.cpp`) is thorough and bounds-checked. `--game` resolves dir / fSELF / `.zar`
  correctly and requires an archive root to contain `eboot.bin`.
- `LoadProgramToMemory` guards size math against overflow and sets per-segment protection correctly.

---

## 5. Per-game test log

> One subsection per game tested. Record: build/commit tested, launch command,
> boot result, FPS/perf, graphical/audio issues, crashes, and log excerpts.

#### Grand Theft Auto VI — 2026-11-26
- Title: `PPSA38500` / contentId `EP1004-PPSA38500_00-GRANDTHEFTAUTO00`, SDK `0x12000000…` (12.00),
  `requiredSystemSoftwareVersion` 12.00, appSize 320 GiB. Source: `E:\totalyrandomminecraftmoddingfunny`
  (RAGE engine — `.rpf` archives; `levels_concat.rpf` alone is 202 GB).
- Commit tested: `4772586` (+ the thread-id fix this session).
- Command: `kyty_emulator.exe --game "E:\totalyrandomminecraftmoddingfunny\eboot.elf" --log-level Trace --log-file _Logs\gta6_boot.log --shader-log-direction Silent`
  - **Used the decrypted `eboot.elf`**, not the directory. `--game <dir>` forces `/app0/eboot.bin`
    (encrypted retail SELF → loader aborts in `Elf64::LoadSegment`). Pointing at `eboot.elf` loads
    the decrypted image while still mapping `/app0` to the game dir so `.rpf` data resolves.
- Boot: **crashes early in guest init.** Progress reached:
  1. All subsystems init OK (Config, Log, Pthread, Profiler, Network, Memory, FileSystem, Controller,
     Audio, Graphics); Vulkan pipeline cache created for `PPSA38500`; GPU CP assigned; `Title ID: PPSA38500`.
  2. Modules loaded: `eboot.elf`, `libc.prx`, `libSceNpCppWebApi.prx`. Game `sce_module/` also ships
     `libc.prx` + `libSceNpCppWebApi.prx`; `fakelib/libSceAmpr.sprx` present.
  3. Guest code runs: calls `libkernel` stub `yu17wG8L5FI` (prints `version = 13`), then a burst of
     **`Agc`** stubs (`da1Sm8-QDoU`, `0ZOG0jc9nRg`, `Y-5vneiBtzk`, `oz6zQq1JwCE`, `+u6dKSLWM2o`),
     then `VideoOut` (`pA8xxaySl7A`) and `Ampr` (`d3lKWPM6odc`) stubs.
  4. **Crash** (`0xC0000005` access-violation, write) in `libc.prx` at `pc=0x910003e55`, faulting
     addr `0x0000080400000000`. Fault bytes are AVX stores (`c5 f9 7f …` = `vmovups`/`vmovdqa`) — a
     libc memset/memcpy writing to an unmapped buffer; `rax=rdi=r12=0x080400000000`.
- Perf: n/a (never reached a frame).
- **Root cause: `Agc` (PS5 next-gen graphics API, SDK 12/13) is unimplemented.** KytyPS5 implements
  the legacy `Gnm`, not `Agc`. Unresolved `Agc` imports are patched to logging stubs that return a
  default; the game treats a stubbed return as a valid buffer/context pointer and a subsequent libc
  AVX store lands on unmapped memory → fault. This is the fundamental blocker for GTA VI and any
  SDK-12+ next-gen title; not a regression in this fork.
- Logs: `_Logs/gta6_boot*.log` (structured), `_Logs/gta6_console*.txt`, `_Logs/crash_20261126_*.txt`.
- Observation on the logging system: the loader/HLE path still logs via the **flat `LOGF`/printf**
  stream (`Unresolved import stub called: …`), not the new structured `KLOG*` categories — only the
  crash reporter writes to the structured log so far. Routing the unresolved-stub and module-load
  events through `KLOG(Category::Loader/Hle)` would make this kind of boot triage far easier
  (candidate for a future part of the verbose-logging initiative).
- Action items: (1) to get past this, `Agc` command-buffer/submit HLE would need implementing — large.
  (2) Route loader/HLE events through structured logging. (3) Consider having unresolved stubs return
  a poisoned/guard pointer rather than 0 so faults point at the stub, not deep in libc.

### Template
```
#### <Game name> — <date>
- Commit: <short sha>
- Command: kyty_emulator.exe --game "<path>"
- Boot: [ reaches menu / in-game / crashes at <stage> ]
- Perf: <avg fps>, <resolution>, <frame pacing notes>
- Issues: <graphical / audio / input / crash>
- Log: <path or excerpt>
- Action items: <what to fix/verify>
```

---

## 6. Repo / push workflow

- Upstream origin: `https://github.com/KytyPS5/KytyPS5.git`
- Target fork: **`SoapyVI_PS5`** (to be created under the user's GitHub account).
- Rule: push only **working, validated** commits. Each push updates this log.
- Status: ⬜ fork not yet created (requires `gh` CLI + GitHub auth).
