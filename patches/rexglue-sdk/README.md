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
| `0002-setup-failure-teardown.patch` (`src/system/runtime.cpp`) | a failed `Runtime::Setup` called `Shutdown()` itself, destroying the graphics system while `ReXApp` still held objects made from it; `ReXApp::OnDestroy` then crashed (access violation in `D3D12Core`). Setup now returns the error and leaves teardown to `~Runtime()` | `tests/sdkcheck/setup_failure_test.cpp` |
| `0003-import-records-across-libraries.patch` (`src/system/xex_module.cpp`, `src/codegen/phase_register.cpp`, `include/rex/codegen/analysis_errors.h`, `src/codegen/analyze.cpp`) | `XexModule::SetupLibraryImports` paired a function import's value slot (record type 0) and thunk (type 1) only inside one import library record. The dashboard has `xam` split over two records, with the slot of `xam 0x8E2` in one and its thunk in the other: the slot half became an import symbol at address 0, the codegen wrote `{ 0x0, __imp__XamNuiGetSupportString }` as the first `PPCFuncMappings` entry, and the runtime, which stops at guest 0, registered no function. The loader now completes the half-filled entry of the same library (slot-only + thunk-only, either order; complete entries stay separate). The codegen no longer maps an import at address 0: a known function import without any thunk is an `ImportWithoutThunk` analysis error (generation blocked unless `--force`) | `tests/sdkcheck/import_records_test.cpp` (synthetic XEX2) |

None has a ReXGlue upstream equivalent (the `development` branch, `bd833a2` in the
local ref, has the same code in these functions).

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

Executables copy `rexruntime*.dll` (and the GPU plugin) next to themselves before
they are linked, on every build of them, also with `--target`
(`x360n_stage_runtime_dlls`), so a test or title does not keep running a DLL built
before a patch.
