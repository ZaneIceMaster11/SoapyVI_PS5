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
- `ctest` run in progress — results to be recorded next.
- Still blocked on `gh` auth for the push (token kept being rejected / network).

---

## 4. Known issues / to verify

- [x] ~~Disk space~~ — freed to 61.7 GB before build; risk reduced.
- [x] clang-cl resolves MSVC 14.52 STL headers — configure succeeded via `winenv.bat` (vcvars64).
- [x] glslang on PATH — `C:\VulkanSDK\1.4.363.0\Bin` added by `winenv.bat`; configure found it.
- [x] Qt route — `aqtinstall`, prefix `C:/Qt/6.8.3/msvc2022_64`.
- [ ] After build: run `ctest --test-dir _Build/windows --output-on-failure`; record pass/fail here.
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

_(No games tested yet — awaiting game files from the user.)_

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
