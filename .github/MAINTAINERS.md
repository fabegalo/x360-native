# GitHub collaboration settings

Read-only inspection on **2026-10-13** using the GitHub REST API:

| Setting | Observed |
| --- | --- |
| Default branch | `main` |
| Issues | Enabled |
| Automatic merge | Disabled |
| Classic protection on `main` | Not protected (`protected: false`; protection endpoint returns 404) |
| Repository rulesets / effective rules on `main` | Empty lists |
| Private vulnerability reporting | Disabled |

No administrative settings were changed. These files and local commits do not
activate branch protection or private reporting.

## Recommended configuration (requires maintainer approval)

Use one active branch ruleset targeting `main` (or equivalent classic protection):

- Require a pull request with **one approving review**; dismiss stale approvals
  when new commits change the diff and require resolution of review conversations.
- Require a code owner review for community changes; `.github/CODEOWNERS` assigns
  the repository to `@fabegalo`.
- Block force pushes and branch deletion. Avoid routine administrator bypass.
- After successful real PR runs, require the **Contribution checks** job from the
  **PR checks** workflow, selecting the GitHub Actions check in the settings UI.
  Do not make a never-run check mandatory; first verify a fork PR and a title edit.
- Keep **Allow auto-merge** disabled. Merge manually after review.

A PR author cannot approve their own PR. With only one maintainer, the owner's PRs
need another trusted reviewer; requiring the sole code owner on their own PR also
needs an explicit policy decision. Resolve this before enforcement (add a trusted
reviewer/code owner or define a narrowly scoped, audited exceptional bypass).
Do not silently bypass community PR review or require a full native build as a gate.

Enable **Private vulnerability reporting** under the repository security settings,
then verify the private report entry point and update the dated note in
`SECURITY.md`. Until enabled, that policy explains how to request the channel
without publishing vulnerability details. No personal email address is needed.

## Lightweight CI scope

One Ubuntu job, three-minute timeout, read-only `contents` permission, cancellation
of superseded runs per PR, no secrets, no persisted checkout credentials, no
submodules and no LFS. Checkout is pinned to a full commit. Python is supplied by
the runner; the only package installed is pinned PyYAML for safe YAML parsing.

The `pull_request` trigger includes title edits; title text is passed through an
environment variable. The diff uses the test merge commit's first parent, so it
covers the whole PR without fetching full history. No path filter suppresses the
check, which allows it to become required later. Fork runs may first need approval
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
