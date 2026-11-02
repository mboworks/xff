# Reuse requested content values between stages (2026-11-02)

## Decision

Retain the request-driven derived-cache handoff. Compiled field renderers identify required hashes and line counts once per run, including defaults and transformed fields. Histogram enums add line demand. Matched/selected workers derive only those values from content already loaded by their evaluator. Move requested digest map nodes and line values into an optional result-indexed snapshot vector; keep raw content in the worker. Ordinary filters retain their result layout and allocate no derived-cache vector.

The existing per-entry snapshot already reuses reads within one evaluator. The gap was the worker-to-coordinator boundary: the coordinator could reread content for fields and reductions after filtering. Gains combine reuse of those bytes and moving requested digest/line computation onto existing workers. They must not be attributed entirely to fewer copies. No thread, clock, stat, shared mutable cache, generic wrapper or user flag is added.

Keep streaming sources without loaded snapshots on the lazy coordinator path. Preserve result-limit laziness, selected/error states, empty/unmatched/regrowing batches, binary line handling, transforms/defaults, archive serial admission and effect invalidation. Cached missing values remain distinct from values not requested. A returned derived snapshot has no raw content; later unrequested consumers can still read lazily.

## Measurement contract

Measured parent: `bcfef58c06979254b2081b6f02b1040821464b5b` (#1052, documentation only). Baseline and `derived_values` use clean `clang_release`/ThinLTO executables copied before timing. Runtime source identities, binary hashes, fixture data, scripts and all raw samples are retained. Later support-test refinements do not alter the timed runtime. Compilation, lint and sanitizers do not overlap timing on a host.

Each host has 50 native cases in three sessions: warmup discarded per case/variant/session, nine rotating rounds and fastest-seven means. Independent output oracles validate every invocation; hash/line expectations are computed from actual fixture bytes before timing. Workloads use 10/1,000/10,000 files plus 272/288/320-entry fixtures with 256 short entries followed by expensive 128 KiB entries. Requested worker counts are one/three/ten. Linux uses tmpfs and CPU affinity; macOS uses warm APFS without affinity.

Twenty-six whole-engine in-memory cells run three sessions and nine repetitions/fastest seven, with separate processes and rotating participant order. Content fields, output/filter/traversal and comparison controls use one/four workers. Four native cases receive five-session confirmation and five kernels receive three-session confirmation, each adding a byte-identical baseline participant. Confirmation never replaces initial slower samples.

## Native results

Median paired session changes; negative means faster.

| Fixture / task           | Workers |   Linux |   macOS |
| :----------------------- | ------: | ------: | ------: |
| flat10000 / derived-hash |       1 |  +0.60% |  -0.10% |
| flat10000 / derived-hash |       3 | -36.22% | -48.53% |
| flat10000 / derived-hash |      10 | -43.18% | -49.17% |
| heavy320 / derived-lines |       1 |  -0.35% |  +0.78% |
| heavy320 / derived-lines |       3 | -26.57% | -35.04% |
| heavy320 / derived-lines |      10 | -19.76% | -34.55% |
| heavy320 / derived-hash  |       3 | -57.77% | -56.91% |
| heavy320 / derived-hash  |      10 | -72.61% | -73.62% |
| flat10000 / files        |       1 |  +1.55% |  +0.12% |
| compare-large / compare  |       3 |  -2.55% |  +0.69% |

One-worker cases stay near baseline; large parallel hash cases improve substantially on both hosts. These are selected workloads, not a claim that every search becomes faster. The largest initial native median costs are +1.55% Linux and +0.78% macOS, with session reversals retained.

## Kernel results and confirmation

| Kernel                     | Initial Linux | Confirm Linux | Baseline repeat Linux | Initial macOS | Confirm macOS | Baseline repeat macOS |
| :------------------------- | ------------: | ------------: | --------------------: | ------------: | ------------: | --------------------: |
| RE2/content-fields/1000/4  |       -67.84% |       -66.75% |                -0.07% |       -71.82% |       -72.19% |                -0.14% |
| RE2/content-fields/10000/1 |        +0.09% |        +0.08% |                +0.05% |        +0.94% |        -1.67% |                +0.04% |
| RE2/content-fields/10000/4 |       -70.14% |       -70.00% |                +0.14% |       -73.01% |       -72.80% |                +0.19% |
| RE2/large-output/10000/1   |        -0.54% |        +0.26% |                +0.14% |        +6.63% |        -0.52% |                +2.66% |
| RE2/rg-files/10000/4       |        +0.25% |        +2.03% |                +1.58% |        +1.47% |        -2.31% |                -3.37% |

Retain the initial macOS 10,000-file/one-worker large-output control (+6.63%) and its targeted confirmation above. That output path requests no derived values, so the exact cause must not be assigned to saved hashing work. The byte-identical baseline participant helps expose noise but does not eliminate it. All slower samples and unrelated shifts remain in the archive.

## Native confirmation

| Fixture / task           | Workers | Linux candidate | Linux baseline repeat | macOS candidate | macOS baseline repeat |
| :----------------------- | ------: | --------------: | --------------------: | --------------: | --------------------: |
| heavy320 / derived-hash  |      10 |         -72.38% |                +0.48% |         -73.52% |                +0.24% |
| flat10000 / files        |       1 |          +2.07% |                +0.35% |          +0.28% |                +0.02% |
| flat10000 / derived-hash |       1 |          +0.70% |                +0.55% |          +0.51% |                -0.00% |
| heavy320 / derived-lines |       1 |          +0.32% |                +0.03% |          +0.82% |                +0.13% |

## Correctness, ownership and footprint

The native field test processes 128 files, including unselected entries, and observes exactly 128 VFS reads with serial and parallel execution while repeated digest/line fields produce identical output. A result limit preserves the lazy path and its single consumed output read. Snapshot tests prove that returned caches retain no bytes, do not initiate reads for unloaded sources, move only requested values and retain binary/read-failure null results. Pool tests cover unmatched, empty and inline regrowth without stale values. Dedicated parallel column, summary, line-bucket histogram, line-sum histogram and count-only histogram cases match serial output and observe one read per file. Rg summary tests exercise rejected content matches and read-error equivalence.

Derived storage is bounded by the current batch population and requested cache values, rather than total file bytes. It is not a new total-process memory cap. Hash strings/map nodes and snapshot metadata still use memory; ordinary searches requesting no such values allocate no side vector. No first-output or total-RSS improvement is inferred from elapsed time alone.

- linux: unstripped CLI output 12,292,800 -> 12,334,504 bytes (+41,704).
- macos: unstripped CLI output 4,051,424 -> 4,069,744 bytes (+18,320).

Both complete core/extension release suites pass 306/306, with required Linux FUSE mounts. Focused ASan passes five suites per host, and Linux TSan passes three worker/run suites. Header-aware local clang-tidy covers 42 affected translation units; initial test const-qualification findings are fixed and rechecked separately. Final normal hooks and current pushed-head CI remain required before merge.

## Lossless evidence

[derived-content-handoff-20261102.json.gz](derived-content-handoff-20261102.json.gz): 1,176,635 bytes; SHA-256 `ad5f377bacbac56bf45aa504038db70f40893324d648e55c67580a1e1a478550`. Resolve fixtures through `fixtures[report.fixtures_ref]`; timed and final support sources are separate, and initial failure/confirmation logs remain distinct.
