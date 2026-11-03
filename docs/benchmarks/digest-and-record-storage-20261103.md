# Borrowed digests and record storage (2026-11-03)

## Decision

Borrow already cached digest bytes for hash actions, equality checks and supplied-snapshot fields. Keep the owning API and the local-snapshot field fallback. Build plain records directly in their final storage; bulk-append printable runs when C-escaping instead of mutating the string for every ordinary byte.

The digest cache layout and lookup policy do not change. Map-node storage remains stable across other insertions; invalidation, transfer, moving or destroying the snapshot ends the public borrow contract. Action output owns its final concatenation before calling the sink. Field transforms consume the view while the supplied snapshot is alive. No new cache, lock, worker, flag, dependency, exception path or host-I/O route is introduced.

Tiny colored records have slower isolated cells even where absolute changes are only a few nanoseconds. Retain those controls. O22b invariant hash-field binding and O22c command-owned reusable output/row buffers remain open; this change does not complete them.

## Measurement contracts

Fixed parent `35f404af9e29c4d1e13db57bdec923b91d143b3f` (#1066). Four canonical variants separate the mechanism: `baseline`, `digest`, `record` (direct construction plus bulk C-escaping), and `both`. Common benchmark/test support is outside the runtime variants. Build manifests retain compiler bytes/version, source/binary hashes, effective release/ThinLTO settings and link actions. Later dependency changes require fresh qualification.

Per host: 40 native cases over 10/1,000/10,000 files, longer paths and larger content, one/three requested workers, listing, hash actions, repeated hashes, expected digest templates and repeated/transformed fields; three sessions, nine rotating rounds and fastest seven retained. Independent Python hashlib values and complete stdout/zero exits are checked. macOS uses warm APFS; Linux uses tmpfs with CPU affinity. Requested workers do not imply newly parallel hash actions.

The in-memory matrix has 82 cases x three sessions x four variants. Final memory runs batch at most eight cases with the same benchmark family and CPU allowance per process, rotate participant/block order, and retain each case's own warmup and nine timed repetitions. This amortizes an untimed Google Benchmark 1.9.5 CPU metadata fallback: missing macOS `hw.cpufrequency` otherwise triggers a one-second busy-loop estimate per fresh process. The reported 24 MHz is the ARM counter, not core frequency. A three-case pilot verifies exact selection/repetition counts. The complete separate Linux case-isolated matrix is retained.

Confirmation adds an identical baseline participant: eight native cases x five sessions and 14 memory cases x three sessions. Duplicate-baseline movement is shown without subtracting it. Timing does not overlap builds, lint, sanitizers or allocation probes on the same host.

## Native commands

Median paired session change; negative means less elapsed time.

| Fixture / action           | CPUs |  Linux |  macOS |
| :------------------------- | ---: | -----: | -----: |
| flat10 / listing           |    1 | +0.44% | +0.62% |
| flat10000 / listing        |    1 | -0.62% | +1.73% |
| flat10000 / listing        |    3 | +1.93% | -0.06% |
| long1000 / listing         |    1 | +0.18% | +0.17% |
| flat10000 / hash-repeat    |    1 | +0.42% | +0.11% |
| flat10000 / fields         |    3 | +1.22% | -1.37% |
| flat1000 / hasheq-template |    1 | -0.97% | +0.69% |
| large1000 / hash-repeat    |    1 | +0.67% | +0.02% |

## Confirmation controls

| Fixture / action           | CPUs | Linux candidate | Linux repeat | macOS candidate | macOS repeat |
| :------------------------- | ---: | --------------: | -----------: | --------------: | -----------: |
| flat10000 / hash-repeat    |    1 |          +0.26% |       -0.11% |          -0.08% |       +0.35% |
| flat10 / listing           |    1 |          +3.30% |       +0.71% |          +2.61% |       +0.39% |
| flat10000 / fields         |    3 |          +0.05% |       +0.89% |          -0.02% |       +0.44% |
| flat1000 / hasheq-template |    1 |          +1.32% |       +0.32% |          +0.68% |       +0.32% |
| long1000 / listing         |    1 |          +1.39% |       -0.59% |          +1.11% |       +0.99% |
| flat10000 / listing        |    3 |          +3.22% |       +0.77% |          +0.12% |       +0.05% |
| large1000 / hash-repeat    |    1 |          +1.29% |       +0.19% |          +0.23% |       -0.14% |
| flat10000 / listing        |    1 |          +3.55% |       +0.87% |          +1.06% |       +0.58% |

## Largest native slowdowns and controls

Largest paired changes across all native cases, including near-neutral controls. Absolute times and paired percentage changes are separate medians.

| Host  | Fixture / action            | CPUs | Baseline ms | Selected ms | Change |
| :---- | :-------------------------- | ---: | ----------: | ----------: | -----: |
| linux | flat10 / fields-transform   |    1 |       0.872 |       0.901 | +3.01% |
| linux | flat10 / hash-sha           |    1 |       0.880 |       0.904 | +2.66% |
| linux | flat1000 / fields           |    3 |       3.418 |       3.441 | +2.35% |
| linux | flat1000 / hash-sha         |    3 |       2.631 |       2.690 | +2.15% |
| macos | flat10000 / listing         |    1 |      12.695 |      12.853 | +1.73% |
| macos | flat1000 / fields-transform |    3 |      19.477 |      19.598 | +1.07% |
| macos | flat10 / hasheq-template    |    3 |       4.622 |       4.670 | +1.05% |
| macos | flat1000 / hasheq-template  |    1 |      19.683 |      19.630 | +0.69% |

## Isolated and whole-engine attribution

Every cell is a paired three-session median; baseline values use the unit named in the case. Whole-engine iterations include preparation, traversal, metadata/content, sinks and teardown; parsing/fixture construction are outside timing. Isolated field cases distinguish freshly invalidated (`0`) from cached (`1`) snapshots.

| Case                                         | Host  |  Baseline | Unit | Digest only | Record only |    Both |
| :------------------------------------------- | :---- | --------: | :--- | ----------: | ----------: | ------: |
| output/record/1/0/1                          | linux |     13.61 | ns   |      -0.12% |     +39.75% | +42.07% |
| output/record/1/0/1                          | macos |     10.06 | ns   |      -3.70% |     +94.97% | +96.27% |
| output/record/160/0/0                        | linux |     20.87 | ns   |      +0.32% |     -60.85% | -59.63% |
| output/record/160/0/0                        | macos |     35.80 | ns   |      +1.06% |     -35.28% | -34.43% |
| output/record/160/1/1                        | linux |    293.62 | ns   |      +9.07% |     -73.26% | -71.66% |
| output/record/160/1/1                        | macos |    527.36 | ns   |      -0.07% |     -62.99% | -58.14% |
| output/hash-fields/16/0                      | linux |    942.96 | ns   |     -22.35% |      +0.42% | -22.51% |
| output/hash-fields/16/0                      | macos |  1,771.35 | ns   |     -15.58% |      -0.66% | -17.75% |
| output/hash-fields/16/1                      | linux |    703.88 | ns   |     -30.15% |      -2.99% | -31.61% |
| output/hash-fields/16/1                      | macos |  1,313.09 | ns   |     -21.25% |      +0.84% | -24.89% |
| engine/production/hash-fields-repeat/10000/1 | linux |  9,210.13 | us   |      -8.02% |      -3.40% |  -7.84% |
| engine/production/hash-fields-repeat/10000/1 | macos | 17,650.84 | us   |      -8.46% |      -1.16% |  -7.43% |
| engine/production/hash-repeat/10000/1        | linux | 10,897.84 | us   |      -3.88% |      -0.58% |  -3.99% |
| engine/production/hash-repeat/10000/1        | macos | 18,096.61 | us   |      -3.82% |      -0.42% |  -3.85% |

## Slower isolated controls

Largest output-kernel regressions, retaining absolute time alongside percentages. These are isolated records/fields, not whole-command slowdowns.

| Host  | Case                   | Baseline | Selected | Unit |  Change |
| :---- | :--------------------- | -------: | -------: | :--- | ------: |
| linux | output/record/1/0/1    |    13.61 |    19.30 | ns   | +42.07% |
| linux | output/record/1/1/1    |    23.67 |    31.69 | ns   | +34.24% |
| linux | output/record/1/1/0    |    20.01 |    20.71 | ns   |  +3.41% |
| linux | output/hash-fields/1/0 |   287.92 |   276.60 | ns   |  -4.17% |
| macos | output/record/1/0/1    |    10.06 |    19.75 | ns   | +96.27% |
| macos | output/record/1/1/1    |    23.68 |    40.21 | ns   | +69.83% |
| macos | output/record/15/0/1   |    25.97 |    36.49 | ns   | +36.96% |
| macos | output/record/1/1/0    |    21.30 |    27.75 | ns   | +28.05% |

## Allocation diagnostics

Separate warmed calling-thread ASan counters count allocation requests/bytes only. Fixture setup and reporting are excluded; cold fields include invalidation. These are not release timings, retained memory or peak RSS.

| Case                    | Host  | Baseline requests | Selected requests | Baseline bytes | Selected bytes |
| :---------------------- | :---- | ----------------: | ----------------: | -------------: | -------------: |
| output/record/160/0/0   | linux |                 2 |                 1 |            352 |            176 |
| output/record/160/0/0   | macos |                 2 |                 1 |            336 |            168 |
| output/record/160/1/1   | linux |                 4 |                 2 |            528 |            528 |
| output/record/160/1/1   | macos |                 4 |                 2 |            520 |            528 |
| output/hash-fields/16/1 | linux |                21 |                 5 |          3,760 |          2,480 |
| output/hash-fields/16/1 | macos |                21 |                 5 |          3,384 |          2,232 |

## Correctness and footprint

Both hosts pass all 325 extension-enabled release suites and eight focused ASan suites. Linux additionally passes eight focused MSan and eight TSan suites, with FUSE required for release testing. Exact selected-source guards precede final validation. Lifetime, insertion stability, owning fallback, invalidation, cached errors, derived handoff, empty/binary bytes, escape boundaries, adjacent controls and tree/display consumers are exercised. Real scoped lint and failed-file refinements are retained; normal hooks/current-head CI remain required for PR readiness.

- linux: unstripped full CLI 18,121,664 -> 18,120,272 bytes (-1,392).
- macos: unstripped full CLI 8,140,216 -> 8,140,216 bytes (+0).

## Rejected prototype and remaining work

The first combined prototype reduced raw-record allocations but regressed 160-byte escaped Linux records by roughly 9-10% in confirmation. It is not selected. Its full Linux matrix, confirmation, sanitizer/allocation evidence, complete Mac native matrix and explicitly partial Mac memory population are retained. Remaining Mac prototype work was intentionally stopped after rejection; no full Mac validation is claimed for it. The revised candidate separately qualifies bulk printable-run appends and all affected escape consumers.

Explicit hash-field binding and command-owned reusable record/row buffers remain separate O22 work. Source/archive replay, coordinated workers and bounded pipeline overlap remain open in the main inventory.

## Lossless evidence

[digest-and-record-storage-20261103.json.gz](digest-and-record-storage-20261103.json.gz): 3,646,571 bytes; SHA-256 `5841e5615cbc286b7fb77820f2049e99d7523b74e0c9ebd38e6694f195fa7faa`.
