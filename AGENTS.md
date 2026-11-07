# Guide for AI maintainers

Single policy for Claude, Codex and other agents. `CLAUDE.md` is only a pointer here. Read the current request, then only the relevant code and current status/next-action section. Historical reports describe evidence, not permanent architecture restrictions.

## 1. Shared architecture — mandatory

**x360-native is the reference architecture for x360-native, ps2-native and xboxone-native. All three must use the same modules, boundaries and lifecycle, not merely similar goals.**

| Module | Common responsibility |
| --- | --- |
| Management launcher | Import, configure, prepare and start content; never imitate the original console interface. |
| Content/catalog | Identify originals, revisions, hashes, modules and dependencies without needless copies. |
| Preparation and native execution | Generic AOT/native execution backend; verified reusable outputs, module loading and explicit unsupported cases. |
| Compatibility runtime + HLE | Explicit service/API compatibility boundary: kernel/system services, users, storage, audio and input; shared ownership of memory, threads, timing and devices. |
| Original GPU / Native GPU | Fidelity path and PC-enhanced path, sharing guest contracts; neither may drop effects or change game logic. |
| Original system and games | Authentic dashboard/Browser, local state and generic game support, not individual game ports or a mock shell. |
| Session supervisor / host adapters | Original system <-> game lifecycle, each console's real menu/return behavior, shutdown and recovery; OS-specific implementation behind shared interfaces. |

Instruction sets, firmware and API implementations differ inside these boundaries; that is not permission to omit HLE, invent another architecture or duplicate runtimes. One logical console state serves both GPU paths. Windows x64/ARM64, Linux x64/ARM64 and macOS Intel/Apple Silicon are targets; Steam Deck uses Linux. Builds/packages are not runtime certification. Missing modules remain explicit implementation gaps, not claims of existing functionality.

## 2. Architecture requires maintainer validation

Before implementing or rejecting an architectural choice, present a short proposal in chat: current implementation, proposed boundary, alternatives, tradeoffs and effect on the common architecture. **Ask the maintainer to validate it and wait for that decision before the architectural change.** Record an approved decision briefly in an existing authoritative document, not a new report.

This applies to HLE coverage, original firmware execution, AOT/direct execution/interpreter/JIT choices, runtime replacement, GPU boundaries, process models and VM/hypervisor requirements. Do not invent permanent bans or turn a reference project's choices into this maintainer's decisions. **HLE is not forbidden in ps2-native.** Preserving the original Browser/dashboard does not by itself require executing every original firmware service, nor does HLE authorize replacing the original UI. Compare selective HLE, original firmware and hybrid approaches and obtain validation; do not silently choose or rule them out.

The common architecture above is already requested. Its unvalidated internal choices are not approved automatically. Existing code is the factual starting point, not proof that a different approach is prohibited. Preserve it until an approved migration replaces it. Do not request approval again for settled decisions, routine fixes or already-authorized focused checks. Continue independent authorized work while an architectural decision is pending.

## 3. Repository implementation map

- CPU/runtime/HLE: pinned ReXGlue under `external/rexglue-sdk/`; PowerPC/XEX -> C++ -> host binary. Kernel/XAM service implementations belong to the runtime, not the launcher. Xenos is the existing fidelity path; `src/native/` owns Native.
- Content/desktop/session: `src/content/`, `src/desktop/`, `src/supervisor/`, `src/platform/`; title/revision bindings in `titles/` and `titles/common/`. All original dashboard revisions are progressive targets; 17559 is the initial reference, not the only allowed revision. Preserve original My Games/Guide, BACK+START, reconnect, saves and return flow.
- `deps.lock.json` owns SDK/tool revisions and patch order. SDK changes use `patches/rexglue-sdk/`, existing applicators and focused `sdkcheck.*` coverage, with patch and before/after hashes. No ad hoc SDK upgrade. Rebuild affected titles/modules and stage matching runtime DLLs; invalidate codegen/receipts correctly. Xenia Canary is a reference, not the current executor or an automatic source of fixes.
- Use existing presets, `scripts/devshell.cmd` on Windows and `scripts/bootstrap.sh` on POSIX as applicable; preserve shared SDK generations and verified path/junction ownership. These commands do not authorize installations. Consult only relevant sections of `STATUS.md`, `docs/architecture.md`, `docs/compatibility.md`, `docs/performance.md` or build guides; preserve `THIRD_PARTY_NOTICES.md`.

## 4. Implementation, correctness and validation

Inspect the actual revision, working tree and affected callers before editing; with API-only edits, verify current file SHAs and reconcile conflicts. Preserve concurrent changes. Reuse existing helpers and build/test interfaces; no speculative frameworks, second launcher, profiler or runtime. Implement and validate the requested path instead of repeatedly producing plans; change the diagnostic method when a hypothesis fails.

**Only skill: [Ponytail](https://github.com/DietrichGebert/ponytail/blob/main/skills/ponytail/SKILL.md), in `full` mode for coding tasks.** Read its installed copy when available; otherwise consult the upstream skill without cloning or adding files. Do not activate or add other project skills unless explicitly requested by the maintainer. If Ponytail is inaccessible, state the limitation and follow its essentials: inspect affected callers, reuse existing code, make the smallest complete change and run focused checks. Do not claim to have used an unavailable skill. Ponytail does not override architecture approval, correctness, permissions or the documentation/disk budgets below.

Keep originals read-only and revision-isolated. Preserve saves, profiles/memory cards, settings, ownership, valid checkpoints, Unicode/long paths, error codes, ABI, synchronization and CPU/GPU coherence. One console has one coherent storage model, not separate incompatible save systems. Do not guess code boundaries, imports or hardware values, fabricate success stubs, hide a fallback, skip a draw or weaken an assertion. Distinguish fact, hypothesis and untested behavior. No fake online connection or content entitlement.

Build only affected targets with the established toolchain, normally Release. Reuse incremental outputs; rebuild every dependency invalidated by codegen, headers, ABI, shaders, compiler or input changes. Verify the effective executable, modules/runtime, settings and renderer. Keep reproducible negative controls for fixes when feasible; run focused regressions and the affected integration path, not every suite/title/platform after every edit. Preserve useful regression source tests; reducing disk use is not permission to remove correctness coverage.

For execution/graphics changes, compare relevant state, pixels, audio, input, events and timing at matched boundaries; exercise persistence/invalidation when affected. Report synthetic, headless, interactive, software-GPU and real-GPU evidence separately, with exact revision and untested targets. A successful build, PID, test fixture or AOT percentage does not establish original boot or gameplay. **Instructions-only edits need diff/content/reference checks, not builds, game sessions or manually rerun CI matrices.**

Performance-first, correctness-preserving: measure the actual user path before/after under comparable conditions, preserve negative results in compact summaries, and optimize the measured bottleneck. Preserve physics, simulation/video timing, audio and effects. Keep optional traces/profiling off the normal path; never remove necessary checks for a benchmark. No unsupported global SIMD/fast-math requirements. Do not benchmark while building.

Respect existing stop requests and permissions for Computer Use, interactive/long sessions, installations, deployment and privileges. Supervise bounded runs; clean up only task-owned processes. A process supervisor is not a security sandbox. State missing inputs/permissions honestly; do not fabricate execution.

## 5. Disk and test-artifact budget

**No unbounded test output, repeated full builds, cloned content trees or accumulating GB/TB of evidence.** Before disk-heavy work, inspect existing outputs/free space and estimate new bytes and file counts. Reuse verified content, toolchains, SDK generations, AOT objects and one incremental build directory per necessary toolchain/configuration. Additional build roots or full baseline/candidate copies need a concrete reason and a cleanup plan, not a directory per attempt.

Use one owned scratch area per active task. Prefer streaming summaries, small synthetic fixtures and bounded buffers to full dumps or per-frame files. Default diagnostic budget: **1 GiB total / 200 generated evidence files per task**, with explicit duration/iteration and log-size limits before running. This budget covers logs, traces, captures and disposable test output, not source tests or required build products; those still require capacity planning and reuse. A larger diagnostic budget needs maintainer approval with the estimated cost. Stop or rotate safely at limits and label incomplete evidence; never silently discard required comparison data or turn truncation into a pass.

Clean task-created disposable files on success, failure and interruption; retain only the minimal useful failure reproducer/current comparison and explicitly needed recovery state. Keep compact measurements, not every raw trace. Do not continually create ZIPs, snapshots, backup trees, copied SDKs, duplicate originals or new worktrees. Respect leases, live receipts and concurrency; preserve the last valid output during transactional publication. Share immutable verified data only when write isolation is safe; never hardlink writable output to originals.

**No blanket deletion of `out/`, `build/`, `.local/`, archives or another agent's files.** Delete only proven disposable task-owned artifacts. User originals, saves, valid checkpoints, active outputs and unrelated work are not garbage; destructive cleanup of existing data needs an explicit scoped decision. At completion, report large generated/retained amounts and cleanup, not thousands of filenames. Do not implement a new cleanup framework just to enforce this policy.

## 6. Documentation and token budget

**Default: create no new Markdown files.** No per-task reports, audit/final-summary files, parallel roadmaps or duplicated policies. A new document needs an explicit request or maintainer-approved need that an existing file cannot serve. Update the shortest relevant section in an existing authoritative document only when a durable fact or active next step changes; do not update STATUS, NEXT-TASK, README and multiple reports for the same result. Small fixes can be explained by the commit and final reply.

Keep this guide concise; no daily logs, machine inventories or test-count histories here. Keep `CLAUDE.md` a short pointer. README translations remain stable public introductions; update both only for public-use changes. Do not delete existing documentation/history just to reduce tokens. Search first and read relevant ranges; avoid whole-tree/doc dumps, repeated inventories, unchanged-file reads and huge tool output. Final replies give the outcome, commits, actual checks and material limitations, not an essay or another document.

## 7. Publication and provenance

Work directly on `main` for authorized changes unless instructed otherwise. Use English Conventional Commits and code identifiers; reply in the user's language. No assistant co-author/generated-by footers. Preserve human/upstream credits and existing BSD-3-Clause/third-party terms; review reuse licenses and seek a compatible maintainer-approved integration when needed. Never assume an external reference grants copying rights.

Do not commit proprietary firmware, games, keys, personal state, dumps/captures or generated proprietary translations; no leaked SDKs, protection bypasses or execution of scripts embedded in user content. No PR, ZIP, tag, Release, history rewrite, force-push or visibility change unless requested. Change another repository only when the task explicitly includes it. Report what was actually changed/tested; updating this policy does not implement the architecture or clean the user's disk.
