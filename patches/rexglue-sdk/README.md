# Local patches to ReXGlue SDK

## 0070 — optional runtime benchmarks

Connects the existing `perf_log_csv` setting to Runtime::Setup, makes counters
opt-in at runtime, and permits instrumentation in Release builds. Records
monotonic completed guest swap timestamps, Windows aggregate process CPU time,
D3D12 shader translation / pipeline creation wall time and failed swaps. Reuses
existing cache/draw/dispatch counters. GPU execution time and VRAM are not
fabricated. No upstream equivalent identified; SDK revision unchanged.

Focused test: `sdkcheck.Performance counters are optional and preserve concurrent totals`.
The reduction and scenario runner are covered/documented in `docs/performance.md`.
The CSV includes failed rendering attempts: cadence is not proof of fidelity.

Base revision: **`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`** (tag `v0.10.0`), the
commit pinned in `deps.lock.json` and checked out as the `external/rexglue-sdk`
submodule. The patches are for that commit only; no upgrade or downgrade.

The submodule's history is not changed: the patches are applied to its working
tree from these files. `deps.lock.json` (`rexglue_sdk.patches`) records each patch
file's SHA-256, the SHA-256 of every file it touches before (`base_files`) and
after the complete patch stack (`files`), and its test. When patches share a
file, `base_files` records the input to that patch and `files` records the final
stack hash in every affected entry. Apply in order; revert in reverse order.
When extending the stack, the official applicator recognizes an intermediate
checkout only when every patched path matches a complete prefix reconstructed
from `base_files`. Missing newly introduced files are checked too. The final
stack hashes remain mandatory after application.

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
powershell -ExecutionPolicy Bypass -File scripts\setup\apply-sdk-patches.ps1          # apply (idempotent)
powershell -ExecutionPolicy Bypass -File scripts\setup\apply-sdk-patches.ps1 -Check   # report; exit 1 if not applied
powershell -ExecutionPolicy Bypass -File scripts\setup\apply-sdk-patches.ps1 -Revert  # back to the pinned sources
```

`scripts\bootstrap.ps1` applies them on a clean clone (after the submodules and
the libmspack symlink copies). The script refuses an SDK checkout at any other
commit and a patch that does not apply cleanly.

CMake checks the result twice (`cmake/sdk/x360n_verify_sdk_patches.cmake`): at
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
`.local/evidence/runs/sdk-patch-evidence/`, not versioned; migrated in October 2026):

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

## Content-library increment (0011–0015)

- 0011: complete unsupported user-preference requests with the real handler
  error. The original 17559 XAM wrapper sends XGI message B0082; a stub left
  the request appearing pending. Before: 7/19 assertions fail; after: 19/19.
- 0012: include XN_SYS_STORAGEDEVICESCHANGED (0x0B) in the existing initial
  system-notification batch. The dashboard's own handler signals its content
  worker; no completion state or guest counter is forced. The test checks
  notification delivery and its single consumption.
- 0013: explicit all-types aggregate queries (FFFFFFFF) scan installed title/type
  directories, respecting XUID and optional title filter. Original XAM 17559
  81681EA0/81681F08 establishes those filters. Also compare header-read X_RESULT
  with ERROR_SUCCESS: XSUCCEEDED incorrectly accepted FILE_NOT_FOUND. Before:
  2/17 assertions fail; after: 23/23. Optical all-types enumeration is pending;
  existing typed queries retain their prior behavior.
- 0014: implement XamContentGetAttributesInternal for installed HDD content.
  Original XAM 0x277 -> 816EE9D0 -> message 2001A -> 81684688 writes a packed
  36-byte output (attributes, three FILETIMEs, size). The extracted-content
  backend reports host timestamps and recursive payload size, not STFS allocation
  size or license information. Missing paths fail; async requests carry the
  actual result. Other devices return ERROR_NOT_SUPPORTED. Before: 6/36
  assertions fail; after: 36/36. Test: aggregate_content_test.cpp.

Evidence logs: .local/evidence/runs/sdk-patch-evidence/0011-*, 0013-*, 0014-* and 0015-*.
No upstream equivalent claimed. XAM is a local reference, not executed by HLE.
The aggregate ABI still returns the SDK's 328-byte structure where 17559 reads
512 bytes; full extended metadata remains pending and must not be fabricated.

- 0015: internal content creation preserves XUID=0 from the aggregate record.
  Previously it resolved common XBLA content under the signed-in profile and
  returned ERROR_PATH_NOT_FOUND. Original XAM path builder 8167F690 reads the
  owner directly. Public XCONTENT_DATA calls retain their prior behavior.
  Before: 2/40 assertions fail (real OPEN_EXISTING result 3); after: 40/40,
  including verification of the mounted package path. No license is granted.

## Content launch (0016)

Implements only the demonstrated HDD/root-image/flags-zero path of
XamContentLaunchImageInternalEx (original XAM 17559 816F00E8 -> 816EFED0).
Opens XSYSLAUNCH, checks the requested image and preserves loader data before
TerminateTitle. Missing files and unsupported modes fail; no license is granted.
Before: 4/56 assertions fail; after: 56/56. Evidence: 0016-launch-{before,after}.log.
The host session selects an existing AOT profile by path and exact XEX hash.

## Persistent window and native child focus (0017–0018)

0017 embeds the real SDL/D3D12 title surface in an optional Windows parent HWND.
Default zero keeps standalone behavior; embedded fullscreen belongs to the host.
Attachment is announced after showing the child, so host focus does not target a
hidden window. The existing shutdown probe checks two consecutive child surfaces
and survival of the same parent (26 assertions).

0018 fixes SDL WIN_UpdateFocus for WS_CHILD: it requires GetFocus()==child and
GetForegroundWindow()==GetAncestor(child,GA_ROOT). The old top-level comparison
never recognized embedded focus. SDL also drops joystick events without keyboard
focus (SDL_PrivateJoystickShouldIgnoreEvent); this explained both input failures.
The user confirmed keyboard/controller and the original My Games cycle after the
fix. The integration probe tests real SDK focus listeners (gain/loss/regain), but
skips when Windows denies foreground activation; that test was skipped here, not
counted as a validated focus test. See child-focus-sdk-final.log and dashboard §0.18.
No background-input override, injected input, or replacement Xbox UI.

## Original loader requests and safe surface teardown (0019–0020)

0019 records XamLoaderLaunchTitle(NULL, flags) as a dashboard request instead of
asserting. XamLoaderTerminateTitle records a separate title-termination request.
Both terminate through KernelState as before; neither returns fictional success.
Flags and launch bytes survive the host handoff; unsupported dashboard flags are
rejected according to the selected dashboard profile. The 17559 profile supports
0 and 0x100: original XAM 816A0798/816A0800..816A0840 packs a 1020-byte context
with reason 23 and user FF for XLNI_DASH_ARCADE. This translation is in the title
profile integration, not the shared runtime. The original handoff remains intact.
Tests exercise the real exports and data round trip, plus the per-version packing.

0020 prevents WIN_UpdateFocus from reacquiring focus while window->is_destroying.
The captured stack was CleanupWindowData -> DestroyWindow -> WIN_UpdateFocus ->
WIN_UpdateWindowICCProfile -> free of the already-freed ICC filename. The parent
probe and focus probe now reuse the supervisor's existing stack capture. Before:
child-focus-debug.log (one failed assertion, teardown crash); after: 37 assertions
passed in window-destroy-focus-after.log. Final sdkcheck: 31/31 passed, no skips.

## System presentation and audio shutdown (0024–0025)

0024 retains the completed guest image described by VdPersistDisplay instead of
allocating a dummy buffer. The selected profile may present that image with a
16-bit RGB LUT on the existing graphics worker, paced by guest vblank. No guest
completion counters are synthesized. The 17559 profile implements the verified
XAM loader LUT loop in HLE and calls its already recompiled matching D3D routines;
it does not execute the original XAM module. Other versions do not inherit it.
D3D12 exercised; Vulkan wiring is unvalidated. Ownership is in-process only.

0025 drains already submitted audio through the existing SDL conversion/mix and
flushes the stream before closing the device. No amplitude ramp is introduced.
A two-second failure bound logs a failed drain, never completion. The focused
test failed before (0/64 frames consumed), passed after (64/64); silent samples
use the configured endpoint because this SDL build disables the dummy backend.
Missing endpoint is an explicit skip. See docs/dashboard.md §0.21 for live limits.

## Profile UI request dispatch (0026)

Routes the original signin/card request layouts to the existing FE application.
Removes fabricated signin/UI-closed notifications and maps failed dispatch to
the original wrapper's error result. Tests: 10 failures before, 11 assertions
pass after; 36 SDK checks pass. This does NOT implement the original system-UI
host, account changes or persistence. Live FE/21008 still fails; dashboard §0.22.

## Original system UI prerequisites (0027–0034)

0027 registers system applications with module ownership and routes the FE UI
request to a title-provided host. 0028 implements the extended allocation ABI;
0029 preserves the module registration identity across VFS aliases on unload.
0030 adds registers/caller information to invalid indirect-call diagnostics.
0031 allows an explicitly opted-in module to keep its AOT dispatch table on the
host: the 17559 XAM table otherwise aliases and corrupts the dashboard image.
0032 implements owned, zeroed virtual XMem storage; physical classes remain
unsupported there and the 17559 binding preserves their original decoder.

0033 loads structurally bounded, unencrypted resource-only DLL XEX images
without requiring a PE header. Executable/code pages, imports, entry points,
TLS and malformed resource ranges do not qualify. Actual 17559 huduiskin.xex
decompresses to XUIZ data; the original skin loader returned success. Synthetic
valid and malformed images test the boundary; hashes do not prove authenticity.
0034 accepts the observed XamAllocEx system virtual pool flag 0x01000000, paired
with XamFree. Its 4096-byte allocation failed before the patch and succeeds
after; pool quotas are not modeled and other classes still fail explicitly.

42 runtime checks / 1183 assertions passed with 0034 (local log
profile-ui-sdk-0034-full.log); related group: 8 / 124. These prerequisites do
not establish visible profile UI, signin/signout or persistence.

## Avatar LDI caller-owned workspace (0035)

Implements Create/Decompress/Reset/Destroy for the observed XAM 17559 profile:
32 KiB independent LZX blocks, explicit reset between calls, 0x9800 caller-owned
workspace and null allocator callbacks. Other configurations/continuation modes
fail explicitly. No persistent host allocation or guest-owned memory free.
Failure status is nonzero; exact original error-code parity is unverified.

New `sdkcheck.LDI` test constructs a synthetic literal LZX block and calls the
actual imports. Before: fails at creation. After: 46 checks pass, including
failure atomicity, bounds, reset lifecycle and two independent contexts.
Original XAM compressed stream and GetAssets worker 0x81966450 passed on three
real assets. Details and limitations: docs/avatar-system.md section 12. Prior
completed suites were not rerun. No full GetAssets export or rendering claim.

## Bounded GDFX parser (0037)

`0037-gdfx-bounded-directory-parser.patch` fixes audit C1 in
`DiscImageDevice`, before any mapped directory/file bytes are exposed to
consumers. It validates complete header, root, record/name, child-table and file
spans with checked sector arithmetic; iteratively walks siblings in the original
in-order sequence; rejects repeated/overlapping records (including directory
aliases/cycles); and only publishes the tree after complete validation.

Directory tables retain the root's 32 MiB limit. Depth is bounded to 256 and the
image to 1,048,576 entries, also bounding Entry destruction/path construction.
Empty child directories and empty files remain supported when their zero-length
range is within the image. Malformed file ranges now fail initialization instead
of allowing partial extraction. The pinned SDK revision below is unchanged.

Tests: `tests/content/extract_disc_test.cpp`, through the real content tool.
23 malformed variants include truncated magic/descriptor/records, extreme
sectors/lengths, sibling and directory cycles. Valid controls cover nesting,
empty entries, a displaced game partition, traversal order and 300 siblings.
The original audit fixture now returns error 1 without a crash; the valid
fixture still extracts identical bytes and hashes. Patch apply/reverse hashes
were checked in an isolated Git fixture; all 37 patches pass `-Check`.
See `docs/reports/maintenance/PROJECT_AUDIT.md` E6 for C1/C3 before/after evidence and limits.

## Sign-in foreground input (0040)

`0040-signin-any-user-input.patch` extends the existing system UI session to
accept the original Sign-in ANY_USER (`FF`) value. The title cannot drain any
controller while that session is visible. Original system-app device-state
queries retain their privileged high-bit path. `XamInputGetKeystrokeHudEx`
uses the verified 17559 ABI at `816F4C78`: user value, flags, output in r5.
An ANY_USER request performs at most four controller queries per invocation;
there is no waiting loop, new thread, graphics copy, or additional presentation.
User/account state and SIGNINCHANGED are untouched.

Focused tests: `x360n_sdk_runtime_checks "[signin]"`, 3 cases / 76 assertions.
They exercise original FE/21001 request bytes and failure/OOM propagation,
application selection, ANY_USER ownership, neutral title state versus privileged
HUD state, a real synthetic MnK B event delivered once through HudEx, invalid
arguments, and deferred close/reopen. No game or visual session was run.
The patch was reapplied to copies of its two pre-patch files with SDK-style LF
line endings; both output hashes match the lock. All 40 patches pass `-Check`.
The SDK pin is unchanged; this does not implement profile enumeration or login.

## 0041 — local profile session (C7.2, partial)

Adds a single persistent local identity to UserProfile and initializes it in the
existing KernelState. Local XamUserLogon/Ex (flags=0) update slot zero only after
the disk transaction succeeds. Identity queries use that state, and only real
session changes emit SIGNINCHANGED. No automatic/fixed profile is created.

The original Sign-in imports already resolve to these shared exports. Its
enumeration/account creation task contracts are still missing: this patch does
not make the original UI's complete sign-in flow functional. See dashboard §0.26.
Focused coverage: `x360n_sdk_runtime_checks "[local-profile]"` (persistence,
restart, XAM identity, sign-out, unsupported modes, corrupt data and write errors).
All three focused tests pass via CTest (0.95 s, 30 s timeout per case). Patch
reverse/apply verified in copied sources with SDK LF attributes and matching
base/final hashes. There is no upstream equivalent identified; the pin is unchanged.

## 0042 — original Sign-in profile enumeration (C7.3, partial)

Exposes the existing local record through XamProfileCreateEnumerator/Enumerate,
FindAccount and LoadAccountInfo. Uses one 392-byte record with a 380-byte account,
UTF-16 name and no Live XUID. Each kernel enumerator owns its cursor and snapshot;
opening, enumerating and closing do not change the session. Only filter 0 and
enumeration flags 0/2 are supported. The store remains device-independent.

The normal 17559 Sign-in LogonEx mode 0x120420 is mapped to the existing local
session transaction (slot zero only). Preview/online/other modes fail explicitly.
Index/mask/device queries use that session. Raw adapters preserve full XUIDs where
the pinned typed argument marshaler otherwise truncates them. XAM memory imports
reuse existing CRT implementations; bounded UTF-16 copy is implemented for the
original request/identity buffers. No new kernel, UI or persistence format.

Five focused `sdkcheck.Local profile` tests pass, including record bounds, cursor
isolation/close, unsupported modes, real confirmation transaction, notification
deduplication, identity queries/restart, and request/string copies. Reverse/apply
on isolated copies matches all three base/final hashes; all 42 patches pass Check.
One bounded graphical run demonstrated original enumeration and the stored name,
not a button-driven login. The copy-import fixes were tested after that run, with
no second visual execution. See dashboard §0.27 for the acceptance gap.

## 0043 — byte count for the original passcode check (C7.5, partial)

RtlCompareMemoryUlong returned a count of matching ULONGs instead of the byte
length of the matching prefix. The 17559 profile constructor compares its four
zero passcode bytes and requires a return value of 4; the old result of 1 marked
an unprotected profile as protected. Return bytes, stop at the first differing
word and retain guest-endian access and alignment rejection. No profile data,
XAM session, UI logic, persistence format or SDK pin changes.

Two focused tests tagged `[passcode-contract]` cover the export's zero/nonzero
patterns, first mismatch, byte units, alignment, and the real enumerated local
account with its session untouched. The comparison test failed five assertions
before the patch; both tests pass afterward. Reverse/apply in an isolated source
copy matches the lock's base/final hashes; all 43 patches pass Check. No upstream
equivalent identified. The only graphical run removed the false lock/passcode
but did not complete login or cancellation; see dashboard §0.29.

## 0044–0053 — existing local profile sign-in (C7.6)

These patches complete the original 17559 sign-in path for the existing single
local profile. No upstream equivalent was identified; the SDK pin is unchanged.

| Patch | Contract |
|---|---|
| 0044 | Retail console mode, avoiding the erroneous data-center GetXUID branch. |
| 0045 | Original merge confirmation (0x120424) and closing snapshot (0x63), through the durable local session. |
| 0046 | Real guest task callbacks, cancellation/join, callback results, local account queries and FILETIME. |
| 0047 | Native S_OK pending / S_FALSE completed task status; handle release. |
| 0048 | Atomic persistent retail XConfig flags for original dialog decisions. |
| 0049 | Local logon queue barrier, completing overlapped only after synchronous commits. |
| 0050 | Retail NUI automation query, restoring ordinary animation updates. |
| 0051 | Owned task/worker handle lifetime and publication before callback startup. |
| 0052 | Initialized optional outputs and no pending online-auth demand in the local runtime. |
| 0053 | Actual system notification broadcast with ID/parameter, mask/version filtering and waitable delivery. |

The last fix delivers the original dashboard's 0x80040016 notification; the
dashboard then closes its profile selection and navigates Home itself. No UI
replacement, forced navigation, fabricated login or discarded persistence error.
Account/worker results remain real, unsupported multi-user/online logon fails.
The task implementation covers this original flow; additional queue APIs and
the full XAM scheduler are not claimed as implemented.

Fourteen focused cases pass across profile/session, retail queries, settings,
HUD ownership, task cleanup and system broadcasts. Real bounded runs demonstrate
original profile enumeration, A sign-in, Home, GamerCard identity, B cancellation
and persistence after restarting. See dashboard §0.30 for exact evidence/scope.
All 53 patches apply from pinned sources and match final hashes; new patch base
hashes also match. `apply-sdk-patches.ps1 -Check` passes. Proprietary inputs,
generated original code, profile fixtures, logs and screenshots remain local.

## 0067 — profile and title save storage (C8)

No upstream equivalent identified; the pinned SDK is unchanged. Public save
operations resolve the signed-in C7 XUID and executable Title ID. Enumeration
excludes shared saves; session epochs revoke old mounts, file I/O and enumerators.
VFS devices have shared lifetime while host files reference their entries, so
revocation/unmount does not leave dangling pointers or retain whole retired mounts.
Flush reaches the host and reports failures. Create dispositions, headers, occupied
aliases and storage failures no longer produce fabricated success.

Tests: `sdkcheck.Save storage uses XAM and NT file contracts across profiles and restarts`,
`sdkcheck.Save storage reports real failures and completes overlapped operations`,
and `sdkcheck.save-storage-processes`. Real PPC export calls, NT file handles,
host payloads and independent processes; no game-specific save adapters. Existing
profile and installed-content tests remain green. See `docs/save-storage.md` for
evidence and the explicit limit: commercial-game save/load is not yet demonstrated.

## 0068 — preserve pdata ends during gap filling

The pinned GapFill split only on returns and known tail targets. A pdata function
ending with an out-of-line backward branch could therefore hide the next leaf's
entry, and register only its trailing block as a separate function. Honor the
metadata end boundary while splitting existing code regions; do not infer new
code from data or manufacture function lengths. No upstream equivalent identified.

`sdkcheck.GapFill preserves a leaf after a pdata out-of-line return` uses an
original synthetic PPC fixture: failed with one unresolved call before the patch,
passed 10 assertions after it. Castle Crashers codegen with the patched producer
left all 41 generated files unchanged. Forza's three affected entries are
`82F5CC68`, `82F76188`, `82F762A0`; their internal epilogues are not functions.

## 0069 — executable metadata for the original content library

The Retail 17559 library queries `XamContentGetMetaDataInternal` before showing
installed disc content. Project the registered content name, size and verified
XEX execution identity into its packed metadata result, retaining the real
content type. Reject malformed or mismatched identities. This does not invent
STFS licenses, artwork or publisher information. The same contract covers
executable disc and XBLA content; routing still matches the installed XEX hash.
See `docs/reports/titles/validation-forza-horizon.md` for focused tests and actual UI evidence.

## 0071 — original HUD reboot and audio callback lifetime

Implement `XamLoaderRebootToDash` from the original 17559 HUD contract: validate
and copy its 1020-byte context before title termination, preserve reason/user,
and publish an explicit Dashboard request. The original HUD owns confirmation.
Normal Xbox Home uses reason/flags zero; the special reason 72 is transported
with flag 0x100000 but is not certified by the dashboard session adapter.

After original termination notifications finish, join active audio callbacks
before retiring guest threads or waking their waits. No host mutex spans guest
execution; a callback can quiesce itself without joining itself. XAudio's worker
uses guest processor 4, matching the original mixers' buffer ownership.
The initial title thread explicitly starts on processor 0, independent of the
number or affinity of previously created host workers. Otherwise fixing audio's
processor shifts the default round robin and breaks Castle's initial heap index.
`XHostThread` initializes FPSCR host policy just as `XThread` does, preventing
guest flush-mode changes from accidentally unmasking native FP exceptions.

No upstream equivalent identified; the SDK pin is unchanged. Tests cover packed
context validation/preservation, original notifications, concurrent and self
quiescence, processor identity, FP policy and SDL drainage. The seven patched
files replay from their recorded base hashes to the recorded final hashes.
The 17559 host separately initializes the original audio CRT lock. Evidence and
the distinction between direct diagnostics and full UI acceptance are recorded
in `docs/reports/titles/validation-forza-horizon.md`.

## 0072 — Isolate performance counter cache lines

Independent runtime counters now occupy separate 64-byte aligned slots on the
measured x64 target. This removes false sharing between independent writers;
the atomics, memory orders, exact sums, snapshot exchanges and gauge semantics
are unchanged. Same-counter contention remains. The debug overlay explicitly
labels counters disabled instead of displaying fabricated zero measurements.

No upstream equivalent identified; the SDK pin is unchanged. Focused
`sdkcheck.Performance` tests cover disabled counters, concurrent sums, gauges,
CSV and snapshots while writers are active. The before/after experiment uses
identical probe executables and frozen title binaries, changing only the runtime
DLL. Results and limits are in `docs/reports/performance/performance-results-2026-10-14.md`.
This reduces instrumentation overhead; it does not establish an FPS improvement
with instrumentation disabled.

## 0073 — Index immutable GPU register metadata

**Withdrawn at P3 close (2026-10-14).** This file is retained as experimental
evidence in `withdrawn/0073-index-gpu-register-metadata.patch`; it is not listed
in `deps.lock.json` and must not be applied. The
original switch is restored byte-for-byte. The accepted pair reduced CPU by
3.97% but worsened 1% low by 27.78%; existing repeats cannot establish that
this is only noise. No simple correction with consistent benefit was identified.
The probe, instrumentation and all benchmark results remain available.

`RegisterFile::GetRegisterInfo` uses a constant indexed table generated from the
same register definitions instead of a large switch. Known names/types, stable
metadata pointers and null results for unknown/out-of-range registers are
preserved. Register writes, diagnostics, barriers and rendering are unchanged.
The table is shared immutable host metadata, not original dashboard resources.

No upstream equivalent identified; SDK pin unchanged. The explicit
`x360n_register_info_probe` checks all 65,536 low indices plus four large boundary
values against the definitions and reports a bounded synthetic lookup benchmark.
Real-game evidence is required separately; a faster synthetic lookup alone does
not establish better game performance.

## 0074 — Optional backend packet validation

Adds a compile-time backend hook immediately after reading a PM4 header. Only
Native defines `REX_GPU_VALIDATE_PACKET`; Xenos retains its existing path with
no added branch, call or instrumentation. Native checks packet extent and its
implemented opcode subset, failing explicitly before shared parsing can ignore
unsupported guest work. No ABI, SDK pin, Xenos translator or backend changes.

No upstream equivalent identified. `sdkcheck.Native-packet-guard` injects an
unsupported opcode through the actual MMIO/ring path: without the hook it times
out without explicit failure; with it the child exits 78 and logs the exact
unsupported opcode. Before/after logs and graphical evidence are described in
`docs/reports/graphics/native-renderer.md`. The patch is applied only through the pinned patch
script, with before/after file hashes in `deps.lock.json` (73 active patches;
withdrawn 0073 is not reintroduced).

## 0076 — Empty resolves are successful no-ops

Checks empty rasterized/scissored resolve coverage before 8-pixel alignment,
preventing an empty unaligned rectangle from becoming a nonempty copy/clear.
Returns the existing zero-extent no-op contract after pitch clipping as well.
Invalid command, vertex-buffer format and MSAA checks remain. Uses the existing
Xenos render-target cache; no additional cache, synchronization removal or title
special case. No upstream equivalent identified; SDK pin unchanged.

`sdkcheck.empty-resolve` exercises the actual helper with synthetic guest memory
and registers. It fails without the patch (84/204 checks) and passes with it.
Real Forza evidence and remaining fetch warnings: `docs/reports/graphics/xenos-empty-resolves.md`.

### 0077 — inactive vertex fetch proof (2026-10-15)

Xenos D3D12: não rejeitar o draw por descritor inválido se a análise conservadora
provar que todas as referências ao slot estão predicadas fora no draw atual.
Sem índice/hash/título especial, sem permitir acesso inválido e sem marcar o
slot como residente. Constantes atuais são reconsideradas a cada draw; casos
não comprovados conservam a rejeição. Native/Vulkan não alterados.

`sdkcheck.inactive-vertex-fetch`: 2.328 checks sintéticos; Forza e Castle reais,
com os limites em [X2](../../docs/reports/graphics/xenos-fetch-predication.md). Patch 0076 intacto.

### 0078 — system command buffer composition (2026-10-16)

Implements the kernel contract used by the original XAM 17559 compositor
(`816CCF00` → `81842B08` → `VdSetSystemCommandBuffer`) instead of stubs:
`VdSetSystemCommandBuffer` keeps a copy of the 0x94-byte descriptor,
`VdQuerySystemCommandBuffer` allows recording while at most one published buffer
waits for the GPU (XAM rotates three; bounded 100 ms wait, no dependency on the
caller's own swap), and `VdGet3dVideoFormat` returns 0 (no stereoscopic output)
instead of leaving r3 undefined. `SetSystemSwapCallback` lets a system UI host
run on the guest thread inside `VdSwap`, for every title, without title hooks.

When a descriptor is published, `VdSwap` emits the ReXGlue-only
`PM4_XE_SYSTEM_COMMANDS` packet. The command processor snapshots the title's
register file, active shaders and bin masks, places the title front buffer in
fetch constant 15 (XAM's inherit tags `0x10000` in tag word 3, decoded from
`81842C08`/`81845860`; inference confirmed by the composed image), runs the
recorded buffer, restores the title state (constants through `WriteRegister`,
other registers directly to avoid replaying scratch/gamma side effects) and
writes the completion sequence to a kernel fence. The swap then presents the
descriptor's system front buffer (base/format/dimensions from the descriptor,
other fetch fields from the title fetch). Without a descriptor `VdSwap` is
unchanged. EDRAM is shared, as on the console. Native/Vulkan backends receive
the same base-class packet; only Xenos D3D12 was exercised.

`tests/sdkcheck/system_command_buffer_test.cpp` checks the callback, the packet
layout, the inherited fetch, the presented fetch, the rotating-buffer limit and
the return to the title frame. It fails without the patch (stub exports and no
callback API). Live evidence: `docs/reports/graphics/guide-composition-2026-10-16.md`.

### 0079 — system UI input ownership (2026-10-16)

`src/kernel/xam/xam_input.cpp`. While a system UI owned a controller, the title
already received an empty state. Three gaps remained, all reproduced by
`tests/sdkcheck/system_ui_input_test.cpp` before this patch (3 failed checks):

- the Guide button bit reached `XamInputGetState` for titles, because the 17559
  host enables `guide_button` for its own polling (the runtime default is off);
  non-privileged queries now never see it;
- a button still held when the UI closed (the B that closes the Guide) became a
  new title press; buttons and triggers held through the UI are now hidden from
  the title until released;
- the key-up of a pad key the HUD consumed reached `XamInputGetKeystroke(Ex)`;
  keys delivered to the HUD keep their remaining transitions with the UI.

No title special case. Live check (Castle Crashers, temporary instrumentation,
removed): B held ~0.85 s after closing the Guide never reached the title; the
next Start did.

### 0080 — local profile gamercard statistics (2026-10-16)

`include/rex/system/xam/user_profile.h`, `src/system/xam/user_profile.cpp`,
`src/kernel/xam/xam_user.cpp`, `src/kernel/xam/xam_misc.cpp`. The original
17559 XuiGamerCard (Guide → Profile) left its statistics blank:

- `XamUserReadProfileSettingsEx` read the size pointer from r9. XAM 17559
  (`816B16A8`) takes flags in r9 and the size pointer in r10; its own
  `XamUserReadProfileSettings` wrapper (`816B1F28`) moves the pointer from r9
  to r10 and passes zero flags. The request failed with `E_INVALIDARG`.
- `XamReadTile`/`XamReadTileEx` were stubs. Local accounts have no gamer
  picture, so they now report `ERROR_NOT_FOUND`, completing the overlapped
  when one is given, instead of a synchronous success with no data. The
  gamercard then continues with its other requests.
- `XamUserIsGuest` and `XamUserGetCachedUserFlags` were stubs; local accounts
  are not guests and have no cached Live flags (0).
- `XPROFILE_GAMERCARD_TITLES_PLAYED` (`0x10040012`) now comes from the
  signed-in account's own record, `<profile store>/<XUID>/titles-played.dat`
  (`X360TP01`, big-endian count and sorted unique title IDs). A title is added
  when a game starts with that account signed in; system titles (`FFFExxxx`)
  and signed-out sessions are not recorded. Gamerscore and achievement counts
  stay 0: there are no per-account unlock records yet.

`tests/sdkcheck/local_profile_gamercard_test.cpp` covers both read ABIs, the
per-XUID count across a session switch and a runtime restart, the system-title
rejection and the tile completion. Live evidence:
`docs/reports/graphics/guide-composition-2026-10-16.md`.

### 0081 — console language ABIs and country (2026-10-16)

`xam_info.cpp`, `xam_locale.cpp`, `xam_misc.cpp`, `xboxkrnl_xconfig.cpp`.
Read the chosen console language in XGetLanguage (base), XTLGetLanguageV2
(through Polish/Russian) and XamGetLanguage (extended). Preserve ABI fallback
limits; reject reserved language 10. XamGetCountry now returns the configured
country instead of residual r3. XConfig reads agree with these getters;
out-of-range countries no longer wrap in locale lookup. Unsupported guest
writes return explicit errors rather than pretending to persist settings.

Regression: `tests/sdkcheck/xam_language_test.cpp`; desktop/session evidence
and contract references: `docs/guides/console-language-locale.md`. The patch changes
no original resource, license region, AV region or dashboard module mapping.

### 0082 — profile settings sizing before sign-in (2026-10-16)

`src/kernel/xam/xam_user.cpp`. Motocross Madness voice initialization asks
`XamUserReadProfileSettingsEx` for the buffer size with user index 255 and a
null buffer before any account is signed in. The sign-in check ran first and
returned `ERROR_NOT_LOGGED_ON` (1245) without the size; the caller propagated
`E_FAIL` and reached a null access. The size computation depends only on the
setting IDs, so the query now reports it with `ERROR_INSUFFICIENT_BUFFER` (122).
Reading account data still requires a signed-in user and leaves the buffer
untouched otherwise. Regression:
`sdkcheck.Profile settings sizing works before sign-in without reading account data`
(both read ABIs, user 0/255, null output pointer, signed-in read).

### 0083 — physical heap transactions keep the global lock (2026-10-16)

`src/system/xmemory.cpp`, `include/rex/system/xmemory.h`. With 4 KiB host pages,
`PhysicalHeap` allocation skipped the global critical region, and
Release/Decommit/Protect moved the lock into `TriggerCallbacks`, which dropped
it before the parent/alias metadata was updated. An allocation could then reuse
parent pages while the previous alias was still being invalidated. The lock is
now held through the whole parent/alias transaction (`TriggerCallbacksLocked`),
and `AcquireHostPageReconcileLock` always takes it, before `heap_mutex_`.
Regression: `sdkcheck.Physical allocations serialize parent and alias metadata`
(3 failures before the patch). The extra lock on 4 KiB-page allocations has not
been measured; it does not by itself certify every invalid-release cause.

### 0084 — XCTD compression query (2026-10-16)

`src/kernel/xboxkrnl/xboxkrnl_io_info.cpp`. `NtQueryInformationFile` class 27
(`XFileXctdCompressionInformation`) logged an error and failed. The VFS exposes
raw streams without console XCTD metadata, so bit 0 now reports whether the file
starts with the big-endian signature `0x0FF512ED`, the same test the title's
XCTD library performs when the query fails (`82AB1FC8`, `82AB2030`). The read
uses an explicit backend offset: no guest cursor change, event or completion.
No host compression flag is consulted and nothing is decompressed. Read-denied
handles keep their error; a short file reports no signature. Regression:
`sdkcheck.XCTD query identifies the encoded stream without consuming file data`.

### 0085 — microphone array idle-user mask (2026-10-16)

`include/rex/system/kernel_state.h`, `src/kernel/xam/xam_voice.cpp`.
`XamVoiceSetMicArrayIdleUsers` was a stub that left the incoming mask in r3.
Retail 17559 XAM (`816FB9C0`, export `0x48C`) stores all 32 bits, queues a DPC
when r4 is non-zero and returns 0. KernelState now keeps the mask and one
coalesced pending update, independent of capture hardware. Nothing consumes
the update yet and microphone capture remains unimplemented. Regression:
`sdkcheck.Mic array idle-user control does not return the incoming user mask`.

### 0086 — XEX image reservation ownership (2026-10-16)

`include/rex/system/xex_module.h`, `src/system/xex_module.cpp`. `ReadImage`
called `Reset()` on the whole image heap before every load or key retry,
erasing the metadata of other modules in that heap; their later releases
failed (`BaseHeap::Release failed because address is not a region start`).
Each XEX now records its own reservation, releases only that one before a
retry, releases it on Unload only if it was allocated, and calls Unload when
both keys fail. Reserve and commit are separate so an occupied base address is
refused (the combined legacy call accepts reserved pages). Regression:
`sdkcheck.XEX load and key retry preserve other allocations in the image heap`.
Checked with the Motocross Madness codegen and title only; the dashboard and
other titles have not been rerun with this patch.

### 0087 — s5.10 fixed-point render targets and raw integer resolves (2026-10-16)

`include/rex/graphics/util/draw.h`, `src/graphics/util/draw.cpp`,
`src/graphics/d3d12/command_processor.cpp`, `src/graphics/d3d12/render_target_cache.cpp`.
Motocross Madness compresses its terrain color tiles to DXT1 on the GPU: a pixel
shader writes each block's four 16-bit words as integers to a `k_16_16_16_16`
render target with exponent bias -10, and the result is resolved to the same
format as signed integers with exponent bias +10, then sampled as DXT1. On the
console that round trip must be bit-exact, which is what an s5.10 fixed-point
target (stored integer = value * 1024) gives. Two host differences broke it:

- the full-range D3D12 host render target path stores these formats as SNORM16
  after dividing by 32, i.e. `round(n * 32767 / 32768)`; 32,702 of the 65,535
  integers changed by one. `draw_util::GetColorExpBiasScale` now also multiplies
  by 32768/32767, so the SNORM16 integer equals `value * 1024`;
- the non-zero destination exponent bias routed the copy to the full resolve
  shaders, which do not implement integer destinations (the blocks came out as
  `0000`/`FFFF`). When the backend stores exact s5.10 integers, a copy of
  `k_16_16[_16_16]` to the same format as signed integers with bias 10 is the
  stored integer, so the existing bitwise copy shaders are used.

Only the D3D12 full-range host render target path opts in; the ROV path, the
-1...1 truncated mode and Vulkan keep their previous behavior. Resolves that
convert these targets to other formats read the stored values with the old
32/32767 factor, a relative difference of 3e-5. The s5.10 scale is inferred
from the title's design (no public hardware document was found).
Regression: `sdkcheck.fixed16-resolve` (every s5.10 integer through SNORM16 for
both formats and biases, the copy-shader choice and its exclusions).

### 0089 — opt-in host dispatch for the entrypoint (2026-10-17)

Adjacent original XEX images cannot host auxiliary function tables at each
`IMAGE_BASE + IMAGE_SIZE`. The existing DLL capability already supports host-only
dispatch; the entrypoint gains an explicit `Runtime::Setup(image, config, true)`
overload and a default-false `ReXApp::UseHostFunctionTable()` hook. The original
Setup overload, RuntimeConfig and PPCImageInfo layouts are retained. Apps opting
in must compile their indirect calls with `REX_HOST_FUNCTION_TABLE=1`; generated
DLLs expose their matching capability through the existing export. The generic
multi-module project applies the mode consistently; existing title profiles keep
their default dispatch. No original image is moved or patched.

Regression: `tests/sdkcheck/entrypoint_dispatch_test.cpp` reproduces refusal of
the adjacent image with the old entrypoint table, verifies the explicit mode,
preserves both mappings and image bytes, and checks the legacy default. Patch
replay and exact before/final hashes are recorded in the lock. No equivalent
upstream commit is known; the SDK pin is unchanged. Evidence and limitations:
`docs/reports/aot-dependencies-2026-10-17.md`.

### 0090 — finite byte computed branches without guessed table slots (2026-10-17)

The PPC analysis recognizes a contiguous def-use chain from `lbzx`, optional
`rlwinm`, constant-base addition and `mtctr` to `bctr` (nops are allowed).
Its finite destination domain comes from all 256 byte values and the exact
rotate/mask, with 32-bit wrapping and the architectural low-bit clearing of
`bctr`. It does not read or guess a table's length. Only destinations in the
current code region, before another known entry, seed local CFG
discovery. Emission switches on the original computed CTR; other destinations
tail-dispatch through the existing checked resolver. The guest load/arithmetic
remain intact. Analyzed tables take precedence over late emission heuristics;
manual switch configuration retains priority. Out-of-line cases may extend past
PDATA into an unowned gap, preserving the existing discovery contract. Other patterns retain the previous
analysis. No runtime scanner, original-byte patch or title-specific rule.
Direct call destinations are indexed in the existing decode pass so a caller
discovered later cannot make the computed domain absorb its callee. PC-capture
calls to the following word are excluded. Successor paths may continue beyond
the largest domain address, up to their terminators and the same entry barrier.

Regression: `tests/sdkcheck/ctr_dispatch_test.cpp`, including adjacent-function
data, register/mask variants and broken def-use chains. The explicit
`x360n_ctr_dispatch_native` target compiles production-generated synthetic PPC
and tests mutable table bytes, indices beyond 512, local cases and a real
cross-function tail dispatch, with the SDK's host resolver. Replay hashes are
recorded in `deps.lock.json`. No equivalent upstream commit is known; the pin
is unchanged. Results: `docs/reports/aot-ctr-dispatch-2026-10-17.md`.

### 0091 — shared camera-unavailable exports (2026-10-18)

The pinned kernel omits `xboxkrnl_usbcam.cpp`, leaving valid imports undefined
at native link time. Compile its 11 existing exports, retaining disconnected
state 0 and moving the established platform creation-failure contract into the
SDK: `X_STATUS_NOT_IMPLEMENTED`, no device allocation or output writes. The
upstream unconditional creation-success body and nine return-without-result
stubs are not enabled. Operations without an established implementation abort
with their name and guest LR. This is a capability/failure contract, not camera
capture support; it is shared across titles and dashboard versions without using
original version-specific assets. Existing local dashboard overrides are retained.

Regression `sdkcheck.usbcam-unavailable` links all exports without title shims,
checks buffers/registers and the state/create contract, and verifies all nine
unsupported operations abort in bounded subprocesses. Existing dashboard camera
and entrypoint tests also pass. No upstream equivalent is known; pin unchanged.
Before/after hashes and replay are recorded in the lock and the AOT report above.

### 0093 — full SHA-1 message length (2026-10-18)

TinySHA1 wrote four zero bytes before the low word of the padded bit count.
Messages of 512 MiB or more therefore received the wrong SHA-1 padding. Emit
the complete 64-bit bit count in big-endian order. Smaller messages and XEX
block verification keep their existing contract; no hashes are bypassed.

`tests/sdkcheck/sha1_test.cpp` checks standard vectors, streaming boundaries,
binary blocks against Windows BCrypt, the 512 MiB boundary, and the guest
XeCryptSha ABI. No equivalent upstream commit is known; the SDK pin remains
unchanged. Before/after source hashes and the patch hash are in `deps.lock.json`.

### 0094 — older local logon and firmware exit (2026-10-18)

The original Retail 1888 Sign-in submits four XUIDs through XamUserLogon,
using offline modes 0x420/0x424 and closing snapshot 0x23. Accept these modes
through the existing persistent local store; reject online variants, unknown
identities and multiple users before mutating the active identity.

The original HUD's nice poweroff uses HalReturnToFirmware(5). Replace the
unconditional process exit for the existing routine 1 and this routine 5 with
a recorded firmware request and normal title termination notifications. The
host exports a distinct handoff and closes its platform session. Routine 1
still ends the host session; this does not implement an automatic console reboot.
Revised 2026-10-19: any other routine also closes the platform through the same
orderly handoff (the pinned SDK exited for all of them), with a warning that it
is not modeled separately; the handoff keeps the routine value. A fatal error
there would have turned a former clean exit into a crash for other titles.
No original module or version is shared.

Focused regressions: `tests/sdkcheck/local_profile_test.cpp`, the bounded
subprocess `tests/sdkcheck/firmware_exit_test.ps1`, and supervisor handoff/session
tests. Source and patch hashes are recorded in `deps.lock.json`; SDK pin unchanged.
The mode-5 interpretation is also described by the upstream Xenia Canary
[HAL implementation](https://github.com/xenia-canary/xenia-canary/blob/canary_experimental/src/xenia/kernel/xboxkrnl/xboxkrnl_hal.cc).

### 0095 — legacy XEX wrapping keys (2026-10-18)

Early XEX2 images can use the older wrapping-key family: Retail 1888 xam.xex
is one such image. Follow the four-key order in Xenia Canary's BSD-licensed
[XEX loader](https://github.com/xenia-canary/xenia-canary/blob/canary_experimental/src/xenia/cpu/xex_module.cc):
current retail/devkit first, then legacy retail/devkit. Each attempt retains
the existing block SHA-1 and PE checks, plus patch 0086's per-image reservation
ownership. Remember the selected family for re-deriving an image session key
after a delta updates its header; a legacy devkit key remains a devkit key.

`tests/sdkcheck/xex_key_test.cpp` builds independent AES-CBC encrypted synthetic
images through Windows BCrypt and verifies all four paths without executing
guest code or losing a neighboring allocation. It reads constants from the SDK
source instead of duplicating key material in project test data. The original
1888 XAM remains unchanged; no other dashboard's module is substituted.

### 0096 — PowerPC condition-register aliases (2026-10-18)

The disassembler emits `crmove` and `crnot` for the two-source `cror`/`crnor`
forms with identical sources. Generate those two-operand forms explicitly,
preserving all other CR bits. This is shared PPC behavior, not an XAM-specific
replacement. `tests/sdkcheck/cr_alias/` assembles and recompiles independent
fixtures and compares cross-field and same-bit operations with a bit-mask
reference. Revised 2026-10-19: the same family's one-operand `crset`/`crclr`
(`creqv`/`crxor` with three identical operands) also had no builder and is now
generated; the fixture adds both (24,576 checks). No current generated title
uses them. After this codegen change, titles generated earlier must be
regenerated with `--ignore-stamp`; Motocross's `xam` module still has the old
`REX_UNIMPLEMENTED` traps for `crmove`/`crnot`. The SDK pin is unchanged;
hashes are recorded in the lock.

### 0097 — real XAM allocation size (2026-10-18)

Replace the XamAllocSize stub with a query of committed guest allocation
metadata. The original 1888 export 818E8770 returns the usable byte count in
r3; its XUI realloc body 81907EF0 uses that count to bound its copy. Leaving
the input pointer in r3 can make growth read beyond the previous allocation.
The shared HLE heap commits whole pages, so the result includes usable page
padding. Null, freed, uncommitted and interior pointers return zero. No extra
tracking map is added. Original physical allocations retain their original
1888 size-query path at the title boundary. The reported size can exceed the
requested one; every known caller (1888/17559 HUD, Sign-in, Create Profile,
Gamer Profile, XUI 818E88F8) is a realloc helper copying min(old, new) into a
fresh block, so the copy stays inside committed, zero-filled storage.

`tests/sdkcheck/xam_allocation_test.cpp` covers all shared virtual allocation
entry points, page boundaries and invalid/freed pointers. The explicitly
invoked `x360n_1888_allocation_probe` exercises the original recompiled XUI
realloc body without entering XAM or creating a GUI. These checks do not
certify the interactive Sign-in or Guide flows. No equivalent upstream commit
is known; SDK pin unchanged, patch/source hashes in `deps.lock.json`.

### 0098 — authenticated encrypted resource XEX2 (2026-10-19)

The resource-only loader previously required an unencrypted DLL with flags
exactly 8. Original encrypted skins can also carry the exports-to-title bit.
Accept those data-only XEX2 images after checking every plaintext page-digest
link, rooted in the security header. This also selects the correct key for
encrypted raw/basic compression, where compressed-block hashes are absent.
Retain the resource bounds, complete data-page coverage, and rejection of
entrypoints, imports, TLS, code sections and unsupported module classes.
Existing compressed-block verification remains active. Encrypted XEX1 resource
images remain unsupported; no failed executable is reclassified as a skin.

The `[xex-resource]` synthetic fixture uses independent Windows BCrypt AES/SHA
and stored LZX, covers three compression modes and four wrapping-key families,
and rejects damaged digest chains or executable/out-of-range metadata. It
contains no original resource bytes. SDK pin unchanged; hashes in the lock.

### 0099 — unavailable custom banner (2026-10-19)

`XCustomGetBannerImage` returns `E_NOTIMPL` while the SDK has no banner
provider; a null argument returns `E_INVALIDARG`. This lets the original caller
use its own fallback artwork instead of treating a stub's unchanged pointer
register as successful image data. The only known caller, Retail 1888 HUD
`913E31E8`, passes one stack word and reads it only on success; since other
argument layouts are unverified, guest memory is not written (revised
2026-10-19; the first version cleared that word). Focused coverage:
`tests/sdkcheck/local_profile_test.cpp` (`[custom-banner]`).

### 0100 — unavailable private input statistics (2026-10-19)

The input backend has no provider for the private device-statistics record.
Return `ERROR_NOT_SUPPORTED` for valid requests, leaving caller storage untouched;
invalid user/output arguments return `ERROR_INVALID_PARAMETER`. This prevents
the former stub's unchanged user index zero from becoming a successful query.
Controller polling and keystrokes are unchanged. The focused test above also
checks all four user slots and untouched output guards. SDK pin unchanged.

## 0101 — posse dos controles que iniciam uma UI de sistema

`0101-system-ui-opening-input.patch` captura, ao iniciar a sessão, os controles
que já estão pressionados. As duas ABIs HUD consomem somente DOWN/repetições/UP
desses controles até sua soltura. Um botão novo continua disponível e START/BACK
voltam a funcionar normalmente após o UP; não há esvaziamento geral da fila.
O host 1888 usa a mesma captura para reter o gesto de fechamento antes de devolver
a fila à dashboard. A captura consulta o estado uma vez por transição; a filtragem
usa a trava e o bookkeeping de keystrokes já existentes, sem instrumentação nos
polls normais dos jogos.

A regressão `[guide-opening]` em `tests/sdkcheck/system_ui_input_test.cpp` falhou
no SDK anterior (24 asserções) e cobre as duas ABIs, três reaberturas, eventos
atrasados/repetidos, preservação de A e START novo, requisição sem botões e o
retorno da fila ao título. `[guide-input]` e `[profile-ui]` preservam os contratos
de saída e sessão. Não há alteração de originais nem dependências entre versões.

### Revisão de 19/10/2026 — ombros e atalho BACK+START

A tabela de captura trocava LB/RB: `VK_PAD_RSHOULDER` é `0x5804` e
`VK_PAD_LSHOULDER` é `0x5805`. A conversão de estado para teclas agora é única
(`PadKeysFromState`) e o caso `Shoulder buttons held at opening…` cobre os dois.
Uma inversão temporária para provar o caso vermelho foi recusada pelo gate de
hashes do SDK no build; o resultado desse ensaio foi descartado.

Controles sem botão Guide (8BitDo VID `0x2DC8`, relatado pelo mantenedor)
sinalizam GUIDE só junto com o segundo botão de BACK+START. Os traços físicos
de 19/10 (`20261019T052843Z`/`053310Z`) mostram o primeiro botão chegando ao
título antes do GUIDE. Com `guide_chord_ms > 0`, o primeiro DOWN de BACK/START
fica retido até aparecer GUIDE (o atalho passa à UI de sistema até a soltura,
nas filas do título e da HUD), até a soltura (entregue imediatamente) ou até o
fim da janela. Eventos seguintes esperam atrás dele, preservando a ordem. O
padrão do SDK é 0 (desligado, sem custo além da leitura do cvar); as dashboards
1888 e 17559 usam 150 ms quando o usuário não configurou o valor. A janela é uma
hipótese a calibrar no teste manual; `[guide-chord]` cobre título, HUD, toque
curto, janela expirada e o modo desligado. Jogos e consultas de estado não mudam.

## 0102 — ciclo de vida da entrada e janela opt-in (19/10/2026)

Substitui o estado global de posse/atalho por estado da instância de entrada.
Desconexão, mudança de identidade/atribuição e Shutdown invalidam pendências e
posse. Um erro de conexão nunca libera DOWN guardado. Runtimes consecutivos
não herdam o gesto; nenhuma fila é drenada na inicialização.

GetState e GetKeystroke compartilham a decisão de janela; ANY_USER percorre os
quatro índices, respeitando o proprietário da UI. A janela agora permanece
desligada também nas dashboards. Habilitação explícita: `guide_chord_ms` (0–1000)
e máscara `guide_chord_users` (padrão 1, usuário 0). A política se aplica ao
usuário guest selecionado, inclusive dispositivos mesclados nele. Eventos
posteriores mantêm sua ordem; UP/repetições são preservados para comandos normais.

Red integrado em Release: três falhas de ciclo de vida. Green: 500 asserções/18
casos de entrada/UI. Testes físicos e calibração continuam pendentes.
[Relatório](../../docs/reports/review-fixes-2026-10-19.md).

### Revisão distributiva do 0095

O patch ativo não inclui valores de chaves legadas. Usa arquivo local no formato
do extrator (`xex1-retail`/`xex1-devkit`, 32 dígitos hexadecimais), selecionado por
`xex_keys_file` ou `X360N_XEX_KEYS`. O driver registra a identidade SHA do arquivo.
Testes fornecem chaves legadas sintéticas; ausência/má formação falha explicitamente.
O gate de distribuição reconhece constantes em bytes, C++ e unified diff.
Fingerprints de exclusão não servem ao carregador. Histórico publicado preservado.

## 0103–0105 — reconexão e posse do gesto Guide (19/10/2026)

0103 serializa consultas, enumeração, atribuição e invalidação no InputSystem.
A sonda concorrente reproduziu `0xC0000005` em `SlotAssignment::OnDevicesChanged`
antes da correção. Shutdown retira os drivers sob a trava, mas os destrói fora
dela para não esperar a UI segurando a trava de entrada.

0104 captura a borda Guide antes do carregamento da aplicação e preserva essa
captura ao iniciar a sessão após a soltura. A borda pertence à conexão: Guide
segurado ao reconectar não abre/fecha outra sessão. O fechamento captura naquele
instante os controles ainda pressionados. Filtragem e consulta são atômicas;
fila vazia libera máscaras de abertura de controles já soltos, inclusive quando
o backend nunca gerou seu UP. Ambas as ABIs HUD e os quatro usuários têm regressões.
Um contador de geração também detecta troca de dispositivo entre GetState e
GetKeystroke; somente nesse caso relê o estado antes de classificar a nova tecla.
A limpeza de fila vazia também exige uma máscara de abertura pendente antes de
consultar o estado, sem acrescentar esse poll quando não houve gesto do Guide.
As dashboards 1888/17559 habilitam a janela existente de 150 ms e máscara 15
somente quando a origem do cvar é o padrão; valores explícitos, inclusive 0,
são preservados. O padrão do SDK continua 0. A janela não desfaz comandos já
entregues antes de seu prazo; calibração e aceite físicos seguem pendentes.

0105 remove o observador SDL também no destrutor, invalida callbacks pendentes
sem capturar o driver e serializa extração/aplicação dos lotes de eventos.
Chegada expirada e duplicada são toleradas; remoção usa o instance ID armazenado.
Soltura cancela repeat antes de sua emissão. A sonda SDL é sem janela visível,
usa joystick virtual e executa o callback enfileirado após destruir o driver.

Testes: `input_hotplug_probe.cpp`, `sdl_hotplug_probe.cpp` e `[input-p0]` em
`system_ui_input_test.cpp`. SDK pinado e originais preservados.
[Evidências, limites e aceite obrigatório](../../docs/reports/controller-reconnect-guide-2026-10-19.md).

## 0106–0111 — conquistas offline e interface original 17559

0106 adiciona armazenamento durável por XUID/Title ID/Achievement ID com
metadados XDBF originais, checksum, trava entre processos e substituição atômica.
O handoff usa a raiz de perfis da sessão. Recusa corrupção e catálogos
incompatíveis; não migra desbloqueios globais antigos nem modifica saves/perfis.

0107 implementa a escrita XGI, leitura de contadores, enumeração por título e
XUID completo, histórico de jogos e imagens originais. Corrige capacidade de
enumeração, marshaling PPC de 64 bits e propagação de falhas assíncronas de uma
aplicação existente. Duplicatas não somam pontos nem notificam novamente.

0108 encaminha as telas de conquistas ao GamerProfile original e notificações
ao serviço FE/0x21013. A ponte em `titles/dash_17559/src/system_ui.cpp` inicializa
sob demanda o apresentador original, usando `notify.xur` e o compositor da XAM;
sem overlay substituto nem mudança da captura de controles.

0109 valida os limites do XDBF e suas tabelas antes de expor seus recursos.
0110 fornece nomes originais dos títulos ao GamerProfile, registra os exports
da interface no resolver e rejeita handles encerrados antes de escrever outputs.
0111 impede persistência de metadados no diretório de entrada quando o runtime
de uma ferramenta usa a raiz de usuário implícita; raízes explícitas isoladas
continuam funcionando. Evita que o codegen altere o inventário importável do jogo.
Testes focados exercitam contratos PPC, erros, corrupção, concorrência, dois
perfis/dois títulos e reinício em processos independentes. Gameplay e exibição
real dependem do aceite manual, não substituído por testes sintéticos.

[Contratos, evidências e roteiro](../../docs/reports/offline-achievements-2026-10-21.md).

## 0112 — toques curtos e pacotes de estado do controle

Preserva um toque real de START/BACK liberado antes do fim da janela do Guide
para a próxima consulta de estado do título. Antes, DOWN era ocultado durante
a janela e UP apagava a espera, perdendo o toque inteiro. O gesto do Guide
continua consumido pela interface original. Mudanças nos botões filtrados
também avançam o pacote visível quando o pacote físico permanece igual.
Estado por usuário/conexão, reset existente, sem alocações no polling nem
lógica por título. Testes em `system_ui_input_test.cpp`, incluindo falha
reproduzida antes da correção. Não há equivalente upstream associado.

## 0113 — contexto de tarefa para notificações originais

Fornece um contexto de tarefa XAM para callbacks síncronos do host, sem criar
threads. Restaura o contexto anterior em aninhamento/exceção, retém a referência
de `XamTaskGetCurrentTask` e implementa leitura sincronizada dos atributos.
O adaptador 17559 usa a identidade do host apenas nesse escopo; workers comuns
mantêm o caminho anterior. Corrige `8007000E` reproduzido na fila original
quando o aviso de conquista era enviado fora de uma tarefa. Testes técnicos
não certificam a apresentação visual durante gameplay. Sem equivalente upstream.
[Causa, testes e aceite](../../docs/reports/achievement-notification-2026-10-21.md).

## 0114 — camada de vídeo original D1OVL no D3D12

Preserva os registradores de scanout emitidos pelo compositor original após
os comandos de sistema, mantendo a restauração dos registradores de desenho do
título. Decodifica superfície RGB, pitch, posição e recorte D1OVL, carrega a
textura pelo cache existente e compõe os pixels originais antes de gamma/FXAA.
Pipeline e superfície intermediária são criados somente quando usados e
reutilizados. O descritor vazio encerra a camada na próxima apresentação,
inclusive quando substitui os últimos comandos originais no mesmo callback.
Sem textos, conquistas, layout ou IDs de jogos no backend. Escopo de backend:
D3D12; Vulkan e Native não recebem certificação por este patch.

Shader próprio `display_overlay.hlsl`, bytecode reproduzível pelo alvo
`x360n_display_overlay_shader_check` (entrada HLSL, saída opcional header).
Testes de decodificação e fechamento em `display_overlay_test.cpp` e
`system_command_buffer_test.cpp`; diagnóstico GPU sem janela pelo alvo
`x360n_display_overlay_gpu_check`. Sem equivalente upstream identificado.
[Evidências e limite do aceite](../../docs/reports/achievement-overlay-2026-10-21.md).

## 0115 — cor e alpha da superfície D1OVL

Corrige o fetch de scanout para BGRA sem inversão dos bytes. A ordem PPC dos
comandos não se aplica aos pixels da superfície de display. A inversão anterior
levava alpha ao vermelho e um canal de cor ao alpha, tornando branco transparente
em ciano opaco. Sem troca de shader, substituição de recursos ou chave de cor.
Teste GPU usa o decodificador compartilhado, superfície de 8 bits e 64 pixels
com cores distintas e alpha zero/parcial/opaco; a configuração anterior é
controle negativo e falha. Sem equivalente upstream identificado.
[Evidências e limite do aceite](../../docs/reports/achievement-overlay-2026-10-21.md).

## 0116 — entradas de jogos do GamerProfile original

Implementa `XamShowGamesUI` e `XamShowAvatarAwardGamesUI`, antes stubs atingidos
na sessão manual de 21/10. Os exports originais Retail 17559 2D0/3C7
(`816C48A0`/`816C49F0`) usam os modos 1/6 do GamerProfile: FE/21008, 544 bytes,
usuário em +0 e XUID de 64 bits em +8. O XUID vem de **r4**, diferentemente de
`XamShowAchievementsUIEx`, que o recebe em r5. Título/detalhe ficam zerados.
Reutiliza a rota existente para o módulo original; sem interface substituta,
dados inventados ou modificação do ledger. Preserva falhas de despacho.
Regressão focada em `achievement_ui_test.cpp`, tag `[profile-navigation]`,
falhou sem o patch. Sem equivalente upstream identificado. Abertura/desenho
e retorno das cenas ainda dependem do aceite manual; o patch não certifica
a correção da queda posterior a CreateProfile.

## 0117 — renomeação local e entrada da edição original

`XamShowEditProfileUI` solicita o modo 3 do GamerProfile, conforme o export
Retail 17559 3BC (`816C4B40`). `XamProfileRenameAccount` preserva o XUID inteiro
em r3 e lê o nome WCHAR em r4, conforme `816DE888`/caller `9010BC20`.
Retorna erro Win32 síncrono; não usa o marshaler que trunca inteiros em 32 bits.
Valida a memória guest e o nome limitado a 15 caracteres ASCII, mantendo o
contrato local já existente. Duplicidade ignora caixa e exclui a própria conta.

A gravação atômica antecede a publicação do nome; falha não altera identidade,
seleção ou estado conectado. XUID, caminhos de saves e ledger permanecem
estáveis; renomear não revoga handles de save. A conta ativa publica a
notificação original 14/slot mask 1 após o commit. Testes `[profile-rename]` e
`[profile-navigation]` falham sem o patch. Não implementa serviços Live nem
certifica o desenho/aceite das cenas. Sem equivalente upstream identificado.

## 0118 — remoção recuperável da identidade local

Implementa `XamProfileDelete` conforme `816DB078`: dispositivo em r3 e XUID
inteiro em r4, resultado HRESULT. O pedido é emitido pela confirmação original
da dashboard. Antes de remover a identidade, copia e sincroniza o snapshot
comprometido em `profiles/deleted/`, sem substituir uma recuperação existente.
Falha no arquivo de recuperação ou no commit recusa a alteração em memória.
Saves, configurações e conquistas continuam em seus caminhos por XUID.

Remover a conta selecionada desconecta e invalida seus handles de armazenamento;
remover outra conta preserva a sessão atual. A remoção do último perfil grava
v2 vazio de 20 bytes, com XUID selecionado zero. O parser reconhece esse estado
sem ressuscitar o v1 preservado como origem de migração. Criar uma nova conta
depois disso mantém o v2. Não apaga conteúdo associado nem implementa restauração
pela UI; o arquivo de recuperação preserva o formato original do snapshot.
Testes `[profile-delete]`, junto dos casos de migração/identidade, usam somente
armazenamento descartável. Sem equivalente upstream identificado.

## 0119 — preferências persistentes e cache de configurações por XUID

Implementa os tipos INT32, INT64, DOUBLE, FLOAT, DATETIME, WCHAR e BINARY no
`XamUserWriteProfileSettings`, antes ignorados com retorno de sucesso para
vários tipos. Valida tipo, tamanho e memória de todos os itens antes de gravar.
Os contadores do ledger e de títulos jogados permanecem somente leitura.

Preferências gerais ficam em `profiles/<XUID>/settings-v1/<ID>.dat`, com os
escalares e WCHAR em big endian; strings terminam em WCHAR zero. Configurações
específicas do título mantêm o caminho e os bytes do armazenamento anterior.
Cada valor é gravado e sincronizado em temporário antes da substituição atômica;
falha não publica esse valor no cache. Isso não promete uma transação conjunta
para falhas de I/O no meio de um lote. Arquivo corrompido retorna erro de leitura.

O cache acompanha XUID e Title ID, restaurando o padrão ou ausência quando
o próximo usuário/título não possui esse valor. Nome e exclusão da identidade
não mudam os caminhos por XUID. Leitura e gravação usam a mesma seção crítica
do logon até copiar a resposta, evitando substituição/liberação de um objeto
`Setting` ainda usado por outra chamada. Sem alteração por frame ou no renderer.
Falha de abertura de arquivo não chega mais a `fwrite`/`fclose` com FILE nulo.
Essa falha de contrato não é atribuída à queda guest `0x00000005` sem evidência.

Testes em `profile_settings_test.cpp`: sete tipos, reinício, renomeação,
troca de usuário, corrupção, falha de escrita, overlapped, buffers e leitura
concorrente. Os casos de persistência falharam antes do patch. Sem equivalente
upstream identificado.

## 0120 — dispositivos de armazenamento persistentes

Troca os dispositivos fictícios por backings do ambiente, IDs estáveis,
capacidade real do host e quotas/reservas opcionais em `storage.toml`.
MU/USB só aparecem com pasta configurada existente; o óptico depende de
um mount CdRom0 realmente somente leitura. Pacotes/headers/enumeração C8
usam a raiz do dispositivo; o layout anterior do HDD é preservado.
Consultas NT do volume do pacote usam os mesmos números de XAM; crescimento
de arquivos com quota passa pela verificação de espaço do backend.
Os testes PPC em `storage_device_test.cpp` e `save_storage_test.cpp` cobrem
volumes separados, reinício, ausência, erro, quota e persistência real.
Limites e aceite original em `docs/reports/storage-p0-2026-10-21.md`.
Sem equivalente upstream identificado.

## 0121 — seletor original de armazenamento

Transporta o pedido Retail 17559 FE/2100B de 40 bytes, mantém o OVERLAPPED
pendente para a conclusão pela aplicação e remove a seleção automática do
HDD. Adapter local preserva os 64 bits de bytes pedidos em `r6`. O perfil
17559 registra o `deviceselector.xex` correspondente por SHA-256 e compila
seu próprio módulo AOT; o host utiliza cenas e recursos originais.
Frontend ausente retorna erro. Testes `[storage-selector]`; controle e
aceite visual permanecem pendentes. Sem equivalente upstream identificado.

## 0122 — capacidades offline de armazenamento de rede

Implementa como desativados os booleans de Cloud Storage consultados pelo
seletor original, em vez de deixar argumentos antigos em `r3`. O runtime
não oferece serviço online e sempre oculta seu dispositivo de rede.
Não fabrica permissões, conectividade ou dados de nuvem. Testes PPC variam
usuário/tipo/Title ID para impedir suporte dependente do argumento residual.
Sem equivalente upstream identificado.

## 0123 — campos opcionais de perfil ausentes

Campos com chave/tipo/tamanho válidos, mas ainda não gravados, são retornados
com origem `NO_VALUE` (0) e tipo `UNSET` (FF), sem criar arquivos nem inventar
valores. Um campo ausente não invalida o lote inteiro de configurações da
dashboard. Chaves malformadas continuam recusadas, inclusive na consulta de
tamanho; arquivos corrompidos continuam sendo erros. A regressão usa os sete
IDs ausentes registrados nas sessões da Retail 17559 e confirma persistência
após gravar um deles. Tags `[profile-settings-optional]`, `[profile-settings]`.
Sem equivalente upstream identificado.

## 0124 — opções no Guide original

Substitui o stub `XamShowOptionsUI` pelo pedido FE/21015 de 12 bytes
`{0, controller, 6}`, conforme o export 4ED (`816C2BD8 -> 816C2AF0`) da
Retail 17559. Usa o HUD original e o tratamento existente de pedidos
encadeados; não altera a identidade nem cria uma tela alternativa.
Preserva validação de usuário e falhas de memória/despacho.
Tag `[profile-options]`, com regressão anterior ao patch.
Sem equivalente upstream identificado.

## 0125 — preferências locais no XGI

Implementa a mensagem XGI B0082 seguindo `8190B3B0 -> 8190A7D0` da
17559: lê os flags persistentes `10040053` do XUID ativo, aplica as máscaras
1/6/24/32/64/128/256/512 e remove o bit de habilitação de 6/24 quando
presente. Preserva S_FALSE quando a preferência composta está desabilitada.
Ausência do campo conserva o resultado zerado da resposta original;
corrupção, usuário desconectado e parâmetros inválidos não viram sucesso.
O wrapper valida saída e OVERLAPPED antes do despacho. Não implementa Live
nem anuncia suporte a quatro usuários simultâneos. Testes `[user-preference]`
e `[profile-preference-flags]`, incluindo isolamento e buffers inválidos.
Sem equivalente upstream identificado.

## 0127 — XUID completo na enumeração agregada

O primeiro argumento de `XamContentAggregateCreateEnumerator` é um XUID
de 64 bits em um GPR PPC. Um adapter local evita a truncagem do marshaller
genérico, que ocultava saves de proprietários reais. O teste C8 consulta
um save da MU pelo XUID completo, via export e `XamEnumerate`, antes e
depois de reiniciar. Não altera o marshaller global. Sem equivalente
upstream identificado. O patch gráfico que também usava o prefixo 0127 foi
renumerado para 0140; a ordem de aplicação é exclusivamente a lista em
`deps.lock.json`.

## 0128 — chave original de gamerpic

Remove os valores fictícios C0DE0001/2/3 de `XamParseGamerTileKey`.
Implementa o descritor de string XAM e os três componentes hexadecimais
conforme o export 30A (`81670A50..81670B68`) da Retail 17559, incluindo
o fallback reservado do original e a atualização do comprimento. Valida
os spans de memória antes de ler ou escrever. Teste `[profile-tile-key]`
falha com o stub anterior e passa com chaves reais, reservadas e inválidas.
Isso não implementa seleção, gravação ou cache de imagens do perfil.
Sem equivalente upstream identificado.

## 0126 — API gráfica explícita no aplicativo

`gpu_backend` atravessa RuntimeConfig/ReXApp até LoadGpuPlugin e a fábrica
existente, sem alterar GpuCreateInfo nem a ABI 1 do plugin. API solicitada
explicitamente não retorna ao padrão. RuntimeConfig C++ ganhou um campo:
recompilar consumidores junto com o runtime. Teste gpu_backend_test.cpp e
probes da fábrica; sem equivalente upstream identificado.

## 0140 — composição D1OVL no Vulkan (antes 0127)

`0140-vulkan-display-overlay.patch` se chamava `0127-vulkan-display-overlay.patch`
e repetia o prefixo do patch de conteúdo 0127; foi renomeado sem mudar o conteúdo
nem o hash. Continua aplicado logo após 0128, pela lista do lock. RequestDisplayTexture aceita
o fetch próprio da camada sem alterar registradores do título. Compute GLSL
traduzido para SPIR-V compõe BGRA/alpha antes de gamma, com descriptors por
frame, barriers e descarte de image/view/memory por submission no resize.
Os probes compararam 48 frames por API e cinco capturas idênticas. Isso ainda
não certifica notificações/Guide originais ou gameplay Vulkan. Sem equivalente
upstream identificado. [Evidências e limites](../../docs/reports/graphics/g1-vulkan-native-2026-10-21.md).

## 0129 — fechar a pilha original de aplicativos

`XamAppUnloadStack(0)` deixa de equivaler a `XamAppUnloadSelf`. A sessão
preserva o escopo da pilha até o fechamento no limite do frame; um close-self
posterior não o reduz. O host 17559 descarta os pais suspensos nessa situação,
mantém o sucessor enfileirado e não restaura o CreateProfile já concluído.
Nenhum módulo é descarregado dentro do callback solicitante.

O teste `[profile-ui-stack]` falhou antes do patch em duas asserções e passou
nas 23 após aplicá-lo. A sonda CPU executa o update original `90106398` com
estados 1/2 e confirma Self/Stack. O host também libera as quatro cópias de
strings do estado de Sign-in descartado, seguindo salvar/restaurar originais.
Sem equivalente upstream identificado; aceite das duas rotas visuais separado.

## 0130 — cache de Cloud Storage ausente no modo offline

`XamNetworkStorageDeviceHasCacheFile` é um BOOL no export original 17559
`615` (`816A6790`, pedido FE/2D002). O stub deixava o ID de dispositivo em
`r3`; a dashboard interpretava HDD/MU como cache existente e acrescentava seu
item de 512 MiB. Retorna falso porque não existe backend de Cloud Storage,
sem inventar arquivos nem alterar a UI original. Teste PPC
`[storage-offline-cache]`: red com IDs 1/2/7/FFFFFFFF na DLL anterior.
Sem equivalente upstream identificado.

## 0131 — carregamento de app original pelo Guide

O atalho **Criar perfil** do Guide 17559 não usa `XamShowCreateProfileUI`:
o handler do HUD (`913E85E0`, modo 2) chama `913EA808`, que executa
`XamAppRequestLoad()` e, se não zero, `XamAppLoad("createprofile.xex",
controle, 0, 0, 0, 0)`. Os dois exports eram `REX_EXPORT_STUB`; os logs
manuais registraram os dois stubs e nenhuma tela foi aberta.

`XamAppRequestLoad` (0x248, `816CAD70 → 816CAC60` tipo 0) devolve se há um
carregador de apps originais selecionado pelo perfil; o gate interno do XAM
não é modelado. `XamAppLoad` (0x244, `816CDD58`) entrega nome, controle e
buffer opcional (no máximo 256 bytes) ao hook `system_app_load`; sem hook,
devolve `E_FAIL` como `816CDF7C`. OVERLAPPED e handle de saída não estão
implementados: a chamada falha com `E_NOTIMPL` e completa o OVERLAPPED com
1627, sem simular carga. O host 17559 aceita somente `createprofile.xex` sem
buffer e o inicia pelo mesmo caminho do CreateProfile original, com pedido
nulo (`90107850` só copia os 136 bytes quando existem) e o controle do
`XamAppLoad`. Que o XAM passe pedido nulo nesse caminho é hipótese:
`816BCE74` não foi rastreado por completo. Teste `[app-load]`. Sem
equivalente upstream identificado.

## 0132 — perfil local nunca é parcial

`XamUserIsPartialProfile` (0xAEA) era `REX_EXPORT_STUB`. O Guide 17559
(`913E6B88`) chama esse export com o índice do controle e, se não zero, abre
as conquistas do título atual (`XamShowAchievementsUI`) em vez da lista de
Jogos (`XamShowGamesUI`). O stub devolvia o próprio índice: para os
controles 1–3 o Guide abria a tela errada. Original `816DAAC8`: usuário
conectado (`816D8470`) e bit `0x80000000` da primeira palavra da conta de
380 bytes em registro+20 (copiada por `816DB950`; `816DAA60` testa Live
`0x20000000` na mesma palavra). Contas locais não têm flags reservados
(`AccountFor`), então o resultado é 0. Teste `[partial-profile]`: red com 5
de 10 asserções falhando (1, 2, 3, 4 e 255 vazavam em `r3`); green 10/10.
Sem equivalente upstream identificado.

## 0133 — diagnóstico da enumeração de conquistas

Somente log, sem mudança de comportamento. Uma linha `info` por criação de
enumerador de conquistas/títulos jogados (título, controle, XUID, flags,
quantidade, resultado) e por página entregue por `XamEnumerate` desses dois
enumeradores (incluindo recusa de flags e buffer insuficiente). Motivo: a
tela original de conquistas do GamerProfile 17559 fica carregando; as
operações originais (`901200D8`) concluem na sonda CPU com cópia dos dados
reais, mas a sessão real não registrava nenhum passo dessas exportações.
Frequência: algumas linhas por abertura das telas de perfil, nenhuma por
quadro. Testes relacionados inalterados: 1.502 asserções / 37 casos.

## 0134 — flags do enumerador de conquistas como no original

A tela de conquistas de um jogo ficava carregando sem parar. A
`LoadAchievementsScene` do GamerProfile 17559 (`XM_INIT` em `90117710`)
configura a operação `901200D8` com **flags `0x27`** e quantidade 150; o SDK
recusava qualquer flag fora de `0x7` (erro 87). A cena recebia a falha, mas a
lista nunca recebia dados. Original `81670190`: valida controle < 4,
quantidade e ponteiros de saída, **não valida flags**; `flags & 7` escolhe
registros de 36 ou 500 bytes. Os demais bits seguem para o serviço; aqui a
única fonte é o livro local. Teste `[offline-achievement-api]`: red
`87 == 0` com `0x27`; green 315 asserções / 3 casos. A asserção anterior que
exigia erro para a flag `8` foi substituída por asserções do comportamento
original (`0x27` com textos; `0x20` com itens de 36 bytes sem textos).
Sonda com cópia somente leitura dos dados reais (perfil Forza, FM4
`4D5309C9`): 50 conquistas com `0x27`/150, a 155 desbloqueada.

## 0135 — copiar e mover conteúdo entre dispositivos

`XamContentCopyInternal` (0x26C) e `XamContentMoveInternal` (0x26D) eram
stubs. Wrapper original `816EEA78` (FE/2000C, 552 bytes): ponteiro opcional
de XUID dono (`r3`), ponteiro opcional de dispositivo de destino (`r4`), item
de 512 bytes (`r5`, cujos 0x148 bytes iniciais são `XCONTENT_AGGREGATE_DATA`;
a dashboard acrescenta o tamanho em +328), callback (`r6`), contexto (`r7`),
argumento extra (`r8`) e OVERLAPPED (`r9`); sem XUID e sem dispositivo é 87.
A dashboard (`922FF840`–`922FF8B4`; Move é o thunk `9293B72C`, Copy o
`9293B70C`) usa um callback no formato `CopyProgressRoutine` (`922FDB00`):
total/transferido em `r3`/`r4`, motivo em `r8` (1 = início), contexto como
nono argumento (`84(r1)`), retorno não zero cancela.

`ContentManager::TransferContent` copia pacote e header para uma área de
preparação no volume de destino e renomeia no lugar; mover só apaga a origem
depois do destino concluído e, se a exclusão falhar, remove o destino. Nunca
sobrescreve (183); verifica espaço com a mesma cota/reserva das consultas
(112); recusa mover conteúdo aberto (5, como a exclusão); cancelamento
completa com 1223 sem deixar destino nem preparação; preparação de processo
interrompido é removida na transferência seguinte. Com OVERLAPPED, a cópia
roda em `XHostThread` própria (não na fila deferida compartilhada) e devolve
997. Hipóteses: o handler FE/2000C do XAM não foi rastreado; os códigos de
conflito/espaço/cancelamento e a ausência de notificação de mudança seguem o
backend local. Teste `[storage-transfer]`: red 2 falhas com o stub; green
58 asserções. Formatação continua não implementada.

## 0136 — HDD único do console

Cvar `content_data_root`: raiz do `ContentManager` (o HDD do console, com
`storage.toml`, MU/USB e todo XContent). Padrão: `user_data_root`, como antes.
A sessão do supervisor passa a raiz da dashboard a todos os títulos; caches e
demais dados continuam na raiz de cada título. Motivo: no console há um único
HDD; com raízes por título, o Storage da dashboard não via saves dos jogos.
Teste `[storage-hdd-root]`: red (cvar inexistente) → green.

## 0137 — registros agregados originais

`XamContentAggregateCreateEnumerator` devolve registros de 584 bytes, como o
XAM 17559: `XCONTENT_AGGREGATE_DATA` (0x148), tamanho u64 em +0x148 e criação
FILETIME em +0x150, preenchidos por `GetContentAttributes`. Filtro de dono
original: XUID 0 lista todos os donos do dispositivo, ~0 só o compartilhado e
os demais só aquele dono (`ContentManager::ListContentOwners`). Motivo: o
Storage listava itens sem tamanho e omitia saves de outros perfis. Teste
`[aggregate-content]`: registro de 584 bytes, tamanhos e filtro de dono.

## 0138 — perfis como pacotes de conteúdo

Como no console, cada perfil é um pacote de conteúdo no dispositivo:
`<dispositivo>/<XUID>/FFFE07D1/00010000/<XUID>/` com `account.dat`
("X360PA01", XUID, gamertag), jogos jogados, `settings-v1/` e conquistas
offline (`achievements.dat`, via resolvedor do `OfflineAchievementStore`),
mais o header 0x10000 com a gamertag como nome. `UserProfile` descobre os
perfis varrendo HDD/MU/USB conectados; `<raiz>/profiles/session-v1.dat`
("X360SS01", XUID, estado) guarda só a seleção. O antigo `local-v2.dat` e o
arquivo de recuperação de exclusões deixaram de existir, sem migração (dados
locais de teste são descartáveis). `XamProfileCreate` cria no dispositivo
escolhido; enumeração, `FindAccount`, `LoadAccountInfo` e `XamProfileDelete`
informam/aceitam o dispositivo real (0 = qualquer). No Storage, apagar ou
mover o pacote do perfil conectado é recusado (5, como conteúdo aberto); após
apagar/copiar/mover pacotes de perfil a lista é relida. Uma cópia do mesmo
XUID em outro dispositivo é a mesma conta: vale a primeira encontrada
(hipótese; o console não foi comparado). Testes `[profile-content]`,
`[profile-delete]`, `[local-profile]` e `[profile-settings]` atualizados para
o novo contrato.

## 0139 — atalho de tela cheia (Alt+Enter / F11)

Como na maioria dos jogos de PC, Alt+Enter ou F11 alternam a tela cheia
(`rex::ui::IsFullscreenToggleKey`; só a primeira pressão alterna, repetições
são consumidas). Num título embutido na janela persistente da sessão
(`parent_window`), quem tem a tela cheia é a moldura do supervisor: o título
envia `WM_APP + 0x362` e a moldura alterna entre janela e tela cheia sem
mudar o modo de vídeo, guardando a posição anterior; com o foco na própria
moldura (entre títulos) os mesmos atalhos funcionam. Fora da sessão o título
alterna a própria janela. Enter sem Alt continua sendo tecla do jogo; o
mapeamento de teclado (mnk) exige modificadores exatos, então Alt+Enter não
aciona Start. A escolha não é gravada nas configurações (a opção "Iniciar
sessão em tela cheia" continua valendo para o início). Teste
`[fullscreen-toggle]`.

## 0141 — overflow de `vmsum3fp128`/`vmsum4fp128` vira QNaN

O codegen emitia `simde_mm_dp_ps` puro. Quando o produto escalar de entradas
finitas excede float32, o Xenon devolve QNaN `0x7FC00000` nos quatro
elementos (semântica do Xenia `DOT_PRODUCT_3/4` + `EmitDotProductResult`;
[rexglue-sdk#491](https://github.com/rexglue/rexglue-sdk/issues/491)); o
`dpps` devolvia ±inf, ou NaN negativo `0xFFC00000` quando produtos de sinais
opostos transbordam embora a soma exata seja finita. Os helpers
`rex::ppc::simde_mm_vmsum3fp128/4fp128` (`include/rex/ppc/intrinsics.h`)
mantêm o resultado finito do `dpps` bit a bit e, só quando ele não é finito
(`ptest` no expoente), recalculam em float64 na ordem do Xenia
`(x+z)+y(+w)`: soma finita que transborda → QNaN; soma finita → valor
arredondado; soma não finita (inf/NaN vindo de uma entrada) → resultado
anterior, sem conversão. Sem hack por título ou endereço. Adaptado ao 0036
(mesmo `vector.cpp`); não copiado do patch 0025 do ForzaHorizon1-ReImagined,
que usa o mesmo desenho.

Divergências registradas: (1) resultados finitos seguem o `dpps` em float32;
o Xenia acumula sempre em float64 e diz que isso fica mais próximo do
hardware em até 1 bit (não verificado em console). (2) Num caso de dupla
arredondamento em que o `dpps` dá exatamente `FLT_MAX` mas a soma exata
alcança o limiar de overflow, aqui sai `FLT_MAX` e no Xenia QNaN. (3) O
arredondamento usa o modo do MXCSR herdado do FPSCR, como as demais
operações VMX do SDK; o VMX real arredonda sempre ao mais próximo.

Teste `sdkcheck.Vmsum` (`tests/sdkcheck/vmsum`): as duas instruções pelo
caminho real `as → rexglue recompile-tests → clang++`, overflow positivo,
negativo e de sinais mistos, quadrado em registrador único, inf/−inf/NaN e
inf·0 na entrada, produtos que transbordam e se cancelam, `FLT_MAX`
±¼ ulp e +½ ulp (empate → 2^128 → QNaN), faixa w ignorada pelo vmsum3,
registradores sobrepostos e 20 000 vetores aleatórios contra modelo escalar.
Antes do patch: 41 falhas registradas, 40 dos casos fixos e o primeiro vetor
aleatório, quando o teste para por limite (`.local/evidence/vmsum/before-test.log`);
depois: 160 416 verificações passam. `ppc.*` do SDK (1459 testes, incluindo
`ppc.vmsum3fp128.test_1`/`vmsum4fp128.test_1`) continuam passando.

Desempenho (Release, Ryzen 7 5700X3D, `x360n_vmsum_checks --bench`, melhor
de 5, 3 execuções; base = 0141 revertido com o lock correspondente): função
de uma instrução 2,20 → 2,84–2,87 ns/chamada; cadeia dependente de 6
`vmsum` 1,416 → 1,80–1,86 ns/instrução. O custo é o teste do expoente e o
prólogo exigido pela chamada fria. Variantes medidas e descartadas: ponteiros
para os registradores (≈1,90), caminho frio `always_inline` + `comiss`
(≈2,08) e teste inteiro estilo Xenia (≈1,85). Impacto em quadro de jogo não
medido. Títulos já gerados só recebem a correção ao regenerar o código AOT.

## 0142 — consulta de cvar fora das draws Vulkan

`VulkanCommandProcessor::IssueDraw` testava `IsReadbackMemexportEnabled` antes
de verificar se a draw tinha faixas de MEMEXPORT. Essa consulta procura o cvar
legado pelo nome no registro (mutex global, `std::string` e hash; ~107 ns
medidos) e acontecia em toda draw. O teste agora começa por
`!memexport_ranges_.empty()`. A função é `const` e sem efeitos colaterais: draws
com MEMEXPORT avaliam as duas condições como antes; as demais não fazem o
readback em nenhum dos casos. O D3D12 já fazia a checagem dentro de
`if (memexport_used)` e não muda. Mesmo mecanismo da referência 0049 do
ForzaHorizon1-ReImagined. Sem equivalente upstream identificado.

`sdkcheck.Vulkan-draw-registry-lock` (com `sdkcheck.D3D12-draw-registry-lock`
como controle) segura o lock do registro em outra thread e exige que 4.096 draws
sem MEMEXPORT terminem. Antes do patch, o Vulkan não terminou em 3 s; depois,
terminou em 9,9 ms. Os testes só existem onde há o draw retido local
(`X360N_DRAW_SNAPSHOT`). Capturas byte a byte idênticas antes/depois.
[Medições e limites](../../docs/reports/performance/g2-xenos-2026-10-22.md).

## 0143 — exp_adjust das texturas no Vulkan

O tradutor SPIR-V aplicava o bias de expoente do resultado de `tfetch` com os
bits 13:18 da word 4 da fetch constant, que pertencem ao `lod_bias`.
`exp_adjust` fica nos bits 13:18 da word 3 (`xe_gpu_texture_fetch_t`), e o
tradutor DXBC já lê dali. No Vulkan, texturas com `exp_adjust` saíam sem o
fator (4× mais escuras com +2) e LOD bias diferente de zero virava multiplicador.
No Forza Horizon, isso deixava a cena preta com só a interface visível. O patch
carrega a word 3 e usa os mesmos bits. Mesmo código em `origin/development`
(`bd833a2`), sem correção upstream identificada.

`sdkcheck.Vulkan-texture-exp-adjust` reproduz um draw retido do Forza (texturas
`k_2_10_10_10_AS_16_16_16_16` com `exp_adjust` +2) em Xenos/Vulkan e
Xenos/D3D12 e exige capturas idênticas. Antes do patch, Vulkan `21c1d95e…`
contra D3D12 `038f08b7…`; depois, as duas `038f08b7…`. Só existe onde há o
snapshot local (`X360N_DRAW_SNAPSHOT`).

## 0144 — gancho de conclusão dos system commands

O handler de `PM4_XE_SYSTEM_COMMANDS` grava a fence de conclusão do buffer de
sistema (Guide, notificações) logo depois de executá-lo e de restaurar o estado
do título. Um backend que adia resultados na memória do guest, como o Native com
os resolves, deixava a fence visível antes desses resultados. O patch acrescenta
`CommandProcessor::OnSystemCommandsExecuted()`, virtual e vazio, chamado
imediatamente antes da fence. O Xenos não muda; o Native publica os resolves ali.
Sem equivalente upstream: o pacote e o handler vêm de patches deste projeto.

`sdkcheck.Native-system-commands` coloca um resolve num buffer de sistema seguido
de 4.096 draws do título e lê o destino no instante em que a fence fica visível:
com o gancho o conteúdo já é o final; com o override desativado (diagnóstico),
3/3 execuções viram a fence antes do resolve.

## 0145 — fetch de vértice inativo no Vulkan

Porta o 0077 para o Xenos/Vulkan. `VulkanCommandProcessor::IssueDraw` rejeitava o
draw inteiro quando um slot do bitmap estático de vertex fetch tinha descritor
inválido, mesmo com o fetch predicado fora. No Forza Horizon em Vulkan, o slot 90
(`00000001 00000000`) atrás de `setp_eq c127.x` com `c127.x = 0` derrubou 73.432
draws numa sessão de 8 min, entre passes de profundidade e de cor, enquanto o
D3D12 com o 0077 desenhava os mesmos shaders
([X2](../../docs/reports/graphics/xenos-fetch-predication.md)).

A prova sai do header D3D12 para `pipeline/shader/inactive_vertex_fetch.h`,
compartilhado, com um parâmetro de denormais. O DXBC sempre os descarta. O SPIR-V
só usa `DenormFlushToZero` quando o device informa
`shaderDenormFlushToZeroFloat32`. Sem isso, um `c127.x` denormal pode comparar
como diferente de zero no host, e a prova o trata como desconhecido (mantém a
rejeição). A RTX 4070 (driver 617.42) informa 0. NaN e zero com sinal valem
igual nos dois tradutores (`OpFOrdEqual`/`OpFUnordNotEqual` como `eq`/`ne` do
DXBC). O header D3D12 só encaminha com descarte ligado; o D3D12 não muda.
Mesmos limites do 0077: sem índice, título ou hash especial; o slot não é marcado
residente e é reavaliado a cada draw.

`sdkcheck.Vulkan-predicated-vertex-fetch` executa no `x360n_native_probe` um VS
sintético (sem conteúdo de jogo) com a mesma estrutura do shader do Forza:
`setp_eq c127.x`, `(!p0) exec` e `vfetch_full` na constante 90 inválida, com
`edram_mode=5`. O draw com `c127.x = 0` tem que passar, e o mesmo draw com
`c127.x = 1` tem que ser rejeitado. Isso prova que o draw chegou à checagem e
que nada fica em cache após mudar a constante. O veredito vem do próprio log do
backend. Antes do patch, o Vulkan rejeitou o draw inativo
(`inactive fetch: rejected=1 failed=1`). `sdkcheck.D3D12-predicated-vertex-fetch`
é o controle: passou antes e depois. `sdkcheck.inactive-vertex-fetch` passa a
cobrir a prova compartilhada com e sem descarte de denormais.

## 0146 — escrita de registradores em lote e bindings com memória coerente

Adapta 0029 (somente Type0 em lote) e 0032 do Forza Horizon 1: ReImagined
`cb676f7e07149b4cc964dc53d49147dd268939b3` (MIT; avisos em
`licenses/forza-reimagined.txt` e `licenses/recompiled-halo-reach.txt`). Type0
consecutivo usa as rotas existentes de constantes em bloco. Escritas repetidas
no mesmo registrador continuam sequenciais; scratch, COHER e gamma mantêm seus
efeitos. Não reintroduz a tabela KnownRegisters nem o 0073 retirado.

Nos dois backends Xenos, descritores fetch idênticos preservam os bindings;
alterações parciais invalidam a faixa que efetivamente mudou. Isso exigiu
corrigir antes uma pré-condição: o cache de vertex residency do SDK pulava
`RequestRange` apenas por igualdade de endereço/tamanho, mesmo após CPU writes.
`SharedMemory` publica uma geração atômica após invalidações CPU, escritas GPU,
reset de páginas no fim de frame e invalidação explícita. Cada draw confere uma
vez essa geração e descarta a residência antiga no próprio command processor.
Os locks, callbacks, proteções de página e política `clear_memory_page_state`
continuam em vigor. Não altera conteúdo, readback, barriers, FPS ou vblank.

`sdkcheck.register-memory-coherence` usa o CommandProcessor e SharedMemory reais,
sem GPU: Type0 normal/com wrap, writes com efeitos, intervalos fetch/parciais,
invalidação CPU e GPU e preservação de páginas GPU no reset. O teste também
compila contra a base para demonstrar falha funcional anterior. Ganho em jogo
e aceitação visual dependem da matriz G3; este teste não os comprova.

## 0147 — separar histórico de readback por representação de resolve

Adapta 0048 da mesma revisão do Forza ReImagined (MIT). Um resolve de cor para
uma faixa antes usada por depth deixava o modo `some` reutilizar a entrada
antiga; `fast` podia devolver o depth anterior. A chave passa a incluir faixa,
source depth/color e formato/number/endian/swap/array/slice do destino. Os
modos, fences, readbacks e expiração dos buffers continuam iguais. Não usa
endereços, shaders ou identificação de título.

A referência mistura a assinatura num único hash de 64 bits; esta adaptação
guarda os três campos e compara igualdade, de modo que uma colisão de hash não
una resolves diferentes. MEMEXPORT mantém a identidade anterior. Exponent
bias, base EDRAM e dimensões não fragmentam o histórico de um mesmo pass.
`sdkcheck.resolve-readback-identity` cobre campos, estabilidade e colisões
forçadas de hash. A fotografia ou outro readback em jogo ainda exige validação.

## 0151 / 0155 — temporário de arredondamento do MEMEXPORT assinado

Corrige o destino de uma instrução BFI no tradutor DXBC: a construção de ±0,5
sobrescrevia o valor exportado e deixava `round_bias_temp` sem definição antes
do ADD. O BFI passa a escrever no temporário alocado, preservando o valor para
a soma e a conversão de float para inteiro. O 0155 corrige também a segunda
ocorrência, no packing separado de quatro componentes de 16 bits. Aplica-se ao packing inteiro ou
normalizado assinado compartilhado por Native e Xenos D3D12, sem condição por
título. Alteração própria sobre o SDK pinado; cabeçalho/licença BSD upstream
preservados. Execução de validação do MEMEXPORT fica pendente por instrução do
mantenedor; compilar não certifica os resultados GPU.

## 0152 — reutilizar a memória de intervalos de SharedMemory

Adapta somente a reutilização de `merged_ranges` do patch 0031 do Forza
ReImagined `cb676f7e07149b4cc964dc53d49147dd268939b3`, MIT, avisos acima.
O scratch pertence ao command processor, como `upload_ranges_`, e conserva a
capacidade entre chamadas. Cada lote limpa o conteúdo antes de validar,
ordenar e juntar os intervalos. Preserva a sincronização das páginas, callbacks,
proteções e uploads; não incorpora o fast path sem lock da referência.

Casos adicionais de `sdkcheck.register-memory-coherence` cobrem lote rejeitado
após prefixo válido, lote vazio, faixa seguinte disjunta e sobreposições fora de
ordem. Foram preparados sem execução nesta retomada. Ganho de tempo/alocações
em cenário real ainda não foi medido.

## 0156–0159 — uploads, submissão e conclusão de pipelines

0156 e 0157 adaptam 0036 e 0038 do Forza ReImagined, revisão e licença MIT
acima. Uploads Vulkan só são movidos antes do render pass quando suas páginas
não foram lidas/escritas nele. MEMEXPORT dinâmico protege todo o espaço físico.
Os streams pertencem à submissão e a fila tem no máximo oito trabalhos pendentes;
fences, sparse binds, apresentação e teardown preservam ordem e posse dos recursos.
Erros do worker são propagados e trabalhos anteriores são drenados antes de destruir recursos.

0158 é implementação própria do objetivo de 0051: cada draw das duas APIs
aguarda sua pipeline real. Falhas permanecem falhas, sem timeout que descarte o
draw nem placeholder certificado como shader real. 0159 mantém a atualização
obrigatória de páginas CPU ao fim do frame, trabalhando apenas nos words alterados
e preservando a geração de residência quando nada muda. Não incorpora a mudança
de política de 0039. A heurística de 50% de 0052 foi rejeitada: ela descarta dados
de resolves e só afeta escala superior a 1, fora do caminho Xenos Fidelity original.

Sondas CPU de hazards e coerência de memória foram preparadas. Execução das
sondas, validação GPU e medição de desempenho são etapas separadas da compilação.

## 0179 — interlock SPIR-V em caminhos de descarte com depth tardio

O tradutor FSI começava o interlock dentro da condição de amostras sobreviventes,
mas o encerrava fora dela. Um pixel shader que exportasse depth ou MEMEXPORT e
tivesse kill/alpha test podia executar `EndInvocationInterlockEXT` sem o `Begin`
correspondente. O patch move somente o início da seção tardia para antes da
condição; os acessos EDRAM continuam condicionais e o caminho early permanece
intacto. A versão de traduções/pipelines passa a 13 para invalidar caches antigos.

Correção própria conforme a regra de execução exatamente uma vez e em ordem da
[SPV_EXT_fragment_shader_interlock](https://github.khronos.org/SPIRV-Registry/extensions/EXT/SPV_EXT_fragment_shader_interlock.html).
`sdkcheck.SPIRV-interlock-control-flow` traduz microcódigo sintético e percorre o
CFG emitido, cobrindo early/late, kill/alpha, demote, ZPD/visibilidade e escala,
sem criar dispositivo ou executar jogo. O PS capturado na queda NVIDIA de
23/10 às 01:12 usa early-FSI e passa esse contrato; este patch não é apresentado
como a causa ou a correção daquela queda.

## 0180 — reinício do programa em cada vértice de retângulo

O tradutor construía o `OpPhi` do program counter antes de acrescentar o caminho
que repete o shader para o próximo vértice do retângulo. Shaders com saltos
produziam um predecessor sem o respectivo operando no `Phi`. O patch adia a
emissão da continuação até todas as arestas existirem, preservando o reinício
em PC zero e os destinos dos saltos. Versão do cache: 14.

A captura da queda Native Vulkan de 23/10 às 01:12 identificou esse defeito no
VS `EAC91CBA481D5FF8`, modificação `0000000A00000007`, durante a criação do PSO10.
SPIRV-Tools recusou o original (dois operandos para três predecessores); as 15
capturas retraduzidas, incluindo esse VS, passaram após 0180. Isso comprova a
correção estrutural do shader; a confirmação em jogo é uma etapa separada.

## 0181 — máscara de amostras após alpha-test e alpha-to-coverage

O caminho FSI sem helper demotion usava máscaras definidas apenas dentro das
condições de alpha-test e A2C após sair delas. Com comparação ALWAYS ou A2C
desligado, as definições não dominavam os usos, gerando SPIR-V inválido. Dois
`OpPhi` preservam a máscara anterior nos caminhos que pulam essas operações.
O merge da condição de escrita em RT0 passa também a conservar o resultado de
A2C quando demotion está disponível; antes, esse resultado era calculado e
descartado, perdendo o descarte parcial de amostras. Não altera o caminho sem
FSI nem os shaders depth-only. Versão do cache de traduções: 15.

Correção própria, verificada com `sdkcheck.SPIRV-interlock-control-flow` e
SPIRV-Tools Vulkan 1.4. A execução CPU isolada com as fontes candidatas 0181 e
0182 passou 60 variantes FSI e 45 casos de controle de fluxo/capturas; os 105
módulos resultantes passaram no validador. Evidências locais:
`.local/g3/spirv-candidates-v2/{translation,validation}.log` e `inputs.json`.
As 24 falhas anteriores de dominância ocorreram com demotion desligado.
Esses resultados não são validação de gameplay nem medição de desempenho.

## 0182 — referências de blocos inalcançáveis no SPIR-V

Instruções após um salto incondicional podem ser traduzidas em blocos que o
serializador do glslang omite. O patch exclui esses predecessores do `Phi` do
program counter e remove decorações que referenciavam suas definições omitidas.
O tratamento preserva os blocos estruturais de continuação e merge, inclusive
quando nenhuma instrução guest alcança a continuação. Versão do cache: 16.

`sdkcheck.SPIRV-rectangle-replay-phi` cobre shaders comuns e retângulos com
saltos condicionais, predicados, incondicionais e inalcançáveis, usando máscaras
de interpolação 0/1/7. O caso de continuação sem entradas impede que a limpeza
apague o valor ainda referenciado pelo cabeçalho do loop. As 30 variantes e
15 capturas, junto às 60 variantes FSI de 0181, passaram nos 105 módulos da
validação CPU isolada registrada acima; não executa dispositivo gráfico.

## 0183 — endereçamento de texturas do Xenia Canary

Porta 344fca7, ace153cb8 e 4a863a0e1 do Canary (`canary_experimental`). Uma
textura cujo nível 0 já está na mip tail lê toda a tail de `base_address`,
mesmo com `mip_address` não nulo. Um `mip_min_level` não nulo descarta a base
somente quando ela coincide com o endereço das mips. O filtro `kBaseMap` usa o
nível 0 quando a base existe (D3D12 e Vulkan). Texturas 2×1 são compactadas
ao longo de X, como as quadradas.

`sdkcheck.canary-texture-layout` usa fetch constants sintéticos: 5 das 13
verificações falham com o `util.cpp` anterior e todas passam com o patch. O
nível 0 do sampler `kBaseMap` não tem teste automático; não houve execução em GPU.

## 0184 — aliases de formatos de 8 bits do Xenia Canary

Porta 8322658: texturas `k_8_A`/`k_8_B` usam `k_8` e `k_8_8_8_8_A` usa
`k_8_8_8_8` (`GetBaseFormat`, tabelas de host D3D12 e Vulkan). Resolves para
esses formatos gravam o formato equivalente. Os avisos de memexport/resolve
"não pesquisados", emitidos por desenho, foram removidos como no Canary.

`sdkcheck.canary-format-alias`: 3 das 16 verificações falham com o `draw.cpp`
anterior; as de `GetBaseFormat` falham com o header anterior. As tabelas de host
não têm teste automático; não houve execução em GPU.

## 0185 — precisão fixed16 da EDRAM do Xenia Canary

Porta 42c2b88: componentes `k_16_16[_16_16]` da EDRAM são s5.10 (10 bits
fracionários). ROV (DXBC) e FSI (SPIR-V) empacotam `floor(v * 1024 + 0.5)` no
intervalo [-32, 32767/1024] e desempacotam com 1/1024, sem o antigo limite -1
do FSI. O clamp PSI usa o mesmo máximo. No Vulkan com render targets de host,
a escala de exp bias passa pelo `GetColorExpBiasScale` de 0087 (2^bias ×
1024/32767), e resolves inteiros com bias 10 usam a cópia exata de 0087.
Versões dos caches de tradução: DXBC `0x20261023`, SPIR-V 17.

`sdkcheck.canary-edram-fixed16` traduz offline um pixel shader real com ROV e
FSI (escalas 1 e 2): 13 das 19 verificações falham com as fontes anteriores.
`sdkcheck.fixed16-resolve` e `sdkcheck.SPIRV-*` continuam passando. Os shaders
de resolve pré-compilados (bytecode sem fontes no SDK) ainda desempacotam com
32/32767; não houve execução em GPU nem validação SPIRV-Tools.

## 0186 — layout de texturas e limites 3D do Xenia Canary

Porta bc6b7a020, 19da17403, c9c8e483b, 81e3deaee e c332733af. Mips de arrays
2D com mais de uma fatia ocupam múltiplos de 4 fatias; volumes reduzem a
profundidade a cada mip; a base 3D compactada no nível 0 usa dimensões em
potência de dois; linhas de mips lineares alinham o número de blocos a
max(256 / bytes por bloco, 32) (96bpp: 384 bytes, não 512); mips 1×1 em Z
contam a base da tail. Os limites tiled 3D passam a ser exatos: o superior é o
maior endereço da última porção de 32 linhas × 4 fatias mais um bloco, e o
inferior considera a troca de banco em grupos Z/4 ímpares. A altura passada ao
limite superior é o stride de linhas da fatia, não a extensão.

`sdkcheck.canary-texture-bounds` compara os limites com busca exaustiva usando
a fórmula `Tiled3D` do Canary (idêntica à do SDK no teste): 6 das 9
verificações falham com o `util.cpp` anterior; a do limite inferior depende do
header. `native.texture` continua passando; não houve execução em GPU.

## 0187 — fase de macro tile no destino de resolve tiled

Porta 89609297c: o D3D avança `RB_COPY_DEST_BASE` em macro tiles 32×32, que
têm menos de 4 KB em 8bpp/16bpp; o resto da base vira deslocamento em X antes
do cálculo tiled 2D (base, offsets e extensões do destino). O Canary cita a
refração da água de 534307D5; não reproduzido aqui.

`sdkcheck.canary-resolve-dest`: uma base avançada N macro tiles equivale à
cópia N×32 pixels à direita da base alinhada; 9 das 16 verificações falham com
o `draw.cpp` anterior (o caso 32bpp de controle passa nos dois). Sem GPU.

## 0188 — correções SPIR-V/Vulkan do Xenia Canary

Porta e519d59e4 (o lerp entre camadas de textura empilhada parte da primeira
camada, não da segunda), 2d5b41080 (índice de camada empilhada limitado a
[0, tamanho − 1] para coordenadas Inf/NaN), 654a8cacf (com 2x emulado como 4x,
a amostra 1 do guest vem da amostra 3 do host na máscara FSI) e 77597d62a
(stencil BF só quando apenas as faces frontais são descartadas, como no D3D12).
A parte de 654a8cacf sobre o Phi de demote já estava coberta por 0181. Versão
do cache SPIR-V: 18.

`sdkcheck.canary-spirv` traduz offline shaders reais (tfetch3D e FSI, escalas
1 e 2) e verifica o fluxo de dados emitido: as 4 verificações de comportamento
falham com as fontes anteriores. A seleção de stencil não tem teste
automático. Sem GPU e sem validação SPIRV-Tools.

## 0189 — limite de camada de textura empilhada no DXBC

Porta 04085efaa: no D3D12, a coordenada Z de texturas 2D empilhadas é limitada
aos centros das camadas [0,5, tamanho − 0,5] nos quatro caminhos (coordenadas
normalizadas ou não, com ou sem offset), para que coordenadas Inf/NaN não
escolham camada indefinida. O Canary cita o fundo preto de 584107FB; não
reproduzido aqui. Par do 2d5b41080 de 0188 no SPIR-V.

`sdkcheck.canary-dxbc` traduz offline tfetch3D reais nas quatro variantes e
procura a sequência max/add/min no programa DXBC: as 4 verificações falham com
a fonte anterior. Sem GPU.

## 0190 — PM4_WAIT_IB_PFD_COMPLETE

Porta 3e9d62ffc: o opcode caía no caso não implementado, que devolve falha e
descarta o restante do ring buffer primário sempre que um título o envia. Os
indirect buffers são executados de forma síncrona, então o pacote só avança o
payload. O guard Native continua recusando o opcode explicitamente
(`Unsupported`), sem mudança neste patch.

`sdkcheck.canary-pm4`: um registrador escrito depois do pacote, no mesmo
buffer, é aplicado pelo command processor real; falha com a fonte anterior.
`sdkcheck.register-memory-coherence` continua passando (813 verificações).

## 0191 — origem negativa do viewport de host

Porta 6579cc2a4: a origem do viewport de host pode ser negativa (até −máximo),
em vez de ser cortada em 0 e compensada na escala/offset NDC. Com tiling
predicado e offset de janela negativo, cada tile passava a usar outra
transformação NDC; o Canary cita o tile direito de 4D5307DF (x −608). A
extensão continua limitada ao máximo. `ViewportInfo::xy_offset` passa a ser
com sinal (D3D12, Vulkan e Native só o convertem para float).

`sdkcheck.canary-viewport`: o tile com offset −608 começa em −608, mantém a
extensão 1280 e a mesma transformação NDC do tile sem offset; 3 das 4
verificações falham com o `draw.cpp` anterior. `native.texture` continua
passando. Sem GPU.

## 0192 — RB_COPY_SURFACE_SLICE em resolves de volume

Porta d8731edc9: em resolves para texturas de volume, a altura da fatia vem de
`RB_COPY_SURFACE_SLICE / pitch` (o D3D grava pitch × altura do nível), e
`copy_dest_height`, que pode incluir o topo do retângulo de origem, fica só
como fallback. Resolves 2D não mudam.

`sdkcheck.canary-resolve-dest` (estendido): com `copy_dest_height` 192 e
fatia de 256 linhas, a fatia Z = 4 usa 256 linhas e coincide com o destino de
altura 256; 2 das 20 verificações falham com o `draw.cpp` anterior. Sem GPU.

## 0193 — grade da mip travada em fetches não normalizados

Porta 8486e97a0: em `tfetch2D` com coordenadas não normalizadas, quando
`mip_min_level == mip_max_level` (word 4 do fetch constant), o denominador
usa max(tamanho >> mip, 1) em vez do tamanho da base, em DXBC e SPIR-V. O
Canary cita o popping de visibilidade de 555308B6 e 5553080B (redutores HZB);
não reproduzido aqui. Versão do cache SPIR-V: 19.

`sdkcheck.canary-spirv` e `sdkcheck.canary-dxbc` traduzem offline um
tfetch2D não normalizado e procuram a seleção do tamanho travado; cada um
falha com a fonte anterior, e o fetch normalizado continua sem a seleção.
Sem GPU.

## 0194 — palavras de vertex fetch fora do buffer leem 0

Porta 9e9d3cdd3: em DXBC e SPIR-V, cada palavra de `vfetch` em ou além de
base + tamanho (bits 2:25 da word 1 do fetch constant) passa a valer 0, como
no hardware, em vez de ler dados de outra região da memória física. O limite
do `vfetch_full` é guardado para os `vfetch_mini` seguintes, e o replay de
retângulos (0180) reinicia esse estado por vértice. Versão do cache SPIR-V: 20.

`sdkcheck.canary-spirv` e `sdkcheck.canary-dxbc` traduzem offline um
`vfetch_full` k_32_32_32_32_FLOAT e procuram a máscara por palavra; cada um
falha com a fonte anterior. `sdkcheck.SPIRV-*` continua passando. Sem GPU.

## 0195 — alias de cor e profundidade na EDRAM com ROV/FSI

Porta a parte ROV/FSI de 9da693480: com ROV (D3D12) ou FSI (Vulkan), um
render target de cor no mesmo endereço da profundidade só desliga depth/stencil
se escrever bits usados por esses testes (profundidade nos bits 8:31, stencil
nos bits 0:7); antes qualquer escrita desligava ambos. O Canary cita nuvens
visíveis através da geometria em 4D530A26. A parte de render targets de host
(RTV/FBO, posse de faixas e profundidade somente leitura) não foi portada.

`sdkcheck.canary-edram-alias` testa `ColorOverlapsDepthStencil` com as
máscaras PSI reais (6 verificações; a regra anterior falharia na escrita só do
byte de stencil). A ligação nos command processors não tem teste automático.
Sem GPU.

## 0201 — full resolve fixed16 em D3D12/Vulkan

Os bytecodes full antigos ainda desempacotavam EDRAM signed16 com
`32/32767` e clamp em −1, apesar do s5.10 já usado pelo produtor (0185).
Kernels próprios fixed16 usam os helpers Canary `1/1024`, preservam negativos,
endereçamento SDK e acumulação precisa de 0199/0200. São 20 recursos
DXBC/SPIR-V reproduzíveis com `generate_resolves.py --family fixed16`
e `--dxbc-checksum`: oito preservam o IR pinado com correção dos operandos
de unpack; R8 escalar e variantes escaladas usam as fontes corrigidas.
Os caches Xenos selecionam esses pipelines apenas para full resolves de
16_16/16_16_16_16; cópias raw, gamma e RGB565 conservam seus pipelines.

RTX 4070: 48 falhas anteriores/API eliminadas; 864 fixed16/API aprovados,
684 gamma/API e 504 controles/API preservados. As 45 diferenças RGB565/API
continuam separadas. CPU: 291.132 verificações e onze regressões focadas.
Release Xenos/Native, regeneração e replay do lock aprovados.
[Evidências e limites](../../docs/reports/graphics/full-resolve-fixed16-0201-2026-10-25.md).

## 0203 — descoberta de destinos diretos sem proprietário

Após o GapFill, saltos diretos incondicionais ainda sem destino resolvido podem
revelar getters e adjustors ocultos depois de wrappers que terminam em `b`.
O compilador descobre esses destinos somente em regiões executáveis sem dono,
preservando blocos já pertencentes a funções, dados, limites `.pdata`, extensões
declaradas e casos de switches. Usa o limite existente de iterações; não adiciona
endereços por título nem modifica o runtime. Sem equivalente upstream identificado.
O teste sintético falha sem o patch; os sete casos `[gapfill]` passam 159
verificações com ele. O SDK continua na revisão pinada.
[Diagnóstico e limites](../../docs/reports/titles/generic-forza-horizon-2-minecraft-2026-10-26.md).

## 0204 — precedência de entradas de cauda explícitas

Executa a descoberta de 0203 também antes do preenchimento especulativo dos gaps.
Um destino explícito não pode desaparecer dentro do corpo inferido de um gap
anterior. O prefixo que chega por fall-through continua executando o corpo
original; não é substituído por retorno antecipado. O teste sintético reproduz
a entrada ausente antes do patch. Os oito casos `[gapfill]` passam 170 verificações.
Sem equivalente upstream identificado; nenhuma mudança no runtime ou no pin.

## 0205 — entradas frameless referenciadas em dados

Descobre callbacks sem RTTI apontados por palavras alinhadas em seções não
executáveis, tanto `.rdata` quanto `.data`. Só aceita destinos executáveis sem
dono, fora de metadados declarados, após padding ou término incondicional e com
prefixo válido limitado a 64 palavras. A CFG determina o corpo completo; imports,
casos já pertencentes a funções, interiores e referências desalinhadas ficam
protegidos. As leituras respeitam os limites da seção e da imagem.
O teste sintético falha sem o patch. Os dez casos `[gapfill]` passam 200 checks.
Sem endereços por título, mudanças no runtime ou atualização do SDK pinado;
nenhum equivalente upstream identificado. Execução real continua necessária.

### 0206 — stable user-module exports

Repeated UserModule ordinal lookups allocated a new caller thunk every time,
including host-dispatch callers, eventually exhausting the fixed reserve. Host
and same-module callers now retain the original guest export address. Legacy
cross-module callers reuse a per-caller export thunk. Exporter unload clears
those bindings before DLL unload and recycles only their dedicated slots;
caller unload destroys its own cache. Data exports remain unchanged. No shared
cache of original modules or title-specific adjustment is introduced.

`sdkcheck [user-exports]` loads an entirely synthetic XEX export table through
UserModule: host/legacy capability, 20,000 repeated queries, data exports,
caller isolation, unload invalidation, reload and slot reuse. Before the patch,
the initial fixture reported five failures (identity, repeat stability,
same-module address, stale function after unload, reload stability).

With 0206 applied, exports, host dispatch and optional fault context passed
139 assertions in four focused cases. Independent replay matched the three
recorded final SHA-256 values. The SDK revision remains pinned.

### 0207 — automatic CRT nonlocal jumps

Recognizes the complete console CRT register/VMX jump-buffer ABI in executable
sections, including the optional setjmp dispatch prefix. Stores discovered
addresses separately from manual configuration and selects the existing native
setjmp/longjmp emitter at direct call sites. Supports multiple copies; rejects
altered slots, altered VMX operations, data sections and truncated bodies.
No title addresses, hashes or manual hints are introduced. The original guest
error path resumes at its saved continuation, instead of returning into an old
host frame. Unrecognized ABI variants and indirect setjmp are not certified.

Synthetic regression: ten failures before the patch; with CRT and existing
boundary checks, 275 assertions in 11 cases passed. Independent replay matched
all five final SHA-256 values. No upstream equivalent identified.

### 0208 — bounded materialized code entries

Finds lis/addi and lis/ori code addresses in an already discovered basic block,
within eight instructions and with a conservative instruction whitelist that
preserves the source register. Uses the candidate checks from 0205: executable
boundary, bounded terminating prefix, no declared metadata interior or owned
body. The broad, disabled pointer scanner stays disabled. No runtime change or
title-specific address is needed.

Synthetic regression: three missing entries before the patch. Controls cover
signed low halves, distinct destination registers, clobbers, control-flow joins,
metadata interiors, ordinary fallthrough and data addresses. CRT plus focused
boundary/callback tests pass 347 assertions in 12 cases. Independent replay
matched the final SHA-256. SDK revision pinned; no upstream equivalent identified.

### 0209 — protect inline jump-table data

Real-content verification of 0208 found inline switch tables being registered
as functions: their pointer words decode as loads and an adjacent case ends
with blr. Preserve the actual table byte length in the analysis graph, including
absolute, byte and halfword formats, and exclude those spans from entry seeding.
Finite computed domains without backing tables and manual tables keep size zero.

An independent synthetic function without pdata reproduces the false entry
before this patch. After applying it: 360 assertions/13 focused CRT/boundary
cases and 234/4 CTR cases pass. Replay matches all three final SHA-256 values.
No title addresses, runtime adjustments or weakened validation; SDK pinned.

### 0210 — optional profile-read DEBUG diagnostics

The Forza Horizon 2 storage selector now completes through the original 17559
service, but the subsequent profile getter reads an invalid settings pointer.
A private fault snapshot shows that the supposed result lacks XAM's header;
the isolated 29-setting read contract still passes. Log sizing, successful
buffer writes and base/Ex API results at DEBUG to distinguish the API response
from later guest changes. These records contain guest addresses/counts/status,
not profile values. The normal INFO path evaluates no diagnostic pointer probes
or formatting and preserves the existing request and completion semantics.

`[profile-read-trace]` failed six checks before the patch. Afterwards,
`[profile-settings],[profile-sizing]` passes 311 assertions/eight cases, including
unmapped-input rejection, persisted values, normal-level silence and the mixed
4,312-byte batch. Replay: 197 patches/267 files, zero SHA-256 mismatches. This
is diagnostic instrumentation, not a claimed gameplay fix or performance gain.

### 0211 — infer complete console CRT fiber lifecycles

The generic title reaches a guest fiber switch that restores another guest
stack and returns on the old AOT C++ stack. Select the SDK's existing native
fiber hooks only after matching the complete linked ConvertThreadToFiber,
ConvertFiberToThread, CreateFiber, DeleteFiber and SwitchToFiber family,
including the full GPR/FPR/VMX switch body and bootstrap. Keep inferred hooks
in analysis state; preserve explicit lifecycle overrides. Reject partial
families, changed save slots, broken links and section truncation. All addresses
come from executable instructions; no game identity or address table is used.

The synthetic discovery control fails ten of 112 checks before applying this
patch, and passes all 112 afterwards. Shared discovery/CRT/CTR regressions pass
706 checks in 18 cases. A bounded CPU probe performs four native fiber round
trips with independent continuations and GPR/FPR/VMX/stack preservation. Real
XEX analysis recognizes all five wrappers plus the direct switch body without
configuration hints. Replay: 198 patches/267 files, zero SHA mismatches.
Additional explicit-override and inferred-state controls pass 135 discovery
assertions in nine variants; user configuration stays untouched.
SDK revision and normal runtime behavior remain unchanged until a recognized
complete CRT family selects those existing hooks.

### 0212 — decode MD immediate rotations during analysis

The native emitter already supports rldicl, rldicr, rldic and rldimi, but the
analysis decoder classified them as unknown. This rejected otherwise bounded
callbacks containing a valid 64-bit rotation. Add the four MD opcodes without
renumbering existing entries, correct the split shift/mask fields, and record
source/destination GPRs and optional CR writes. Leave unimplemented MDS and
reserved encodings unknown; preserve all entry and inline-table guards.

The synthetic callback fails discovery before this patch. Exhaustive controls
cover 32,768 opcode/shift/mask/record combinations, including r0 destinations
and rldimi's destination read. Together with focused CRT, callback and CTR
controls, 767 assertions in 19 cases pass. GNU binutils independently confirms
eight representative encodings. Real-content analysis recovers the missing
callback with zero graph errors. Replay: 199 patches/271 files, zero SHA-256
mismatches. No native arithmetic or runtime behavior was changed, and the SDK
revision remains pinned. A new executable still requires gameplay validation.
