# Indexed archive directory observations, 2026-11-05

Use the archive index's existing immutable metadata with eager directory listings, avoiding a
second per-entry path split, normalization and map lookup. Full-command 100,000-member summary
cases improve on both local hosts. Metadata CSV has smaller serial gains and some parallel costs;
this is not a universal traversal speedup. The separately tested compile-time mode specialization
is rejected and is not part of the source change.

## Implementation and boundaries

Experimental and publication parent is PR #1107,
`b2ce83a6daf6e7cf33962c2983164131d8ede7bc`. The archive backend shares one private directory
listing implementation between ordinary entries and complete directory observations. Existing
owned-key normalization, direct-child ordering, errors, readonly flags and bounded initial entry
storage remain unchanged. Only the observation path copies the stored complete metadata into its
owned sidecar; ordinary names-only listing does not request it.

The walker requests this capability only for eager metadata demand. Full-path archive backends can
provide it even when mounted, while native name-only listings deliberately disabled under
`--archive=all` keep their existing full-path convention. The observation owner transfers before
borrowing records. Followed symlinks retain ordinary target-stat fallback; malformed and incomplete
observations retain the existing error/fallback behavior. Mounted leaf workers still cannot submit
nested stat work. No worker, lock, user flag, registry entry, host-I/O path, dependency, or count/time
policy is introduced. The held borrowed-key O19g variants are not included.

## Method and independent output checks

The 88 in-memory cases contain 44 ordinary listing controls and 44 metadata-listing cases: flat,
wide, deep and chain-shaped TARs, 10 through 100,000 members (chains through 1,000). `one` and `ten`
mean one or up to ten directory queries, not one or ten total files; wide/deep leaf directories hold
ten files. Fixture creation, archive indexing and complete Entry/Metadata equality checks are
untimed. The parent does not support ReadDirMetadata: its metadata benchmark explicitly performs
ordinary ReadDir followed by Stat for each entry. An unsupported default call is never timed as a
valid empty result. That benchmark correction and a deprecated-API compile failure occurred before
timing; their logs and source snapshots remain in the evidence.

The inherited 66 complete-command cases cover ordinary broad/deep trees, TAR/ZIP, one/three workers,
names and CSV path/type/size/mode/mtime. Their independent complete output/exit/empty-stderr preflights
are reused from immutable fixtures and checked against every frozen binary. Only the 100,000-member
names controls occur in this original matrix. Sixteen added complete commands cover wide/deep TAR
summaries at 1,000, 10,000 and 100,000 members, plus 100,000-member metadata CSV, with one/three
workers. These added cases use `-mindepth 1` and UTC: an independent oracle checks every member's
path, regular-file type, one-byte size, mode 640 and fixed timestamp, and exact summary count/bytes.
They exclude the host container; inherited CSV includes its root when `-type f` selects it. Do not
compare those totals without accounting for that different selection.

Each case has three balanced process-order sessions, nine repetitions, and the fastest seven
retained. A byte-identical copied parent independently measures drift. Complete-command variants
are interleaved per invocation and all output bytes and exit results are checked every time.
There are 5,940 inherited and 1,440 added native invocations per host, including ordinary warmups;
extra pause/resume warmups are separately retained. Native elapsed measurements include startup,
index construction, traversal and output. Sanitizer runtimes are never performance evidence.

Negative elapsed changes mean faster; positive mean slower. Reported percentages are medians of
three within-session ratios. Absolute times are medians of session values, so dividing the absolute
columns need not reproduce the paired percentage. Copied-parent percentages are controls, not
subtracted corrections. Every session and raw sample is retained.

Local machines: Apple M5 Pro Mac (arm64, 18 logical CPUs), and AMD Ryzen 9 9950X 16-Core Processor Linux (x86_64, 10 available logical CPUs). Linux native measurements pin one/three allowed logical CPUs; no Mac affinity enforcement is claimed. Both builds use `--config=clang_release --config=xff_full`. Exact OS, affinity, build commands and module sources are in the evidence.

## Decision, improvements and retained costs

The eager metadata path has useful large-archive gains. The table below isolates the four
100,000-member summary commands; entries show paired elapsed changes, not cohort averages.

| Tree | Workers | Mac change | Linux change |
| :--- | ------: | ---------: | -----------: |
| wide |       1 |    -11.00% |       -8.61% |
| wide |       3 |     -0.88% |       -5.28% |
| deep |       1 |     -7.22% |       -6.38% |
| deep |       3 |     -1.64% |       -3.60% |

Serial 100,000-member metadata CSV improves roughly 4-5% on both hosts. Hot metadata directory
microbenchmarks improve as much as about 63% on Mac and 62% on Linux. Ordinary names controls remain
in the full tables rather than being inferred from the eager-path results.

Costs are explicit: inherited parallel CSV reaches about +3.00% on Mac and +5.35% on Linux; added
100,000-member parallel CSV costs up to about +2.04% Mac and +1.59% Linux. Microbenchmark root cases
also include slower cells and copied-parent drift. These costs remain O19h investigation inputs,
including the earlier independent 1,000-member Linux regressions from directory admission. No
unmeasured worker threshold or claim of uniform benefit is made.

## Correctness and private qualification

The new standalone archive test compares all 14 Metadata fields and ordered Entry fields with
ordinary Stat, including known files, directories and symlinks, birth/basic fields, temporary path
lifetimes, destroyed filesystem owners, custom/URI/nested/absolute paths, normalization, empty
listings, errors and capabilities. Walker tests cover full-path observations, fallback and errors,
metadata demand, followed links, mounted owners, native archive-all handling, order and one/three
workers; existing safety and filesystem routing remain unchanged.

All 331 release tests pass on each host with all nine extensions and real required Linux FUSE.
Nine focused ASan suites pass on both hosts; nine MSan and TSan suites pass on Linux. Canonical
focused coverage reaches 24/24 changed coverable lines and 18/18 branches. The actual canonical
local lint checks all 12 independently enumerated affected translation units on both hosts,
including header consumers and the new standalone test. The corrected manual benchmark's actual
translation unit passes lint again on each host. No new suppression or coverage exclusion is used.
Private qualification is not a substitute for normal signed publication, hooks and the resulting
PR's own current-head CI.

Post-qualification whole executables exactly matched the original measured candidates. Unstripped
release-config CLI sizes are 8,274,424 bytes on Mac (+816) and 18,435,008 on Linux (+3,544). These are
not claimed to be shipped stripped binary sizes. After the rejected specialization experiment,
selected sources were restored byte-for-byte. Linux rebuilt to the same exact candidate image.
Mac's strict whole-hash restoration check failed and is retained as a failure: a separate precise
Mach-O comparison proves only 40 bytes differ, from two N_SO sandbox directory strings and the
dependent code-signature page hash. UUID and every other runtime byte are unchanged; both images
pass strict code-signature verification. The original before-experiment exact qualification and
this later runtime-equivalence proof are separate evidence, not a relabeled whole-hash pass.

## Rejected compile-time observation mode

A separately frozen candidate makes the shared helper a template with two compile-time boolean
instantiations. Six focused release suites and all nine actual affected archive-header consumer
lint checks pass on both hosts. No full release, sanitizer, coverage or native-command qualification
is claimed for this rejected variant.

Its diagnostic parent is the selected runtime-mode candidate above, not PR #1107. A bounded
16-case microbenchmark matrix uses the same three-session 9/7 protocol and unchanged-parent copy.
Mac flat/root/10,000 metadata has a median +10.89% cost, with sessions +10.89%, -1.70%, +21.91%;
copy sessions are -1.20%, +1.41%, +2.47%. Some other large-root copy controls are unstable (up to
about 16.5%); they remain visible and are not all attributed to code. Linux does not establish a
general gain. The specialization saves 32 Mac executable bytes but adds 12,616 Linux bytes.
It is rejected. The pinned Google Benchmark's random interleaving uses random_device and has no
supported seed flag; order/heap effects are hypotheses, not an established cause of these results.

## All 88 memory cases

| Case                                        | Mac parent / candidate [us] | Mac change | Mac copy | Linux parent / candidate [us] | Linux change | Linux copy |
| :------------------------------------------ | --------------------------: | ---------: | -------: | ----------------------------: | -----------: | ---------: |
| archive-directory-metadata/chain/one/10     |               0.269 / 0.237 |    -10.76% |   +0.95% |                 0.135 / 0.105 |      -22.15% |     -0.26% |
| archive-directory-metadata/chain/one/100    |               0.749 / 0.660 |    -11.78% |   -0.18% |                 0.406 / 0.378 |       -7.66% |     +0.96% |
| archive-directory-metadata/chain/one/1000   |               8.999 / 7.949 |    -11.56% |   +0.55% |                 4.808 / 4.701 |       -2.13% |     +0.24% |
| archive-directory-metadata/chain/root/10    |               0.137 / 0.145 |     +5.80% |   -1.46% |                 0.122 / 0.115 |       -5.43% |     +0.01% |
| archive-directory-metadata/chain/root/100   |               0.823 / 0.836 |     +1.60% |   +0.23% |                 0.716 / 0.687 |       -1.80% |     -2.87% |
| archive-directory-metadata/chain/root/1000  |             14.750 / 14.799 |     +0.33% |   +0.45% |                 9.002 / 9.026 |       +0.26% |     +0.26% |
| archive-directory-metadata/chain/ten/10     |               3.009 / 2.617 |    -13.00% |   +0.04% |                 1.297 / 1.036 |      -20.04% |     +0.09% |
| archive-directory-metadata/chain/ten/100    |               8.176 / 7.098 |    -13.16% |   -0.24% |                 4.869 / 4.416 |       -9.42% |     -1.07% |
| archive-directory-metadata/chain/ten/1000   |             99.198 / 89.270 |    -10.20% |   -0.10% |               50.649 / 48.362 |       -4.30% |     +0.44% |
| archive-directory-metadata/deep/one/10      |               1.125 / 0.583 |    -48.23% |   -0.94% |                 0.467 / 0.263 |      -43.56% |     +0.08% |
| archive-directory-metadata/deep/one/100     |               1.271 / 0.596 |    -52.94% |   -1.31% |                 0.525 / 0.283 |      -46.16% |     -0.32% |
| archive-directory-metadata/deep/one/1000    |               1.389 / 0.609 |    -56.28% |   -1.16% |                 0.584 / 0.294 |      -49.65% |     +0.01% |
| archive-directory-metadata/deep/one/10000   |               1.658 / 0.670 |    -59.58% |   -0.20% |                 0.824 / 0.328 |      -60.28% |     -0.29% |
| archive-directory-metadata/deep/one/100000  |               1.705 / 0.701 |    -58.89% |   +0.31% |                 0.877 / 0.337 |      -61.54% |     -0.08% |
| archive-directory-metadata/deep/root/10     |               0.172 / 0.163 |     -5.06% |   +1.21% |                 0.130 / 0.128 |       -1.61% |     -0.46% |
| archive-directory-metadata/deep/root/100    |               0.549 / 0.532 |     -3.00% |   +0.40% |                 0.420 / 0.425 |       -0.19% |     +1.03% |
| archive-directory-metadata/deep/root/1000   |               6.049 / 6.067 |     +2.03% |   +2.64% |                 4.703 / 4.793 |       +1.14% |     +2.15% |
| archive-directory-metadata/deep/root/10000  |             83.056 / 82.280 |     +0.03% |   -0.93% |               73.830 / 72.864 |       -2.15% |     -1.86% |
| archive-directory-metadata/deep/root/100000 |         1128.639 / 1039.313 |     -0.41% |   -6.27% |           2125.059 / 2108.359 |       +0.47% |     -3.93% |
| archive-directory-metadata/deep/ten/10      |               1.123 / 0.581 |    -48.40% |   +0.01% |                 0.467 / 0.264 |      -43.51% |     -0.09% |
| archive-directory-metadata/deep/ten/100     |              14.991 / 6.374 |    -57.41% |   -0.06% |                 6.105 / 3.193 |      -47.39% |     -0.38% |
| archive-directory-metadata/deep/ten/1000    |              16.456 / 6.668 |    -59.48% |   +0.04% |                 7.542 / 3.545 |      -53.15% |     -0.48% |
| archive-directory-metadata/deep/ten/10000   |              18.600 / 7.074 |    -61.97% |   +0.17% |                 8.720 / 3.731 |      -57.21% |     -0.20% |
| archive-directory-metadata/deep/ten/100000  |              20.513 / 7.608 |    -62.93% |   -0.35% |                10.177 / 3.934 |      -61.33% |     -0.59% |
| archive-directory-metadata/flat/root/10     |               0.623 / 0.323 |    -48.35% |   -0.50% |                 0.853 / 0.728 |      -14.90% |     -0.17% |
| archive-directory-metadata/flat/root/100    |               8.820 / 3.634 |    -58.80% |   -0.35% |                 9.100 / 7.592 |      -16.85% |     -0.17% |
| archive-directory-metadata/flat/root/1000   |             97.583 / 68.173 |    -30.38% |   -1.67% |              107.919 / 72.545 |      -32.78% |     +0.16% |
| archive-directory-metadata/flat/root/10000  |          1315.484 / 899.930 |    -31.64% |   +0.76% |            1288.790 / 790.763 |      -38.64% |     -0.03% |
| archive-directory-metadata/flat/root/100000 |        13949.875 / 7043.954 |    -50.05% |   -0.65% |         16453.440 / 10768.559 |      -34.18% |     -0.03% |
| archive-directory-metadata/wide/one/10      |               0.903 / 0.576 |    -36.16% |   -0.50% |                 0.412 / 0.250 |      -39.12% |     -0.37% |
| archive-directory-metadata/wide/one/100     |               0.884 / 0.575 |    -35.06% |   -0.83% |                 0.448 / 0.256 |      -42.88% |     +0.12% |
| archive-directory-metadata/wide/one/1000    |               0.952 / 0.621 |    -35.72% |   +0.41% |                 0.484 / 0.267 |      -44.84% |     -0.02% |
| archive-directory-metadata/wide/one/10000   |               1.331 / 0.688 |    -48.34% |   -0.96% |                 0.593 / 0.298 |      -49.62% |     +0.02% |
| archive-directory-metadata/wide/one/100000  |               1.476 / 0.713 |    -51.55% |   -1.08% |                 0.724 / 0.320 |      -55.92% |     -0.15% |
| archive-directory-metadata/wide/root/10     |               0.143 / 0.130 |     -8.73% |   -1.29% |                 0.111 / 0.106 |       -4.20% |     -0.46% |
| archive-directory-metadata/wide/root/100    |               0.990 / 0.634 |    -35.25% |   -0.38% |                 1.107 / 0.955 |      -13.68% |     -0.48% |
| archive-directory-metadata/wide/root/1000   |              13.881 / 8.410 |    -38.81% |   -0.18% |               14.393 / 11.814 |      -17.65% |     -0.86% |
| archive-directory-metadata/wide/root/10000  |           207.754 / 180.625 |    -14.85% |   +0.40% |             212.895 / 144.264 |      -31.73% |     -2.52% |
| archive-directory-metadata/wide/root/100000 |         3364.734 / 2113.909 |    -35.76% |   -0.68% |           3035.368 / 2114.104 |      -30.35% |     -3.03% |
| archive-directory-metadata/wide/ten/10      |               0.911 / 0.579 |    -37.16% |   -1.55% |                 0.412 / 0.251 |      -39.24% |     -0.09% |
| archive-directory-metadata/wide/ten/100     |              10.221 / 6.208 |    -39.11% |   -0.70% |                 4.766 / 2.801 |      -41.48% |     +0.34% |
| archive-directory-metadata/wide/ten/1000    |              11.236 / 6.493 |    -42.28% |   -1.13% |                 5.575 / 3.093 |      -44.61% |     -0.24% |
| archive-directory-metadata/wide/ten/10000   |              13.793 / 7.088 |    -48.10% |   -1.15% |                 6.302 / 3.309 |      -47.52% |     +0.21% |
| archive-directory-metadata/wide/ten/100000  |              16.093 / 7.573 |    -53.23% |   +1.71% |                 7.748 / 3.587 |      -53.71% |     -0.40% |
| archive-directory/chain/one/10              |               0.191 / 0.191 |     -0.32% |   +0.59% |                 0.099 / 0.094 |       -4.20% |     -3.91% |
| archive-directory/chain/one/100             |               0.611 / 0.613 |     -0.59% |   +0.23% |                 0.359 / 0.365 |       +1.85% |     -1.04% |
| archive-directory/chain/one/1000            |               8.450 / 8.400 |     -0.76% |   +2.62% |                 4.611 / 4.675 |       +1.64% |     +0.48% |
| archive-directory/chain/root/10             |               0.103 / 0.104 |     +1.72% |   +0.42% |                 0.103 / 0.106 |       +2.59% |     +0.04% |
| archive-directory/chain/root/100            |               0.776 / 0.803 |     +3.41% |   +0.63% |                 0.674 / 0.700 |       +4.16% |     +0.85% |
| archive-directory/chain/root/1000           |             14.579 / 14.649 |     +0.25% |   -0.04% |                 8.991 / 9.000 |       +0.10% |     -1.22% |
| archive-directory/chain/ten/10              |               2.152 / 2.110 |     -1.30% |   -1.48% |                 0.878 / 0.914 |       +4.08% |     +0.70% |
| archive-directory/chain/ten/100             |               6.576 / 6.661 |     +0.99% |   -0.43% |                 3.984 / 4.137 |       +3.84% |     +2.94% |
| archive-directory/chain/ten/1000            |             89.262 / 89.657 |     +1.11% |   +0.54% |               47.757 / 48.400 |       +1.26% |     +0.55% |
| archive-directory/deep/one/10               |               0.518 / 0.514 |     -0.97% |   -0.52% |                 0.219 / 0.225 |       +2.53% |     +1.19% |
| archive-directory/deep/one/100              |               0.537 / 0.536 |     +0.08% |   -0.44% |                 0.237 / 0.243 |       +2.31% |     +1.25% |
| archive-directory/deep/one/1000             |               0.544 / 0.539 |     -0.67% |   +0.23% |                 0.249 / 0.253 |       +1.65% |     +1.65% |
| archive-directory/deep/one/10000            |               0.623 / 0.602 |     -3.16% |   -0.25% |                 0.285 / 0.289 |       +1.58% |     +0.64% |
| archive-directory/deep/one/100000           |               0.647 / 0.635 |     -1.80% |   +0.04% |                 0.295 / 0.301 |       +2.22% |     +0.01% |
| archive-directory/deep/root/10              |               0.125 / 0.125 |     +0.20% |   -1.07% |                 0.108 / 0.111 |       +2.44% |     +0.02% |
| archive-directory/deep/root/100             |               0.488 / 0.487 |     -0.14% |   -1.47% |                 0.409 / 0.402 |       -0.28% |     +0.64% |
| archive-directory/deep/root/1000            |               6.055 / 6.090 |     +0.66% |   +0.93% |                 4.724 / 4.854 |       +2.76% |     +0.32% |
| archive-directory/deep/root/10000           |             82.145 / 81.419 |     -0.42% |   -0.20% |               74.445 / 73.491 |       -0.62% |     -1.11% |
| archive-directory/deep/root/100000          |         1133.695 / 1051.935 |     -0.73% |   -5.17% |           2594.436 / 2560.420 |       -0.89% |     -3.95% |
| archive-directory/deep/ten/10               |               0.516 / 0.513 |     -0.82% |   -1.18% |                 0.220 / 0.226 |       +2.93% |     -0.24% |
| archive-directory/deep/ten/100              |               5.667 / 5.625 |     -0.88% |   -0.42% |                 2.704 / 2.757 |       +1.93% |     +0.02% |
| archive-directory/deep/ten/1000             |               5.983 / 5.944 |     -1.33% |   +0.02% |                 3.044 / 3.080 |       +1.11% |     +0.43% |
| archive-directory/deep/ten/10000            |               6.467 / 6.287 |     -2.77% |   -0.69% |                 3.260 / 3.288 |       +0.89% |     +0.23% |
| archive-directory/deep/ten/100000           |               6.961 / 6.867 |     -1.37% |   -0.32% |                 3.494 / 3.527 |       +0.98% |     +0.14% |
| archive-directory/flat/root/10              |               0.275 / 0.277 |     +0.45% |   -1.78% |                 0.657 / 0.665 |       +1.55% |     +0.00% |
| archive-directory/flat/root/100             |               3.027 / 2.959 |     -2.52% |   -1.64% |                 6.793 / 7.371 |       +8.46% |     -0.09% |
| archive-directory/flat/root/1000            |             26.704 / 26.400 |     -2.37% |   -0.61% |               68.050 / 67.962 |       +0.19% |     +0.13% |
| archive-directory/flat/root/10000           |           472.519 / 493.543 |     +6.02% |   -1.44% |             714.169 / 717.540 |       +0.70% |     +0.05% |
| archive-directory/flat/root/100000          |         4061.949 / 4138.384 |     +0.91% |   -0.59% |           9297.981 / 9328.324 |       +0.33% |     -0.72% |
| archive-directory/wide/one/10               |               0.534 / 0.533 |     -0.40% |   -0.11% |                 0.216 / 0.218 |       +0.83% |     -0.79% |
| archive-directory/wide/one/100              |               0.508 / 0.504 |     -0.39% |   -0.89% |                 0.222 / 0.227 |       +2.32% |     +0.98% |
| archive-directory/wide/one/1000             |               0.556 / 0.551 |     -0.44% |   -2.23% |                 0.231 / 0.236 |       +1.64% |     -0.09% |
| archive-directory/wide/one/10000            |               0.633 / 0.611 |     -2.93% |   -1.18% |                 0.257 / 0.260 |       +1.01% |     -0.13% |
| archive-directory/wide/one/100000           |               0.669 / 0.649 |     -3.25% |   -0.52% |                 0.281 / 0.284 |       +1.07% |     -0.39% |
| archive-directory/wide/root/10              |               0.107 / 0.109 |     +1.76% |   -0.27% |                 0.092 / 0.095 |       +3.63% |     +0.40% |
| archive-directory/wide/root/100             |               0.579 / 0.573 |     -0.96% |   -2.24% |                 0.888 / 0.899 |       +1.80% |     -0.80% |
| archive-directory/wide/root/1000            |               7.557 / 7.637 |     +1.06% |   +0.12% |               11.074 / 11.353 |       +1.51% |     +0.98% |
| archive-directory/wide/root/10000           |             94.601 / 93.526 |     +0.52% |   -0.56% |             135.288 / 142.399 |       +4.20% |     +1.58% |
| archive-directory/wide/root/100000          |         1703.119 / 1694.523 |     +2.97% |   -1.78% |           1958.670 / 2062.672 |       +5.24% |     -0.58% |
| archive-directory/wide/ten/10               |               0.537 / 0.533 |     -0.80% |   -0.77% |                 0.215 / 0.218 |       +1.32% |     -0.40% |
| archive-directory/wide/ten/100              |               5.519 / 5.422 |     -2.27% |   -0.75% |                 2.368 / 2.426 |       +2.42% |     -0.30% |
| archive-directory/wide/ten/1000             |               5.825 / 5.789 |     -0.62% |   -0.21% |                 2.581 / 2.631 |       +2.18% |     +0.12% |
| archive-directory/wide/ten/10000            |               6.465 / 6.342 |     -2.49% |   -0.48% |                 2.781 / 2.819 |       +1.39% |     -0.48% |
| archive-directory/wide/ten/100000           |               6.996 / 6.794 |     -3.29% |   +0.97% |                 3.070 / 3.100 |       +0.73% |     -0.15% |

## All 66 inherited complete commands

| Case                              | Mac parent / candidate [ms] | Mac change | Mac copy | Linux parent / candidate [ms] | Linux change | Linux copy |
| :-------------------------------- | --------------------------: | ---------: | -------: | ----------------------------: | -----------: | ---------: |
| archive/tar/deep/10/1/metadata    |               7.110 / 7.145 |     +1.33% |   +0.44% |                 1.057 / 1.054 |       -0.41% |     +0.37% |
| archive/tar/deep/10/1/names       |               4.638 / 4.594 |     -0.98% |   +0.44% |                 0.858 / 0.865 |       -0.13% |     +0.29% |
| archive/tar/deep/10/3/metadata    |               7.157 / 7.215 |     +0.72% |   -0.90% |                 1.093 / 1.096 |       +0.27% |     -0.15% |
| archive/tar/deep/10/3/names       |               4.646 / 4.613 |     -1.41% |   -0.67% |                 0.897 / 0.880 |       -0.71% |     +0.34% |
| archive/tar/deep/1000/1/metadata  |             10.310 / 10.263 |     -0.45% |   +0.01% |                 2.585 / 2.513 |       -2.78% |     +0.75% |
| archive/tar/deep/1000/1/names     |               6.219 / 6.177 |     -0.68% |   -0.61% |                 2.059 / 2.069 |       +0.49% |     -0.67% |
| archive/tar/deep/1000/3/metadata  |             10.720 / 10.579 |     -0.95% |   +0.70% |                 3.585 / 3.639 |       +3.74% |     -0.50% |
| archive/tar/deep/1000/3/names     |               6.326 / 6.367 |     +0.68% |   +0.43% |                 2.502 / 2.477 |       -1.00% |     +0.09% |
| archive/tar/deep/10000/1/metadata |             39.094 / 37.823 |     -3.74% |   -0.42% |               18.431 / 17.666 |       -3.88% |     +0.05% |
| archive/tar/deep/10000/1/names    |             20.670 / 20.751 |     +0.39% |   -0.12% |               14.023 / 14.054 |       +0.23% |     +0.44% |
| archive/tar/deep/10000/3/metadata |             38.680 / 40.137 |     +3.00% |   +1.38% |               26.617 / 26.534 |       -0.38% |     +2.39% |
| archive/tar/deep/10000/3/names    |             21.122 / 21.130 |     +0.16% |   +0.87% |               16.233 / 16.694 |       +1.17% |     +3.85% |
| archive/tar/deep/100000/1/names   |           173.354 / 173.345 |     +0.16% |   +0.48% |             168.910 / 168.972 |       -0.07% |     +0.09% |
| archive/tar/flat/10/1/names       |               4.620 / 4.653 |     +1.14% |   -0.65% |                 0.860 / 0.850 |       -0.74% |     +0.28% |
| archive/tar/flat/10/3/names       |               4.632 / 4.604 |     -0.59% |   -0.87% |                 0.876 / 0.879 |       -0.49% |     -0.68% |
| archive/tar/flat/10000/1/names    |             12.811 / 12.859 |     +0.39% |   +0.01% |                 7.025 / 6.999 |       +0.18% |     -0.68% |
| archive/tar/flat/10000/3/names    |             12.711 / 12.876 |     +0.55% |   -0.11% |                 7.147 / 7.145 |       -0.01% |     -0.14% |
| archive/tar/wide/10/1/metadata    |               7.078 / 7.051 |     -0.43% |   -0.05% |                 1.062 / 1.047 |       -1.45% |     -0.61% |
| archive/tar/wide/10/1/names       |               4.712 / 4.689 |     +0.02% |   -0.27% |                 0.838 / 0.838 |       +0.07% |     +0.38% |
| archive/tar/wide/10/3/metadata    |               7.114 / 7.094 |     -1.39% |   -0.05% |                 1.068 / 1.076 |       -0.81% |     +0.20% |
| archive/tar/wide/10/3/names       |               4.696 / 4.579 |     -0.43% |   -0.22% |                 0.876 / 0.876 |       -0.26% |     -0.10% |
| archive/tar/wide/1000/1/metadata  |              10.062 / 9.833 |     -1.59% |   -0.27% |                 2.027 / 1.977 |       -2.39% |     +0.09% |
| archive/tar/wide/1000/1/names     |               5.692 / 5.687 |     -0.08% |   -0.10% |                 1.471 / 1.464 |       -0.12% |     +0.72% |
| archive/tar/wide/1000/3/metadata  |               9.839 / 9.798 |     -0.41% |   +0.90% |                 2.919 / 2.962 |       +3.59% |     +0.52% |
| archive/tar/wide/1000/3/names     |               5.808 / 5.932 |     +0.32% |   +0.38% |                 1.949 / 1.942 |       +0.95% |     +2.48% |
| archive/tar/wide/10000/1/metadata |             32.707 / 31.553 |     -2.80% |   +0.43% |               12.111 / 11.664 |       -3.69% |     +0.07% |
| archive/tar/wide/10000/1/names    |             14.745 / 14.864 |     -0.17% |   +0.05% |                 8.048 / 8.090 |       +0.53% |     +0.21% |
| archive/tar/wide/10000/3/metadata |             32.748 / 33.229 |     +1.40% |   +0.69% |               19.106 / 19.345 |       +1.12% |     +0.48% |
| archive/tar/wide/10000/3/names    |             15.215 / 15.408 |     +0.55% |   -1.12% |                 9.930 / 9.808 |       -1.22% |     +0.10% |
| archive/tar/wide/100000/1/names   |           112.229 / 111.767 |     +0.08% |   -0.19% |               89.356 / 89.645 |       +0.39% |     +0.11% |
| archive/zip/deep/10/1/metadata    |               7.186 / 7.106 |     -0.37% |   -0.88% |                 1.041 / 1.044 |       -0.00% |     +0.19% |
| archive/zip/deep/10/1/names       |               4.802 / 4.846 |     -0.19% |   -0.84% |                 0.866 / 0.870 |       +0.58% |     +0.18% |
| archive/zip/deep/10/3/metadata    |               7.090 / 7.061 |     -0.40% |   +0.29% |                 1.115 / 1.105 |       +0.06% |     +0.12% |
| archive/zip/deep/10/3/names       |               4.931 / 4.965 |     +0.70% |   -0.24% |                 0.918 / 0.936 |       +1.48% |     +1.06% |
| archive/zip/deep/1000/1/metadata  |             12.229 / 12.160 |     -0.80% |   +0.66% |                 3.001 / 2.965 |       -1.19% |     -0.29% |
| archive/zip/deep/1000/1/names     |               8.354 / 8.401 |     +0.66% |   +0.67% |                 2.448 / 2.459 |       +0.31% |     -0.84% |
| archive/zip/deep/1000/3/metadata  |             12.433 / 12.561 |     +1.03% |   +1.16% |                 4.022 / 4.073 |       +0.84% |     -0.48% |
| archive/zip/deep/1000/3/names     |               8.593 / 8.515 |     -0.77% |   -0.02% |                 2.999 / 2.970 |       -0.97% |     -1.44% |
| archive/zip/deep/10000/1/metadata |             57.739 / 56.934 |     -1.46% |   +0.58% |               21.953 / 21.503 |       -2.02% |     -0.21% |
| archive/zip/deep/10000/1/names    |             40.107 / 39.996 |     -0.06% |   +0.09% |               17.858 / 18.003 |       +0.98% |     -0.38% |
| archive/zip/deep/10000/3/metadata |             56.970 / 58.054 |     +1.19% |   -0.20% |               29.966 / 30.405 |       +1.65% |     +0.70% |
| archive/zip/deep/10000/3/names    |             41.207 / 40.745 |     -0.15% |   -0.54% |               19.805 / 20.069 |       +1.15% |     +0.02% |
| archive/zip/flat/10/1/names       |               4.833 / 4.868 |     -1.13% |   -1.06% |                 0.862 / 0.862 |       +0.80% |     +0.50% |
| archive/zip/flat/10/3/names       |               4.815 / 4.755 |     -1.24% |   -0.38% |                 0.908 / 0.916 |       +0.36% |     +0.10% |
| archive/zip/flat/10000/1/names    |             32.701 / 32.698 |     +0.13% |   -0.07% |               10.735 / 10.848 |       +1.06% |     +0.08% |
| archive/zip/flat/10000/3/names    |             32.452 / 32.419 |     -0.10% |   +0.17% |               11.010 / 11.109 |       +1.76% |     +0.03% |
| archive/zip/wide/10/1/metadata    |               7.243 / 7.127 |     +1.10% |   +0.29% |                 1.035 / 1.029 |       -1.03% |     +0.04% |
| archive/zip/wide/10/1/names       |               4.808 / 4.792 |     -0.37% |   -0.26% |                 0.867 / 0.866 |       -0.76% |     -0.54% |
| archive/zip/wide/10/3/metadata    |               7.163 / 7.132 |     +0.79% |   +1.53% |                 1.080 / 1.077 |       -0.13% |     +0.08% |
| archive/zip/wide/10/3/names       |               4.830 / 4.758 |     -1.48% |   -0.89% |                 0.891 / 0.900 |       +0.76% |     +1.86% |
| archive/zip/wide/1000/1/metadata  |             11.669 / 11.604 |     -0.70% |   -0.03% |                 2.488 / 2.448 |       -1.62% |     +0.09% |
| archive/zip/wide/1000/1/names     |               7.799 / 7.849 |     +0.59% |   -0.52% |                 1.909 / 1.912 |       +1.07% |     -0.43% |
| archive/zip/wide/1000/3/metadata  |             11.910 / 11.950 |     -0.13% |   +0.92% |                 3.356 / 3.345 |       -0.35% |     -2.51% |
| archive/zip/wide/1000/3/names     |               8.179 / 8.049 |     -1.59% |   -0.68% |                 2.390 / 2.407 |       -0.32% |     -0.25% |
| archive/zip/wide/10000/1/metadata |             51.690 / 50.446 |     -2.32% |   -0.54% |               15.907 / 15.495 |       -2.36% |     -0.37% |
| archive/zip/wide/10000/1/names    |             34.403 / 34.726 |     +0.54% |   -0.19% |               11.914 / 12.067 |       +1.29% |     -0.39% |
| archive/zip/wide/10000/3/metadata |             51.441 / 52.181 |     +1.44% |   +0.34% |               22.474 / 23.135 |       +5.35% |     +2.76% |
| archive/zip/wide/10000/3/names    |             34.902 / 34.893 |     +0.04% |   +0.24% |               13.812 / 13.932 |       +0.96% |     -0.83% |
| native/broad/10/1/names           |               4.522 / 4.538 |     +0.57% |   -0.49% |                 0.814 / 0.813 |       +0.13% |     +0.76% |
| native/broad/10/3/names           |               4.506 / 4.486 |     -0.51% |   +0.60% |                 0.847 / 0.858 |       -0.39% |     -0.60% |
| native/broad/10000/1/names        |             11.294 / 11.236 |     -0.31% |   -0.65% |                 2.522 / 2.525 |       -0.49% |     -0.49% |
| native/broad/10000/3/names        |             11.323 / 11.288 |     -0.10% |   -0.13% |                 2.595 / 2.616 |       +0.81% |     +0.38% |
| native/deep/10/1/names            |               4.961 / 4.851 |     -1.72% |   -0.19% |                 0.849 / 0.850 |       +0.40% |     +0.53% |
| native/deep/10/3/names            |               5.065 / 5.022 |     -0.35% |   -0.40% |                 1.184 / 1.192 |       +0.61% |     -1.27% |
| native/deep/10000/1/names         |             14.902 / 14.898 |     -0.03% |   +0.32% |                 2.781 / 2.788 |       -0.03% |     -0.25% |
| native/deep/10000/3/names         |             11.207 / 11.219 |     +0.01% |   -0.53% |                 3.500 / 3.502 |       +0.06% |     -2.15% |

## All 16 additional complete commands

| Case                                            | Mac parent / candidate [ms] | Mac change | Mac copy | Linux parent / candidate [ms] | Linux change | Linux copy |
| :---------------------------------------------- | --------------------------: | ---------: | -------: | ----------------------------: | -----------: | ---------: |
| archive/tar/deep/1000/1/summary-members-only    |               6.129 / 5.961 |     -2.73% |   +0.74% |                 2.101 / 2.029 |       -3.43% |     +0.37% |
| archive/tar/deep/1000/3/summary-members-only    |               6.270 / 6.179 |     -0.28% |   +1.24% |                 2.560 / 2.529 |       -1.21% |     +1.25% |
| archive/tar/deep/10000/1/summary-members-only   |             21.312 / 19.876 |     -6.78% |   -0.57% |               14.743 / 13.786 |       -6.36% |     +0.21% |
| archive/tar/deep/10000/3/summary-members-only   |             20.619 / 20.532 |     -0.37% |   +0.20% |               16.260 / 16.137 |       -1.73% |     +0.83% |
| archive/tar/deep/100000/1/metadata-members-only |           321.488 / 307.504 |     -4.04% |   +0.11% |             213.933 / 205.470 |       -3.84% |     +0.11% |
| archive/tar/deep/100000/1/summary-members-only  |           179.930 / 166.941 |     -7.22% |   +0.62% |             176.418 / 165.082 |       -6.38% |     +0.30% |
| archive/tar/deep/100000/3/metadata-members-only |           313.513 / 319.903 |     +2.04% |   -0.04% |             284.940 / 287.279 |       +0.66% |     -0.10% |
| archive/tar/deep/100000/3/summary-members-only  |           170.013 / 166.322 |     -1.64% |   -0.25% |             181.821 / 175.277 |       -3.60% |     +0.31% |
| archive/tar/wide/1000/1/summary-members-only    |               5.545 / 5.466 |     -2.08% |   -1.23% |                 1.560 / 1.489 |       -4.57% |     +0.18% |
| archive/tar/wide/1000/3/summary-members-only    |               5.790 / 5.610 |     -1.56% |   -0.06% |                 2.001 / 1.982 |       -0.38% |     +0.44% |
| archive/tar/wide/10000/1/summary-members-only   |             15.362 / 14.330 |     -6.64% |   +0.43% |                 8.859 / 8.212 |       -8.53% |     +0.94% |
| archive/tar/wide/10000/3/summary-members-only   |             14.974 / 15.004 |     +0.20% |   +0.19% |               10.679 / 10.653 |       +0.44% |     -0.05% |
| archive/tar/wide/100000/1/metadata-members-only |           254.040 / 241.070 |     -4.74% |   -0.38% |             132.094 / 126.851 |       -4.54% |     +0.43% |
| archive/tar/wide/100000/1/summary-members-only  |           117.388 / 105.214 |    -11.00% |   +0.16% |               96.369 / 87.955 |       -8.61% |     +1.19% |
| archive/tar/wide/100000/3/metadata-members-only |           248.653 / 248.608 |     -0.03% |   -1.26% |             195.518 / 201.465 |       +1.59% |     +1.08% |
| archive/tar/wide/100000/3/summary-members-only  |           111.275 / 109.826 |     -0.88% |   +0.14% |             106.505 / 100.601 |       -5.28% |     +0.22% |

## Sampled peak RSS, added 100,000-member commands

These are per-command sampled maximum RSS values. Each cell gives the median and largest value
across the 27 parent or candidate measurements; units are MiB. They do not isolate sidecar bytes
from archive indexing, process startup or output. Copied-parent samples remain in the raw evidence.

| Host  | Case                                            | Parent median / max [MiB] | Candidate median / max [MiB] |
| :---- | :---------------------------------------------- | ------------------------: | ---------------------------: |
| macos | archive/tar/deep/100000/1/metadata-members-only |             67.08 / 69.12 |                69.84 / 70.78 |
| macos | archive/tar/deep/100000/1/summary-members-only  |             68.33 / 69.03 |                69.64 / 70.91 |
| macos | archive/tar/deep/100000/3/metadata-members-only |             69.12 / 70.66 |                69.61 / 72.27 |
| macos | archive/tar/deep/100000/3/summary-members-only  |             68.84 / 69.95 |                70.16 / 71.28 |
| macos | archive/tar/wide/100000/1/metadata-members-only |             48.77 / 49.22 |                49.98 / 51.16 |
| macos | archive/tar/wide/100000/1/summary-members-only  |             48.50 / 48.98 |                49.34 / 50.59 |
| macos | archive/tar/wide/100000/3/metadata-members-only |             49.12 / 50.17 |                50.33 / 52.12 |
| macos | archive/tar/wide/100000/3/summary-members-only  |             48.98 / 50.33 |                50.20 / 51.28 |
| linux | archive/tar/deep/100000/1/metadata-members-only |             59.51 / 59.81 |                59.58 / 59.76 |
| linux | archive/tar/deep/100000/1/summary-members-only  |             59.57 / 59.79 |                59.61 / 59.79 |
| linux | archive/tar/deep/100000/3/metadata-members-only |             59.55 / 59.76 |                59.65 / 59.79 |
| linux | archive/tar/deep/100000/3/summary-members-only  |             59.59 / 59.79 |                59.62 / 59.79 |
| linux | archive/tar/wide/100000/1/metadata-members-only |             37.32 / 37.52 |                37.30 / 37.52 |
| linux | archive/tar/wide/100000/1/summary-members-only  |             37.35 / 37.56 |                37.34 / 37.56 |
| linux | archive/tar/wide/100000/3/metadata-members-only |             37.32 / 37.50 |                37.27 / 37.56 |
| linux | archive/tar/wide/100000/3/summary-members-only  |             37.31 / 37.50 |                37.33 / 37.55 |

## All 16 specialization diagnostic cases

The baseline here is the selected runtime-mode candidate; these percentages must not be combined
with the #1107-parent tables above.

| Case                                        | Mac change | Mac copy | Linux change | Linux copy |
| :------------------------------------------ | ---------: | -------: | -----------: | ---------: |
| archive-directory-metadata/deep/one/100000  |     -0.16% |   +0.67% |       -0.35% |     +0.28% |
| archive-directory-metadata/deep/root/10000  |     +5.49% |   +4.39% |       +1.44% |     -0.42% |
| archive-directory-metadata/deep/root/100000 |     +8.89% |  +16.45% |       +4.13% |     +3.27% |
| archive-directory-metadata/deep/ten/100000  |     -0.86% |   +0.84% |       +0.06% |     -0.37% |
| archive-directory-metadata/flat/root/10000  |    +10.89% |   +1.41% |       -1.47% |     -1.98% |
| archive-directory-metadata/flat/root/100000 |     -1.77% |   +3.10% |       -2.67% |     -3.95% |
| archive-directory-metadata/wide/one/100000  |     +0.12% |   +0.73% |       -1.54% |     -0.50% |
| archive-directory-metadata/wide/root/10000  |     +7.85% |   -1.43% |       -1.78% |     -2.24% |
| archive-directory-metadata/wide/root/100000 |     +2.89% |   +6.16% |       +1.49% |     +3.72% |
| archive-directory-metadata/wide/ten/100000  |     -1.31% |   -0.12% |       -0.39% |     -0.39% |
| archive-directory/deep/root/10000           |     +2.59% |   +4.15% |       +1.72% |     -0.43% |
| archive-directory/deep/root/100000          |     +8.73% |  +16.49% |       +2.88% |     -1.50% |
| archive-directory/flat/root/10000           |     +2.33% |   -1.78% |       -3.33% |     -5.05% |
| archive-directory/flat/root/100000          |     +0.09% |   +2.02% |       -0.22% |     -0.86% |
| archive-directory/wide/root/10000           |     +1.85% |   +5.72% |       -1.25% |     -0.35% |
| archive-directory/wide/root/100000          |     +2.45% |   +5.36% |       +2.87% |     +0.98% |

## Lossless evidence and remaining work

The deterministic [evidence archive](archive-directory-observations-20261105.json.gz) contains
938 original text records (472 Mac,
466 Linux), represented by 312 unique contents.
It is 5,066,369 compressed bytes, 197,214,972 uncompressed bytes,
reconstructing 570,133,729 original record bytes. Every record's byte count,
SHA-256, complete text and host/path provenance are checked during round-trip reconstruction.
Repeated samples, failures and copied-parent records are preserved, not pooled away.

Format `xff-lossless-text-evidence-v2` stores `contents[sha256]` and each host's indexed records with
`path`, `bytes`, and `sha256`. Recover the exact original text from the matching content hash.
Sources, drivers, source/identity manifests, all raw timings, independent output checks, complete
qualification logs, LCOV, rejected specialization and precise Mac restoration proof are retained.
Executables, generated fixture trees and full compilation databases are excluded. Packaging and
report/staging producers are included as text records. Archive SHA-256:
`57215cc07ba1f7fa30341e89156c5499dfb802e3b43ba1e6d00114d513a676d3`.

O19h parallel metadata-output costs remain open; O20j observation storage and O20k native
non-followed symlink observations remain independent measured investigations. No claim is made
that the archive result proves those unimplemented changes safe or fast.
