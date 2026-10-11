# Local patches to ReXGlue SDK

Base revision: **`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`** (tag `v0.10.0`), the
commit pinned in `deps.lock.json` and checked out as the `external/rexglue-sdk`
submodule. The patches are for that commit only; no upgrade or downgrade.

The submodule's history is not changed: the patches are applied to its working
tree from these files. `deps.lock.json` (`rexglue_sdk.patches`) records each patch
file's SHA-256, the SHA-256 of every file it touches before (`base_files`) and
after (`files`), and its test.

| Patch | Fixes | Test (`ctest -R "sdkcheck[.]"`) |
|---|---|---|
| `0001-function-table-xex-heap.patch` (`src/system/xmemory.cpp`) | `Memory::InitializeFunctionTable` / `DestroyFunctionTable` used the `0x80000000` XEX heap only, so an image in the `0x90000000` XEX heap made `Runtime::Setup` fail (`Failed to allocate function table at 93028000`, dashboard 2.0.17559.0). The table keeps its address, `image_base + image_size`, which generated code hard-codes; it is now allocated in the XEX heap that holds its whole range, with that heap's page size, and requests that overflow, leave the heap, are misaligned, are empty or overlap another table are rejected | `tests/sdkcheck/function_table_test.cpp` |
| `0002-setup-failure-teardown.patch` (`src/system/runtime.cpp`) | a failed `Runtime::Setup` called `Shutdown()` itself, destroying the graphics system while `ReXApp` still held objects made from it; `ReXApp::OnDestroy` then crashed (access violation in `D3D12Core`). Setup now returns the error and leaves teardown to `~Runtime()` | `tests/sdkcheck/setup_failure_test.cpp` |

Neither has an upstream equivalent (the `development` branch had the same code
when they were written).

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

The verification refuses unpatched sources, so the "before" build needs the
explicit, warning-emitting switch `X360N_SDK_PATCHES=OFF`:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\apply-sdk-patches.ps1 -Revert
scripts\devshell.cmd cmake --preset win-amd64 -DX360N_SDK_PATCHES=OFF
scripts\devshell.cmd cmake --build --preset win-amd64-debug --target x360n_sdk_runtime_checks_stage_dlls
.local\build\win-amd64\tests\Debug\x360n_sdk_runtime_checks.exe     # 2026-10-11: 1/5 cases pass
powershell -ExecutionPolicy Bypass -File scripts\apply-sdk-patches.ps1
scripts\devshell.cmd cmake --preset win-amd64 -DX360N_SDK_PATCHES=ON
scripts\devshell.cmd cmake --build --preset win-amd64-debug
.local\build\win-amd64\tests\Debug\x360n_sdk_runtime_checks.exe     # 5/5 cases, 118 assertions
```

Unpatched, only the `0x80000000` case passes (the games' behavior, unchanged by
0001); the `0x90000000` table, both heaps together, the rejection of invalid
requests and the setup-failure case fail. The logs of that comparison on this
machine are under `.local/runs/sdk-patch-evidence/` (not versioned). Executables copy `rexruntime*.dll` next to themselves on every
build (`x360n_stage_runtime_dlls`), so a test or title does not keep running a
DLL built before the patch.
