# Forza Horizon — Windows x64 investigation, 2026-10-14

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
Logs: `.local/logs/forza-audit-*.log`.

## Original 12 UnresolvedCall diagnostics

Baseline reproduced without hints in
`.local/runs/20261014T062355Z-forza-baseline-codegen/` (84.4 seconds).
Disassembly: `.local/logs/forza-ppc-unresolved.txt`.

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
(`.local/runs/20261014T063747Z-castle-sdk68-codegen/`).

The next layer exposed `82B3B8B0/B8/C0/C8`: three original `lwz/b` wrappers and
one five-instruction leaf. These have entry seeds, with sizes left to CFG
discovery. The final emission also exposed thunk `830EBE90 -> 830ED910`; the
target is an original context-save routine, now seeded rather than emitted as
an unresolved-call trap.

The byte switch at `826B5D6C` lost the `r4 -> r30` index alias and read beyond
the ten entries bounded by `cmplwi cr6,r4,9` at `826B5D3C`. The exact original
ten bytes at `82041470` alternate targets `826B5D70` and `826B5DFC`; the profile
records that table. No data is converted into code. Evidence:
`.local/logs/forza-ppc-codegen-warnings.txt` and `forza-pdata-overlap-final.log`.

## Code generation and limits

Optimized patched codegen completed in 92.9 seconds: 473 files, zero unresolved
calls. Log: `.local/logs/forza-sdk68-optimized-codegen.log.err`. The Debug producer
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
its dependency and artifact closure. Log: `.local/logs/forza-preparation-sdk68.log`.
The final pdata scan checked 57,127 functions with zero overruns. Exact input
identity tests passed 62 assertions (`.local/logs/forza-identity-targets.log`).

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
(`.local/logs/forza-facade-codegen-3.log.err`). Pdata scans checked 2,104 XMedia
and 4,775 Speech functions with zero overruns.

## Library observations

The production library-registration model passed four assertions and installed
the verified inventory and aggregate header under the Retail 17559 user root
(`.local/logs/forza-library-registration.log`). This does **not** prove that the
original UI displays or launches the game. The real My Games capture
`.local/validation/forza-runtime/library-debug.png` still shows only the two
previous XBLA entries. Debug enumeration reports four items; the original
`ContentCache.pkg/CDIs.bin` contains the Forza name, title ID and type `00004000`.
The UI invokes `XamContentGetMetaDataInternal`, still a SDK stub. Its causal
role in filtering Forza is not yet proven; no content type or license was faked.

The derived list cache was preserved with its hash under
`.local/validation/forza-runtime/content-cache-before/` and regenerated once.
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

Evidence: `.local/logs/forza-pointer-xrefs.txt`, `forza-pointer-candidates.toml`,
`forza-pointer-disasm.txt`. Codegen completed without unresolved calls in 96.4 s
(203 files changed, 412 unchanged). Pdata validation again checked 57,127 entries
with zero overruns (`forza-pdata-pointer-final.log`).

The incremental diagnostic binary displayed the actual intro and **PRESS START**
at 20 seconds: `.local/validation/forza-runtime/pointer-diagnostic/20s.png`.
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

The verified preparation in `.local/logs/forza-final-preparation.log` completed
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
success. Original disassembly: `.local/logs/forza-fiber-ppc.txt`.
The focused host-fiber unit test passed 3 assertions (`forza-native-fiber-test.log`).

## Final native build and initial driving scene

The normal driver completed again after adding the fiber bindings:
`.local/logs/forza-fiber-preparation.log` (12 s configure, 123 s producer,
96 s codegen, 10 s reconfigure, 251 s native build, exit 0). Codegen changed
32 files and retained 583, without unresolved calls. The generated registration
contains all five `rexcrt_*` addresses. The published preparation receipt is
current and the incomplete marker is absent. Final executable SHA-256:
`91ecfe14c80dfcde79ffb5a11ae3bf6366714627abb8bf8451d9be6b6f404e91`.

`20261014T083021Z-forza-fibers-runtime` used the supervisor's normal receipt
gate, the exact disc extraction and the connected **Forza** profile. It passed
the previous `8310C340` fault, reproduced the opening movie and reached the
initial driving scene. Real captures are under
`.local/validation/forza-runtime/forza-fibers-runtime/`:

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
  (`.local/validation/forza-runtime/input-guard/`).

The final strict production registration check required `forza_horizon` to be
present, prepared and have a nonempty library identity, and validated the
Retail 17559 installation. All 7 assertions passed in one focused case:
`.local/logs/forza-registration-fibers-final.log`. This validates files/receipts
and registration only; it does not replace the real My Games UI evidence.

A final 60.6-second Retail 17559 check after registration delivered the real
My Games URI and again called `XamContentGetMetaDataInternal`; its capture
`my-games-final/my-games.png` still shows Home during transition and does not
prove a completed library scene. The earlier two-item My Games captures remain
the listing evidence. At requested window shutdown this final dashboard log
recorded a guest read at `0x14` during `KernelState::TerminateTitle`, then exited.
This is a separate dashboard shutdown defect, not a Forza gameplay crash.
