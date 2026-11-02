# Prepare compact hash operands (2026-11-02)

## Decision

Retain compact prepared records for `-hash` and `-hasheq`: resolve explicit algorithm/encoding syntax once using the existing hash registry, and resolve only requested defaults from each reached context. Keep the expected-hash template under its existing single unique owner. No algorithm spelling list, public hash API, flag, lock, thread, shared owner, per-file clock or new mutation route is introduced.

A prepared record stores a checked index into `AlgorithmNames()`, encoding/default-use bits, validity and the optional expected-template owner. A compile-time assertion keeps it within the existing 24-byte operand alternative, so unrelated operands do not grow. Preparation callbacks receive the expression through existing constexpr dispatch metadata; argument-only families use one registered adapter and preserve their previous missing-argument guard. A boolean in existing dispatch-record padding permits bare hash preparation without another callback pointer.

Direct tree callbacks remain the reference. Expected-field rendering, conditional content reads, malformed/missing operands and hash-verification bookkeeping retain their order. Reused/moved plans observe each direct caller's current defaults; no view into temporary spec/default strings is cached. Explicit algorithms use the static registry index, while default-dependent algorithms still validate the current direct context.

Preparation is not free: bare hash now owns an inline pool record/capacity, and decoding explicit syntax once increases preparation work. Hash digests, snapshot cache keys, field rendering and output construction still allocate/copy as before. O22 remains separate. The narrower operation does not close the entire EP03/EP07 qualification plan.

## Measurement contracts

Parent `77abeacdcc8fad1028ae5485e66208dc63ec5c7b` (#1055), clean `clang_release`/ThinLTO variants. Runtime changes only `evaluate.cc`; common support sources are identical between builds. Build manifests capture compiler version/binary hash, actual build/toolchain configuration, complete CLI link actions including parameter contents, source and executable hashes. Copied benchmark binaries load only stable system libraries. No build, lint or sanitizer overlaps timings on its host.

Per host: 56 native cases x three sessions, independent complete stdout/hashlib digest/zero-exit oracles, discarded warmups and nine rotating rounds/fastest-seven. Inputs include 1/10/1,000/10,000 seven-byte files and 1,000 4-KiB files. Linux tmpfs/affinity and macOS warm APFS are separate host contracts. Native requested workers 1/3 do not imply parallel hash actions. Globals after primaries are exercised deliberately.

Separate in-memory populations cover 36 whole-engine cases, 16 hot kernels and eight preparation-only cases, in three sessions per variant, with nine repetitions/fastest-seven. Whole engine includes run preflight/preparation/traversal/hash/output/teardown, excluding parser/fixture construction; preparation and hot kernels retain the existing expression-benchmark contracts. Do not compare their absolute values to native process startup or apply external-tool normalization.

Confirmation adds a byte-identical baseline: eight native cases x five sessions and 12 memory cases x three sessions. The initial private Linux pilot and missing-duplicate-executable setup failure remain separate from final qualification. Unrelated name/diff controls and slower cases remain visible.

## Native command controls

Median paired session change, negative is faster. File I/O dominates these commands, so hot-loop savings need not produce a large native change.

| Task            |  Files | Workers |  Linux |  macOS |
| :-------------- | -----: | ------: | -----: | -----: |
| hash-md5        |      1 |       1 | +0.34% | -0.41% |
| hash-md5        | 10,000 |       1 | -0.69% | -0.21% |
| hash-global     |      1 |       1 | +2.27% | -0.51% |
| hash-global     | 10,000 |       1 | +1.28% | -0.02% |
| hash-sha        |      1 |       1 | +1.15% | -0.06% |
| hash-sha        | 10,000 |       1 | -0.09% | -0.03% |
| hasheq-md5      |      1 |       1 | +1.35% | -0.24% |
| hasheq-md5      | 10,000 |       1 | -0.80% | +0.18% |
| hasheq-template |      1 |       1 | +0.04% | -0.11% |
| hasheq-template | 10,000 |       1 | +0.34% | -0.41% |
| hasheq-global   |      1 |       1 | +0.68% | +0.10% |
| hasheq-global   | 10,000 |       1 | +0.60% | -0.11% |

## In-memory whole-engine timing

Median session fastest-seven means; microseconds per complete run. Delta is derived from the displayed population estimates, not a native-command estimate.

| Task            |  Files | Linux baseline us | Linux prepared us | Linux delta | macOS baseline us | macOS prepared us | macOS delta |
| :-------------- | -----: | ----------------: | ----------------: | ----------: | ----------------: | ----------------: | ----------: |
| hash-explicit   |     10 |             12.55 |             12.47 |      -0.64% |             20.58 |             20.07 |      -2.46% |
| hash-explicit   | 10,000 |          3,078.92 |          2,877.30 |      -6.55% |          6,234.00 |          5,808.54 |      -6.82% |
| hasheq-explicit |     10 |             13.33 |             13.20 |      -0.94% |             21.47 |             21.03 |      -2.03% |
| hasheq-explicit | 10,000 |          3,353.68 |          3,061.88 |      -8.70% |          6,493.84 |          6,058.14 |      -6.71% |
| hash-default    |     10 |             13.84 |             14.17 |      +2.41% |             21.61 |             21.42 |      -0.92% |
| hash-default    | 10,000 |          4,330.08 |          4,236.57 |      -2.16% |          7,019.34 |          6,978.74 |      -0.58% |
| hasheq-default  |     10 |             15.38 |             15.29 |      -0.59% |             23.30 |             23.49 |      +0.81% |
| hasheq-default  | 10,000 |          5,227.51 |          5,089.05 |      -2.65% |          8,427.21 |          8,423.33 |      -0.05% |
| name            |     10 |             10.22 |             10.21 |      -0.08% |             15.37 |             15.35 |      -0.11% |
| name            | 10,000 |          1,163.09 |          1,113.81 |      -4.24% |          1,658.41 |          1,622.85 |      -2.14% |
| diff-normalized |     10 |             19.99 |             19.94 |      -0.25% |             29.11 |             29.60 |      +1.67% |
| diff-normalized | 10,000 |          3,942.05 |          3,769.13 |      -4.39% |          5,145.95 |          5,169.39 |      +0.46% |

## Preparation costs

Nanoseconds per one-primary preparation under the existing benchmark contract. Preserve absolute costs alongside ratios; do not project these percentages onto full CLI startup.

| Primary family | Linux baseline ns | Linux prepared ns | Linux delta | macOS baseline ns | macOS prepared ns | macOS delta |
| :------------- | ----------------: | ----------------: | ----------: | ----------------: | ----------------: | ----------: |
| hash-explicit  |            168.97 |            229.03 |     +35.54% |            363.39 |            436.19 |     +20.03% |
| hash-default   |            163.24 |            222.27 |     +36.16% |            360.20 |            408.31 |     +13.36% |
| hash-template  |            252.89 |            321.58 |     +27.16% |            501.68 |            545.17 |      +8.67% |
| name           |            196.06 |            222.91 |     +13.70% |            418.04 |            421.50 |      +0.83% |

## Native confirmation and slower controls

Each confirmation contains the candidate and an identical baseline executable, both relative to the baseline participant. These results supplement the initial samples.

| Fixture / task            | Workers | Initial Linux | Confirm Linux | Repeat Linux | Initial macOS | Confirm macOS | Repeat macOS |
| :------------------------ | ------: | ------------: | ------------: | -----------: | ------------: | ------------: | -----------: |
| flat10 / hash-global      |       3 |        +2.27% |        +1.34% |       -0.45% |        -0.74% |        +0.87% |       +1.48% |
| flat10000 / listing       |       1 |        +2.95% |        +2.46% |       -0.77% |        -0.17% |        +0.20% |       -0.01% |
| flat10000 / hash-md5      |       1 |        -0.69% |        -0.26% |       +0.76% |        -0.21% |        +0.12% |       +0.25% |
| flat10000 / hasheq-global |       3 |        +1.18% |        +1.63% |       -0.38% |        -0.52% |        +0.98% |       +0.66% |
| flat10000 / hasheq-md5    |       3 |        -0.11% |        +0.70% |       +0.43% |        +0.76% |        +0.44% |       +0.34% |
| flat1 / hash-sha          |       3 |        +2.45% |        +1.48% |       +0.17% |        -0.23% |        +1.32% |       +0.98% |
| large1000 / hash-md5      |       1 |        -0.21% |        +0.13% |       +0.02% |        -0.12% |        -0.38% |       -0.04% |
| flat10 / listing          |       3 |        +2.79% |        +1.08% |       -0.91% |        +0.11% |        +0.73% |       +0.02% |

The raw artifact also retains memory confirmation, hot-kernel results, all non-displayed counts/workers, owned-byte counters and individual samples. A faster unrelated name/diff control is not evidence that its algorithm was improved by hash binding. No universal speedup or reduction in content I/O is claimed.

## Correctness and footprint

Tests cover all registered algorithms and both encodings, changing valid/invalid/empty direct defaults across reused and moved plans, malformed specs with content-dependent/empty expected templates, empty/binary input, read failures, short-circuiting and missing arguments. Existing evaluator/whole-run suites retain final CLI defaults, archive/VFS sources, exactly-once effects, summaries and snapshot behavior. The new fixture cannot mutate the host.

All 322 extension-enabled release tests pass per host, with required Linux FUSE; six focused ASan suites pass per host and six Linux TSan suites pass. Scoped local lint covers all modified translation units. Normal enforcing hooks and current pushed-head CI remain required before merging.

- linux: unstripped CLI 12,360,488 -> 12,387,664 bytes (+27,176).
- macos: unstripped CLI 4,071,248 -> 4,078,832 bytes (+7,584).

## Lossless evidence

[prepared-hash-operands-20261102.json.gz](prepared-hash-operands-20261102.json.gz): 1,069,985 bytes; SHA-256 `21fa9fb0387b4ae60123cc64632f12ed731451258fc6c673862a41da26dd8661`. Final, confirmation and private-pilot populations retain separate identities.
