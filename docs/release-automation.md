# Automated Windows x64 releases

`.github/workflows/release.yml` builds the desktop distribution entirely on a
GitHub-hosted `windows-2022` runner. It reuses `scripts/package-desktop.ps1`;
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

## Architectural reference

Studied at Pinyon Shift commit `a72a92a5f215a32875386b824b8b6f0f34e1caf9`:
[release workflow](https://github.com/arcanite24/pinyon-shift/blob/a72a92a5f215a32875386b824b8b6f0f34e1caf9/.github/workflows/release.yml)
and [launcher packager](https://github.com/arcanite24/pinyon-shift/blob/a72a92a5f215a32875386b824b8b6f0f34e1caf9/tools/package-launcher.ps1).
The reusable pattern is a self-contained hosted build, deterministic ZIP with
source provenance and content boundaries, artifacts on manual runs, and a
separate publication job after successful packages on tag pushes. x360-native
keeps its own native desktop build and existing distribution/license gates.
