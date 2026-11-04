# Exact threshold-aware similarity verification

`-similar` now stops verification when exact integer bounds decide whether its threshold is
met. The selected implementation rejects impossible cardinalities and checks progress every
32 probes. Both existing percentage APIs remain unchanged. Candidate/reference reads, binary
checks, fresh-byte cache validation, failures and admission limits retain their current order.

The decision compares three implemented strategies rather than assuming that checking more
often is faster. Per-element checks win some cells but cause substantial Linux regressions;
cardinality-only rejection misses larger balanced/disjoint gains. All variants remain in the evidence.

## Native whole-process decision

Changes are the median of three paired session ratios minus one; negative is faster. Each
session shuffles cases and rotates participants over nine measured rounds, retaining the fastest
seven after one discarded warmup per participant/case. An identical baseline-copy participant
records ordinary variation.

| Fixture / width / threshold / workers | macOS elapsed change | Linux elapsed change |
| :------------------------------------ | -------------------: | -------------------: |
| half-1000 / 1 / 100% / 3              |              -10.00% |              -20.19% |
| half-1000 / 3 / 100% / 3              |               -8.49% |              -22.53% |
| disjoint-1000 / 3 / 100% / 1          |               -6.44% |              -17.31% |
| near-1000 / 3 / 50% / 3               |               -2.27% |               -7.52% |
| full-1000 / 3 / 50% / 1               |               -2.21% |               -7.63% |
| full-1000 / 1 / 100% / 1              |               +2.94% |               +1.68% |
| near-1000 / 1 / 100% / 3              |               +2.47% |               +1.31% |
| full-32 / 1 / 50% / 1                 |               +0.38% |               +1.53% |
| full-1000 / 1 / 50% / 3 / raw         |               +0.70% |               +1.14% |
| reference-missing / 1 / 0% / 1        |               +0.83% |               -0.08% |

The fixture suffix is nominal word scale: 32-word fixtures contain 1,000 candidate files;
1,000-word fixtures contain 100. Balanced full/near/half/disjoint cases and both 32:1 asymmetric
directions exercise widths 1/3, thresholds 50/100 and requested workers 1/3. Candidate variants
add unique words, fold case or reverse word order; selection is computed independently.

The matrix has 110 cases, three participants and 9,900 invocations per host including warmups:
19,800 across both hosts. Raw/empty output are unrelated controls. Threshold-zero missing/binary
reference and binary-candidate cases ensure that constant boolean bounds do not bypass content
observations. Every invocation checks the exit and complete stdout against an independent Python
byte-word/shingle-set oracle with the original integer half-up rounding.

Linux uses shared-memory fixtures and recorded CPU affinity; macOS uses local fixtures without
an affinity claim. Paired ratios are local comparisons, not cross-machine normalization.

| Host  | Strongest native gain | Largest native cost | Identical-copy control range |
| :---- | --------------------: | ------------------: | :--------------------------- |
| macos |               -10.00% |              +3.02% | -1.32% to +3.45%             |
| linux |               -22.82% |              +2.79% | -4.68% to +2.76%             |

The slower full-match cells are retained as a real tradeoff. Do not interpret the largest
unrelated binary/output-control variation as the cost of an intersection loop it never reaches.

## Direct strategy comparison

The in-memory matrix contains 96 threshold cases and 16 unchanged exact-percentage controls.
Four participants and three sessions yield 1,344 case/participant/session records per host,
each with nine raw timed repetitions: 2,688 records and 24,192 repetitions across both hosts.
Fixture creation, exact-result validation and reference preparation are outside timed loops.

The 96 cases cover 32/1,000-word scales, six shapes, widths 1/3, thresholds 50/100, and cached
reference versus uncached comparison. Distinct separators keep full-shingle matches from taking
the existing byte-equality shortcut. Timings use real time, converted to microseconds per record.

| Host / direct case              | Cardinality-only change | Per-element change | Batched change |
| :------------------------------ | ----------------------: | -----------------: | -------------: |
| macos / `disjoint/1000/1/100/1` |                  -8.35% |            -22.53% |        -21.74% |
| macos / `half/1000/3/50/1`      |                  -2.44% |            -12.32% |         -9.70% |
| macos / `full/1000/1/100/0`     |                  -2.94% |             +1.54% |         +0.59% |
| macos / `full/32/1/100/1`       |                  -0.48% |             +1.33% |         +1.48% |
| macos / `left-small/32/1/50/1`  |                 -14.14% |            -14.45% |        -16.99% |
| linux / `disjoint/1000/1/100/1` |                  -1.52% |            -25.61% |        -25.40% |
| linux / `half/1000/3/50/1`      |                  +2.21% |            -11.89% |        -11.90% |
| linux / `full/1000/1/100/0`     |                  -1.06% |             +1.95% |         +1.81% |
| linux / `full/32/1/100/1`       |                  -0.86% |             +5.78% |         +3.92% |
| linux / `left-small/32/1/50/1`  |                 -13.79% |            -16.23% |        -15.51% |

Case components are shape / nominal words / width / threshold / cached-reference selector.
The archive retains all raw repetitions, absolute times, exact-percentage controls and every
slower cell. No allocation-count or instrumented timing is used to claim a speedup.

## Exact integer bounds

Let `I` be intersection cardinality, `N` the sum of set cardinalities and `U = N - I`. The
existing percentage is `floor((100*I + floor(U/2))/U)`, with the existing empty-set handling.
For thresholds `t` from 1 through 100, reaching that percentage is exactly equivalent to:

```text
(199 + 2*t) * I >= (2*t - 1) * N
required = ceil(N * (2*t - 1) / (199 + 2*t))
```

The odd-union half-unit does not add another integer case. Quotient/remainder decomposition
computes `required` without multiplying a large cardinality by the numerator. A minimum set
cardinality below `required` rejects immediately. Otherwise the smaller set is probed; each
32-entry checkpoint can succeed when enough intersections exist or fail when the remaining
entries cannot supply enough. Partial final batches use the exact accumulated count.

No percentage API is replaced by an approximation. In particular, 200/201 rounds to 100, so
a set mismatch alone cannot reject a 100% threshold. Width zero retains score zero before
equal-byte handling; absent reference owners also score zero. Out-of-range manually constructed
thresholds preserve their old comparisons. These pure shortcuts happen after engine I/O checks.

## Qualification and retained boundaries

Both hosts pass all 314 enabled release targets and five affected ASan targets. Linux also
passes the five affected MSan/TSan targets and requires FUSE for its complete release run.
Actual canonical compile databases cover six changed translation units and the public header.

Committed tests compare every threshold from -1 through 101 against an independent allocating
oracle over small set cardinalities/overlaps and multiple widths. Large balanced full/near/half/
disjoint cases deterministically cover both early-check outcomes for owned and borrowed keys.
Tests retain width-zero, empty/default/moved owner, rounding and 200/201-versus-200/202 boundaries.

Fresh-reference regression tests exercise initial/repeated/changed/binary/failed/recovered reads
under -1, zero and 101 thresholds for tree, prepared and production-coordinator execution.
The existing cache-admission and isolation tests remain in place. The expanded fuzz harness
compares both boolean APIs with exact percentages over arbitrary bytes and adds extreme signed
thresholds. New checkpoint/fuzz tests pass release and affected sanitizers as explicit supplements.
Each host also completes a separate 30-second bounded ASan/libFuzzer campaign with recorded seed,
limits, logs and platform-specific runtime configuration. This is correctness work, not timing.

There is no new worker, lock, shared cache, file-I/O path, flag or heap wrapper. Literal-reference
admission and invalidation are unchanged, and dynamic references retain fresh observations.
Reference grouping/indexing and independent direct width-one insertion remain separate work.

All source substitutions have restoration guards. Neither host runs builds, tests, lint, fuzz
campaigns or heavy fixture work concurrently with its release timing. The original compile/lint
script failures and fixes remain labeled in the evidence rather than being discarded.

## Footprint and reproduction

| Host  | Baseline full CLI bytes | Batched full CLI bytes | Change |
| :---- | ----------------------: | ---------------------: | -----: |
| macos |               8,205,544 |              8,205,608 |    +64 |
| linux |              18,270,232 |             18,266,440 | -3,792 |

Frozen release/ThinLTO builds use parent `53aa094d807279736cac11ed1d06073c0abff811`, whose Git tree
is identical to synchronized #1079 head `0ccc68077e0e27125b33dfc0549c103383491901`. Worktrees are
synchronized before timing; actual compiler/source/executable/link identities are retained.
Checkpoint and fuzz test additions do not change the measured runtime sources.

The coverage follow-up removes private threshold guards that neither public caller can reach:
both callers already handle thresholds outside `[1, 100]` before constructing sets. It also
extends fresh-read/failure/recovery checks to manually constructed width-zero expressions.
The frozen timing artifact predates that redundant-branch removal; its exact sources remain
archived rather than being relabeled as measurements of a later Git revision. The follow-up
passes all five affected release and ASan suites on both hosts, the same five MSan/TSan suites
on Linux, and actual changed-file source/header checks on both hosts.

Run the retained manual cases with:

```sh
bazel run //xff/matching/similarity:similarity_benchmark --config=clang_release --config=xff_full -- --benchmark_filter='^threshold/'
```

[The compressed evidence](threshold-aware-similarity-20261103.json.gz) retains reconstruction
sources, all variants and measured rounds, exact-output protocols, failed precursors/fixes,
qualification, compiler/link identities, dry runs and fuzz results. Generated fixtures, binaries,
objects and canonical compile-database payloads are excluded.

Archive: 6,324,834 bytes, 549 labeled files. SHA-256:
`092a4d4088ba5b2110d7d95900e28622e9beb83d09e4ad72aca7f3989e1801fc`.
