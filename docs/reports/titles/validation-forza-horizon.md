# Forza Horizon — Windows x64 investigation, 2026-10-14

Atualizações Xenos de 2026-10-15: [resolves vazios](../graphics/xenos-empty-resolves.md) e
[fetch predicado X2](../graphics/xenos-fetch-predication.md). X2 repetiu o ciclo em harness
isolado; o gate de preparação de produção não foi recertificado e a navegação
após retorno ficou inconclusiva. O ciclo com recibos abaixo é evidência de 14/10.

## Completed original-library cycle — Retail 17559, Windows x64

The complete requested Forza cycle passed in a normal `x360n session`, started
through the original My Games UI, with prepared receipts and no alternate
launcher, including revalidation after the shared initial-CPU correction.
Final evidence: `.local/evidence/validation/forza-runtime/final-sdk-cycle/`.
The observer attached to the already-launched game only collected exceptions;
it did not launch the title, replace the supervisor or issue its return.

| Transition | Run under `.local/evidence/runs/` | Observed result |
|---|---|---|
| Dashboard → My Games → Forza | `20261014T121150Z-session-0-dash_17559` | Original selection, content hash match; exit 0, no cancellation/timeout |
| Forza → gameplay → Guide/Xbox Home | `20261014T121331Z-session-1-forza_horizon` | Rendering/input, original confirmation and reboot; exit 0, no cancellation/timeout; 344.832 seconds |
| Return to Dashboard → My Games | `20261014T121923Z-session-2-dash_17559` | Same 17559, no repeated intro, profile retained and navigation responsive |

All three children used parent HWND **7341630** and XUID **E00AD9FD043F47E5**
(profile **Forza**). The game imported 1020 bytes, kind Image, flags zero.
The original HUD called `XamLoaderRebootToDash` at **09:19:15.599 local time**,
reason/user/flags zero and 1020 bytes. Five original termination callbacks
completed; eight game audio frames drained, callbacks quiesced at 09:19:15.656,
and execution completed at 09:19:15.662. The attached observer recorded
`exit=0 captured=0`. The supervisor consumed kind Dashboard/flags zero and
the next dashboard imported the same 1020-byte handoff at 09:19:25.276.

Real screenshots, all from that single session:

- `home.png`, `my-games.png`, `forza-selected.png`: original navigation/selection.
- `forza-launch.png`, `start-screen.png`, `single-player.png`, `introduction.png`:
  actual title launch and original menus.
- `scene-progress.png`: car, road, traffic, minimap and HUD, 98 MPH in third gear.
- `brake-response.png`: LT held 2500 ms, car moving in reverse at 22 MPH.
- `acceleration-steering-response.png`: RT held 2500 ms then left steering 500 ms,
  forward motion at 15 MPH in first gear and changed orientation/position.
- `guide.png`, `xbox-home-confirmation.png`: original Guide over the race and Yes.
- `dashboard-return.png`, `my-games-after-return.png`: actual returned dashboard
  and responsive library with all three items still present.

The captures include elapsed simulation time; they are functional input evidence,
not a calibrated performance measurement. `actions.jsonl` records input duration,
timestamp, PID and root/child window handles. `cycle-records.json` records process
results and decoded handoffs. Launch-data SHA-256 is
`403e56dcadd8ac10eb9c7d09862ee1551c12ddc32fba578986b8c6f47bca5e5f` outbound,
and `1cfc9251644a358213d808b8088517b87a9f024bdf46198a3ed692b98b7373a7`
for the original HUD's new return context. These are different contexts by design.

Only after return and a new My Games capture, the test closed the platform window.
The final dashboard report therefore says cancelled/kind WindowClose; that is
explicit cleanup **after** acceptance, not evidence of the game's return.

Normal final preparations: `.local/evidence/logs/dash-main-cpu-preparation.log`,
`castle-main-cpu-preparation.log` and `forza-main-cpu-preparation.log`.
Neither ISO extraction nor original
module versions changed. All 71 SDK patches check successfully; patch 0071's
seven files replay to the locked hashes (`sdk71-main-cpu-replay.log`). Focused tests:
165 assertions/7 title-return cases, 5 assertions/1 SDL-drain case,
790 assertions/20 C7/C8 cases and 127 assertions/4 ownership/version cases.
Earlier metadata and content-launch checks are recorded below.

The persistent-file comparison (`persistent-files-after.json`) retained all
11 tracked paths: both profile stores, installed-content headers and five of the
six Forza save/header files were byte-identical (10/11 hashes unchanged). The
game's own `ForzaProfile` payload changed during play; it was not restored or
replaced by the test. This does not certify a commercial save/load round trip.

Remaining limits are explicit: GPU resolve/fetch warnings and module heap-release
warnings remain; full visual fidelity, campaign and commercial save/load are
outside this acceptance. One earlier AV at the scene transition had no captured
stack and did not recur in two diagnostics or the final observed session; its
cause is not claimed resolved. The original contents of experimental system
region `8E030000` remain unknown. The host rejects a conflicting title-owned
`media:` mapping, and other titles need their own verified presentation boundary
to host Guide; generic launch routing and loader services have no Forza-ID branch.
Reason 72/flag 0x100000 transport is tested but not an accepted dashboard route.
The unprepared Charlie Murder item remains present, but the final library capture
shows its package filename as the display fallback. Its installed header is
byte-identical; launch is correctly rejected for missing prepared AOT. This
cycle does not certify that title's metadata display or execution.

The earlier complete cycle remains in `original-cycle-verified/`, runs
`20261014T112333Z`, `112434Z`, `112947Z`, HWND 8586482. Both observed full cycles
returned with game exit 0. The final run's verbose GPU logs rotated away its
earliest startup lines; process arguments, binary hashes, input/window records,
original loader return, debugger exit and returned-dashboard import are retained.

## Investigation history (earlier partial results)

### Shared-SDK Castle regression: initial processor

The final shared-SDK regression selected Castle in original My Games, but run
`20261014T113944Z-session-1-castle_crashers` failed before its menu. The bounded
diagnostic in `castle-sdk71-crash/crash.txt` reproduced the null heap access at
`8209BDD8`, called by `820A71E8`. Original `820B5EB8` reads PCR+0x10C to select
the heap initialized for the initial processor. Fixing the audio worker on CPU4
had removed one increment from the SDK's global default-CPU round robin, so the
title's main thread no longer started on CPU0. This is a shared loader defect,
not a disc/XBLA metadata or profile difference.

Patch 0071 now gives `PrepareModuleLaunch` an explicit CPU0 creation mask;
host-worker creation order cannot select the initial title processor. The test
`initial title processor does not depend on preceding host workers` alternates
default and CPU4 host workers and checks the main KTHREAD and guest-visible PCR.
Replay with the SDK's LF policy matches all seven locked file hashes in
`.local/evidence/logs/sdk71-main-cpu-replay.log`. Both real cycles were revalidated
after this shared change; the final Forza acceptance is recorded above.

Castle's final regression then passed in normal session HWND **7274664**:
`20261014T115914Z-session-0-dash_17559` →
`20261014T115940Z-session-1-castle_crashers` →
`20261014T120025Z-session-2-dash_17559`. The user operated this session and
explicitly confirmed Castle was normal. The Castle process ran 37.340 seconds,
exit 0, no timeout/cancellation, zero stubs/errors/criticals. Its original
`XamLoaderLaunchTitle(NULL, 0x100)` at 09:00:17.725 dispatched all three
termination callbacks and exported 1020 bytes; the returned dashboard imported
kind Dashboard/flags 0x100 with the same XUID E00AD9FD043F47E5. It then accepted
another original-library request for Forza. That subsequent launch correctly
failed receipt validation because Forza had not yet been prepared with the final
lock. Evidence: `castle-final-cycle/supervisor.log`, `cycle-records.json`, and
the three run reports. This final Castle regression has logs and human
confirmation, **not new menu screenshots from the script**; do not repeat it.
The preceding `115628Z` attempt selected Charlie Murder and was rejected for
missing verified AOT; it is not a Castle failure or a successful game launch.

Final focused tests after the CPU correction: 165 assertions in 7 title-return
cases (`sdk71-main-cpu-return-tests.log`) and 790 assertions in 20 C7/C8 cases
(`sdk71-main-cpu-c7-c8.log`). The latter used an isolated synthetic store under
`.local/tmp/`, not the user's persistent profiles or saves.

### Real launch from My Games and remaining return work

The normal supervisor session `20261014T091835Z-session-0-dash_17559`
launched `20261014T092009Z-session-1-forza_horizon` after selecting Forza in
the original library. Both children used HWND `31394104` and the dashboard's
XUID `E00AD9FD043F47E5`. The request used flags zero and 1020 launch-data bytes;
the supervisor matched the installed XEX hash and launched the prepared AOT.
The normal preparation logs are `forza-cycle-dashboard-preparation.log` and
`forza-cycle-game-preparation.log` under `.local/evidence/logs/`.

Real screenshots and timestamped input/window records are in
`.local/evidence/validation/forza-runtime/original-cycle-1/`:
`home.png`, `my-games.png`, `forza-selected.png`, `single-player.png`,
`gameplay-start.png` and `brake-response.png`. The driving scene shows the
car, road, traffic and HUD. After LT input, the displayed speed fell from
159 to 19 MPH (captures include elapsed simulation time).

The original pause menu's **Quit** explicitly says it returns to the title
screen (`quit-dialog.png`); accepting it did exactly that (`title-after-quit.png`).
It did not issue a loader/dashboard request. Closing the bounded test afterward
was cleanup, not a demonstrated return. The full cycle remains unproven until
the original system Guide can request Dashboard from the game's process.

The in-progress host reuses the 17559 XUI/HUD implementations and verifies that
dashboard's original assets. Forza's original swap wrapper `82C3F200` identifies
the two front-buffer texture headers. Composition runs once before the original
`824079B8` resolve of one of those headers; drawing at the swap itself was too
late. An earlier diagnostic hook at `829F0298` was never reached. Original XUI
initialization exposed a dependency on `8E03860A`, inside the existing 17559
experimental system region. Its original contents remain unknown; this is not
a newly verified system-memory implementation. Diagnostics are separate from
the normal supervisor acceptance session.

### Original Guide return diagnostics (not acceptance)

The first final-preparation attempt (`original-cycle-final/`, runs
`20261014T105645Z-session-0-dash_17559` and
`20261014T105803Z-session-1-forza_horizon`) launched from My Games with the same
HWND `2883814`, XUID and 1020-byte context. Dashboard exited normally, but Forza
exited with `STATUS_ACCESS_VIOLATION` at the introduction/scene transition.
That normal run did not capture a stack; it is a failed acceptance attempt.

`gameplay-crash-1/` then reached gameplay with the same binaries and real launch
data under a debugger, without reproducing that fault. Guide input did not open
the HUD during driving, and the bounded diagnostic required timeout cleanup.
The original engine wrapper was too narrow a frame boundary and its front-buffer
state was thread-local. The integration now observes D3D Swap `829EFB30` directly
and shares the observed buffer headers atomically across submitting threads,
composing once before their matching `824079B8` Resolve.

The incremental build log is `forza-gameplay-guide-boundary-build.log`.
`gameplay-guide-boundary-1/` reached the driving scene, opened the actual Guide
over gameplay (`guide.png`) and accepted its Xbox Home dialog (`yes-selected.png`).
At 08:12:07, all five callbacks completed and audio quiesced before thread
shutdown. The debugger finished with `exit=0 captured=0`, without timeout or
cleanup. This is still a direct diagnostic. The isolated AV above is not claimed
to have been diagnosed merely because these subsequent runs passed.

`guide-host-5/guide-menu.png`, `home-confirmation.png` and `yes-selected.png`
under the same evidence directory show the actual 17559 Guide over Forza and
its original Xbox Home confirmation. The request reaches `XamLoaderRebootToDash`
with reason/user/flags zero and 1020 bytes. All five original termination
callbacks run, but the audio worker subsequently faults at guest `0x14`.
The debugger in `guide-exit-debug-2/crash.txt` identifies the original XAM mixer
callback `819A5878` and the null buffer dereference at `819A4210`. A raw process
exit of zero in an earlier attempt did not establish a clean return.

The first audio join diagnostic (`guide-audio-join-1/threads.txt`) exposed a
worker blocked in `RtlEnterCriticalSection` at `819A5878`. The system UI host
had omitted the audio lock initialization from CRT constructor `81A71288`.
The original mixer setup `819A4570` also allocates the system callback buffers
for guest processor 4, whereas the SDK assigned its audio worker a rotating
processor. The pending correction initializes that original lock, pins the
generic XAudio callback worker to processor 4, and joins callbacks after
termination notifications but before retiring guest threads. This diagnostic
reached its time limit and is not a Dashboard return.

With the lock and processor corrected, `guide-audio-affinity-1/crash.txt`
exposed `STATUS_FLOAT_INEXACT_RESULT` in the original mixer's `819B8888`.
`XHostThread::Execute` omitted the FPSCR host-policy initialization already
performed by `XThread::Execute`; a guest flush-mode update could therefore
unmask host exceptions. Patch 0071 initializes that policy once on host workers.

`guide-audio-fpscr-1/` then opened the original Guide and confirmed Xbox Home.
Its log at 07:37:40 records the reboot request, all five completed callbacks,
eight drained game audio frames, the audio join and normal execution completion.
The debugger reports `exit=0 captured=0`; there was no cleanup input or timeout.
This establishes clean original exit in a direct diagnostic, **not** the full
supervisor cycle. Known heap-release warnings during module teardown remain
separate from the fixed access violation and floating-point exception.

Focused logs in `.local/evidence/logs/`: `sdk71-fpscr-return-tests.log` (120 assertions,
six cases), `sdk71-c7-c8-regression.log` (790 assertions, 20 cases), and the
earlier host ownership/version checks in `sdk71-session-isolation-tests.log`
(127 assertions, four cases). Full original-library acceptance remains pending.

The host is selected from the dashboard preparation receipt, mounts only that
profile's hash-verified originals, and refuses an existing title-owned `media:`
alias rather than replacing it. Titles opt into a frame integration boundary
through their manifest; launch routing remains based on registered XEX hashes.

### Disc versus XBLA launch contracts

| Contract | Forza Horizon disc | Castle Crashers XBLA |
|---|---|---|
| Source | ISO/GDFX extraction, reused | Extracted original LIVE/STFS content |
| Identity | `4D5309C9`, `0.0.0.10`, type `00004000` | `584108B7`, `0.0.1.3`, type `000D0000` |
| Original library | Required missing executable metadata before displaying the entry | Already displayed via existing XBLA path |
| Launch | `XamContentLaunchImageInternalEx`, `XSYSLAUNCH:\default.xex`, flags `0` | Same service, path alias and flags |
| Native transition | Exact installed XEX digest resolves its registered prepared executable | Same supervisor contract |
| Original title Quit | Returns to Forza title screen | Calls `XamLoaderLaunchTitle(NULL, 0x100)` for Arcade dashboard context |

The fix does not reclassify the disc as XBLA or fabricate package licenses.
The return differences explain why copying Castle's menu interaction cannot
complete Forza's cycle. Guide's original Home command needs the system HUD and
`XamLoaderRebootToDash`, independently of the game's own pause-menu Quit.

The first demonstrated blocker is the missing `XamContentGetMetaDataInternal`
contract. Retail 17559 requests metadata before constructing its installed-disc
entry. SDK patch 0069 projects execution identity, installed byte size and the
registered display name into the packed 0x93D6 metadata result, preserving the
original content type. Unknown artwork/publisher/volume information stays absent;
this is a partial extracted-content implementation, not an invented STFS license.
Malformed headers and a mismatched Title ID fail without publishing output.

Focused tests: metadata 68 assertions; existing aggregate/content-launch contract
80 assertions. Logs: `.local/evidence/logs/sdk69-tests.log` and
`.local/evidence/logs/sdk69-aggregate-regression.log`.

Real My Games now lists three entries, including Forza:
`.local/evidence/validation/forza-runtime/metadata-ui/my-games.png`. The subsequent
`disc-launch-bound/selected.png` shows Forza selected. Its `rex.log` records the
original request with title `4D5309C9`, base version `0000000A`,
`XSYSLAUNCH:\default.xex`, flags zero, 33 display-transition frames and the four
completed dashboard termination callbacks. This raw-dashboard diagnostic does
not establish a supervisor handoff or playable scene. No ISO was re-extracted.

Keyboard scripts use explicit digital-pad bindings for deterministic navigation:
the original My Games list consumes keystrokes, so left-stick taps did not move
its selection. The earlier diagnostic attempts selected the first XBLA entry;
they are not Forza launches.

Session identity now comes from the dashboard's profile store for every child,
while each title retains its content/save root and untouched standalone profile
store. The root is reserved against manifest argument overrides. Focused checks:
17 identity assertions, 48 original-return assertions, 47 session-contract
assertions and 895 assertions across 80 synthetic process transitions. These
process tests establish routing/persistence contracts, not a real game cycle.
Logs: `.local/evidence/logs/forza-session-identity-test.log`,
`forza-handoff-regression.log`, `forza-session-tests.log` and
`forza-session-process-test.log` in the same directory.

Original input: Title ID `4D5309C9`, media `2DC7007B`, version/base `0.0.0.10`,
20,856,832-byte XEX, SHA-256
`db40df605ade49a612b35a7a24c38f6004bcb17a88ed6b48288de16df9e3987c`.
Existing extraction reused: 2,432 files, 7,320,461,527 bytes. ISO SHA-256
`83dfc7d0b58b804232385de6f794d9acb92b89a37cf13da14b824b286c824490`.
The title profile checks the XEX hash at configure and before build codegen.
No proprietary executable, generated PPC, save or screenshot is committed.

## AOT audit

Reviewed `04c5174`, `845f08a` and `aa220c0`. Retained the compact output path,
full catalog fingerprint in the owner record, preserved old build trees,
SDK-owned `rexglue.cmake` versus application-owned `x360n_rexglue.cmake`,
manifest-based executable discovery and dependency-based receipts.

Corrected four contracts:

- The generic host included `generated/default/<id>_init.h` although only the
  compact output directory was on its include path. Include the header by name.
- `-BuildDir` could override the checked owner while codegen still wrote into
  the declared build. Generic projects now reject a different native directory;
  manual projects retain explicit alternate builds.
- Direct Ninja builds of `.local/aot/<id>` used the root lease, without title
  exclusion or invalidation. They now use the same title lease and marker as
  registered manual projects.
- Compact receipts must include the matching owner record. Application-owned
  glue is included in the preparation source snapshot to detect mid-build edits.

Focused tests: preparation 8 cases / 73 assertions; generic AOT 2 cases / 41
assertions; receipt publication and build leases passed. The direct generic
build fixture confirms marker creation and rejection of a concurrent producer.
Sandbox-denied renames were rerun against test-owned host temporary directories.
Logs: `.local/evidence/logs/forza-audit-*.log`.

## Original 12 UnresolvedCall diagnostics

Baseline reproduced without hints in
`.local/evidence/runs/20261014T062355Z-forza-baseline-codegen/` (84.4 seconds).
Disassembly: `.local/evidence/logs/forza-ppc-unresolved.txt`.

| Call site | Target | Diagnosis / correction |
|---|---|---|
| `82B13498` | `82B1ABD0` | Frameless getter; original `lis/ori/lwzx/blr`; entry seed |
| `82B1352C` | `82B1ABE8` | Two-word adjustor thunk; entry seed |
| `82B13540` | `82B1ABF0` | Two-word adjustor thunk; entry seed |
| `82B13550` | `82B1ABF8` | Two-word adjustor thunk; entry seed |
| `82B13588` | `82B1AC00` | Two-word adjustor thunk; entry seed |
| `82B13560` | `82B1AC08` | Frameless `addis/addi/blr`; entry seed |
| `82CED38C` | `82CF87F8` | Adjustor between pdata end `82CF87F8` and start `82CF8800` |
| `82F029F4`, `82F1EF58` | `82F1ED30` | Virtual-base adjustor, then tail call at `82F1ED44` |
| `82F5CCEC` | `82F5CCDC` | Internal epilogue of leaf starting `82F5CC68`; SDK patch 0068 |
| `82F76204` | `82F761F4` | Internal epilogue of leaf starting `82F76188`; SDK patch 0068 |
| `82F7631C` | `82F7630C` | Internal epilogue of leaf starting `82F762A0`; SDK patch 0068 |

Patch 0068 fixes GapFill splitting at actual pdata ends when the preceding
function ends with an out-of-line backward branch. A synthetic two-function PPC
fixture fails before the patch with one unresolved call and passes 10 assertions
after it. No internal epilogue is promoted to a function. Castle Crashers
regeneration with the patched producer left all 41 files unchanged
(`.local/evidence/runs/20261014T063747Z-castle-sdk68-codegen/`).

The next layer exposed `82B3B8B0/B8/C0/C8`: three original `lwz/b` wrappers and
one five-instruction leaf. These have entry seeds, with sizes left to CFG
discovery. The final emission also exposed thunk `830EBE90 -> 830ED910`; the
target is an original context-save routine, now seeded rather than emitted as
an unresolved-call trap.

The byte switch at `826B5D6C` lost the `r4 -> r30` index alias and read beyond
the ten entries bounded by `cmplwi cr6,r4,9` at `826B5D3C`. The exact original
ten bytes at `82041470` alternate targets `826B5D70` and `826B5DFC`; the profile
records that table. No data is converted into code. Evidence:
`.local/evidence/logs/forza-ppc-codegen-warnings.txt` and `forza-pdata-overlap-final.log`.

## Code generation and limits

Optimized patched codegen completed in 92.9 seconds: 473 files, zero unresolved
calls. Log: `.local/evidence/logs/forza-sdk68-optimized-codegen.log.err`. The Debug producer
hit its 360-second supervision limit during Register and was terminated; it is
not counted as a successful run.

Two conditional-count branches at `82AD8138` and `82AD813C` target
`82AD836C`, a zero word immediately after pdata end. The generated explicit trap
is retained; no function is invented at that address. Whether those original
paths are reachable at runtime remains unverified. The large generated C++
function warning for `8241A370` describes source size, not a measured execution
failure; its original pdata extent is `8241A370–8243512C`.

Native preparation completed with exit 0: configure 11 s, producer 154 s,
codegen 111 s, reconfigure 13 s, native build/link 329 s (concurrent builds were
running, so these are elapsed times rather than comparative performance data).
The 88,904,192-byte Windows x64 executable has SHA-256
`f0b01eda61aaa3f5f2aceab9c7d654a4c0f8929f6e834ccd630d07ac3b1bd710`.
The normal driver published `forza_horizon.exe.preparation.json` after verifying
its dependency and artifact closure. Log: `.local/evidence/logs/forza-preparation-sdk68.log`.
The final pdata scan checked 57,127 functions with zero overruns. Exact input
identity tests passed 62 assertions (`.local/evidence/logs/forza-identity-targets.log`).

Runtime results are recorded below as they are verified.

## First runtime and original facade modules

`20261014T071628Z-forza-first-runtime` ran the native executable under the
supervisor's crash collector for 18.6 seconds. D3D12 and input initialized, but
there was no demonstrated game frame. The original call at `824813F8`
(`LR=824813FC`) reached `88050388`, export 1 of `XMediaFacade_default.xex`.
The module entry `88050800` was also absent from the native function table.
The captured stack includes `824813C8 -> 82481228 -> 824843E8 -> 8255AE10`.

Both facade XEX files belong to the same disc, title `4D5309C9`, media `2DC7007B`,
version `0.0.0.10`; neither is a dashboard module or a replacement from another
version. They now have native DLL bindings in this title's manifest:

| Original | SHA-256 | Additional verified entry |
|---|---|---|
| XMediaFacade_default.xex | `ed285beefeafcdc90cf64d6085d19c874e3f1e15072c12cdf9ba2237c88e2ce5` | `88055B80`: `lwz/lwz/mtctr/bctr`, export 6's tail destination |
| SpeechFacade_default.xex | `d54f6aae6ae2fd6da9b8c37d48f0a6bb295caa18ad82d0d9df66026141a41594` | `890E2FA0`: six-instruction argument adapter, tail destination of adjustor `890E2FB8` |

The app verifies all three original XEX hashes before runtime construction and
rejects update overlays requiring another analysis profile. Module codegen
completed in 96.9 seconds with 615 total files and no unresolved calls
(`.local/evidence/logs/forza-facade-codegen-3.log.err`). Pdata scans checked 2,104 XMedia
and 4,775 Speech functions with zero overruns.

## Library observations

The production library-registration model passed four assertions and installed
the verified inventory and aggregate header under the Retail 17559 user root
(`.local/evidence/logs/forza-library-registration.log`). This does **not** prove that the
original UI displays or launches the game. The real My Games capture
`.local/evidence/validation/forza-runtime/library-debug.png` still shows only the two
previous XBLA entries. Debug enumeration reports four items; the original
`ContentCache.pkg/CDIs.bin` contains the Forza name, title ID and type `00004000`.
The UI invokes `XamContentGetMetaDataInternal`, still a SDK stub. Its causal
role in filtering Forza is not yet proven; no content type or license was faked.

The derived list cache was preserved with its hash under
`.local/evidence/validation/forza-runtime/content-cache-before/` and regenerated once.
The new cache still contains Forza. That run launched Charlie Murder's original
request and exited; it is not counted as a Forza launch.

The initial hidden-window test emitted sound without a visible frame. Subsequent
scripts use `ProcessStartInfo.CreateNoWindow` for the console supervisor while
allowing the native GUI. Captured subprocesses now also use `CREATE_NO_WINDOW`;
this suppresses auxiliary consoles without hiding title windows.

## Pointer-only entries and first rendered title screen

After the facade bindings, the second run failed at `830B52F0`, reached from
`830B5084` (`LR=830B5088`). This is a real frameless function referenced at
`.rdata:822B486C`, ending before the zero word at `830B53E4`; the previous
adjustor is `830B52E8`. The disc image was scanned with the existing
`check-pointer-targets.ps1` criteria: original pointer reference, alignment,
absence from pdata/generated extents, a preceding boundary and a terminating
instruction before the next known entry within 64 words. 191 candidates passed;
all were checked against original disassembly without unknown/data instructions
in the examined entry-to-terminator range. The other 2,891 candidates were not
promoted. Only starts are supplied in `analysis/pointer_targets.toml`.

Evidence: `.local/evidence/logs/forza-pointer-xrefs.txt`, `forza-pointer-candidates.toml`,
`forza-pointer-disasm.txt`. Codegen completed without unresolved calls in 96.4 s
(203 files changed, 412 unchanged). Pdata validation again checked 57,127 entries
with zero overruns (`forza-pdata-pointer-final.log`).

The incremental diagnostic binary displayed the actual intro and **PRESS START**
at 20 seconds: `.local/evidence/validation/forza-runtime/pointer-diagnostic/20s.png`.
A bounded second run accepted Start and reached the real game's profile
requirement (`start-diagnostic/after-start.png`, `start-diagnostic/55s.png`).
This proves title rendering and input, not menu or gameplay. These raw-executable
diagnostics did not bypass or weaken the supervisor receipt gate; a final normal
preparation is required after the incremental changes.

The local profile **Forza** was persisted during the Retail 17559 session; the user explicitly
selected it for this test. Its existing 33-byte `local-v1.dat` was imported
unchanged into the previously empty Forza profile store; both copies have
SHA-256 `81706351ae0f3eb4db42bd52050f86cbf3303bb9463680be67ab46221fb24705`.
No fabricated identity or signed-in fixture was used. The current architecture
keeps per-title user roots; this copy is a one-time import, not a claim that
platform-wide profile synchronization is implemented.


## Connected-profile crash and fiber diagnosis

The verified preparation in `.local/evidence/logs/forza-final-preparation.log` completed
with exit 0 (12 s configure, 126 s producer, 98 s codegen, 11 s reconfigure,
260 s build). Its supervisor run `20261014T081744Z-forza-profile-runtime`
recognized XUID `E00AD9FD043F47E5`, then crashed before the scripted Start input.
`crash.txt` reports `STATUS_ACCESS_VIOLATION`, guest null read, at
`sub_8310C340` after call `8310C498 -> 8310C640`, reached through
`8310C5A0 -> 82C77E00 -> 82C11C38`. The missing first-use files immediately
beforehand are observations, not proof of the cause.

`8310C640` calls `830EBE90`, the original SwitchToFiber thunk. Its target
`830ED910` saves the current fiber, restores another fiber's GPR/FPR/VMX/LR,
and tails at `830EDD18` to `KeSetCurrentStackPointers` (`831EE58C`). That SDK
service changes the guest SP and PCR fields, but does not switch the C++ call
stack. Returning to `8310C49C` with the other fiber's registers explains the
null r20 access. Executing this guest context switch as an ordinary native
function is not a valid fiber continuation.

The exact XAPI lifecycle was identified from original instructions and bound
using the existing `[rexcrt]` interface: ConvertThreadToFiber `830EBC70`,
ConvertFiberToThread `830EBD00`, CreateFiber `830EBD50`, DeleteFiber `830EBE30`,
SwitchToFiber `830EBE90`. The 0xA50 context, KTHREAD+0x164, argument registers,
stack allocation and original bootstrap `830ED900` match the SDK host-fiber
ABI. This reuses real native context switching, not an ignored call or forced
success. Original disassembly: `.local/evidence/logs/forza-fiber-ppc.txt`.
The focused host-fiber unit test passed 3 assertions (`forza-native-fiber-test.log`).

## Final native build and initial driving scene

The normal driver completed again after adding the fiber bindings:
`.local/evidence/logs/forza-fiber-preparation.log` (12 s configure, 123 s producer,
96 s codegen, 10 s reconfigure, 251 s native build, exit 0). Codegen changed
32 files and retained 583, without unresolved calls. The generated registration
contains all five `rexcrt_*` addresses. The published preparation receipt is
current and the incomplete marker is absent. Final executable SHA-256:
`91ecfe14c80dfcde79ffb5a11ae3bf6366714627abb8bf8451d9be6b6f404e91`.

`20261014T083021Z-forza-fibers-runtime` used the supervisor's normal receipt
gate, the exact disc extraction and the connected **Forza** profile. It passed
the previous `8310C340` fault, reproduced the opening movie and reached the
initial driving scene. Real captures are under
`.local/evidence/validation/forza-runtime/forza-fibers-runtime/`:

- `20s.png`, `after-start.png`, `50s.png`: progressing original introduction.
- `scene.png`: rendered car, road, racing line, minimap, 89 MPH, destination
  2.9 miles away and original RT-to-accelerate prompt.
- `driving-input.png`: after targeted E (RT) and A (left stick) messages,
  progressed road/traffic, 77 MPH, destination 2.1 miles away and the original
  rewind tutorial after a collision. Input was held for about 2.6 seconds,
  with steering for 0.6 seconds; the distance change spans the two captures,
  not just that input interval.

This establishes **initial gameplay rendering with routed input**, not a
completed race, campaign compatibility, performance benchmark or save/load
round trip. Menu navigation was not separately tested after profile sign-in.
The bounded wrapper ultimately terminated the supervisor and its process tree
at its outer deadline; no final `report.json` was produced for that run.
Retained rotating `rex*.log` and screenshots are the evidence, rather than a
claim of clean shutdown or an unhandled-crash report. The previous null-read
crash report remains available separately.

Known remaining limits:

- Forza is registered in the production catalog and aggregate cache, but was
  **not launched from the original My Games UI**. The UI still filters it;
  `XamContentGetMetaDataInternal` is unimplemented, but its causal role has not
  been established. No fake content type/license or alternate launcher added.
- GPU logs still report empty resolves and invalid vertex fetch constant 90,
  including failed draw submissions in the driving scene. No permissive GPU
  bypass was enabled. Visible initial gameplay does not certify all effects.
- Per-title profile stores are separate; the authorized original profile was
  imported byte-for-byte once, without overwriting an existing game profile.
- XMedia/Speech and entrypoint hash guards accepted the exact originals and
  rejected a missing/replaced facade in focused CMake checks
  (`.local/evidence/validation/forza-runtime/input-guard/`).

The final strict production registration check required `forza_horizon` to be
present, prepared and have a nonempty library identity, and validated the
Retail 17559 installation. All 7 assertions passed in one focused case:
`.local/evidence/logs/forza-registration-fibers-final.log`. This validates files/receipts
and registration only; it does not replace the real My Games UI evidence.

A final 60.6-second Retail 17559 check after registration delivered the real
My Games URI and again called `XamContentGetMetaDataInternal`; its capture
`my-games-final/my-games.png` still shows Home during transition and does not
prove a completed library scene. The earlier two-item My Games captures remain
the listing evidence. At requested window shutdown this final dashboard log
recorded a guest read at `0x14` during `KernelState::TerminateTitle`, then exited.
This is a separate dashboard shutdown defect, not a Forza gameplay crash.

## Xenos X2 — 2026-10-15

O patch 0077 corrige a rejeição indevida de draws com fetch comprovadamente
inativo por predicação, preservando o 0076. Forza voltou à condução e o ciclo
My Games → Forza → Guide/Xbox Home → 17559 foi validado em harness isolado,
mesmo HWND e perfil. Não é recertificação dos recibos de produção nem aprovação
de fidelidade integral. [Evidências e limites](../graphics/xenos-fetch-predication.md).
