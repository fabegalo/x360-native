# Local patches to ReXGlue SDK

Base revision: **`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`** (tag `v0.10.0`), the
commit pinned in `deps.lock.json` and checked out as the `external/rexglue-sdk`
submodule. The patches are for that commit only; no upgrade or downgrade.

The submodule's history is not changed: the patches are applied to its working
tree from these files. `deps.lock.json` (`rexglue_sdk.patches`) records each patch
file's SHA-256, the SHA-256 of every file it touches before (`base_files`) and
after (`files`), and its test. No two patches touch the same file.

| Patch | Fixes | Test (`ctest -R "sdkcheck[.]"`) |
|---|---|---|
| `0001-function-table-xex-heap.patch` (`src/system/xmemory.cpp`) | `Memory::InitializeFunctionTable` / `DestroyFunctionTable` used the `0x80000000` XEX heap only, so an image in the `0x90000000` XEX heap made `Runtime::Setup` fail (`Failed to allocate function table at 93028000`, dashboard 2.0.17559.0). The table keeps its address, `image_base + image_size`, which generated code hard-codes; it is now allocated in the XEX heap that holds its whole range, with that heap's page size, and requests that overflow, leave the heap, are misaligned, are empty or overlap another table are rejected | `tests/sdkcheck/function_table_test.cpp` |
| `0002-setup-failure-teardown.patch` (`src/system/runtime.cpp`) | a failed `Runtime::Setup` called `Shutdown()` itself, destroying the graphics system while `ReXApp` still held objects made from it; `ReXApp::OnDestroy` then crashed (access violation in `D3D12Core`). Setup now returns the error and leaves teardown to `~Runtime()` | `tests/sdkcheck/setup_failure_test.cpp`; through a real `ReXApp`: `tests/sdkcheck/app_shutdown_test.cpp` (failed setup) |
| `0003-import-records-across-libraries.patch` (`src/system/xex_module.cpp`, `src/codegen/phase_register.cpp`, `include/rex/codegen/analysis_errors.h`, `src/codegen/analyze.cpp`) | `XexModule::SetupLibraryImports` paired a function import's value slot (record type 0) and thunk (type 1) only inside one import library record. The dashboard has `xam` split over two records, with the slot of `xam 0x8E2` in one and its thunk in the other: the slot half became an import symbol at address 0, the codegen wrote `{ 0x0, __imp__XamNuiGetSupportString }` as the first `PPCFuncMappings` entry, and the runtime, which stops at guest 0, registered no function. The loader now completes the half-filled entry of the same library (slot-only + thunk-only, either order; complete entries stay separate). The codegen no longer maps an import at address 0: a known function import without any thunk is an `ImportWithoutThunk` analysis error (generation blocked unless `--force`) | `tests/sdkcheck/import_records_test.cpp` (synthetic XEX2) |
| `0004-app-teardown-order.patch` (`src/ui/rex_app.cpp`) | `ReXApp::OnDestroy` destroyed the window before the runtime. After a successful setup the input drivers are attached to the window, and `~MnkInputDriver` (from `Runtime::Shutdown`) detached from the destroyed window: access violation / `STATUS_HEAP_CORRUPTION` whenever the app quit by itself. The SDL window is destroyed without `OnClosing` to its listeners (`src/ui/window_sdl.cpp:131-134`). The runtime is now reset while the window exists, after the drawers made from the graphics device are released and the presenter is detached, and the window last | `tests/sdkcheck/app_shutdown_test.cpp`, which runs `tests/shutdown_probe` (a minimal `ReXApp`) without a GPU plugin and with `xenos` (D3D12) |
| `0005-image-page-entry-bit0.patch` (`src/kernel/xboxkrnl/xboxkrnl_memory.cpp`) | `KeGetImagePageTableEntry` returned the page index (`\| 0x40000000` for 4 KB pages), so bit 0 was the index's parity. The dashboard checks bit 0 for the pages of its Lua chunks and calls `KeBugCheck(0xF7)` when it is clear (its chunk at `0x92FD2204`: `0x40002FD2`). Bit 0 is now set for the 4 KB-page XEX heap, as in Xenia Canary `331ddf79bf96` (the code this export came from); the 64 KB-page heap and non-XEX addresses are unchanged. The value stays an approximation; the meaning of bit 0 is not verified | `tests/sdkcheck/image_page_entry_test.cpp` |

Patches 0001–0005 have no ReXGlue upstream equivalent (the `development` branch, `bd833a2` in the
local ref, has the same code in these functions). 0005 follows a Xenia Canary commit.

`src/ui/rex_app.cpp` is compiled into each executable (`rexglue_configure_target`),
not into `rexruntime`: a title picks up 0004 when it is rebuilt.

## Navigation fixes (0008–0010)

- **0008-mnk-button-keystrokes.patch** queues real configured button transitions
  in the mouse/keyboard driver. Previously `GetDeviceKeystroke` always had an
  empty queue. Chords, aliases and focus-loss releases are covered by 33 checks
  (`mnk-before-test.log` fails with `X_ERROR_EMPTY`; `mnk-after-test.log` passes).
  Analog-stick keystrokes and held-key repeat remain unimplemented.
- **0009-dashboard-datetime-abi.patch** fixes the 17559 FILETIME-in-r3,
  buffer-in-r4, byte-count-in-r5 ABI while retaining the legacy argument layout.
  Raw wrappers preserve all 64 FILETIME bits. The former wrapper wrote to
  guest 0x100. 122 checks cover both layouts, timestamps and buffer canaries;
  67 failed before the patch. Format and timezone policy are unchanged.
- **0010-common-string-unavailable.patch** returns a null pointer for a missing
  common-string resource instead of the incoming integer ID. The guest's
  confirmation dialog rejects null; it does not approve the action. Five of
  ten checks failed before, all ten pass after. Actual common strings are
  still unavailable.

Title-local font locator and unsupported URI checks live under
`titles/dash_17559/src/`, not in these SDK patches. See `docs/dashboard.md` §0.12.

## Language exports (0006)

Patch **0006-xam-language.patch** implements `XamGetLanguage` from the existing
`user_language` configuration (IDs 1–17; invalid values warn and fall back to
English, 1), and `XamGetLanguageLocaleFallbackString`. The latter returns a
runtime-owned, stable guest pointer to big-endian UTF-16, including an empty
string for English. The table follows [Xenia Canary 2372a54105d9,
XamState::LoadLanguageLocaleFallback](https://github.com/xenia-canary/xenia-canary/blob/2372a54105d9d598ab5bc7ba94a72ef1bea4b271/src/xenia/kernel/xam/xam_state.cc).
Invalid fallback IDs and allocation failure return null with a diagnostic.
This does not implement fonts, region-dependent language selection, or change
`XGetLanguage`. The local configuration is an approximation of console settings.
The table is allocated lazily under the module lock and freed with the module,
so another runtime cannot reuse a stale guest address.

Before 0006, all 8 `XamGetLanguage` checks failed (r3 retained its poisoned input).
After implementing that export, dashboard run `20261012T020718Z-dash17559-language-trace`
exposed the next failure: the fallback-string stub returned language ID 1 as a
pointer, read by `sub_921CB1E8`. The fallback test failed at `address > 0x10000`.
With both exports implemented, `tests/sdkcheck/xam_language_test.cpp` passes
8 language checks and 100 fallback checks, including two runtime lifetimes.
Its three SDK files are listed with base/result hashes in `deps.lock.json`.

## DPC dispatch (0007)

**0007-dpc-dispatch.patch** changes `src/kernel/xboxkrnl/xboxkrnl_threading.cpp`.
`KeInsertQueueDpc` previously asserted that dispatch was missing, then inserted
into a list with no consumer in builds without assertions. Dashboard callback
`0x921C89B0` therefore never signaled its submission thread. The GPU completed
the work already submitted (`EVENT_WRITE_SHD`, completion `0x0B`) and had no
commands to produce the awaited `0x0D`. This was not a stuck host GPU.

The patch invokes the registered guest routine through `ExecuteTrap`, with the
four DPC arguments, the calling thread's KPCR, `IRQL_DISPATCH`, and `dpc_active`.
It preserves interrupted PPC register state and restores IRQL/active state.
It follows the inline HLE approach of [Xenia Canary 2372a54105d9,
KeInsertQueueDpc](https://github.com/xenia-canary/xenia-canary/blob/2372a54105d9d598ab5bc7ba94a72ef1bea4b271/src/xenia/kernel/xboxkrnl/xboxkrnl_threading.cc),
but does not leave an already executing DPC queued. Missing thread/routine is
fatal, not a fabricated completion. This is **not a complete per-CPU DPC
scheduler**: deferred timing and target-processor semantics remain limitations.

`tests/sdkcheck/dpc_dispatch_test.cpp` executes a registered callback on a real
`XHostThread`: 16 assertions cover arguments, two insertions/reuse, IRQL/active
state, register preservation and removal after completion. Reverting only 0007
reproduces the original assertion and failed wait in
`20261012T030604Z-dpc-test-unpatched`. With 0007 it passes. The separate title
camera test adds 4 assertions; related `sdkcheck` tests pass 17/17. Dashboard
submissions then advance through `0x0D` and beyond. The subsequent title-local
camera and setjmp/longjmp corrections reach actual rendered content, with the
remaining limitations documented in `docs/dashboard.md` §0.11.

## Apply, check, revert

```powershell
powershell -ExecutionPolicy Bypass -File scripts\apply-sdk-patches.ps1          # apply (idempotent)
powershell -ExecutionPolicy Bypass -File scripts\apply-sdk-patches.ps1 -Check   # report; exit 1 if not applied
powershell -ExecutionPolicy Bypass -File scripts\apply-sdk-patches.ps1 -Revert  # back to the pinned sources
```

`scripts\bootstrap.ps1` applies them on a clean clone (after the submodules and
the libmspack symlink copies). The script refuses an SDK checkout at any other
commit and a patch that does not apply cleanly.

CMake checks the result twice (`cmake/x360n_verify_sdk_patches.cmake`): at
configure time, and at build time before `rexruntime` compiles. Both stop with
the file, its SHA-256 and the expected one when a patch is missing. The check
runs in the main tree and in every title tree under `titles/`.

## Reproducing the before/after results

The verification refuses unpatched sources, so a "before" build needs the
explicit, warning-emitting switch `X360N_SDK_PATCHES=OFF`. One patch can be
reverted alone with `git -C external/rexglue-sdk apply --reverse patches\rexglue-sdk\<patch>`:

```powershell
scripts\devshell.cmd cmake --preset win-amd64 -DX360N_SDK_PATCHES=OFF
git -C external/rexglue-sdk apply --reverse patches\rexglue-sdk\0003-import-records-across-libraries.patch
scripts\devshell.cmd cmake --build --preset win-amd64-debug --target x360n_sdk_runtime_checks
.local\build\win-amd64\tests\Debug\x360n_sdk_runtime_checks.exe "[xex],[codegen]"
git -C external/rexglue-sdk apply patches\rexglue-sdk\0003-import-records-across-libraries.patch
scripts\devshell.cmd cmake --preset win-amd64 -DX360N_SDK_PATCHES=ON
scripts\devshell.cmd cmake --build --preset win-amd64-debug
```

Results on this machine (2026-10-11 and 2026-10-12; logs under
`.local/runs/sdk-patch-evidence/`, not versioned):

| Reverted | Result without | With the patch |
|---|---|---|
| 0001 and 0002 | 1 of 5 cases pass: only the `0x80000000` table (the games' case, unchanged by 0001); the `0x90000000` table, both heaps together, the rejection of invalid requests and the setup-failure case fail | 5/5, 118 assertions |
| 0003 alone | `[xex],[codegen]`: 1 of 3 cases pass (16 assertions fail): the split import is two entries, one with thunk 0, and a symbol at 0; the generated `PPCFuncMappings` starts with guest 0, so 0 of its 5 entries are reachable; the slot-only import is mapped at 0 and nothing stops generation. The "two complete records" case passes on both sides | 3/3 |
| 0004 alone | `[app]`: after a successful setup the probe exits with `0xC0000005`, without and with `xenos`; stack `~MnkInputDriver` → `DetachFromWindow` → `CallInUIThreadSynchronous` → `IsInUIThread` (run `20261012T001518Z-probe-0004-before-crashstack`) | 2/2: exit 0 in both, exit 1 after a failed setup |
| 0002 alone (0004 applied) | the probe's failed-setup case exits with `0xC0000005` | exit 1 (`EXIT_FAILURE`), no unhandled exception |
| 0005 alone | `[kernel]`: 2 of 7 assertions fail (`0x92FD2204` -> `0x40002FD2`, `0x90000000` -> `0x40000000`) | 7/7 |

Executables copy `rexruntime*.dll` (and the GPU plugin) next to themselves before
they are linked, on every build of them, also with `--target`
(`x360n_stage_runtime_dlls`), so a test or title does not keep running a DLL built
before a patch.
