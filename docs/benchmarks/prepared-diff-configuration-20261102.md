# Prepare invariant file-diff configuration (2026-11-02)

## Decision

Reuse complete immutable file-diff options for each distinct action style during a run. Existing whole-command validation retains its error ordering. Preparation resolves algorithm, style/default format and context, normalization tokens and the RE2 ignored-line expression without observing files or rendering peer templates. A reached action looks up the shared style record after its existing peer/source reads, exact equality and binary handling. Direct evaluator callers may omit the table or a style and retain context-dependent lazy construction.

The table owns ordinary unique records, keyed by owned style strings, and survives AST cloning for ordered/deferred evaluation. It has no reference to temporary source records and no per-file lock, erased callback, thread, clock or reference-counted configuration. It is bounded by the number of distinct action styles. `DiffOptions` embeds a noncopyable/nonmovable RE2 object; borrowing complete records avoids rebuilding or wrapping regex objects per entry. Existing validation compiles its regex once separately, then each distinct style is compiled once for execution.

This closes only the diff-configuration subset of O15/EP03. Peer and expected-hash templates were already prepared; no duplicate template mechanism is added. Hash specification/default binding and the wider EP07 qualifications remain open. Comparison-mode patches already prepare their own options and are unchanged.

## Measurement contracts

Parent `563a5c1abb50c1410993cb0860c333c76faccdba` (#1054); clean `clang_release`/ThinLTO baseline and candidate binaries. Runtime variants change only `evaluate.h`, `evaluate.cc` and `run.cc`; both receive identical benchmark fixtures/scenarios. Every native invocation has an independent stdout and zero-exit oracle. Retain source/fixture/binary identities, flags, drivers, raw values and validation logs in the compressed evidence.

Native: 42 cases per host, three sessions, per-case/participant discarded warmups, nine rotating rounds and fastest-seven means. Flat 10/1,000/10,000-file trees cover normalized, plain, equal and skipped diff actions with one/three/ten requested workers, plus ordinary listing controls. Linux uses tmpfs and CPU affinity; macOS uses warm APFS without affinity. Diff actions remain ordered coordinator work; requested workers do not prove action concurrency.

In-memory: 32 whole-engine cases per platform, three sessions and two variants, nine repetitions/fastest-seven per process. Each includes run preflight, option preparation, traversal, diff, output and teardown; parser/fixture construction is excluded. File counts 1/10/1,000/10,000 and requested workers 1/3 cover normalized/plain/skipped diffs and names. Reference-tree/production output checks run before timing; these kernels are not external-command timings or externally normalized tool comparisons.

Confirmation: six native controls, five sessions, a byte-identical baseline participant, warmup/nine rounds/fastest-seven. Confirmed data supplements rather than replaces the original population. No compilation, lint or sanitizer overlaps a timing population on its host. An earlier prototype exceeded the callback complexity limit; its reports/sources are retained separately. All displayed results use the rebuilt final helper form.

## Native normalized file diffs

Median paired session change; negative means faster.

|  Files | Requested workers |   Linux |  macOS |
| -----: | ----------------: | ------: | -----: |
|     10 |                 1 |  -1.83% | -1.52% |
|     10 |                 3 |  -5.09% | -0.10% |
|     10 |                10 |  -0.23% | -0.02% |
|  1,000 |                 1 | -20.37% | -5.80% |
|  1,000 |                 3 | -19.87% | -5.61% |
|  1,000 |                10 | -19.59% | -5.24% |
| 10,000 |                 1 | -20.53% | -5.86% |
| 10,000 |                 3 | -20.31% | -6.48% |
| 10,000 |                10 | -19.75% | -5.78% |

## In-memory whole-engine controls

Median session fastest-seven times, candidate relative to baseline. Includes preparation each iteration.

| Task            |  Files | Requested workers |   Linux |   macOS |
| :-------------- | -----: | ----------------: | ------: | ------: |
| diff-normalized |      1 |                 1 |  -0.36% |  +0.76% |
| diff-normalized |      1 |                 3 |  +0.23% |  +1.60% |
| diff-normalized | 10,000 |                 1 | -86.85% | -88.33% |
| diff-normalized | 10,000 |                 3 | -86.81% | -88.26% |
| diff-plain      |      1 |                 1 |  +1.65% |  +0.66% |
| diff-plain      |      1 |                 3 |  +1.15% |  -0.34% |
| diff-plain      | 10,000 |                 1 |  -3.07% |  -3.06% |
| diff-plain      | 10,000 |                 3 |  -2.58% |  -0.48% |
| diff-skipped    |      1 |                 1 | +23.46% | +21.55% |
| diff-skipped    |      1 |                 3 | +23.79% | +20.08% |
| diff-skipped    | 10,000 |                 1 |  +0.35% |  -0.33% |
| diff-skipped    | 10,000 |                 3 |  +0.64% |  +1.50% |
| name            |      1 |                 1 |  +0.62% |  +0.66% |
| name            |      1 |                 3 |  +1.78% |  -0.63% |
| name            | 10,000 |                 1 |  +0.29% |  -0.26% |
| name            | 10,000 |                 3 |  +0.27% |  +2.05% |

## Slower controls and confirmation

Keep the initial slower controls and compare the candidate and duplicate-baseline confirmation separately.

| Files / task        | Workers | Initial Linux | Confirm Linux | Baseline repeat Linux | Initial macOS | Confirm macOS | Baseline repeat macOS |
| :------------------ | ------: | ------------: | ------------: | --------------------: | ------------: | ------------: | --------------------: |
| 10 / listing        |      10 |        -2.14% |        +3.59% |                -1.48% |        +0.20% |        +1.53% |                +0.11% |
| 1,000 / normalized  |       3 |       -19.87% |       -19.40% |                -0.01% |        -5.61% |        -4.36% |                +0.43% |
| 10,000 / listing    |       3 |        +1.42% |        +0.15% |                -1.16% |        -0.14% |        +0.16% |                +0.08% |
| 10,000 / equal      |      10 |        -0.43% |        +0.34% |                -0.07% |        -0.26% |        +0.31% |                +0.28% |
| 10,000 / normalized |       1 |       -20.53% |       -21.29% |                -0.33% |        -5.86% |        -5.73% |                -0.18% |
| 10 / plain          |       1 |        -0.17% |        -1.10% |                -0.74% |        +1.18% |        +0.99% |                +1.04% |

Do not attribute changes in unrelated listing/equal-file controls to eliminated regex work. Retain modest positive costs as well as faster cases. Tiny cases include once-per-run record allocation and a style lookup; normalized large cases amortize these costs. The full raw population, including all sessions and non-displayed file counts, remains available.

## Correctness, scope and footprint

Direct tests cover style deduplication/default overrides, normalization errors, no-consumer behavior, byte-identical output and read counts versus unprepared contexts, skipped observations, missing-table-style fallback, separate runs with different defaults, and invalid globals before content reads. Existing full suites retain absent/binary/reversed patches, multiple formats, templates, archive/VFS behavior, safety, deferred effects and error handling. The fixture cannot mutate host paths.

Complete core/extension release suites pass 321/321 on each host, with all extensions enabled and required Linux FUSE. The earlier lean invocation with ten skipped CLI targets is retained separately. Six focused ASan suites pass per host and six Linux TSan suites pass. Scoped clang-tidy and normal enforcing hooks remain required before publication.

- linux: unstripped CLI 12,337,640 -> 12,360,488 bytes (+22,848).
- macos: unstripped CLI 4,069,696 -> 4,071,248 bytes (+1,552).

Backend regex allocation bytes are not estimated as table capacity. There is no new global cache and each run releases its records. No blanket claim of allocation-free preparation, first-command speedup or parallel diff execution is made.

## Lossless evidence

[prepared-diff-configuration-20261102.json.gz](prepared-diff-configuration-20261102.json.gz): 1,527,277 bytes; SHA-256 `5db3615728ac98b9040abb2eaea024a7a5242b188c399e9169f977bc5d9876b2`. Initial, confirmation, in-memory and development-failure populations have separate provenance.
