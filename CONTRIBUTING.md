# Contributing to x360-native

Small, focused contributions are welcome: documentation, reproducible bug reports,
compatibility evidence, synthetic tests and fixes. Search existing issues first.
For a substantial feature or SDK change, open a proposal before implementing it.
An issue is optional for a small, self-contained fix.

Read [STATUS.md](STATUS.md) for current limits and the
[architecture](docs/architecture.md) for the AOT/runtime boundaries. Later dated
evidence takes precedence over historical entries. C7 (original profile/sign-in
flows) is partial; do not describe it as complete.

## Development setup

Fork the repository on GitHub, then clone your fork and create a focused branch:

```sh
git clone https://github.com/YOUR-USERNAME/x360-native.git
cd x360-native
git remote add upstream https://github.com/fabegalo/x360-native.git
git switch -c fix/describe-the-change
```

Documentation and collaboration changes do not need Xbox content, submodules or a
native build. For C++ work, the tested environment is Windows x64 with VS 2022
Build Tools (C++, MSVC x64 and Windows SDK), Git, CMake and Ninja. The bootstrap
obtains the pinned LLVM toolchain and SDK dependencies. Follow
[Building and development](docs/guides/BUILDING.md) for prerequisites, bootstrap,
configuration and build commands; use `scripts\devshell.cmd` for native builds.
Linux/macOS presets or a successful compilation do not establish runtime support.

Keep dependencies pinned by [deps.lock.json](deps.lock.json). Keep local builds,
inputs and logs under `.local/`. Contributors using coding agents must also read
[AGENTS.md](AGENTS.md) and [CLAUDE.md](CLAUDE.md).

Use the [documentation index](docs/README.md), [repository filetree](docs/filetree.txt)
and [content layout](docs/guides/content-layout.md) when adding files. Keep tests
under `tests/`, tools in the corresponding `scripts/` group, and generated/local
outputs out of tracked source.

## Choose tests that match the change

State the commands, results and limits in the PR. Documentation-only changes
usually need link/content review and the lightweight checks below, not a build.
For code changes, build the affected targets and select tests covering the changed
contract. Add a focused regression test for a behavior fix where practical.
Never validate a failed build using stale binaries.

For example, after successfully building the affected supervisor tests, inspect
the SHA-256 selection before running it:

```powershell
scripts\devshell.cmd ctest --preset win-amd64-debug -N -R "x360n[.]sha256"
scripts\devshell.cmd ctest --preset win-amd64-debug -R "x360n[.]sha256" --output-on-failure --timeout 30
```

Replace the filter with the relevant test names from [tests/CMakeLists.txt](tests/CMakeLists.txt).
The `project` label is broad: it includes local/content tests and the graphical
Iruka regression. Neither `-L project` nor a whole `sdkcheck.*` group is a safe
default for a small edit. Some synthetic SDK tests also open a graphics window.
Review the selected tests and their prerequisites first.

Reuse recorded evidence when a change cannot affect it. Explain unavailable tests
and remaining uncertainty rather than claiming success. Full suites, graphical
tests and long game/dashboard sessions are decisions for changes that need them,
agreed with the maintainer in advance. Agent-driven Computer Use requires explicit
authorization. Bound execution time and supervise any background process.

The PR check runs without native builds. To reproduce it with Python 3.11+ and Git:

```sh
python -m pip install PyYAML==6.0.2
git fetch upstream main
python .github/scripts/check_contributions.py --base upstream/main --title "docs(contributing): clarify setup"
```

The script checks tracked changes against that base (including staged/unstaged
edits); stage new files first. It checks whitespace, UTF-8/newlines and conflict
markers in maintained text, changed JSON/TOML/YAML syntax, contribution metadata
and the PR title. Patch bytes and third-party license texts are exempt from text
formatting checks. It does not certify code correctness or content provenance.

## Report bugs and compatibility

Use **Issues → New issue** and select **Bug Report**, **Game Compatibility**,
**Dashboard Compatibility** or **Feature Request**. No build is needed to report
an issue. For vulnerabilities, follow [SECURITY.md](SECURITY.md) instead.

Include the release/commit, OS/architecture, relevant hardware, exact steps,
expected/observed behavior and short sanitized logs. For games, include Title ID,
version/region/update and executable SHA-256 (or say unknown). For dashboards,
include the exact version/variant, module/resource identities, features used and
the last demonstrated stage. See [title profiles](titles/README.md),
[compatibility evidence](docs/compatibility.md) and [dashboard records](docs/dashboard.md).

Distinguish **Compiled**, **Boots**, **Renders**, **In-game**, **Playable** and
**Fully Functional**, as defined in the [README](README.md#current-status).
Report failures before compilation too. Timed startup is not proof of playability;
save/load, progression, input, audio and return-to-dashboard need their own evidence.
Sanitize usernames, paths, account IDs and secrets in logs. Share screenshots only
when you have permission; do not attach dumps containing proprietary data.

## ReXGlue, patches and title changes

Check behavior against the pinned SDK source, not an assumed API or an old wiki.
Explain whether the fix is general or specific to a title/version. Title-specific
analysis, bindings and workarounds belong in `titles/`; general fixes need a test
that does not rely only on a proprietary game.

Do not submit an unexplained dirty submodule or silently change its revision.
Follow [the local patch format](patches/rexglue-sdk/README.md): preserve the pin,
record reproducible patches and before/after hashes in `deps.lock.json`, and add
a focused `sdkcheck.*` regression that fails without the fix. When an upstream
equivalent exists, cite its commit and use a backport conditional on the pinned
revision through the root CMake integration. Propose SDK upgrades separately with
their affected contracts and validation plan. Reference upstream issues/PRs when
available and preserve human credits and third-party notices.

List all changed patches, SDK revisions, system modules and manifests in the PR.
Keep module/resource associations tied to the exact profile; a 17559 observation
does not establish compatibility with another dashboard. Do not hide missing
behavior by returning success or weaken integrity checks to make a test pass.

## Performance changes

Preserve correctness. Supply a reproducible before/after comparison: project and
SDK revisions, patch set, content hashes (no binaries), build configuration,
OS/CPU/GPU/driver, workload, settings, warm-up, run duration and repetition count.
Report relevant metrics with units and variability (for example frame time,
throughput or memory), logs and functional checks. State limitations and tradeoffs.
An unmeasured hypothesis is not a performance improvement. Agree on any expensive
or graphical measurement before running it; existing evidence may be sufficient.

## Commits and pull requests

Use `type(scope): short description` for commits and PR titles. Allowed types:
`feat`, `fix`, `refactor`, `perf`, `test`, `docs`, `build`, `ci`, `chore`.
Use `!` before the colon for a breaking change and explain the migration in the PR.
Examples: `fix(supervisor): reject invalid paths` and
`docs(dashboard): clarify validation limits`.

Review your diff, commit only your change, and push the branch to your fork:

```sh
git diff --check
git add path/to/changed-file
git commit -m "docs(contributing): clarify setup"
git push -u origin fix/describe-the-change
```

Open a PR from your fork to `fabegalo/x360-native:main`. Fill in the template:
problem, changes, relevant validation, regression risks, integration changes and
performance evidence when applicable. Use `Not applicable` with a short reason
where appropriate. Draft PRs are welcome for incomplete work. Keep unrelated fixes
separate, respond to review and wait for a maintainer to merge; a full suite is
not required for every PR.

## Content and licensing

Never commit or upload games, XEX files, dashboards, firmware, leaked SDKs, keys,
Xbox fonts, proprietary assets, saves or generated code derived from proprietary
binaries. Do not link unauthorized
downloads. Supply authorized content locally under `.local/inputs`; submit hashes,
sanitized diagnostics and synthetic fixtures instead. Inspect staged files before
committing, including archives and logs. Preserve licenses and attribution; see
[LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
