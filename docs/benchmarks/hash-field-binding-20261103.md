<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Compact hash-field binding (2026-11-03)

## Decision and contracts

Bind static hash qualifiers when a field template is compiled. Two bytes beside the existing
post-processing enum hold an algorithm catalog ID and encoding/default choice. IDs derive from
`hash::AlgorithmNames`; there is no second algorithm catalog, string-encoded state, per-segment
allocation or added string view. Tracked template storage is unchanged for every measured shape.
This conservative capacity accounting is not an allocator/RSS measurement.

Explicit algorithms and encodings avoid repeated qualifier parsing. Defaults still resolve against
each render context, including changes after compilation, copied/moved templates, invalid defaults,
and post-render substitutions/extractions. Independent explicit choices ignore invalid unrelated
defaults. Local snapshot results remain owned; supplied snapshot digest views keep their existing
lifetimes and invalidation behavior. Static diagnostics, unknown-field behavior and output bytes
remain unchanged. No public flag, dependency, worker, lock, content cache or host-I/O route is added.

The first prototype duplicated hash qualifier validation during compilation and loaded a binding
tag to dispatch every field. The selected refinement validates the bound result once, binds bare
and transformed defaults directly, and reuses the already loaded field function for dispatch.
This removes most of the extra compilation cost. It still adds work to some unrelated field paths;
those controls and absolute costs are retained below.

## Protocol

Frozen parent `8c12718f8c04b8e2772e9133ecb4e6fac025da2c` includes the synchronized #1068 parent
and #1069 materialization implementation. Filename-aware formatting precedes frozen builds.
Compiler hashes, configurations, release link actions, source identities and binary sizes are
retained. Both hosts use the same baseline and common tests/benchmark support.

The first comparison has 47 independent cases x three sessions x two variants (282 participant-case
runs per host). Refinement compares baseline, first prototype and selected implementation over the
same 47 cases x three sessions (423 runs per host). The reference/first binaries are reused only
after proving identical compiler/configuration and exact binary hashes. Populations remain separate.

Independent confirmation adds 20 native cases x three sessions, with three participants: baseline,
selected, and a byte-identical baseline repeat. Every case discards a warmup per participant, checks
all stdout and exit codes against independently constructed paths/hashlib digests, and retains the
fastest seven of nine rounds (1,800 invocations per host including warmups). Native fixtures cover
10/1,000/10,000 files, long-path controls, one/three requested workers, listing and ordinary/transformed
hash fields. Linux fixtures use tmpfs and CPU affinity; macOS uses warm APFS without pinning.

Separate memory confirmation has 16 cases x three sessions x three participants (144 runs per host).
Every memory case retains nine repetitions/fastest seven. Batches of at most eight compatible
family/CPU cases amortize untimed Google Benchmark metadata startup; each retains its own fixture,
warmup and measured repetitions. Whole engine cases use an in-memory VFS. No timing overlaps builds,
lint or instrumentation on the same host. Reference-repeat movement is shown, never subtracted.

Hash-binding scenario IDs: `0` context defaults (`md5/base64`); `1` explicit `md5/base64`;
`2` explicit `md5` with context encoding; `3` context algorithm with explicit `base64`;
`4` post-render substitution using context defaults. The preceding counts are repeated fields
(`1`/`16`) and warm-cache choice (`0` cold/`1` warm). Compile scenarios are `0` name,
`1` ordinary mixed fields, `2` bare hash, `3` explicit hash and `4` mixed/transformed hash fields.

## Memory and whole-engine results

Median paired-session elapsed-time changes; negative means faster. Initial is the complete refined
three-variant comparison, not the rejected prototype. A missing confirmation cell means that case
was not repeated in the separate confirmation matrix. All raw cases remain in the archive.

| Case                                           | Linux initial | Linux confirm | Linux repeat | macOS initial | macOS confirm | macOS repeat |
| :--------------------------------------------- | ------------: | ------------: | -----------: | ------------: | ------------: | -----------: |
| engine/production/hash-fields-repeat/10/1      |        -1.72% |           n/a |          n/a |        -2.96% |           n/a |          n/a |
| engine/production/hash-fields-repeat/10/3      |        -1.82% |           n/a |          n/a |        -3.48% |           n/a |          n/a |
| engine/production/hash-fields-repeat/1000/1    |        -4.12% |        -4.32% |       +0.31% |        -3.04% |        -4.96% |       +0.60% |
| engine/production/hash-fields-repeat/1000/3    |        -3.48% |           n/a |          n/a |        -5.65% |           n/a |          n/a |
| engine/production/hash-fields-transform/10/1   |        -1.21% |           n/a |          n/a |        -1.01% |           n/a |          n/a |
| engine/production/hash-fields-transform/10/3   |        -1.28% |           n/a |          n/a |        +1.96% |           n/a |          n/a |
| engine/production/hash-fields-transform/1000/1 |        -6.12% |           n/a |          n/a |        -1.58% |           n/a |          n/a |
| engine/production/hash-fields-transform/1000/3 |        -4.39% |        -5.39% |       +1.39% |        -3.77% |        -1.59% |       +1.70% |
| engine/production/name/10/1                    |        -0.29% |           n/a |          n/a |        +0.88% |           n/a |          n/a |
| engine/production/name/10/3                    |        +0.37% |           n/a |          n/a |        +1.03% |           n/a |          n/a |
| engine/production/name/1000/1                  |        +0.76% |           n/a |          n/a |        -0.43% |           n/a |          n/a |
| engine/production/name/1000/3                  |        +0.27% |        +0.14% |       -0.09% |        +0.30% |        -0.13% |       +1.08% |
| fields/components/10/8                         |        +1.16% |           n/a |          n/a |        +3.73% |           n/a |          n/a |
| fields/components/1000/8                       |        +1.93% |        +1.31% |       +0.40% |        +5.25% |        -1.30% |       -2.84% |
| fields/name/10/8                               |        +2.33% |           n/a |          n/a |        +1.61% |           n/a |          n/a |
| fields/name/1000/8                             |        +2.03% |        +2.08% |       +0.24% |        +1.02% |        -0.48% |       -1.46% |
| output/compile-fields/0                        |        -0.14% |        -0.48% |       +0.00% |        -5.99% |        +6.82% |       +0.41% |
| output/compile-fields/1                        |        +2.02% |        +0.32% |       +0.51% |        -1.54% |        +1.42% |       -3.09% |
| output/compile-fields/2                        |        -0.06% |        -0.02% |       +0.21% |        +8.24% |        -2.61% |       -4.35% |
| output/compile-fields/3                        |        +4.89% |        +4.40% |       +0.29% |        -3.55% |        -5.96% |       +0.86% |
| output/compile-fields/4                        |        +3.48% |        +2.81% |       +0.28% |        +1.95% |        -1.29% |       +0.47% |
| output/hash-binding/1/0/0                      |        +0.43% |           n/a |          n/a |        +2.70% |           n/a |          n/a |
| output/hash-binding/1/0/1                      |       -10.74% |        -9.49% |       -0.03% |        -9.67% |       -11.85% |       -0.99% |
| output/hash-binding/1/0/2                      |       -10.41% |           n/a |          n/a |       -10.21% |           n/a |          n/a |
| output/hash-binding/1/0/3                      |        +0.28% |           n/a |          n/a |        +1.29% |           n/a |          n/a |
| output/hash-binding/1/0/4                      |        +2.08% |           n/a |          n/a |        -0.99% |           n/a |          n/a |
| output/hash-binding/1/1/0                      |        +0.65% |        +0.15% |       -0.42% |        -4.38% |        -1.45% |       +4.87% |
| output/hash-binding/1/1/1                      |       -43.51% |       -43.41% |       -0.04% |       -48.42% |       -46.52% |       +0.58% |
| output/hash-binding/1/1/2                      |       -43.31% |           n/a |          n/a |       -48.32% |           n/a |          n/a |
| output/hash-binding/1/1/3                      |        -0.36% |           n/a |          n/a |        -4.05% |           n/a |          n/a |
| output/hash-binding/1/1/4                      |        -0.30% |           n/a |          n/a |        -1.25% |           n/a |          n/a |
| output/hash-binding/16/0/0                     |        -0.94% |           n/a |          n/a |        -0.90% |           n/a |          n/a |
| output/hash-binding/16/0/1                     |       -33.89% |       -34.68% |       -0.09% |       -45.52% |       -46.77% |       -2.26% |
| output/hash-binding/16/0/2                     |       -33.14% |           n/a |          n/a |       -43.09% |           n/a |          n/a |
| output/hash-binding/16/0/3                     |        -3.33% |           n/a |          n/a |        -6.87% |           n/a |          n/a |
| output/hash-binding/16/0/4                     |        +5.07% |           n/a |          n/a |        -1.93% |           n/a |          n/a |
| output/hash-binding/16/1/0                     |        -2.09% |        -1.95% |       +0.13% |        -3.20% |        -1.86% |       +1.86% |
| output/hash-binding/16/1/1                     |       -41.12% |       -41.34% |       +0.59% |       -57.33% |       -55.54% |       +1.28% |
| output/hash-binding/16/1/2                     |       -39.26% |           n/a |          n/a |       -55.61% |           n/a |          n/a |
| output/hash-binding/16/1/3                     |        -2.94% |           n/a |          n/a |        +0.61% |           n/a |          n/a |
| output/hash-binding/16/1/4                     |        +5.42% |           n/a |          n/a |        -1.02% |           n/a |          n/a |
| output/hash-fields/1/0                         |        +1.16% |           n/a |          n/a |        -0.83% |           n/a |          n/a |
| output/hash-fields/1/1                         |        +2.39% |           n/a |          n/a |        -7.96% |           n/a |          n/a |
| output/hash-fields/16/0                        |        +0.51% |           n/a |          n/a |        -2.58% |           n/a |          n/a |
| output/hash-fields/16/1                        |        -0.49% |           n/a |          n/a |        -5.73% |           n/a |          n/a |
| output/record/15/0/0                           |        +1.29% |           n/a |          n/a |        -0.17% |           n/a |          n/a |
| output/record/160/0/0                          |        +1.86% |           n/a |          n/a |        -0.86% |           n/a |          n/a |

The explicit cached kernels improve roughly 41-56% in confirmation. Larger engine workloads
improve about 2-5%; real commands gain much less because traversal, content hashing and output remain.
The Linux name-field control retains about a 2% cost (roughly one nanosecond per rendered entry).
Mac field/compile controls vary across populations, including slower isolated cells; do not infer
a universal improvement from the explicit-hash kernels.

## Native confirmation

| Fixture / action             | CPUs | Linux selected | Linux repeat | macOS selected | macOS repeat |
| :--------------------------- | ---: | -------------: | -----------: | -------------: | -----------: |
| flat10 / fields              |    1 |         +0.15% |       +0.89% |         +0.39% |       +0.62% |
| flat10 / fields-transform    |    1 |         -0.94% |       -0.29% |         +0.87% |       +0.24% |
| flat10 / listing             |    1 |         -1.23% |       -0.10% |         +0.60% |       +0.35% |
| flat10 / fields              |    3 |         -0.69% |       +1.15% |         +0.44% |       +0.10% |
| flat10 / fields-transform    |    3 |         +0.15% |       -0.76% |         +0.01% |       -0.18% |
| flat10 / listing             |    3 |         -2.21% |       -0.90% |         +0.15% |       +0.65% |
| flat1000 / fields            |    1 |         -0.86% |       +0.93% |         -0.29% |       -0.01% |
| flat1000 / fields-transform  |    1 |         +0.06% |       +0.76% |         -0.14% |       -0.10% |
| flat1000 / listing           |    1 |         +0.01% |       +0.32% |         +0.04% |       -0.61% |
| flat1000 / fields            |    3 |         -2.95% |       +0.42% |         -0.46% |       -0.30% |
| flat1000 / fields-transform  |    3 |         -2.91% |       +0.09% |         -0.16% |       +0.33% |
| flat1000 / listing           |    3 |         -0.91% |       -0.78% |         +0.42% |       +0.84% |
| flat10000 / fields           |    1 |         -1.48% |       +0.22% |         -0.66% |       -0.14% |
| flat10000 / fields-transform |    1 |         -0.64% |       +0.34% |         -0.03% |       -0.14% |
| flat10000 / listing          |    1 |         +1.03% |       -2.54% |         +0.35% |       +0.17% |
| flat10000 / fields           |    3 |         -1.46% |       +0.24% |         -0.16% |       +0.10% |
| flat10000 / fields-transform |    3 |         -0.02% |       +1.12% |         -0.26% |       -0.01% |
| flat10000 / listing          |    3 |         +0.54% |       -1.84% |         +0.41% |       +0.22% |
| long1000 / listing           |    1 |         -0.40% |       -0.18% |         +0.37% |       +0.04% |
| long1000 / listing           |    3 |         +1.55% |       +1.62% |         -0.22% |       -0.11% |

## Cold and unrelated controls

Absolute initial refined times below use each benchmark's reported unit. A fields/name case renders
1,000 entries per iteration, so a microsecond per iteration equals a nanosecond per entry here.
Do not compare a whole-engine iteration directly with a single-field or compilation iteration.

| Case                     | Host  | Baseline | Selected | Unit |
| :----------------------- | :---- | -------: | -------: | :--- |
| fields/name/1000/8       | linux |    43.09 |    44.22 | us   |
| fields/name/1000/8       | macos |   238.37 |   238.40 | us   |
| fields/components/1000/8 | linux |   110.82 |   112.97 | us   |
| fields/components/1000/8 | macos |   373.52 |   391.59 | us   |
| output/compile-fields/0  | linux |    34.70 |    34.64 | ns   |
| output/compile-fields/0  | macos |    76.59 |    71.50 | ns   |
| output/compile-fields/2  | linux |    34.56 |    34.55 | ns   |
| output/compile-fields/2  | macos |    69.96 |    77.18 | ns   |
| output/compile-fields/3  | linux |    56.89 |    60.07 | ns   |
| output/compile-fields/3  | macos |   128.68 |   121.68 | ns   |

The first prototype increased bare-hash compilation by about 42-49% and explicit-hash compilation
by about 29-33% in its initial population. It is not selected. Its complete source, data and setup
failures remain separate, including the initially over-complex Compile extension, corrected scoped
lint findings and failed partial-edit build. The selected source is formatted and passes actual lint;
no complexity threshold or policy was relaxed.

## Correctness and footprint

Three new committed test cases exercise changing defaults across copied/moved templates, every
algorithm from the shared catalog with destroyed temporary qualifier storage, and invalid dynamic
defaults with independent explicit fields and transformations. Existing field, derivation, snapshot,
extraction, lifetime, invalidation, output and fuzz tests remain.

Both hosts pass 313 selected root-and-extension release suites and nine focused ASan suites.
Linux also passes nine MSan and nine TSan suites; its complete release selection requires working
FUSE. Actual scoped lint passes the changed implementation, test and common benchmark on both hosts;
the refined implementation passes a focused recheck. Source guards preserve exactly the selected
four production/test/benchmark files after source-swapping builds and before/after validation.
Normal enforcing hooks and current-head GitHub checks remain required for merge readiness.

- linux: unstripped full CLI 18,159,248 -> 18,160,688 bytes (+1,440).
- macos: unstripped full CLI 8,158,392 -> 8,158,760 bytes (+368).

O22c2 reusable command output and the other performance candidates remain open.

## Lossless evidence

[hash-field-binding-20261103.json.gz](hash-field-binding-20261103.json.gz): 1,146,277 bytes; SHA-256 `83957c3fc83b46c62a95544a4b0b89a9eef9392a061be4637d66b3f452686ec1`.
