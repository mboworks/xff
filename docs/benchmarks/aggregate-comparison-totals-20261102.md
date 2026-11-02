# Aggregate ordinary comparison totals (2026-11-02)

## Decision

Retain direct side aggregation for ordinary overall/type/extension comparison summaries when their scopes need only side totals, combined totals or roots. Previously even default left/right totals retained a summary vector and maps per path, plus a category map, then rebuilt those totals after pairing. The selected path keeps existing root aggregates and constructs the side columns directly.

Eligibility uses the existing independent-filter audit. Any category scope, complex summary, action, traversal control, collection or verification retains the previous per-entry implementation. It adds no flag-name checks or new effect vocabulary. This qualification covers O16a only: the two ordered tree maps, comparison ordering, worker policy, reads and first-output latency are unchanged. O16b completed-directory streaming and O17/O18 stage scheduling remain open.

## Measurement contracts

Measured parent `4e77548bebf05152bb55b64dadd8e7ec9da8f539` (#1056), clean `clang_release`/ThinLTO variants changing only `run.cc`. Common benchmark/test sources are identical. Manifests retain compiler identity, build configuration, link actions and executable/source hashes. macOS uses warm APFS; Linux uses tmpfs and pins each case to its 1/3 CPU allowance. Native invocation includes process startup; in-memory whole-engine timing excludes parser/fixture construction. Neither is sanitizer timing.

Per host: 72 native cases x three sessions (broad/deep trees, 10/1,000/10,000 paths, 1/3 requested workers, overall/type/extension totals and category/mixed/status controls), discarded warmups and fastest seven of nine rotating rounds. Complete stdout equality and independent physical side count/byte and comparison-category oracles must pass. Separate in-memory timing has 30 cases x three sessions x two variants. Confirmation adds a byte-identical baseline: eight native cases x five sessions and five in-memory cases x three sessions x three variants. Source/link/test logs and all slower controls are retained; no cross-tool normalization masks a within-host regression.

## Native commands

Median paired session change, negative is faster.

| Fixture / task        | Workers |   Linux |  macOS |
| :-------------------- | ------: | ------: | -----: |
| broad10 / overall     |       1 |  +0.02% | -0.38% |
| broad1000 / ext       |       1 | -14.42% | -4.04% |
| broad10000 / ext      |       1 | -16.80% | -4.95% |
| broad10000 / type     |       3 | -22.26% | -5.96% |
| deep10000 / overall   |       1 | -18.41% | -5.47% |
| deep10000 / type      |       3 | -22.13% | -6.88% |
| broad10000 / category |       1 |  +1.04% | +0.46% |
| deep10000 / mixed     |       3 |  -1.02% | +0.05% |
| broad10000 / status   |       3 |  +0.75% | +0.27% |

## In-memory whole-engine timing

Microseconds per run; median of each session's fastest-seven mean. Fixture construction and command parsing are outside the timed loop; run preflight, traversal, comparison, summaries and teardown remain inside.

| Task / scope                                       |  Files | CPUs | Linux baseline us | Linux aggregate us | Linux delta | macOS baseline us | macOS aggregate us | macOS delta |
| :------------------------------------------------- | -----: | ---: | ----------------: | -----------------: | ----------: | ----------------: | -----------------: | ----------: |
| overall / compare                                  |     10 |    1 |             44.44 |              41.94 |      -5.62% |             57.81 |              54.17 |      -6.30% |
| overall / compare                                  | 10,000 |    1 |          9,884.16 |           5,069.58 |     -48.71% |          9,699.73 |           3,683.39 |     -62.03% |
| type / compare                                     | 10,000 |    3 |          8,887.72 |           4,113.35 |     -53.72% |         10,037.93 |           4,077.63 |     -59.38% |
| ext / compare                                      | 10,000 |    1 |          9,917.12 |           5,431.53 |     -45.23% |          9,601.70 |           3,703.04 |     -61.43% |
| overall / left-only,right-only,different,identical | 10,000 |    1 |         10,075.56 |          10,107.73 |      +0.32% |         10,086.23 |           9,809.10 |      -2.75% |
| ext / left-total,left-only                         | 10,000 |    3 |          8,824.97 |           8,699.36 |      -1.42% |         10,082.80 |           9,879.85 |      -2.01% |

## Confirmation and slower controls

An identical baseline participant makes scheduling/sample movement visible; no subtraction of that movement is applied to candidate results.

| Fixture / task        | CPUs | Initial Linux | Confirm Linux | Repeat Linux | Initial macOS | Confirm macOS | Repeat macOS |
| :-------------------- | ---: | ------------: | ------------: | -----------: | ------------: | ------------: | -----------: |
| broad10 / overall     |    1 |        +0.02% |        +0.39% |       +0.72% |        -0.38% |        +0.85% |       +0.31% |
| deep10000 / overall   |    1 |       -18.41% |       -18.20% |       -0.30% |        -5.47% |        -4.78% |       +0.22% |
| deep10000 / mixed     |    3 |        -1.02% |        +0.37% |       +1.99% |        +0.05% |        +1.16% |       +0.63% |
| broad10000 / category |    1 |        +1.04% |        +1.33% |       +1.28% |        +0.46% |        +1.23% |       +1.01% |
| broad10 / status      |    3 |        +2.21% |        -0.75% |       -1.30% |        -0.06% |        +1.12% |       +0.54% |
| deep10000 / type      |    3 |       -22.13% |       -21.48% |       -0.81% |        -6.88% |        -5.16% |       +0.10% |
| broad10000 / status   |    3 |        +0.75% |        -0.67% |       +1.60% |        +0.27% |        +0.39% |       +1.04% |
| broad10000 / ext      |    1 |       -16.80% |       -15.59% |       -0.66% |        -4.95% |        -4.59% |       +0.36% |

## Memory, size and correctness

Peak RSS is from the native launcher and includes the complete process, not only summary storage. Whole-tree retention remains linear; this change does not introduce a bounded streaming algorithm.

| Host / large overall fixture | Baseline peak MiB | Aggregate peak MiB |
| :--------------------------- | ----------------: | -----------------: |
| linux / broad10000           |             16.46 |              14.09 |
| macos / broad10000           |             18.03 |              14.53 |

All 323 extension-enabled release tests pass on each host, with required Linux FUSE. Five focused ASan suites pass on each host; five Linux TSan suites pass. All three modified translation units pass scoped local lint. Dedicated read-only VFS tests compare direct totals against the existing action fallback across scopes, filters and repeated summaries; independent expected counts cover directory roots, file/directory changes and empty sides. Formatting oracles compare complete baseline/candidate output. Normal enforcing hooks and current pushed-head CI remain required.

- linux: unstripped CLI 12,387,664 -> 12,393,880 bytes (+6,216).
- macos: unstripped CLI 4,078,832 -> 4,079,152 bytes (+320).

## Lossless evidence

[aggregate-comparison-totals-20261102.json.gz](aggregate-comparison-totals-20261102.json.gz): 1,217,750 bytes; SHA-256 `c5c9037feb003ea1a5bf5cdffb44d9baa561706ec7a9392f5d270d921211a14f`.
