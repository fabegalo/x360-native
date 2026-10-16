# GitHub collaboration settings

Read-only GitHub REST inspection on **2026-10-15**. No settings were changed.

| Setting | Observed |
|---|---|
| Default branch / Issues | `main` / enabled |
| Automatic merge | Disabled |
| Effective main rules | Pull request required; review conversations must be resolved; zero required approving reviews; extra approval for unattributed changes enabled |
| Required check | **Contribution checks**, GitHub Actions; branch must be up to date |
| History and deletion | Non-fast-forward updates and branch deletion blocked |
| Private vulnerability reporting | Disabled; follow [SECURITY.md](../SECURITY.md) |

The branch reports `protected: true`. Classic protection returns 404 because
protection is supplied by repository rulesets (25031546 and 25031552).
Inspect `rules/branches/main` as well as the classic protection endpoint.
Use a PR and wait for the required check; do not bypass rules or force-push.
Enabling private reporting or changing review requirements needs a separate
maintainer decision. The current rules do not require a second approving reviewer.

## Lightweight CI scope

One Ubuntu job, three-minute timeout, read-only `contents` permission, cancellation
of superseded runs per PR, no secrets, no persisted checkout credentials, no
submodules and no LFS. Checkout is pinned to a full commit. Python is supplied by
the runner; the only package installed is pinned PyYAML for safe YAML parsing.

The `pull_request` trigger includes title edits; title text is passed through an
environment variable. The diff uses the test merge commit's first parent, so it
covers the whole PR without fetching full history. No path filter suppresses the
check, which is currently required by the main ruleset. Fork runs may first need approval
under GitHub's Actions policy. Merge conflicts must be resolved before GitHub can
run this merge-based workflow.

Checks cover Conventional Commit PR titles, changed text whitespace/encoding/EOF
and conflict markers, changed JSON/TOML/YAML parsing, required contribution files,
basic issue-form structure, CODEOWNERS and local contribution-guide links. Patch
files and upstream license bytes retain their existing formatting. This is basic
hygiene, not a complete GitHub schema validator, proprietary-content scanner or
functional regression suite. Review workflow changes as executable code.

No SDK builds, codegen, game/dashboard execution, graphical tests, large toolchain
downloads or Computer Use run here. Wider tests remain a per-change decision.

References: [protected branches](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-protected-branches/about-protected-branches),
[issue forms](https://docs.github.com/en/communities/using-templates-to-encourage-useful-issues-and-pull-requests/syntax-for-issue-forms)
and [PR events](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#pull_request).
