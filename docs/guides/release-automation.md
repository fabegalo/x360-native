# Automated Windows x64 releases

`.github/workflows/release.yml` builds the desktop distribution entirely on a
GitHub-hosted `windows-2022` runner. It reuses `scripts/packaging/package-desktop.ps1`;
there is no separate packager or dependency on a maintainer's computer.

## Manual validation

After the workflow is committed on the default branch:

```powershell
gh workflow run release.yml --ref main -f version=0.1.0-ci
gh run list --workflow release.yml --event workflow_dispatch
gh run view <run-id> --log-failed
gh run download <run-id> --name x360-native-0.1.0-ci-windows-x64 --dir .local/ci-download
python .github/scripts/release.py verify --directory .local/ci-download --version 0.1.0-ci --commit <run-head-sha> --ref refs/heads/main --run-id <run-id>
```

The manual event only uploads a ZIP and its `.zip.sha256` to Actions for 14 days.
It cannot invoke the publishing job, even if dispatched against a tag. Versions
use `MAJOR.MINOR.PATCH[-prerelease]` without the `v` prefix.

## Publishing the next version

Prepare and review `docs/releases/<version>.md` in the same source commit as
the release. Its first line must be a Markdown title. Missing notes fail the
build; generated notes are not substituted. After the maintainer authorizes
the release, push the corresponding `v<version>` tag. Both lightweight and
annotated tags resolve to the exact event commit, including in the publisher.
Tag creation/publication is not part of manual validation.

Only a successful packaging job can start publication. A separate Ubuntu job
downloads the artifacts from that same run, checks their complete inventories,
SHA-256, x64 PE headers, source/SDK/patch/tool identities and run metadata, then
creates a draft release using the committed notes and `--verify-tag`. It
downloads and compares both uploaded assets before publishing the draft.
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
  There is no artifact upload step on failure.

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
