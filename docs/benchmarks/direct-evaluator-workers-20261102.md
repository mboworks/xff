# Direct evaluator worker qualification (2026-11-02)

## Decision

Reject this implementation. It removes one production worker-wrapper allocation, but the native and whole-engine measurements do not establish an acceptable cross-platform improvement. Keep the shipping evaluator unchanged. Retain the exact candidate below so a later design can be compared with this failure rather than repeating it.

The current shipping adapter has a heap-stable prepared plan and an allocated polymorphic coordinator/concurrent worker wrapper. The candidate keeps the stable plan and stores either its observer or private prepared worker directly in a small standard variant, alongside the existing test-only qualification-state alternative. This removes the production worker wrapper and virtual evaluation call while preserving independent regex state and moved-owner lifetimes. It does not remove the plan allocation or its once-per-worker factory dispatch.

Actual ARM64 ThinLTO baseline disassembly at the matcher entry loads the worker vtable and calls its evaluation slot. That demonstrates a remaining production indirection, not that every indirect instruction in the surrounding function belongs to it. Existing qualification executors remain test-only; they are not counted as shipping overhead. Removing a virtual call is not by itself an elapsed-time win.

## Measurement contract

Exact measured parent: `634b85e42f42fb53aa993ea02f5af6f8302156c9` (#1051). The current two-file implementation is preserved as a patch against the full baseline sources. Both hosts build and copy clean `clang_release`/ThinLTO executables before timing. No build, sanitizer or lint overlaps timing on a host.

There are 34 native cases per platform, three sessions, a discarded warmup per variant/case/session, nine rotating rounds and fastest-seven means. Every invocation checks a separate output oracle. Flat 10/1,000/10,000-file name, content-filter and line-output cases use one/three/ten workers; listing, metadata and comparisons provide controls. Linux uses tmpfs with pinned CPU allowances; macOS uses warm APFS without affinity.

Thirty whole-engine in-memory cells run three sessions and nine repetitions/fastest seven, using separate processes per case/variant. Seven focused kernel cells add a third participant copied byte-for-byte from the baseline; six native cases add that control in five sessions. These confirmation populations do not replace the original slower results. In-memory runs include setup/traversal/evaluation/output and are not pure dispatch timings.

## Native confirmation

Changes are median paired session ratios; negative means faster.

| Fixture / task          | Workers | Initial Linux | Confirm Linux | Baseline repeat Linux | Initial macOS | Confirm macOS | Baseline repeat macOS |
| :---------------------- | ------: | ------------: | ------------: | --------------------: | ------------: | ------------: | --------------------: |
| flat10000 / name-exact  |       1 |        +4.88% |        +0.94% |                +1.03% |        +0.04% |        +0.38% |                +0.22% |
| flat10000 / name-exact  |      10 |        -3.07% |        +1.15% |                -1.15% |        +0.31% |        +0.61% |                +0.16% |
| flat10000 / metadata    |      10 |        +2.04% |        +3.48% |                -3.10% |        -0.38% |        +1.78% |                +0.78% |
| compare-large / compare |       1 |        +3.45% |        +4.40% |                +0.60% |        +0.07% |        +1.59% |                +0.82% |
| compare-large / compare |      10 |        +3.76% |        +2.20% |                -0.55% |        +1.62% |        +1.38% |                -0.19% |
| flat1000 / native-lines |       1 |        +0.87% |        +2.81% |                -0.19% |        -0.82% |        +1.27% |                +0.14% |

## Whole-engine kernel confirmation

| Kernel                     | Initial Linux | Confirm Linux | Baseline repeat Linux | Initial macOS | Confirm macOS | Baseline repeat macOS |
| :------------------------- | ------------: | ------------: | --------------------: | ------------: | ------------: | --------------------: |
| RE2/filter/10000/1         |        +1.50% |        +0.33% |                -0.99% |        +0.03% |        -0.38% |                -0.47% |
| RE2/filter/10000/4         |        +1.64% |        +1.95% |                +0.03% |        -0.15% |        -0.41% |                +0.21% |
| RE2/native-lines/10000/4   |        +0.98% |        +0.36% |                +0.20% |        -0.82% |        -0.24% |                -0.55% |
| RE2/rg-files/10000/1       |        -3.29% |        -2.11% |                +0.37% |        +0.26% |        +0.56% |                -0.16% |
| RE2/traversal/10/4         |        -2.23% |        -2.55% |                -1.50% |        -1.49% |        -0.67% |                -1.82% |
| large-early-different/10/4 |       +40.18% |       +14.38% |                -2.36% |        -0.51% |        -0.62% |                +0.29% |
| small-identical/1000/4     |        -0.46% |        -1.75% |                -1.30% |        -0.26% |        -0.71% |                -1.98% |

The Linux early-difference/four-worker comparison median is +40.18% initially and +14.38% in confirmation. Confirmed absolute times are 308.21/240.94/234.64 microseconds for baseline and 290.48/308.16/268.38 for the candidate; the duplicate baseline is 261.52/235.26/238.52. These are noisy short threaded commands, but the candidate remains slower in two confirmation sessions while its allocation savings are small and paid only at worker creation. The exact cause of the comparison shift is not established. Do not assign it solely to evaluator dispatch or erase it as a presumed runner effect.

Linux native confirmation also retains +4.40% for a one-worker large comparison and +2.81% for 1,000-file one-worker native line output. macOS mostly remains near baseline. The lossless archive keeps gains, regressions, all repetitions and source identities. No reference normalization is used to mask paired results.

## Isolated allocation diagnostics

ASan counts allocation traffic, not release elapsed time, peak/live memory or allocator-resident bytes. Each host executes 108 selected production/tree phase cases for each variant. Parsing, binding, preparation, worker creation, first/steady evaluation and teardown remain separate; tree cases provide a negative control.

| Host / expression / role            | Baseline worker calls | Candidate worker calls | Baseline requested bytes | Candidate requested bytes |
| :---------------------------------- | --------------------: | ---------------------: | -----------------------: | ------------------------: |
| macos / RegexProductionCoordinator1 |                     1 |                      0 |                       16 |                         0 |
| macos / RegexProductionConcurrent1  |                     3 |                      2 |                      160 |                       144 |
| linux / RegexProductionCoordinator1 |                     1 |                      0 |                       16 |                         0 |
| linux / RegexProductionConcurrent1  |                     3 |                      2 |                      160 |                       144 |

Preparation counts stay unchanged; coordinator creation loses its only wrapper allocation and concurrent creation loses one 16-byte request. Steady-state allocation traffic is already zero for these examples. Those improvements do not compensate for retained workload regressions.

## Size, correctness and remaining work

- linux: unstripped CLI build output 12,292,800 -> 12,310,472 bytes (+17,672).
- macos: unstripped CLI build output 4,051,424 -> 4,050,272 bytes (-1,152).

Both hosts pass the four release suites `expression_execution_test`, `parallel_match_test`, `rg_test` and `run_test` for the candidate. They cover whole-run differential behavior, moved execution owners, capture independence, pool transitions and output. Both hosts also pass the selected ASan phase diagnostics. This records experimental correctness, not final validation of a shipping runtime change: the candidate is rejected, so the PR changes documentation and retained evidence only.

A deeper typed-plan representation is not implemented or qualified here. It must preserve the stable prepared-data lifetime across owner moves and keep mutable matcher state private; it should be driven by preparation/hot-loop profiles rather than another generic wrapper. EP07 optimizer and operand qualification, JC role budgets, derived-value handoff (O13) and the remaining O14-O22 candidates stay distinct.

## Lossless evidence

[direct-evaluator-workers-20261102.json.gz](direct-evaluator-workers-20261102.json.gz): 933,138 bytes; SHA-256 `4fd0e237df2002c3d666608a845344b19c3657a038053fedcff6088f6b2652c2`. Native fixture tables resolve through `fixtures[report.fixtures_ref]`. The bundle contains both variants, rejected patch, scripts, raw reports, allocation cases/XML and the retained shipping disassembly.
