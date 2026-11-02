# Fuzzy score normalization and workspace (2026-11-02)

## Decision

Compute the pattern-only normalization ceiling once for each immutable Fzf term and reuse bounded score rows in the coordinator and each existing persistent matching worker. Allocate exactly the first input size, then double on growth up to 65,536 cells per row (512 KiB total on the supported hosts). Larger inputs use temporary storage without enlarging the retained buffer. No new thread pool, lock, reference count, option or exception cleanup is added. Plain subsequence matching still avoids scoring when no consumer needs it. Levenshtein and shingle models are outside this change.

An identical equal-length self-match has exactly one alignment, so summing match, consecutive and word-boundary bonuses replaces the old quadratic self-match. The candidate still searches all valid alignments against the input. Its first row initializes every cell before any read; the other row needs no zeroing. Immutable query state is shared, and scratch is owned by a single coordinator or worker. Moving a workspace transfers storage and leaves the source reusable.

A separate correctness regression reproduces a valid match after 1,500 skipped bytes: the old `-1000` unmatched sentinel hid the valid raw score `-1484`. The new sentinel leaves one gap of integer headroom below all ordinary scores. First-row and later-row long gaps now retain a match with normalized quality zero. The capped general fuzz campaign cannot reach this long-input counterexample, so the explicit regression remains.

## Measurement contract

Final baseline `44381e2b39f2acc52931f877add25721508426d4` has the same runtime tree as merged main `2b21790502fd6bf9b6e1afb53cc1444cf140f296`. Initial causal/capacity trials use #1041 merge `cf23601ab3533c2acb938acda1439dcd69091f61`; do not compare their absolute timings with the later baseline. Full source snapshots/patches, source and binary hashes, build flags, raw repetitions and experiment scripts are preserved below.

Local M5 Pro uses warm APFS fixtures, one/three/ten requested workers and no affinity. Linux drogon uses a Ryzen 9950X KVM guest, tmpfs fixtures and child CPU affinity restricted to one/three/ten CPUs. Release builds use LLVM 23 and ThinLTO. Builds and allocation diagnostics finish before timing. Absolute times are not compared across hosts.

Each final native case runs three sessions, discards a warmup for each variant, interleaves nine measured rounds and retains the fastest seven. Counts are 10/1,000/10,000/100,000 with one/three workers, plus 100,000 with ten workers. Ranked name/path, plain subsequence and no-fuzzy controls make 36 cases per session. Every invocation checks exact filename output against an independent oracle; all selected populations are identical. Both variants use `--exact`, `--sort=dir`, explicit UTC and no config/pager. The final matrix contains 4,320 fresh invocations across both hosts, including discarded warmups.

Existing in-memory benchmarks separately measure preparation and prepared kernels (1/16 predicates) plus full-engine scored selection over 10/1,000/10,000 files and one/three workers. They also use three sessions, nine repetitions and fastest seven. Times below are medians of session means; changes are medians of within-session candidate/baseline ratios. Negative changes mean faster execution.

## Final native 100,000-file results

| Host  | Case        | Workers | Baseline ms | Candidate ms | Change |
| ----- | ----------- | ------: | ----------: | -----------: | -----: |
| macos | ranked-name |       1 |     248.562 |      237.348 | -4.51% |
| macos | ranked-path |       1 |     275.081 |      253.360 | -7.95% |
| macos | bare        |       1 |     230.181 |      229.489 | -0.43% |
| macos | control     |       1 |     226.122 |      226.749 | +0.31% |
| macos | ranked-name |       3 |     207.446 |      195.669 | -5.75% |
| macos | ranked-path |       3 |     234.268 |      214.411 | -8.80% |
| macos | bare        |       3 |     191.115 |      190.505 | -0.28% |
| macos | control     |       3 |     186.277 |      186.844 | +0.17% |
| macos | ranked-name |      10 |     215.581 |      206.386 | -4.71% |
| macos | ranked-path |      10 |     242.170 |      220.984 | -8.65% |
| macos | bare        |      10 |     198.586 |      197.997 | +0.57% |
| macos | control     |      10 |     194.663 |      193.575 | -0.56% |
| linux | ranked-name |       1 |     125.808 |      120.370 | -4.51% |
| linux | ranked-path |       1 |     133.624 |      132.960 | -0.54% |
| linux | bare        |       1 |     116.105 |      115.622 | -0.42% |
| linux | control     |       1 |     113.700 |      113.466 | -0.02% |
| linux | ranked-name |       3 |     104.189 |      100.800 | -3.83% |
| linux | ranked-path |       3 |     110.944 |      107.937 | -3.04% |
| linux | bare        |       3 |      96.294 |       95.326 | -1.45% |
| linux | control     |       3 |      92.209 |       91.934 | -0.59% |
| linux | ranked-name |      10 |     110.506 |      109.349 | -1.05% |
| linux | ranked-path |      10 |     124.488 |      116.697 | -6.26% |
| linux | bare        |      10 |     106.051 |      104.664 | -1.31% |
| linux | control     |      10 |     102.794 |      102.504 | -0.53% |

## Kernels, preparation and full-engine selection

| Host  | Case                                         |   Baseline |  Candidate |  Change |
| ----- | -------------------------------------------- | ---------: | ---------: | ------: |
| macos | kernel/prepared/constant-true/1/1000 [ns]    |   12586.27 |   12900.28 |  +2.49% |
| macos | kernel/prepared/fuzzy/1/1000 [ns]            |   20328.97 |   20717.42 |  +3.02% |
| macos | prepare/prepared/fuzzy-score-and/1 [ns]      |     495.32 |     501.72 |  +1.76% |
| macos | kernel/prepared/fuzzy-score-and/1/1000 [ns]  |  143969.08 |   68612.22 | -52.34% |
| macos | kernel/prepared/fuzzy-score-and/16/1000 [ns] | 2449251.15 | 1252928.05 | -49.44% |
| macos | engine/production/scored-replay/10000/1 [us] |    7255.29 |    6747.95 |  -6.10% |
| macos | engine/production/scored-replay/10000/3 [us] |    8048.68 |    7131.61 | -11.39% |
| linux | kernel/prepared/constant-true/1/1000 [ns]    |    9528.25 |    9521.51 |  -0.02% |
| linux | kernel/prepared/fuzzy/1/1000 [ns]            |   15181.01 |   15822.93 |  +4.25% |
| linux | prepare/prepared/fuzzy-score-and/1 [ns]      |     239.21 |     248.46 |  +2.77% |
| linux | kernel/prepared/fuzzy-score-and/1/1000 [ns]  |   64275.37 |   42799.85 | -33.54% |
| linux | kernel/prepared/fuzzy-score-and/16/1000 [ns] | 1192220.58 |  843602.85 | -29.21% |
| linux | engine/production/scored-replay/10000/1 [us] |    4168.73 |    3897.86 |  -6.52% |
| linux | engine/production/scored-replay/10000/3 [us] |    5336.81 |    5126.70 |  -4.05% |

The kernel gain is specific to scored matching. The unrelated bare fuzzy kernel is slower by 3.02% macOS / 4.25% Linux in this matrix, and the constant-true control changes +2.49% / -0.02%; preserve these layout/noise counterexamples rather than attributing them to removed score allocations. Whole native bare/control paths remain near baseline. Cached ceilings add a small preparation cost (about 2-3% for the one-predicate scored case), which is paid once.

## Allocation and capacity decision

The isolated ASan allocation diagnostic runs 1,000 scored queries. Creating a fresh selected workspace each time makes 1,000 allocations / 64,000 requested bytes / 1,000 frees. Reusing a warmed workspace makes zero allocations, zero requested bytes and zero frees on both hosts, with equal result checksums. These are allocation counts only; sanitizer runtimes are never used as performance evidence.

Initial four-way controls separate baseline, cached ceiling alone, one allocation per standalone scoring call, and persistent engine scratch. A 64-character minimum reduced allocation calls but requested 512,000 bytes in the same fresh diagnostic and regressed the tiny kernel relative to ceiling caching alone. The second capacity experiment selects exact initial size and later doubling. Both the rejected minimum and accepted policy retain their complete raw evidence.

## Counterexamples, footprint and limits

- macos: slowest paired native median +1.65% at (1000, 3, 'control'); every small/native control is retained in the raw data.
- macos: build-output executable 4,069,152 -> 4,069,776 bytes (+624); these are not stripped/compressed release sizes.
- linux: slowest paired native median +1.85% at (10, 1, 'ranked-path'); every small/native control is retained in the raw data.
- linux: build-output executable 12,306,928 -> 12,315,416 bytes (+8,488); these are not stripped/compressed release sizes.

Each active scoring worker may retain up to 512 KiB. Nothing is allocated for its score rows until needed; unusually large inputs use temporary memory and still have the existing quadratic alignment cost. This change does not address all fuzzy models, Unicode semantics, traversal, startup or file I/O. It preserves scoring/ranking, inversion, anchors, folding, short-circuiting and deferred result handling.

## Verification

Targeted fuzzy, evaluator, expression-execution, parallel matching, CLI and whole-engine release tests pass on both hosts. Tests exercise normalization against the full-alignment self-score oracle, separator/camel-case patterns, growth, retention caps, moves, alternating query lengths, shared immutable queries with independent worker scratch and long-gap regressions. The complete release/extension suite passes 304/304 on macOS and Linux; Linux uses the existing FUSE-required configuration because drogon can mount. Scoped clang-tidy passes all six changed runtime/test translation units, including the allocation diagnostic. Normal commit hooks pass. Linux MemorySanitizer qualification is separate from release timing.

## Lossless raw evidence

- [final-macos](fuzzy-score-workspace-20261102-final-macos.json.gz): 144,878 bytes; SHA-256 `8f3a3736c0a9b88fce0462b5ad8e4c17ac42f428f2fd39f82ca4714840467a4a`.
- [final-linux](fuzzy-score-workspace-20261102-final-linux.json.gz): 143,001 bytes; SHA-256 `a3ae40b7eadee2ab028ef59a7078c8e39f40ab0996132c3e911ff97441617edd`.
- [controls-macos](fuzzy-score-workspace-20261102-controls-macos.json.gz): 284,420 bytes; SHA-256 `0be4405ad10a3e1ef1e4cbf94f65c3253ae13be25e5010c0cac8f52dd46146eb`.
- [controls-linux](fuzzy-score-workspace-20261102-controls-linux.json.gz): 283,494 bytes; SHA-256 `257c7e88bfcf051320a52dbf8d9ea7e07c05defb296323c8e007b6094173bdcf`.
- [capacity-macos](fuzzy-score-workspace-20261102-capacity-macos.json.gz): 224,928 bytes; SHA-256 `fd18e18812ee910358dd40592ae1e2b16ae88ef392576a6a837a3e4398d6171b`.
- [capacity-linux](fuzzy-score-workspace-20261102-capacity-linux.json.gz): 219,727 bytes; SHA-256 `0192c72030eb7a9d540d955cce4fae821f23941192da3fd92a0410d178095bf4`.
- [provenance](fuzzy-score-workspace-20261102-provenance.json.gz): 486,983 bytes; SHA-256 `db37fd94ece2cb0f1207ab441ceae8fd0d87810196e0ca98536dc98776fab5f3`.
