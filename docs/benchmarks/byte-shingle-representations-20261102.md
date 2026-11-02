# Byte-word shingle representations (2026-11-02)

## Decision

Retain an exact-byte equality shortcut, staged owned keys for one-word shingles, and borrowed views over stable normalized buffers for wider windows. The one-word path keeps the existing word-vector stage and removes the unnecessary singleton join. Wider shingles avoid temporary joined strings and per-word string storage. No shared cache, worker, lock, flag, dependency, exception mechanism or approximate equality is added.

The algorithm remains byte based: ASCII alphanumerics and bytes with the high bit set form words; only ASCII case folds. ASCII separators become internal NUL boundaries, preserving unambiguous keys. Width zero, short/empty inputs, uniqueness, Jaccard and integer rounding retain their contracts. The header explicitly distinguishes this from Unicode decoding/case folding. Views are constructed only after normalized strings reach their final local owners and die before those strings. No moved record contains self-referential views.

O21a is per-comparison representation work only. O21b reference caching and exact candidate indexing remain open: dynamic targets, effects, immutable-source identity, bounded retention and the unresolved run-wide grouping semantics still require their own design.

## Rejected prototypes

A three-way initial matrix isolates baseline, equality-only and all-view implementations. All-view one-word kernels regress on macOS by about 26-33% for larger nearby/disjoint inputs, despite attractive mixed native results. Do not ship that path or hide it behind the mixed-corpus average. A direct word-to-owned-set prototype reduces the problem but does not establish a consistent macOS disjoint win against duplicate-baseline controls. The selected staged construction is separately qualified. All original sources, samples and counterexamples remain in the raw artifact.

## Measurement contracts

Measured parent `d528de1d81f4aeb3dbf369f9b9cb394b362b4156`, identical in tracked contents to main `6bebe940ba36328a399461a19434bf627118bf75` after #1057. Publication parent additionally contains only the isolated #1058 CI cache action/test fix; the declared tooling test was rerun on both hosts. Final runtime changes only `similarity.cc`; common header/test/benchmark support is identical across variant builds. Clean `clang_release`/ThinLTO manifests retain compiler, configuration, link actions, source and executable identities. No build, lint, sanitizer or allocation probe overlaps release timing on its host.

Per host, final qualification has 66 native cases x three sessions (1/10/1,000/10,000 files, one/three requested workers, short/128-word/long-word inputs, pure disjoint/case-only controls, width-one and wider predicates, identity and listing), discarded warmups, nine rotating rounds/fastest seven. An independent Python byte-token/set oracle verifies exact complete stdout and zero exits. Worker requests do not imply that similarity predicates were newly parallelized. Linux uses tmpfs/affinity; macOS uses warm APFS.

Separate whole-engine/kernel timing has 51 cases x three sessions x two variants. Whole engine excludes command parsing/fixture creation; pure kernels measure only the score function. Confirmation adds a byte-identical baseline in ten native cases x five sessions and eight memory cases x three sessions. Initial all-view matrices, equality-only counterfactuals and refinement pilots remain separate populations.

## Native commands

Median paired session change; negative is faster.

| Fixture / task              | CPUs |   Linux |   macOS |
| :-------------------------- | ---: | ------: | ------: |
| short10 / near              |    1 |  -0.41% |  -0.06% |
| words10000 / near           |    1 | -46.02% | -18.38% |
| words10000 / strict         |    3 | -49.03% | -26.65% |
| words10000 / equal          |    1 | -79.61% | -37.85% |
| long10000 / near            |    1 | -21.72% | -20.79% |
| disjointwords10000 / single |    1 | -26.97% |  -4.37% |
| disjointwords10000 / strict |    1 | -40.85% | -19.33% |
| casewords10000 / single     |    3 | -30.54% |  -4.80% |
| short1 / listing            |    1 |  +0.79% |  -0.33% |
| short10000 / listing        |    1 |  +8.21% |  -0.28% |

## Whole-engine and pure-kernel controls

Each unit is explicit; do not compare kernel nanoseconds with whole-engine microseconds or native startup times. Values are median session fastest-seven means.

| Case                                       | Unit | Linux baseline | Linux selected | Linux delta | macOS baseline | macOS selected | macOS delta |
| :----------------------------------------- | :--- | -------------: | -------------: | ----------: | -------------: | -------------: | ----------: |
| engine/production/similar-equal/10000/1    | us   |       2,519.82 |       1,794.44 |     -28.79% |       3,656.87 |       2,345.02 |     -35.87% |
| engine/production/similar-disjoint/10000/1 | us   |       2,387.61 |       2,027.65 |     -15.08% |       3,565.00 |       2,887.18 |     -19.01% |
| engine/production/similar-zero/10000/3     | us   |       2,543.51 |       2,210.68 |     -13.09% |       3,670.96 |       3,059.61 |     -16.65% |
| engine/production/name/10000/3             | us   |       1,134.57 |       1,133.69 |      -0.08% |       1,658.76 |       1,660.70 |      +0.12% |
| BmWordShinglePercent/nearby/2048/1         | ns   |     106,007.25 |      71,351.44 |     -32.69% |     104,337.07 |      85,083.96 |     -18.45% |
| BmWordShinglePercent/disjoint/2048/1       | ns   |     143,265.33 |     109,667.70 |     -23.45% |     131,944.99 |     107,707.04 |     -18.37% |
| BmWordShinglePercent/nearby/2048/5         | ns   |     165,091.97 |      93,009.75 |     -43.66% |     245,646.64 |     157,987.62 |     -35.69% |
| BmWordShinglePercent/equal/2048/5          | ns   |     144,571.90 |         112.39 |     -99.92% |     230,074.48 |         472.33 |     -99.79% |

## Native confirmation and slower controls

Candidate and identical-baseline repeat are independently compared with the baseline participant; no subtraction of repeat movement is applied.

| Fixture / task              | CPUs | Initial Linux | Confirm Linux | Repeat Linux | Initial macOS | Confirm macOS | Repeat macOS |
| :-------------------------- | ---: | ------------: | ------------: | -----------: | ------------: | ------------: | -----------: |
| short10 / listing           |    3 |        +0.90% |        +1.35% |       -0.90% |        +1.33% |        +1.18% |       +0.30% |
| disjointwords10000 / single |    1 |       -26.97% |       -27.52% |       +0.03% |        -4.37% |        -4.47% |       +0.21% |
| words10000 / equal          |    1 |       -79.61% |       -79.81% |       -0.31% |       -37.85% |       -38.11% |       +0.23% |
| long10 / strict             |    3 |        -6.46% |        -3.91% |       -1.03% |        -2.95% |        -2.50% |       +0.37% |
| casewords10000 / single     |    3 |       -30.54% |       -30.30% |       -0.02% |        -4.80% |        -4.53% |       -0.06% |
| short10 / near              |    1 |        -0.41% |        +1.19% |       -0.99% |        -0.06% |        +1.25% |       +0.46% |
| long10000 / near            |    1 |       -21.72% |       -21.99% |       +0.04% |       -20.79% |       -20.58% |       +0.11% |
| words10000 / near           |    1 |       -46.02% |       -46.11% |       +0.03% |       -18.38% |       -18.43% |       -0.01% |
| short10000 / listing        |    1 |        +8.21% |        +2.36% |       +0.95% |        -0.28% |        +0.66% |       +0.27% |
| words10000 / strict         |    3 |       -49.03% |       -49.15% |       -0.18% |       -26.65% |       -25.80% |       -0.30% |

## Allocation diagnostics

ASan replaces the allocator: these are warmed calling-thread allocation/requested-byte counts, not production timings or exact production allocator behavior. The probe is a separate executable and its transient Bazel/source edits are restored. Normal release timings above come from separate uninstrumented binaries.

| Case               | Host  | Baseline allocations | Selected allocations | Baseline requested bytes | Selected requested bytes |
| :----------------- | :---- | -------------------: | -------------------: | -----------------------: | -----------------------: |
| equal/2048/5       | linux |                4,211 |                    0 |                  657,232 |                        0 |
| equal/2048/5       | macos |                4,196 |                    0 |                  572,976 |                        0 |
| nearby/2048/1      | linux |                  125 |                  112 |                  510,608 |                  457,808 |
| nearby/2048/1      | macos |                  116 |                  120 |                  472,688 |                  459,856 |
| nearby/2048/5      | linux |                4,220 |                  131 |                  891,632 |                  507,384 |
| nearby/2048/5      | macos |                4,220 |                  126 |                  940,784 |                  503,520 |
| repeated/2048/4096 | linux |                   28 |                   20 |                  217,488 |                   49,104 |
| repeated/2048/4096 | macos |                   28 |                   20 |                  217,488 |                   49,104 |

## Correctness and footprint

Independent reference tests cover separator normalization, ambiguous boundaries, duplicate words, long words, zero/one/oversized widths, empty inputs, opaque byte values and seeded reused comparisons. Existing read-only engine tests retain conditional target reads, binary/failure handling and dynamic templates. All 323 extension-enabled release tests pass per host, with required Linux FUSE; six focused ASan suites pass per host and six Linux TSan suites pass. Scoped lint and normal enforcing hooks remain required; current pushed-head CI determines readiness.

- linux: unstripped CLI 12,393,880 -> 12,402,544 bytes (+8,664).
- macos: unstripped CLI 4,079,152 -> 4,079,328 bytes (+176).

## Lossless evidence

[byte-shingle-representations-20261102.json.gz](byte-shingle-representations-20261102.json.gz): 2,751,662 bytes; SHA-256 `804df02ab92bfec0f0dde9af7cbf09d5f37151e43f1617317525091145d7e145`.
