# Steam Deck (LCD/OLED)

Steam Deck LCD and OLED are a P0 target of the **Linux x64** package. They use
the same `x360-native-<version>-linux-x64.tar.gz` as any other Linux x64 system:
there is no separate build, branch or implementation. Platform differences stay
in `src/desktop/model/handheld.{h,cpp}`.

## What the package does

- `DetectHandheld()` identifies the Deck by its DMI identity
  (`/sys/class/dmi/id/sys_vendor` = `Valve`, `product_name` = `Jupiter` for LCD,
  `Galileo` for OLED). When the DMI identity cannot be read, Steam's `SteamDeck=1`
  marker reports a Deck of unknown model.
- SteamOS Gaming Mode is detected as a gamescope session
  (`XDG_CURRENT_DESKTOP=gamescope` or `GAMESCOPE_WAYLAND_DISPLAY`). The launcher then
  starts fullscreen, because gamescope shows a single fullscreen surface. Desktop
  Mode keeps the normal resizable window.
- A launch by the Steam client (`SteamGameId`/`SteamAppId`) is reported as Steam
  Input. Steam Input exposes the built-in controls as a standard gamepad, which the
  launcher already navigates (SDL3 gamepad + Dear ImGui gamepad navigation).
- Diagnostics show the model, Gaming Mode and Steam Input lines, plus the Vulkan
  device the loader reports (on the Deck: AMD Van Gogh/Sephiroth APU through RADV).

The packaging requirements already apply: glibc 2.35 or later (SteamOS 3 ships a
newer glibc), x86-64-v2 and the C++ runtime linked statically.

## Automated tests (CI, every run)

| Test | Covers |
|---|---|
| `tests/desktop/handheld_test.cpp` (`desktop.catalog`) | LCD/OLED/unknown classification, other vendors refused, Gaming Mode vs Desktop Mode, fullscreen decision, diagnostics text |
| `linux-x64` package job | build, distribution tests, glibc floor, dependency allowlist, license gate, archive verification |

These use synthetic environments and DMI values. They do not run on a Deck.

## Real-hardware homologation (pending)

Nothing below has been run yet on a Steam Deck. Every item stays pending until
someone records the result for a specific package version, Deck model and
SteamOS version.

| Item | LCD | OLED |
|---|---|---|
| Package extracts and `./x360-native` starts in Desktop Mode | pending | pending |
| Added as a non-Steam game, starts fullscreen in Gaming Mode | pending | pending |
| Built-in controls navigate the launcher through Steam Input | pending | pending |
| Steam on-screen keyboard (Steam + X) types into launcher text fields | pending | pending |
| Diagnostics report the model, Gaming Mode and the RADV Vulkan device | pending | pending |
| Suspend/resume with the launcher open | pending | pending |
| Self-update from the published `linux-x64` package | pending | pending |
| Preparation (AOT) of a title on the Deck | pending | pending |
| Dashboard or game session: video, audio, input | pending | pending |

The environment variable names and the Steam on-screen keyboard behavior are
working hypotheses taken from public SteamOS/gamescope behavior, to be confirmed
on hardware. A green `linux-x64` CI run proves the package builds and passes its
tests. It says nothing about gameplay on the Deck.
