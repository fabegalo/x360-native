<div align="center">
  <img src="docs/assets/x360-native-banner.png" alt="Abstract illustration of a classic console becoming native processor code" width="100%" />

  # x360-native

  **An open research platform for running Xbox 360 software through static PowerPC recompilation.**

  [![Windows x64](https://img.shields.io/badge/platform-Windows%20x64-107c10)](#current-status)
  [![Experimental](https://img.shields.io/badge/status-experimental-orange)](#current-status)
  [![BSD-3-Clause](https://img.shields.io/badge/license-BSD--3--Clause-blue)](LICENSE)

  [English](README.md) · [Português (Brasil)](README.pt-BR.md) · [Documentation](docs/README.md) · [Contributing](CONTRIBUTING.md)
</div>

> **Experimental software.** Windows x64 is the only platform with end-to-end evidence, results depend on the exact dashboard and game version, and no game is certified as fully functional.

## See it running

| Original Dashboard 17559 · Home | Dashboard 17559 · Games tab |
|---|---|
| ![Dashboard 17559 home screen](docs/assets/screenshots/dashboard-17559-home.png) | ![Dashboard 17559 games tab with the My Games tile](docs/assets/screenshots/dashboard-17559-games.png) |
| **Castle Crashers** · main menu | **Forza Horizon** · open road with HUD |
| ![Castle Crashers main menu](docs/assets/screenshots/castle-crashers-menu.jpg) | ![Forza Horizon car on a mountain road with HUD and minimap](docs/assets/screenshots/forza-horizon-driving.jpg) |
| **Forza Horizon 2** · title screen (Generic AOT) | **Minecraft** · tutorial world (Generic AOT) |
| ![Forza Horizon 2 title screen](docs/assets/screenshots/forza-horizon-2-title.jpg) | ![Minecraft Xbox 360 Edition tutorial world](docs/assets/screenshots/minecraft-tutorial-world.jpg) |

<sub>Real captures from local test sessions, 13–27 October 2026, with the default Xenos D3D12 backend. The Games tab capture uses the experimental Native D3D12 backend. They show states that were reached, not certified gameplay or performance. Game and dashboard imagery belongs to its respective owners.</sub>

## What is x360-native?

x360-native analyzes the original PowerPC code **before** launch, generates C++ and compiles it into a native PC executable. No JIT or instruction interpreter runs in the main path. A compatibility runtime, built on [ReXGlue](https://github.com/rexglue/rexglue-sdk), still recreates the console services and translates Xenos graphics work to the host GPU. Compiling a game does not make it playable.

The **original Xbox 360 dashboard** is a target in its own right: boot, navigation, My Games, launching a prepared game and returning to the dashboard are tracked as compatibility flows. The desktop app imports and prepares your local content; it does not replace the dashboard.

<a id="current-status"></a>

## Current status

Primary configuration: **Windows x64 with the Xenos D3D12 backend.** Other systems and CPU architectures are goals, not supported releases.

| Target | Reached | Not yet certified |
|---|---|---|
| **Dashboard 17559 (Metro)** | Original intro and audio, navigation, My Games, local profiles, Guide, game launch and return | Several system services and settings are partial; one local user in slot 0 |
| **Castle Crashers** | Launch from 17559, menu, input, rendering, return to the dashboard | Campaign, entitlement, save/load |
| **Forza Horizon** | Driving with input and audio, Guide/Xbox Home return to 17559 | Campaign, save/load |
| **Minecraft** | Trial tutorial, movement and block breaking (Generic AOT) | Save/reload, sustained play |
| **Forza Horizon 2** | Initial drive with HUD, minimap and input (Generic AOT) | Campaign, save/reload, sustained play |
| **Motocross Madness** | Tutorial movement and objectives | Full My Games/profile/Guide flow, races, save/load |
| **Charlie Murder** | AOT preparation and My Games routing; reached NetCF bootstrap | XNA initialization, rendering, gameplay |
| **Dashboard 6717 (Blades)** | Builds; historical navigation | Matching font/resource missing; diagnostic only |
| **Native D3D12 / Vulkan** | Experimental execution and synthetic GPU contracts | Gameplay performance; Xenos results do not transfer |

**No listed game is certified as Playable or Fully Functional.** The [compatibility matrix](docs/compatibility.md) has the evidence and limits per title and version, and [STATUS.md](STATUS.md) the dated log.

<details><summary><strong>What the compatibility labels mean</strong></summary>

| Label | Meaning |
|---|---|
| **Identified / Imported** | Content was recognized or added to the local library. |
| **Compiled** | A native build completed. The program may still fail at launch. |
| **Boots** | The original program started; nothing more is implied. |
| **Renders** | A visual scene was observed. Input, audio, progression and persistence may be missing. |
| **In-game** | Some gameplay was reached. This does not imply sustained playability. |
| **Playable** | Sustained gameplay and progression were validated, including input and persistence. No game has this yet. |
| **Fully Functional** | All relevant end-to-end flows are verified. No game or dashboard is claimed fully functional. |

</details>

## Try the preview

**[Download the Windows x64 preview (v0.69.27-preview.25)](https://github.com/fabegalo/x360-native/releases/tag/v0.69.27-preview.25)** · [release notes](docs/releases/0.69.27-preview.25.md)

1. Download the application ZIP and its SHA-256 checksum from the Release page (not GitHub's "Source code" archive).
2. Extract it and run `x360-native.exe`. Keep the `data/` folder next to it: it holds your library, preparations and settings.
3. Import the dashboard files and games you are authorized to use. The app identifies them and reports what is missing or unsupported.
4. Prepare the dashboard and the games you want to try. Pinned tools are downloaded with your consent and the code is compiled locally; some Microsoft C++ build components may need their official installer.
5. Start the original dashboard, open **My Games** and launch a prepared game.

The package contains no Xbox content. Imported or prepared does not mean a game will run, and first use on a clean Windows install is not yet verified. More in the [preparation guide](docs/guides/desktop-preparation.md).

## How it works

![x360-native architecture: local content is statically recompiled, then runs with compatibility services and host graphics translation](docs/assets/architecture.svg)

1. **Bring your own files.** Inputs are identified by SHA-256 and matched to a dashboard or title profile.
2. **Recompile ahead of time.** PowerPC code becomes C++ and then a native build.
3. **Run with the compatibility runtime.** Kernel/XAM services, storage, input and audio are reimplemented; Xenos commands and shaders are translated to the host GPU. The original Microsoft kernel and firmware are not booted.
4. **Validate the real flow** for each dashboard version and game build.

More: [architecture and decisions](docs/architecture.md) · [universal library](docs/guides/universal-library.md) · [content layout](docs/guides/content-layout.md)

## Roadmap

Goals, not promises:

- Dependable first-time installation and preparation on a clean Windows system.
- More original dashboard versions, each with its own matching resources and evidence.
- Better profiles, Guide, XAM services and real game save/load.
- More titles through generic AOT improvements.
- Linux, macOS and ARM64 as separate targets with their own end-to-end validation.

Progress lives in the [functional checklist](docs/CHECKLIST_FUNCIONAL_x360-native.md).

## FAQ

<details><summary><strong>Does "native" mean no emulation?</strong></summary>
No. The game's PowerPC code is compiled ahead of time, but console services and the Xenos graphics pipeline still need compatible implementations; GPU commands and shaders are translated for the host.
</details>

<details><summary><strong>Can I play every Xbox 360 game?</strong></summary>
No. Importing, identifying or compiling a game does not mean it boots or is playable. Check the exact title and version in the compatibility matrix.
</details>

<details><summary><strong>Can I use any dashboard files I find?</strong></summary>
No. Use legally obtained files, and keep each dashboard's original modules with its matching version profile. A component from another version may be incompatible even when its filename or exports match.
</details>

<details><summary><strong>Does it support Linux, macOS or ARM?</strong></summary>
They are goals. Current end-to-end evidence is for Windows x64; building on another platform does not establish runtime support.
</details>

<details><summary><strong>Are saves, Xbox Live, multiplayer and peripherals supported?</strong></summary>
Local profile and storage flows are implemented in part, but commercial game save/load is not certified. Xbox Live, online multiplayer and special peripherals are out of scope.
</details>

<details><summary><strong>Why does a title stop after compiling?</strong></summary>
It may depend on a missing PowerPC function, an unimplemented XAM/kernel export, an unsupported resource or runtime behavior not yet reproduced. These failures stay visible instead of being treated as compatibility.
</details>

<details><summary><strong>Does the project distribute Xbox software?</strong></summary>
No. Never submit games, dashboards, XEX files, fonts, firmware, saves, proprietary assets or code generated from proprietary binaries.
</details>

## Contributing

Documentation, reproducible bug reports, synthetic tests, compatibility evidence and focused fixes are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) first. When reporting a compatibility issue, include the exact content version/hash, project revision, platform, steps and sanitized logs, never proprietary content or personal data. Use Conventional Commits (`docs(readme): clarify compatibility status`). Security reports follow [SECURITY.md](SECURITY.md).

## Documentation

| Start here | What it covers |
|---|---|
| [Project status](STATUS.md) | Current evidence, recent changes and known limits |
| [Compatibility matrix](docs/compatibility.md) | Per-dashboard and per-game results |
| [Architecture](docs/architecture.md) | Static recompilation, runtime boundaries and process model |
| [Dashboard](docs/dashboard.md) | Original interface, modules, profiles and launch/return evidence |
| [Build guide](docs/guides/BUILDING.md) | Developer prerequisites and local build workflow |
| [Performance](docs/performance.md) | Measurement policy and roadmap |
| [Documentation index](docs/README.md) | All engineering documents and reports |

## License

Project code is licensed under [BSD-3-Clause](LICENSE). Dependencies keep their own licenses; see [third-party notices](THIRD_PARTY_NOTICES.md) and the [license audit](docs/reports/maintenance/LICENSE_AUDIT.md). x360-native is independent and not affiliated with or endorsed by Microsoft. Xbox names and marks belong to their owners. Recompilation does not grant rights to redistribute original software or assets.

Thanks to the [ReXGlue project](https://github.com/rexglue/rexglue-sdk) and its contributors, and to the Xenia work on which ReXGlue's compatibility runtime is based.
