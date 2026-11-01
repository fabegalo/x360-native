# Automated desktop releases

`.github/workflows/ci.yml` builds, tests and verifies the desktop distribution on
GitHub-hosted runners for every supported package, reusing
`scripts/packaging/package-desktop.ps1` on all of them:

| Package | Runner | Toolchain |
|---|---|---|
| `windows-x64` (`.zip`) | `windows-2022` | MSVC/Windows SDK of the image, pinned Git/CMake/Ninja/LLVM, `-march=x86-64-v2` |
| `windows-arm64` (`.zip`) | `windows-11-arm` | native ARM64 MSVC/Windows SDK, pinned ARM64 Git/CMake/Ninja/LLVM (`llvm_windows_arm64`), `-march=armv8-a` |
| `linux-x64` (`.tar.gz`) | `ubuntu-24.04` | pinned LLVM/CMake/Ninja and Ubuntu 22.04 sysroot (glibc 2.35), `-march=x86-64-v2` |
| `linux-arm64` (`.tar.gz`) | `ubuntu-24.04-arm` | pinned ARM64 LLVM/CMake/Ninja and Ubuntu 22.04 arm64 sysroot, `-march=armv8-a` |
| `macos-arm64` (`.zip`) | `macos-15` | pinned Xcode, CMake/Ninja; deployment target macOS 13.3, ad-hoc signed |
| `macos-x64` (`.zip`) | `macos-15-intel` | same Xcode and universal CMake/Ninja, `x86_64` only; deployment target macOS 13.3 |

Every package is built and tested natively on a runner of its own system and
architecture (no cross builds), from one code base: `scripts/setup/host-platform.ps1`
names the platform, the locks select its tools, and `release.py` checks the PE
machine, ELF `e_machine` or Mach-O CPU type of every executable. A runner build
is packaging evidence only: it says nothing about gameplay on that platform.
Steam Deck LCD/OLED uses the `linux-x64` package; see
[steam-deck.md](steam-deck.md) for what is automated and what still needs
real hardware.

Pull requests, pushes to `main` and manual runs upload each verified archive and
its `.sha256` as an Actions artifact for 7 days, with version
`<computed>-ci.<run number>`. They never publish. Documentation-only changes skip
the build; the fast `PR checks` workflow (required "Contribution checks") is
separate. A newer commit cancels the run of an older one on the same ref.

The POSIX toolchains (`scripts/packaging/posix-toolchain.lock`, and the sysroots of
`linux-sysroot-jammy.lock` / `linux-sysroot-jammy-arm64.lock`) are cached per platform by lock hash; every component is verified
against its lock receipt when restored. Tag builds bypass the cache and download
and verify every archive. Locally, `sh scripts/packaging/prepare-posix-toolchain.sh
<platform> <dir>` prepares the same tools (WSL default:
`~/.cache/x360n-posix-toolchain/linux-x64`).

## Manual validation

```powershell
gh workflow run ci.yml --ref main
gh run list --workflow ci.yml --event workflow_dispatch
gh run view <run-id> --log-failed
gh run download <run-id> --pattern 'x360-native-*' --dir .local/ci-download
python .github/scripts/release.py verify --platform linux-x64 --directory .local/ci-download --version <version> --commit <run-head-sha> --ref refs/heads/main --run-id <run-id>
```

A failed package job uploads only its build evidence (toolchain, dependency and
compile-command records) for 3 days, never a partial package. Versions use
`MAJOR.MINOR.PATCH[-prerelease]` without the `v` prefix.

## Publishing the next version

Prepare and review `docs/releases/<version>.md` in the same source commit as
the release. Its first line must be a Markdown title. Missing notes fail the
build; generated notes are not substituted. After the maintainer authorizes
the release, push the corresponding `v<version>` tag. Both lightweight and
annotated tags resolve to the exact event commit, including in the publisher.
Tag creation/publication is not part of manual validation.

Only a tag on `main` whose six package jobs all succeeded can start
publication. A separate Ubuntu job downloads the artifacts from that same run,
requires all twelve CI deliverables (six archives and six `.sha256` sidecars), checks their complete
inventories, SHA-256, file modes, PE/ELF/Mach-O architectures, source/SDK/patch/tool identities and run metadata, then
creates a draft release using the committed notes and `--verify-tag`.
**Only the six verified installation archives are attached to the public GitHub Release.**
The checksum sidecars remain CI artifacts for internal verification, not public release assets.
The publisher downloads and byte-compares all six public archives against the locally verified copies
before publishing the draft. GitHub displays the SHA-256 digest of each uploaded asset.
Versions with a prerelease suffix (including `preview`) become pre-releases
and are not marked Latest. Stable versions become regular releases.

An existing Release is never replaced. If upload/verification fails, a draft
may remain for maintainer inspection, but no incomplete Release is published.
Inspect/remove that draft before retrying; do not move a published tag.

## Build inputs and gates

- Checkout uses the event's immutable commit SHA with persisted credentials
  disabled. Actions are pinned by full commit SHA.
- Git, CMake, Ninja and LLVM come from the existing lock files, with archive
  SHA-256 verification. ReXGlue comes from the gitlink matching `deps.lock.json`.
  Only the nine desktop dependencies are fetched at their SDK gitlink revisions;
  full runtime/graphics dependencies and original Xbox files are unnecessary.
- All SDK patches are applied to pristine LF sources and checked against the
  locked final hashes, then replayed in the packager's separate source tree.
- The runner provides Visual Studio 2022's C++ workload and Windows SDK.
  `devshell.cmd` selects the installed Microsoft environment and the pinned LLVM.
  Missing components fail before compilation. Microsoft tool/SDK versions and
  runner image versions are recorded; hosted image updates mean these components
  are identified, not promised to be byte-for-byte immutable.
- LLVM's verified XZ is decoded by the runner's 7-Zip and extracted into staging
  using the existing bootstrap's TAR/layout handling. The Windows Server TAR XZ
  decoder failed to finish in a bounded 15-minute diagnostic attempt. The hosted
  7-Zip binary SHA-256 is recorded; missing 7-Zip fails explicitly. Local bootstrap
  keeps its existing default extractor.
- The existing Release desktop build, focused `desktop.catalog` tests and
  license/content gate run before creating a ZIP. No complete SDK/game suite,
  visual test or Computer Use runs. Original dashboard version isolation remains
  enforced by the source/configuration and the existing tests.
- `package.json` records source commit, tag/ref, workflow/run URL and attempt,
  compiler identity, SDK revision, patch hashes, dependency revisions, downloaded
  tool identities and Microsoft build environment. Complete package/payload
  inventories and the ZIP checksum are verified again after download.
- XEX, games, firmware, original resources, generated original code, runtime
  DLLs and private machine data are rejected by the existing distribution gate.
  On failure only build evidence is uploaded, never a package.

The workflow has `contents: read`; only the tag publisher gets `contents: write`.
Jobs have bounded timeouts and runs for the same ref cannot overlap. The fast
Pull Request workflow is unchanged. A hosted build does not establish clean
Windows first-use, gameplay or broader platform compatibility.

## Verified manual run — October 14, 2026

[Run 38515926098](https://github.com/fabegalo/x360-native/actions/runs/38515926098)
completed successfully in approximately five minutes, building source commit
`3615c7aa277f367187ce0b00d32a078615c95b43` on hosted Windows Server 2022 image
`20261004.326.1`. Dependency preparation took 67 seconds; distribution build,
tests and packaging took 204 seconds. The 17 synthetic release-tooling tests,
focused `desktop.catalog` CTest, original license/content gate (88 components,
three executables), ZIP verification and upload all passed. The publisher was
skipped because the event was `workflow_dispatch`.

Artifact: `x360-native-0.1.0-ci-windows-x64`, GitHub artifact ID `11871269847`.
Its two deliverables were downloaded and independently checked:

| Deliverable | Bytes | SHA-256 |
|---|---:|---|
| `x360-native-0.1.0-ci-windows-x64.zip` | 3,516,821 | `b7c6d196aace73b5ca6be3462403942f67f6328423c3d877149ad762503039f0` |
| `x360-native-0.1.0-ci-windows-x64.zip.sha256` | 103 | Contains the ZIP hash and exact filename |

All 491 ZIP entries, both complete indices, x64 executable headers and source,
SDK, patch and tool provenance were verified after download. Recorded Microsoft
tools: MSVC `14.44.35207`, Windows SDK `10.0.26100.0`; LLVM `22.1.8`, ReXGlue
`f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`, all 67 patches and nine desktop
dependency revisions. No original Xbox content or title runtime is included.

Real implementation issues resolved before success: hidden output directories
in upload-artifact, shell quoting for Microsoft environment collection, PATH
persistence between steps, the Windows TAR XZ decoding stall, and a test's
textual comparison between `runneradmin` and its `RUNNER~1` path alias. That
test now compares physical file identity and still requires the correct
`default.xex` entrypoint in the XContent hierarchy. The failing test withheld
the ZIP; it was not skipped or converted into a successful result.

Local evidence: `.local/evidence/validation/ci-release-success.log` and
`.local/evidence/validation/ci-release-38515926098/`. GitHub artifacts expire after 14
days; the recorded run, source revision and hashes identify this validation.
The Release list was unchanged before/after validation. No release tag was
created and no Release was published. Publication paths were tested with
mocked CLI calls only; a real tag publication awaits maintainer authorization.

## Architectural reference

Studied at Pinyon Shift commit `a72a92a5f215a32875386b824b8b6f0f34e1caf9`:
[release workflow](https://github.com/arcanite24/pinyon-shift/blob/a72a92a5f215a32875386b824b8b6f0f34e1caf9/.github/workflows/release.yml)
and [launcher packager](https://github.com/arcanite24/pinyon-shift/blob/a72a92a5f215a32875386b824b8b6f0f34e1caf9/tools/package-launcher.ps1).
The reusable pattern is a self-contained hosted build, deterministic ZIP with
source provenance and content boundaries, artifacts on manual runs, and a
separate publication job after successful packages on tag pushes. x360-native
keeps its own native desktop build and existing distribution/license gates.
