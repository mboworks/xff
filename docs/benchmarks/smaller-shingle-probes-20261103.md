# Probe the smaller byte-word shingle set

This candidate changes only intersection traversal: count membership hits by iterating the smaller
of the two existing shingle sets. Normalization, shingle construction, duplicate removal, empty-set
handling, integer half-up score rounding, cache validation and file reads remain unchanged. No new
flag, dependency, lock, persistent state or allocation is introduced by the intersection loop.

The choice is deliberately narrow. A cached reference can have many more distinct shingles than a
candidate, or the reverse. The old loop always walks the candidate's set. The selected loop performs
at most `min(left.size(), right.size())` membership queries. Hashing long keys can still dominate;
fewer queries do not promise that every balanced input becomes faster.

## Decision and measured tradeoffs

The complete native measurements improve the strongest asymmetric 1,000-file case by 18.3% on
macOS and 26.6% on Linux. This is a tradeoff: balanced width-three native cases add up to 2.7% on
Linux; the worst literal-reference macOS cell adds 2.2%. Most per-file dynamic-reference cells
remain close to baseline, with the worst such cell about +0.6% on either host. A separate small
cached-reference Linux kernel adds about 5.0%. The worst macOS kernel median is about +4.2%,
in a tiny equal-input fast path.
These values are not an application-wide speed guarantee.

Use the median of three within-session ratios. Each session discards one warmup per participant,
then interleaves nine rounds and retains the fastest seven. An identical copy of the baseline is a
separate participant in native tests. The copy controls process, allocator and system variability;
it is not a claim that separately rebuilt baseline executables are byte-identical.

The current chosen variant is `chosen_min`, the two-call `std::ranges::count_if` formulation (R1),
with an explicit `<algorithm>` include. A const-reference-selection loop (R2), recursive operand
swap (R3), count-if tail swap (R4), duplicated branches (R5), and outlining trials are retained as
rejected experiments. Some formulations reduced one balanced-input cost while adding repeatable
cost to Linux cached long-word cases. No assembly-level explanation is claimed without proof.
No compiler-specific outlining annotation is part of the selected implementation.

## Whole-process coverage

Elapsed changes are selected/baseline minus one; negative means faster. Representative native cells
follow; the complete group ranges and every raw cell, including slower controls, are retained.

| Fixture / requested workers / width | macOS elapsed change | Linux elapsed change |
| :---------------------------------- | -------------------: | -------------------: |
| 1000-p0-large-small / 1 / 1         |              -18.31% |              -26.55% |
| 1000-p0-large-small / 1 / 3         |              -12.43% |              -21.22% |
| 1000-p0-large-small / 1 / 5         |              -11.66% |              -21.07% |
| 1000-p0-large-small / 3 / 1         |              -18.25% |              -26.51% |
| 1000-p0-large-small / 3 / 3         |              -12.21% |              -20.82% |
| 1000-p0-large-small / 3 / 5         |              -12.01% |              -21.02% |
| 1000-p64-large-small / 1 / 5        |               -5.43% |               -9.55% |
| 1000-p0-small-large / 1 / 3         |               +1.09% |               -0.65% |
| 1000-p0-balanced / 1 / 3            |               -0.85% |               +2.71% |
| 1000-p0-balanced / 1 / 5            |               +0.00% |               +1.51% |
| 1000-p64-small-large / 3 / 1        |               +2.20% |               +0.03% |
| 10-p0-balanced / 1 / 3              |               +0.63% |               +0.90% |

Fixture labels encode file count, per-word padding bytes and candidate/reference cardinality.
`large-small` is the direction that previously walked the larger candidate set. The reverse
direction already used few membership queries; it is retained as an explicit control.

| Complete native group                      | macOS elapsed-change range | Linux elapsed-change range |
| :----------------------------------------- | -------------------------: | -------------------------: |
| Literal similarity                         |          -18.31% to +2.20% |          -26.55% to +2.71% |
| Literal listing/name/regex controls        |           -0.25% to +1.06% |           -1.50% to +1.99% |
| Per-file dynamic references                |           -1.70% to +0.59% |           -4.89% to +0.53% |
| Shared references in dynamic matrix        |           -3.77% to -1.02% |           -9.49% to -3.99% |
| Dynamic-matrix listing/name/regex controls |           -0.21% to +1.76% |           -1.01% to +0.60% |

Identical-copy control ranges are available for every native cell in the raw evidence. They
capture ordinary measurement variation; no copy-normalization is applied to the displayed ratios.

Literal-reference tests cover 78 cases: 10/100/1,000 input files, short words or 64/256 padding bytes,
balanced and both asymmetric directions, shingle widths 1/3/5, requested worker counts 1/3, and
ordinary listing/name/regex controls. Three sessions and three participants produce 7,020 checked
invocations per host, including warmups.

A separate 36-case native matrix covers per-file `peer/{name}` references, shared-reference controls,
and ordinary listing/name/regex controls in both cardinality directions. The per-file peers carry
distinct punctuation suffixes; the expected byte-word sets and integer scores are independently
calculated. Complete output bytes and exit status are checked on every invocation. This matrix
adds 3,240 invocations per host. It is necessary because literal-reference caching does not exercise
the uncached per-file target path.

Linux fixtures are in shared memory and CPU allowance is pinned. macOS uses its local filesystem
and has no equivalent affinity claim. Each host is measured alone, without concurrent local builds,
tests, lint or allocation diagnostics.

## Direct verifier and in-memory engine coverage

The 246-case matrix includes exact-score and reusable-reference APIs, equal/nearby/disjoint content,
32/256/2,048 words, short/64/256-byte padding, widths 1/3/5, and reversed asymmetric shapes. It also
includes in-memory engine shared/distinct-reference workloads and name/regex controls at one and
three requested workers. Two variants and three sessions give 1,476 case/participant/session records
per host, each containing all nine raw repetitions. Results use CPU time in the benchmark's declared
unit. These are release measurements, never sanitizer timing.

| Direct case                                            | macOS CPU-time change | Linux CPU-time change |
| :----------------------------------------------------- | --------------------: | --------------------: |
| `BmReferencePercent/disjoint-reversed/2048/1`          |               -21.75% |               -22.95% |
| `BmReferencePercent/disjoint-reversed/2048/3`          |               -17.80% |               -24.07% |
| `BmReferencePercent/disjoint-reversed/2048/5`          |               -14.06% |               -20.88% |
| `BmWordShinglePercent/disjoint-reversed/2048/3`        |               -10.03% |               -18.24% |
| `BmReferencePercent/disjoint/256/3`                    |                +0.02% |                +5.03% |
| `BmReferencePercent/nearby-padding256/2048/3`          |                -0.03% |                -2.66% |
| `BmReferencePercent/nearby-padding256-reversed/2048/3` |                -0.39% |                -4.29% |

Across the complete 246-case direct/engine matrix, selected median session ratios span
-21.75% to +4.17% on macOS and -24.07% to +5.03% on Linux.
Tiny equal-input kernels bypass the intersection loop and run at nanosecond scale; their variation
is retained rather than attributed to the new loop. The +5.03% Linux cached 256-word disjoint
width-three result repeats across all three sessions and is a disclosed cost. Longer padded
cached-reference regressions were worse in rejected variants; no universal neutral-control claim
is made for the selected implementation.

## Footprint

| Host  | Baseline full CLI bytes | Selected full CLI bytes | Change |
| :---- | ----------------------: | ----------------------: | -----: |
| macOS |               8,186,040 |               8,186,040 |     +0 |
| Linux |              18,220,928 |              18,227,152 | +6,224 |

No retained-object layout changes. These are the recorded unstripped full CLI executables; a rebuilt
baseline is identified by its own hash, not assumed identical to an earlier build.

## Correctness and provenance

Both hosts passed all 314 enabled release targets on the frozen chosen source; drogon required its
working FUSE integration. Five affected ASan targets passed on both hosts. The same five MSan and
TSan targets passed on Linux. Actual canonical compile-database clang-tidy checks passed for the
three changed translation units on both hosts.

The added regression compares strongly asymmetric sets in both operand directions through both
exact-score and reusable-reference APIs against the independent set oracle. Existing byte-word,
opaque-byte, fuzz and engine error/read-order coverage remains enabled. Width zero, empty/equal
inputs and integer rounding keep their existing behavior.

Current-parent qualification starts from `c555b87e110da4609834d9a49c64e6276b79ae0b`, after the
retention-fixture repair. Earlier R1/R2/control cohorts on prior heads remain separately labeled;
they are supporting/rejected evidence, not substitutes for current-source qualification. Each
build records compiler identity, flags, link actions, frozen source hashes and executable hashes.
The selected implementation and benchmark/test sources are frozen before building, validation and
timing. Source-restoration guards protect all temporary variant substitutions.

[The lossless compressed evidence](smaller-shingle-probes-20261103.json.gz) retains 3,617 labeled source,
JSON and console-log files across both hosts, including all selected and rejected measured rounds,
protocols, commands, validation results and binary/compiler identities. Generated fixture trees,
executables/objects and canonical compile-database payloads are excluded; their identities and
reconstruction sources are retained. Unrelated #1075 CI-repair artifacts are already published in
that change's evidence. This candidate's report does not claim completion of ZIP seeking,
threshold-aware boolean verification, immutable candidate indexes or grouping.

Archive size: 20,354,253 bytes. SHA-256:
`a4a97bc679425fd672afe16bc88635e719f2c072039d4a76f9c7674e931c24eb`.
