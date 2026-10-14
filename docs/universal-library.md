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
Computer Use. The persistent import environment is `.local/validation/library20`.

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

Focused logs: `.local/logs/universal-tests-final.log`,
`.local/logs/universal-routing-tests.log`, `.local/logs/universal-receipt-tests.log`.
Real imports: `.local/logs/universal-real-{cc,charlie,iso}.log`.
Desktop restart: `.local/validation/library20-final-evidence/`.
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
The production build log is `.local/logs/desktop/28764875/`; the tool check
verified all 67 existing patches and LLVM 22.1.8. ReXGlue emitted 19 files.
An unchanged second preparation reused the verified product without compilation
(`.local/logs/universal-generic-reuse.log`). Two independent desktop processes
showed Ready, with the explicit runtime-compatibility caveat:
`.local/validation/generic-aot-desktop-native/library-pass{1,2}.png`.

The original dashboard enumerated both games, captured in
`.local/validation/library20-launch-original/my-games.png`. Its supervisor
recorded the original Charlie Murder request and matched the verified XEX hash
`0e9d4f0967f8dd121c0c93694b601d30cad66d7a87fb4cbb0b9e3325c3d7b4d1`.
The native game then failed after 5.840 seconds with
`Execute(82163C60): function not in function table` and
`LR=820407E0 CTR=82161600`. This is a real codegen/analysis coverage blocker at
runtime, not proof of compatibility. The session log is
`.local/validation/library20-generic-focused/session.log`; its report is
`.local/runs/20261014T032903Z-session-1-g0a4c6253b34297dc971555c160fb8affde66f554296bd83bae609/report.json`.

The runtime failure remains in the Library after two further desktop restarts;
duplicate reimport preserves the diagnostic. The managed My Games header was
withdrawn while the imported files, executable, receipt and user data remained.
Captures and identical row dumps are in
`.local/validation/library20-runtime-persistence/`, with the passing script log
at `.local/logs/universal-runtime-persistence.log`. Preparing again can retry
after future analysis/runtime corrections. Forza Horizon was imported and
persisted; its AOT build and gameplay were not tested.

Castle Crashers and the 17559 dashboard were rebuilt successfully with verified
receipts (`.local/logs/universal-final-{cc,dashboard}-build.log`). Castle launched
through original My Games and reached its real menu:
`.local/validation/library20-launch-original/castle-menu.png`. The bounded
60-second run recorded zero errors, critical messages, stubs or unimplemented
calls. Its report is
`.local/runs/20261014T032022Z-session-1-castle_crashers/report.json`.
Two historical missing shader opens remain in the report. Commercial save/load
and return-to-dashboard were not newly demonstrated by this session; prior C7/C8
evidence and limitations remain unchanged. No further game session was run
after the maintainer confirmed the Charlie Murder attempt.

## Updating

Use the normal managed environment update from the extracted preview.20 ZIP.
Do not move/delete `.local` to update sources. Catalogs, imports, generated
projects, profiles, saves and preferences remain local. Updated sources may
invalidate preparation receipts; preparing again is expected. C7/C8 and the
67 pinned reproducible SDK patches are retained. A local candidate does not
authorize a push or public Release.
