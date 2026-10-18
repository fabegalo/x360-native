# Universal Library and automatic AOT — preview.20

Import, preparation and runtime compatibility are separate facts. The desktop
catalog accepts valid Xbox 360 game content before an AOT project exists. The
original dashboard remains the launcher. No game bytes are distributed.

## Data and identity

`.local/catalog/<SHA-256>.json` records a sorted path/hash inventory, the imported
XEX identity and available metadata. Package names come from STFS, never an online
guess. Missing fields remain absent/empty. Additional XEX modules retain their
own identity and declared imports. Import does not supply missing modules from
another dashboard version. Matching original resources remain mandatory.

Exact duplicates reuse verified files. Different versions or inventories have
different records and generic build roots. An incomplete record is durable
before copies begin; verified files can be reused after interruption. Publication
is atomic. Malformed records are isolated as error rows. Existing conflicting
files, unrelated data, profiles, saves and settings are preserved. A structurally
valid XEX header establishes identity, not that all instructions can execute.

Optional profiles under `titles/` apply only when the complete expected inventory
matches. Castle Crashers keeps its exact 0.0.1.3 profile and analysis hints. Legacy
imports under profile roots remain visible without destructive migration. A new
matching profile can use an existing catalog import as its preparation source.

## Automatic preparation

For a generic import, **Verify and prepare** creates `.local/aot/<id>/` from
verified content. It emits the manifest, inventory, preparation metadata,
CMake preset, pinned SDK integration and the shared native host. This directory
is generated local state, not a required manual title registration. Titles use
the same build script, LLVM, SDK, runtime, VFS, GPU and launch handoff as existing
projects. No original dashboard module is added to a generic game project.

The stages are project generation, configure, build codegen tool, ReXGlue codegen,
reconfigure, native build and receipt publication. The generic path provides no
guessed function boundaries or success stubs. Missing analysis/runtime support
can fail at the actual affected stage. The build marker retains the stage, exit
status, log path and diagnostic tail. Retrying repairs generated files; verified
final receipts can skip compilation. A failed runtime is recorded separately.

Receipts bind the original XEX, project/configuration, compiler, SDK commit,
patches, dependency graph, codegen producer and produced EXE/DLL hashes. Merely
finding an executable is insufficient. Changes invalidate reuse. The native host
forwards original launch data and return requests through the existing supervisor.

Only prepared game entries are registered in original My Games. Its header is
derived from verified metadata (or an explicitly synthesized installed-disc
entry), not a replacement STFS package or a license grant. The registration name
is content-specific so different versions coexist. A stale managed header is
archived without deleting installed files or saves. The supervisor discovers
prepared generated projects dynamically and revalidates each actual transition.
An incomplete optional project does not block starting the dashboard.

**Ready means the AOT build is verified, not that gameplay is compatible.**
Imported games without a successful preparation stay in the desktop Library.
Known recipe support and generic preparation attempts do not establish Playable,
Fully Functional, commercial save/load, Xbox Live or new platform support.

## Local validation

Tests use repository scripts and native process/window messages only, without
Computer Use. The persistent import environment is `.local/evidence/validation/library20`.

| Source | Verified identity | Import result |
|---|---|---|
| Castle Crashers LIVE and extracted folder | 584108B7 / 0.0.1.3 | 708 files; existing profile selected |
| Charlie Murder LIVE | 58410B83 / 0.0.0.2 | Package name retained; no manual project exists |
| Forza Horizon ISO | 4D5309C9 / 0.0.0.10 | 2,432 files / 7,320,461,527 bytes extracted; zero extraction errors |

The ISO did not supply a verified marketing name through the current metadata
reader; its row uses Title ID/version. Two separate desktop processes verify
identical Library rows, with real PNG captures. Unit coverage includes duplicate
and version handling, malformed/truncated files, CON/LIVE/PIRS and GDFX routing,
interrupted import/retry, conflicting installed bytes, optional exact recipes,
stable project generation/repair, receipt validation and managed My Games removal.

Focused logs: `.local/evidence/logs/universal-tests-final.log`,
`.local/evidence/logs/universal-routing-tests.log`, `.local/evidence/logs/universal-receipt-tests.log`.
Real imports: `.local/evidence/logs/universal-real-{cc,charlie,iso}.log`.
Desktop restart: `.local/evidence/validation/library20-final-evidence/`.
The focused catalog/extraction/identity run passed 23 cases / 235 assertions
(`universal-tests-final.log`). Subsequent generation/routing and runtime-error
tests passed 2 cases / 24 assertions (`universal-runtime-diagnostic-tests.log`);
these overlap earlier generation coverage and are not an additive suite count.
The preparation-receipt PowerShell checks passed. Real prepared registration
and failed-runtime withdrawal each passed 4 assertions in the hidden local test.

### Automatic preparation and original launch

Charlie Murder has no manual project in `titles/`. Import and **Verify and
prepare** in the actual desktop generated its project, ran ReXGlue codegen,
compiled its native executable and published a verified preparation receipt.
The production build log is `.local/evidence/logs/desktop/28764875/`; the tool check
verified all 67 existing patches and LLVM 22.1.8. ReXGlue emitted 19 files.
An unchanged second preparation reused the verified product without compilation
(`.local/evidence/logs/universal-generic-reuse.log`). Two independent desktop processes
showed Ready, with the explicit runtime-compatibility caveat:
`.local/evidence/validation/generic-aot-desktop-native/library-pass{1,2}.png`.

The original dashboard enumerated both games, captured in
`.local/evidence/validation/library20-launch-original/my-games.png`. Its supervisor
recorded the original Charlie Murder request and matched the verified XEX hash
`0e9d4f0967f8dd121c0c93694b601d30cad66d7a87fb4cbb0b9e3325c3d7b4d1`.
The native game then failed after 5.840 seconds with
`Execute(82163C60): function not in function table` and
`LR=820407E0 CTR=82161600`. This is a real codegen/analysis coverage blocker at
runtime, not proof of compatibility. The session log is
`.local/evidence/validation/library20-generic-focused/session.log`; its report is
`.local/evidence/runs/20261014T032903Z-session-1-g0a4c6253b34297dc971555c160fb8affde66f554296bd83bae609/report.json`.

The runtime failure remains in the Library after two further desktop restarts;
duplicate reimport preserves the diagnostic. The managed My Games header was
withdrawn while the imported files, executable, receipt and user data remained.
Captures and identical row dumps are in
`.local/evidence/validation/library20-runtime-persistence/`, with the passing script log
at `.local/evidence/logs/universal-runtime-persistence.log`. Preparing again can retry
after future analysis/runtime corrections. Forza Horizon was imported and
persisted; its AOT build and gameplay were not tested.

Castle Crashers and the 17559 dashboard were rebuilt successfully with verified
receipts (`.local/evidence/logs/universal-final-{cc,dashboard}-build.log`). Castle launched
through original My Games and reached its real menu:
`.local/evidence/validation/library20-launch-original/castle-menu.png`. The bounded
60-second run recorded zero errors, critical messages, stubs or unimplemented
calls. Its report is
`.local/evidence/runs/20261014T032022Z-session-1-castle_crashers/report.json`.
Two historical missing shader opens remain in the report. Commercial save/load
and return-to-dashboard were not newly demonstrated by this session; prior C7/C8
evidence and limitations remain unchanged. No further game session was run
after the maintainer confirmed the Charlie Murder attempt.

## Updating

### Compact generic build paths (2026-10-14)

Forza's next preparation failed at `1-configure` in SDL's CPU-detection
`try_compile`: the old build path used the complete 64-character title identifier
and its nested temporary directory reached 265 characters. Evidence is the
installed desktop log `37403250` and its `x360n-build-1-configure.log`.

Generic builds now use `.local/build/aot/<first 17 title-id characters>/`, while
the catalog, manifest filename, title name, original inventory, user data and
final executable filename retain their existing identities. The full title and
inventory identity are recorded in `.x360n-aot-owner`; a prefix collision or an
unidentified existing build directory fails before replacement. Generated code
lives in that build's `generated/default/`, avoiding the long source path too.
Previous build files remain preserved at their old locations; re-preparation
produces the newly declared native path.

### Existing local content (2026-10-18)

Importing an extracted game already inside this environment's
`.local/inputs/games/` verifies and registers its existing directory instead of
copying the disc again. Links and paths outside that root cannot use this path.
A root bound to another catalog inventory keeps the isolated import behavior;
the existing record is never silently rebound. External imports still copy and
verify content normally. The focused import tests passed 12 cases / 182 assertions.

For a source checkout, `scripts/start-desktop.cmd` opens the separate desktop
build with this repository as its environment, falling back to the full Debug
tree when the separate executable is absent. Prepare the separate Debug build with
`scripts/devshell.cmd cmake -S . -B .local/build/desktop-only -DX360_DESKTOP_ONLY=ON -DCMAKE_BUILD_TYPE=Debug`,
then `scripts/devshell.cmd cmake --build .local/build/desktop-only --target x360-native`.
The desktop does not require launching every game after each change. Use focused
checks for affected contracts and manual game sessions when requested. Readiness
continues to require real preparation receipts; Xbox-original XBE content cannot
use the Xbox 360 XEX/PowerPC pipeline.

The build driver reads the generated preset's `binaryDir`, verifies ownership,
retains codegen source/stamp files during native invalidation, and certifies
dependencies from both legacy and compact codegen locations. Automatic launch
discovery follows the manifest's declared `native_exe` and its receipt. An orphan
executable under the compact AOT build root remains unlaunchable. No dashboard
module, original content or SDK revision is substituted by this path change.

Focused evidence: `.local/evidence/logs/forza-aot-short-focused-tests.log` and
`.local/evidence/logs/forza-aot-short-receipt-tests.log`. Real preparation and delivery of
this correction were exercised in the affected installed environment. Project
generation passed 11 assertions (`.local/evidence/logs/forza-aot-real-generation.log`).
The real configure passed in 406 seconds and the codegen producer built in
148 seconds. The SDK then rejected Forza's analysis after 86 seconds with
12 `UnresolvedCall` errors at 11 distinct destinations. The game remains
incomplete and was neither compiled nor started. Evidence is
`.local/evidence/logs/forza-aot-real-preparation.log` and the installed compact build's
`x360n-build-2-codegen.log`.

That execution also demonstrated that the pinned CLI refreshes
`generated/rexglue.cmake` even when its template does not support an absolute
entrypoint output path. The native project therefore includes the separately
owned `generated/x360n_rexglue.cmake`; codegen may refresh its own file without
replacing the integration used by CMake. A focused test covers that refresh and
optional registration without recursive discovery
(`.local/evidence/logs/forza-aot-producer-glue-tests.log`, 31 assertions).

Hotfix.3's package and source update were applied with all four persistent
catalog/profile/save/preference hashes preserved:
`.local/evidence/logs/forza-aot-installed-update.log`. Its package gates passed, including
88 license/content components and three executables; source commit `04c5174`.
The earlier failed build and user data remain at their original paths. The
final package `0.1.0-preview.20-hotfix.4` was generated from commit `845f08a`,
passed its distribution and license/content gates, and was applied to the same
installation. ZIP SHA-256
`1d0f6efa4525a3da49c95f907b6a3dd5087c4d08ef521643e0ebf46ee51b1a02`.
Logs: `.local/evidence/logs/forza-aot-final-package.log` and
`.local/evidence/logs/forza-aot-final-installed-update.log`. All four persistent hashes
remained identical; package/source backup is
`.local/archive/preserved/forza-aot-hotfix4-package/`.

Final project generation passed 11 assertions
(`.local/evidence/logs/forza-aot-final-real-generation.log`). A focused warm configure
with the SDK-refreshed glue still present passed in 3.0 seconds of configure
plus 1.8 seconds of generation (`.local/evidence/logs/forza-aot-final-configure.log`).
CMake still warns about long object filenames from the complete native title
name; native Forza compilation has not been reached. The real `2-codegen`
failure marker and its log were retained for the Library; no success receipt or
My Games registration was synthesized, and the unchanged failing analysis was
not repeated. Forza requires specific analysis support before it can prepare.

### Long installed paths: Forza Horizon import recovery (2026-10-14)

The user's extracted preview.20 installation lived at
`C:\Users\fabeg\Desktop\Xbox 360 Native\x360-native-0.1.0-preview.20-windows-x64`.
Its ISO extractor completed 2,432 files / 7,320,461,527 bytes with zero errors
(`data/environment/.local/evidence/logs/desktop/35410437/extraction.log`). Both ISO and
folder imports then failed in the shared verified-copy stage: a final asset
filename could fit the traditional Windows limit while the longer
`.x360n-import-<filename>/verified.tmp` path did not. Saving MSVC's localized
`filesystem_error::what()` as UTF-8 JSON produced a secondary
`invalid UTF-8 byte` error, leaving the durable catalog incomplete.

Import staging, retry validation, copy and atomic publication now use extended
Windows paths, including UNC conversion. Content roots, inventory identities,
hash verification, refusal to overwrite different bytes and dashboard-version
isolation remain in force. Filesystem failures persist their numeric error code
and Unicode paths as UTF-8. Serialization runs before opening the JSON staging
file, so an encoding failure cannot truncate it.

A synthetic 250-character final filename reproduced the failure before the fix:
`.local/evidence/logs/forza-import-long-path-before.log`. Six focused tests passed with
31 assertions after the fix, covering long staging paths, interrupted staging,
catalog diagnostics/retry, conflicting destinations and mismatched original
modules: `.local/evidence/logs/forza-import-focused-tests-host.log`. The restricted
sandbox blocked extended-path rename/publication; the successful run used the
host against test-owned temporary files.

The production model resumed the real folder import into that existing
installation, reused verified copies and checked all 2,432 files. The persistent
record `72c219d7fa539e44a3b1b9d84c53b04d99cd95a8b911d75e525d47b26efb8eff`
is complete; Title ID `4D5309C9`, version `0.0.0.10`. The bounded 180-second
process passed 8 assertions (`.local/evidence/logs/forza-installed-folder-recovery.log`)
without opening the UI, preparing AOT or running a game. The earlier successful
ISO extraction supplies evidence for the unchanged extractor; it was not
repeated. Forza AOT/runtime/gameplay compatibility remains untested.

The local package `0.1.0-preview.20-hotfix.2` was produced from commit
`990d8727f3a3d0b8b1d693aa0aa19be2af5ccbfc`. Its distribution test and
license/content gates passed (`.local/evidence/logs/forza-import-hotfix-package.log`);
ZIP SHA-256 `e4a57013494f959ef9b7836bce365d374bd81a005614dbd8b7b86ce41a54fed9`.
After the user closed the app and authorized application, the same installation
received the verified package and 432 indexed source files. Four persistent
catalog/profile/save/preference files retained their SHA-256 values. The previous
package and changed sources were preserved under
`.local/archive/preserved/forza-import-hotfix2-package/`; application evidence is
`.local/evidence/logs/forza-import-installed-update.log`. The user reported antivirus
removal of the standalone native update helper; deployment therefore used
PowerShell with verified indexes, backups and the environment's exclusive lock,
without changing antivirus settings. The updated UI was not opened by an agent.

Use the normal managed environment update from the extracted preview.20 ZIP.
Do not move/delete `.local` to update sources. Catalogs, imports, generated
projects, profiles, saves and preferences remain local. Updated sources may
invalidate preparation receipts; preparing again is expected. C7/C8 and the
67 pinned reproducible SDK patches are retained. A local candidate does not
authorize a push or public Release.

## Dependent AOT modules — 17 October 2026

Generic preparation now inspects the verified imported inventory for XEX images,
including files named `.dll`. Executable DLLs receive their own generated native
targets and the SDK's module registry. Original relative guest paths, hashes,
image bases and entry points are retained. Ambiguous or unsupported images fail
preparation; there is no search in sibling installations or dashboard versions.
Resource-only images require bounded metadata validation; a zero entry point
alone does not exclude executable code. Optional analysis entries are selected
by the original module's complete SHA-256 in `titles/common/module_analysis.toml`.

Multi-module generic projects opt into the SDK's host function table for the
entrypoint and DLLs, so auxiliary tables do not occupy the next original image.
Existing single-module projects retain their previous dispatch mode. Receipts
cover every declared original module, the generated project, native dependency
closure and shared recipe. A guest XEX named `.dll` remains an input and is never
staged as a native DLL. A partial build does not receive a success receipt.

Charlie Murder's historical `0x82163C60` failure was reproduced in a frozen
diagnostic copy. Its real HostLoader DLL now compiles and registers 1,461
functions, including that entry point. The complete 32-image codegen succeeds,
but the RuntimeHost C++ build fails on a speculative switch boundary. Charlie
therefore remains incompletely prepared; My Games execution and XNA gameplay
have not been validated. See the [implementation and evidence report](../reports/aot-dependencies-2026-10-17.md)
and the separate [Castle boundary investigation](../reports/castle-tail-boundary-2026-10-17.md).

Focused builds produced new receipts for Iruka, Motocross, Castle and 17559.
The original My Games launched Castle and Motocross and both returned through
the original Guide/Xbox Home confirmation. Old content folders without active
headers still appear under hash names; attempting Charlie's old folder was
refused with `no verified AOT profile`. No Charlie process started, and its
inactive header was preserved. These short checks do not certify gameplay,
commercial saves, complete XAM support or performance; detailed diagnostics and
preservation hashes are in the report.
