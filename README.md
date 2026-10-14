# x360-native — Xbox 360 Native Recompilation Platform

Run original Xbox 360 software on modern computers through **static PowerPC
recompilation to native code**, with a compatibility environment based on
[ReXGlue](https://github.com/rexglue/rexglue-sdk).

**Performance-first, correctness-preserving.** We aim for the best native
performance possible while preserving the original programs' correct behavior.
Performance improvements must be backed by measurements, not assumptions.

> Independent, experimental, and under active development. **Windows x64 is the
> currently tested platform**, using D3D12. Linux and macOS are roadmap goals.
> The first technical preview is `v0.1.0-preview.19`; it is experimental.

**v0.1.0-preview.20** adds a persistent game Library
with automatic generic AOT preparation. Import valid ISO/GDFX images, CON/LIVE/PIRS packages
or extracted game folders. Different versions remain separate; exact duplicates
reuse verified content. **Importing a game does not mean it can run.** Unsupported
games stay visible in the desktop Library. Preparing generates a local project,
runs the pinned ReXGlue codegen and compiles it without manual TOML/CMake/C++ edits.
Existing title profiles are optional compatibility overrides. Failed stages and
logs remain available for retry; compilation is not a compatibility claim. Only verified, prepared games enter
the original dashboard's My Games. The corrected preview.20 package also fixes
first-time dashboard preparation when no generated guest modules exist.
See the [release notes](docs/releases/0.1.0-preview.20.md).

[Current capabilities](#current-capabilities) · [Getting started](#download-and-installation) ·
[Roadmap](#roadmap) · [Documentation](#documentation-and-contributing)

## The original experience

The desktop app imports and prepares your content, then starts the original
console interface. The demonstrated reference flow is:

**Original boot animation with sound → Dashboard 17559 → My Games → Castle Crashers → Dashboard.**

That cycle preserves the same host window and returns without replaying the
intro. Original launch and return requests drive the transition. This is a
validated combination of versions, not a claim that every dashboard or game works.

## Current capabilities

Evidence reviewed on **13 October 2026**. Results below combine recorded manual
sessions and automated checks on `main`; they are not a new gameplay certification.

**Compiled** means a native build succeeded. **Boots** means the original program
starts. **Renders** means visible output was verified. **In-game** means gameplay
was reached. **Playable** requires sustained gameplay validation, including relevant
progression and persistence. **Fully Functional** requires all relevant features
and end-to-end flows to be verified. No game below is claimed Playable or Fully
Functional; a timed smoke test cannot establish either status.

| Target or feature | Verified result | Current limits |
|---|---|---|
| **Dashboard 17559 (Metro)** | **Renders** — original text, continuous rendering, keyboard/controller navigation and My Games launching Castle Crashers. | Some settings, profiles and Guide remain incomplete. |
| **Original 17559 boot animation** | **Renders + audio** — original AOT module completes and enters the dashboard. | Validated for the 17559 content set; not a universal intro for other versions. |
| **Dashboard 6717 (Blades)** | **Compiled**; historical rendering and blade navigation. | A later audit smoke crashed; cause unresolved. Matching font missing; diagnostic target, no guided session. |
| **Iruka / Dolphin sample** | **Renders** — automated L1–L5 checks verify startup, rendering and animation. | Technical reference sample; input, audio and long sessions unvalidated. |
| **Castle Crashers 0.0.1.3** | **Renders** — menu, keyboard/controller input, original launch and return verified. Gameplay was user-reported. | Sustained gameplay, save/load and full-game entitlement unvalidated; smoke results do not prove playability. |
| **Motocross Madness** | **In-game** — tutorial movement and objectives observed, including after integration into `main`. | Black terrain and deformed rider; full races and save/load unvalidated. No graphical preparation profile yet. |
| **Avatars** | Experimental opt-in service completes real `Initialize`, asset assembly and mipmap generation in Motocross. | Visual fidelity, catalog animations and persistent user avatars unvalidated; not a universal avatar service. |
| **Original launch and return** | 17559 ↔ Castle cycle verified; launch flags/data preserved, failures distinguished from normal return. | Other title/dashboard combinations require their own validation. |
| **Profiles and Guide** | Partial system UI bindings and original profile scene creation. | No complete original UI flow or real local sign-in/out; per-user saves and achievements unvalidated. |
| **Desktop app** | Windows GUI opens from an extracted preview, imports content and manages the installed library. | Clean Windows installation and the complete first-use experience remain unvalidated. |
| **Content preparation** | Identity/integrity checks, guided AOT preparation and reuse of verified artifacts implemented. | Exact supported versions only; recognition or extraction does not make a title runnable. |

See the [technical audit and subsequent validations](docs/PROJECT_AUDIT.md),
[dashboard evidence](docs/dashboard.md) and [avatar integration results](docs/avatar-system.md).
Older entries in the technical documents are historical; later dated validations
take precedence. Xbox Live, online multiplayer and special peripherals are not validated.

## What makes the platform useful

- **Native AOT execution.** PowerPC code is translated ahead of time and compiled
  for the host. A modified, pinned ReXGlue runtime provides console compatibility.
- **Original dashboard and boot sequence.** The GUI handles preparation; the
  original dashboard and My Games remain the console experience.
- **A library tied to exact content.** Title/version profiles identify executables
  and resources. Original launch requests select a verified, prepared target.
- **Guided import and preparation.** Import extracted files/folders, ISO game
  images (GDFX), or Xbox 360 CON/LIVE/PIRS packages, including extensionless XBLA
  packages. Originals are preserved; incomplete content cannot become ready.
  Unknown games can attempt automatic AOT preparation and report real codegen,
  compilation or runtime limitations.
- **Verified reuse and reproducible preparation.** Pinned tools, SDK revisions and
  patches, isolated builds and preparation receipts connect inputs to native
  executables and their dependencies. Valid artifacts can be reused. Independent
  builds of the documented Windows preview produced byte-identical ZIPs; this is
  evidence for those candidates, not a guarantee for every build or machine.
- **Experimental original system modules.** Selected original routines are
  recompiled AOT for boot animation, XUI and avatar work, with version-specific
  bindings and explicit limits.

AOT does **not** remove all emulation: the runtime still reproduces console
services and translates Xenos GPU commands and shaders. The project does not boot
the complete original Microsoft kernel, XAM or firmware. See the
[architecture](docs/architecture.md) for the division of responsibilities.

## Download and installation

**[Download v0.1.0-preview.19 — First Technical Preview](https://github.com/fabegalo/x360-native/releases/tag/v0.1.0-preview.19).**
Choose the Windows x64 ZIP and its SHA-256 file. This is an experimental pre-release.
Installation on clean Windows, including obtaining missing Microsoft build
components, is **not validated**. See the [release record](docs/FIRST_RELEASE.md)
and [license obligations](docs/LICENSE_AUDIT.md).

The intended experience is **Download → Extract → Launch → Import → Prepare → Play**.
For this technical preview:

1. **Extract the whole Windows x64 application ZIP** and launch `x360-native.exe`.
   It opens without Xbox content. GitHub's source-code archives are not the app.
2. **Import your own authorized content.** Supply the matching original dashboard
   files and games, including games without AOT support. The app identifies versions, checks components
   and shows what is missing. Unpack ZIP/RAR/7z archives first.
3. **Prepare supported content.** The app checks/downloads pinned tools with your
   consent and builds native code locally. Additional Microsoft C++ components may
   require their official installer. Preparing a dashboard does not require a game.
4. **Start Xbox 360**, then use the original **My Games** to launch prepared games.
   The confirmed reference is 17559 with Castle Crashers; use its **Return to
   Arcade** option to return. Closing the platform window ends the session.

The package includes the graphical app, supervisor, content extractor,
preparation sources/scripts, and license notices. It does **not** include games,
dashboards, Xbox fonts, firmware, proprietary assets, generated game code, the
complete ReXGlue runtime or build tools. Preparation obtains tools separately and
uses the content you provide locally.

Keep the **`data/` folder** when updating or removing the app: it contains the local
environment, imported content, preparations and settings. Reopening can reuse
valid preparations. Update recovery has automated coverage, but complete GUI
acceptance and automatic downgrade are not established. See
[desktop preparation and package validation](docs/desktop-preparation.md) and the
[later audit results](docs/PROJECT_AUDIT.md) for the exact scope of each preview.

## Demonstrations

Public screenshots and videos are pending provenance and publication-permission
review. The planned gallery covers Dashboard 17559, the original intro, the desktop
app and verified gameplay. No illustrative mockups are presented as execution evidence.
For now, the linked validation documents record observations and their limits.

## Roadmap

These are **long-term goals**, not completed features or compatibility promises:

- Progressively support original **Blades, NXE, Kinect and Metro dashboards**, including betas.
- Expand compatibility across the Xbox 360 catalog, validating each title and version.
- Complete original local profiles, Guide, avatars, achievements and storage services.
- Support additional original system modules while preserving original launch/return behavior.
- Support **XNA / Xbox Live Indie Games**, including their managed runtime requirements.
- Investigate backward compatibility with **original Xbox (2001) games**.
- Bring the complete application and preparation experience to **Windows, Linux and macOS**, with separate validation per platform.
- Optimize against **real benchmarks**, preserving correctness rather than claiming unmeasured superiority.

The [functional checklist](docs/CHECKLIST_FUNCIONAL_x360-native.md) tracks coverage
and acceptance criteria. Immediate needs include reliable first-use installation,
complete profile/storage flows and resolution of the documented visual defects.

## Tests and quality

The latest recorded full suite after C9/C12, on **13 October 2026**, contains
**1,807 tests: 1,802 passed, 4 skipped and 1 pre-existing failure** in
`unit.Depfile escapes characters make treats as syntax`. All **136 project tests**
passed. The suite is **not fully passing**. These counts were checked against the
recorded log; see [audit E11](docs/PROJECT_AUDIT.md#e11-c9c12--ambiente-dos-testes-e-sessões-contínuas--13102026).

Testing combines PowerPC translation checks, runtime/ABI regressions, content and
artifact integrity checks, and session/import/update tests. Per-title runs add
logs, rendering checks and manual input/gameplay observations. Synthetic session
cycles test handoff contracts; they do not certify long gameplay sessions or saves.

## Documentation and contributing

Most detailed engineering records are currently in Portuguese.

Start with the [contribution guide](CONTRIBUTING.md) to report issues, prepare a
focused change and open a pull request. Report vulnerabilities through the
[security policy](SECURITY.md).

| Document | Purpose |
|---|---|
| [Building](docs/BUILDING.md) | Development setup, content layout, build commands, tests and diagnostics |
| [Architecture](docs/architecture.md) | AOT, runtime boundaries and process model |
| [Technical status](STATUS.md) / [Audit](docs/PROJECT_AUDIT.md) | Milestones, dated evidence, later corrections and unresolved issues |
| [Functional checklist](docs/CHECKLIST_FUNCIONAL_x360-native.md) | Coverage, priorities and acceptance criteria |
| [Compatibility](docs/compatibility.md) / [Title profiles](titles/README.md) | Compatibility stages, historical results and per-version configuration |
| [Dashboard](docs/dashboard.md) / [Avatars](docs/avatar-system.md) | Original UI, transitions, modules and avatar evidence |
| [Iruka](docs/validation-iruka.md) / [Castle Crashers](docs/validation-castle-crashers.md) / [Motocross](docs/validation-motocross-madness.md) | Title investigations; later Motocross results are in the avatar document |
| [Desktop preparation](docs/desktop-preparation.md) | Application workflow and preview validation |

Contributions should reproduce a concrete issue, preserve existing title behavior
and include relevant validation. Report the exact content version/hash, project
revision, platform and reproduction steps; keep proprietary files and private data
out of submissions. Use Conventional Commits. Start with the build guide and the
existing checklist rather than assuming that a compiled title is compatible.

## License and acknowledgments

Original project code is licensed under [BSD-3-Clause](LICENSE). Dependencies and
adapted code retain their own terms and credits; see
[third-party notices](THIRD_PARTY_NOTICES.md) and the [license audit](docs/LICENSE_AUDIT.md).
The maintainer authorized publication of this technical preview. That decision
does not replace third-party license conditions or grant rights over Xbox content;
the audit preserves the scope and limitations of the license evidence.

x360-native is independent and is not affiliated with or endorsed by Microsoft.
Xbox and related names belong to their respective owners. Proprietary Xbox content
is not included: users must supply files they are authorized to use. Recompilation
does not grant redistribution rights over the original software or its assets.

Thanks to ReXGlue and its contributors, and to the Xenia work on which its
compatibility runtime is based.
