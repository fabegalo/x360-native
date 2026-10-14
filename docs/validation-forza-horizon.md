# Forza Horizon — Windows x64 investigation, 2026-10-14

Original input: Title ID `4D5309C9`, media `2DC7007B`, version/base `0.0.0.10`,
20,856,832-byte XEX, SHA-256
`db40df605ade49a612b35a7a24c38f6004bcb17a88ed6b48288de16df9e3987c`.
Existing extraction reused: 2,432 files, 7,320,461,527 bytes. ISO SHA-256
`83dfc7d0b58b804232385de6f794d9acb92b89a37cf13da14b824b286c824490`.
The title profile checks the XEX hash at configure and before build codegen.
No proprietary executable, generated PPC, save or screenshot is committed.

## AOT audit

Reviewed `04c5174`, `845f08a` and `aa220c0`. Retained the compact output path,
full catalog fingerprint in the owner record, preserved old build trees,
SDK-owned `rexglue.cmake` versus application-owned `x360n_rexglue.cmake`,
manifest-based executable discovery and dependency-based receipts.

Corrected four contracts:

- The generic host included `generated/default/<id>_init.h` although only the
  compact output directory was on its include path. Include the header by name.
- `-BuildDir` could override the checked owner while codegen still wrote into
  the declared build. Generic projects now reject a different native directory;
  manual projects retain explicit alternate builds.
- Direct Ninja builds of `.local/aot/<id>` used the root lease, without title
  exclusion or invalidation. They now use the same title lease and marker as
  registered manual projects.
- Compact receipts must include the matching owner record. Application-owned
  glue is included in the preparation source snapshot to detect mid-build edits.

Focused tests: preparation 8 cases / 73 assertions; generic AOT 2 cases / 41
assertions; receipt publication and build leases passed. The direct generic
build fixture confirms marker creation and rejection of a concurrent producer.
Sandbox-denied renames were rerun against test-owned host temporary directories.
Logs: `.local/logs/forza-audit-*.log`.

## Original 12 UnresolvedCall diagnostics

Baseline reproduced without hints in
`.local/runs/20261014T062355Z-forza-baseline-codegen/` (84.4 seconds).
Disassembly: `.local/logs/forza-ppc-unresolved.txt`.

| Call site | Target | Diagnosis / correction |
|---|---|---|
| `82B13498` | `82B1ABD0` | Frameless getter; original `lis/ori/lwzx/blr`; entry seed |
| `82B1352C` | `82B1ABE8` | Two-word adjustor thunk; entry seed |
| `82B13540` | `82B1ABF0` | Two-word adjustor thunk; entry seed |
| `82B13550` | `82B1ABF8` | Two-word adjustor thunk; entry seed |
| `82B13588` | `82B1AC00` | Two-word adjustor thunk; entry seed |
| `82B13560` | `82B1AC08` | Frameless `addis/addi/blr`; entry seed |
| `82CED38C` | `82CF87F8` | Adjustor between pdata end `82CF87F8` and start `82CF8800` |
| `82F029F4`, `82F1EF58` | `82F1ED30` | Virtual-base adjustor, then tail call at `82F1ED44` |
| `82F5CCEC` | `82F5CCDC` | Internal epilogue of leaf starting `82F5CC68`; SDK patch 0068 |
| `82F76204` | `82F761F4` | Internal epilogue of leaf starting `82F76188`; SDK patch 0068 |
| `82F7631C` | `82F7630C` | Internal epilogue of leaf starting `82F762A0`; SDK patch 0068 |

Patch 0068 fixes GapFill splitting at actual pdata ends when the preceding
function ends with an out-of-line backward branch. A synthetic two-function PPC
fixture fails before the patch with one unresolved call and passes 10 assertions
after it. No internal epilogue is promoted to a function. Castle Crashers
regeneration with the patched producer left all 41 files unchanged
(`.local/runs/20261014T063747Z-castle-sdk68-codegen/`).

The next layer exposed `82B3B8B0/B8/C0/C8`: three original `lwz/b` wrappers and
one five-instruction leaf. These have entry seeds, with sizes left to CFG
discovery. The final emission also exposed thunk `830EBE90 -> 830ED910`; the
target is an original context-save routine, now seeded rather than emitted as
an unresolved-call trap.

The byte switch at `826B5D6C` lost the `r4 -> r30` index alias and read beyond
the ten entries bounded by `cmplwi cr6,r4,9` at `826B5D3C`. The exact original
ten bytes at `82041470` alternate targets `826B5D70` and `826B5DFC`; the profile
records that table. No data is converted into code. Evidence:
`.local/logs/forza-ppc-codegen-warnings.txt` and `forza-pdata-overlap-final.log`.

## Code generation and limits

Optimized patched codegen completed in 92.9 seconds: 473 files, zero unresolved
calls. Log: `.local/logs/forza-sdk68-optimized-codegen.log.err`. The Debug producer
hit its 360-second supervision limit during Register and was terminated; it is
not counted as a successful run.

Two conditional-count branches at `82AD8138` and `82AD813C` target
`82AD836C`, a zero word immediately after pdata end. The generated explicit trap
is retained; no function is invented at that address. Whether those original
paths are reachable at runtime remains unverified. The large generated C++
function warning for `8241A370` describes source size, not a measured execution
failure; its original pdata extent is `8241A370–8243512C`.

Native preparation completed with exit 0: configure 11 s, producer 154 s,
codegen 111 s, reconfigure 13 s, native build/link 329 s (concurrent builds were
running, so these are elapsed times rather than comparative performance data).
The 88,904,192-byte Windows x64 executable has SHA-256
`f0b01eda61aaa3f5f2aceab9c7d654a4c0f8929f6e834ccd630d07ac3b1bd710`.
The normal driver published `forza_horizon.exe.preparation.json` after verifying
its dependency and artifact closure. Log: `.local/logs/forza-preparation-sdk68.log`.
The final pdata scan checked 57,127 functions with zero overruns. Exact input
identity tests passed 62 assertions (`.local/logs/forza-identity-targets.log`).

Runtime results are recorded below as they are verified.
