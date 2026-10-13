# Local patches to ReXGlue SDK

Base revision: **`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`** (tag `v0.10.0`), the
commit pinned in `deps.lock.json` and checked out as the `external/rexglue-sdk`
submodule. The patches are for that commit only; no upgrade or downgrade.

The submodule's history is not changed: the patches are applied to its working
tree from these files. `deps.lock.json` (`rexglue_sdk.patches`) records each patch
file's SHA-256, the SHA-256 of every file it touches before (`base_files`) and
after the complete patch stack (`files`), and its test. When patches share a
file, `base_files` records the input to that patch and `files` records the final
stack hash in every affected entry. Apply in order; revert in reverse order.

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

Evidence logs: .local/runs/sdk-patch-evidence/0011-*, 0013-*, 0014-* and 0015-*.
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
See `docs/PROJECT_AUDIT.md` E6 for C1/C3 before/after evidence and limits.

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
