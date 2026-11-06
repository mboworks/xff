# Complete native symlink observations, 2026-11-05

Admit complete non-followed symlink metadata from the existing Darwin bulk directory read. This
removes repeated per-link stat calls when eager metadata is required. On the local Mac, summaries
over 100,000 symlinks improve about 55% for broad serial trees, 34% for deep serial trees and 26%
for deep three-worker trees. Ordinary controls and separately measured followed-link costs remain
explicit. No executable size increase occurs; the Linux runtime executable is byte-identical.

## Source and correctness boundary

Experimental and publication parent is PR #1108, commit
`03cff15dc10b6c92861ea057eca062718235596e`. Four source files change: the pure Darwin decoder/header
and its existing decoder/native test files. There is no new target, dependency, syscall in production,
worker policy, flag, lock, allocation strategy or modification of host mutation enforcement.
Private qualification does not replace the resulting PR's normal signed hooks and own current-head CI.

The decoder accepts vnode type VLNK only under the same complete returned-attribute masks, successful
entry status, valid times, bounded link count, nonnegative length/allocation and exact block-unit
checks already required for regular files. Native type/mode constants are statically checked against
the Apple SDK. Link type and mode bits describe the link inode. Unsupported/missing/invalid attributes
and other special types retain ordinary fallback. The public observation contract remains entry
metadata, never followed target metadata.

The existing walker still re-stats followed symlinks, including dangling-target fallback. Mount
ownership, error/order semantics, metadata demand and leaf-worker isolation are unchanged. The Mac
backend still declines bulk metadata for a coordinator's parallel stat work; read-ahead leaf jobs
can use the single-worker capability. Thus broad three-worker results need not gain, while deep
three-worker directory reads can benefit. No new count or timer threshold is introduced.

Apple's [getattrlistbulk specification](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/man/man2/getattrlistbulk.2)
defines link-object attributes and requires checking which attributes were returned. It does not
establish complete stat equivalence on every filesystem. This candidate therefore requires native
field-by-field tests before timing; no Apple implementation source is copied into XFF.

## Tests and measurement contract

The first real native gate compares all 14 Metadata fields against ordinary non-followed Stat for
both basic and birth-time requests. Expanded tests cover short/long/absolute/dangling symlinks,
directory targets, self-loops and a hardlinked symlink inode. The original fixture attempt used
std::filesystem::create_hard_link and actually linked its target on this Mac. That test failure is
retained; the corrected test-only annotated no-follow linkat fixture verifies type, inode and link
count before comparing observations. Production host mutation APIs do not change.

Pure wire-format tests cover complete symlink fields, missing masks, invalid values and every
truncation for both regular and link records; all other special types keep conservative fallback.
The existing directory-only empty-sidecar test still requires ordinary metadata. Regular-file,
resource-fork, sparse and hardlink controls remain. Existing walker follow/order/error/owner tests
run on both hosts. Fixture construction is outside every timing interval.

The 200 native cases contain 80 ordinary regular-file controls and 120 symlink-only/mixed controls.
All use broad/deep trees, 10/100/1,000/10,000/100,000 entries and one/three requested workers. Regular
controls cover listing, native name filtering, path/size CSV and extension summaries. Their summary
format is JSONL for an independent oracle; these are not relabeled original console invocations.
New controls cover names, path/type/size/mode/mtime CSV and overall JSONL summaries. Mixed trees
alternate one-byte regular files and symlinks; link targets are the literal missing path, seven bytes.
Deep trees have eight common levels and ten entries per bucket. New selection excludes directories.

Independent Python lstat/path oracles check every CSV field, exact unique listing membership, and
summary counts/bytes. Fixture mtimes are fixed in UTC. Parent and candidate must also match complete
output order/bytes, exit zero and empty stderr. These properties are checked before timing and
complete output hashes/lengths/exits are checked at every invocation.

A separate 16-case `-L` matrix covers symlink-only broad/deep 1,000/100,000-entry trees, one/three
workers and metadata CSV/summary. Every target is dangling: followed mode must exactly match the
independently checked ordinary fallback output. These cases quantify the overhead of observations
that cannot replace followed target lookup; they are not mixed with default-mode results.

Each population has three balanced sessions, a discarded warmup, nine interleaved measurements and
the fastest seven retained. A byte-identical copied parent independently measures drift. Each host
has 18,000 original native invocations and 1,440 followed-mode invocations, plus any separately
recorded pause/resume rewarmups. The same frozen binaries serve both populations. Native timing
includes startup, traversal, metadata and output, using the existing native launcher identified by
its recorded binary SHA. No sanitizer elapsed value is used as performance evidence.

Linux pins one/three allowed logical CPUs; no Mac affinity enforcement is claimed. Mac uses warm
APFS. Linux's old regular fixtures use tmpfs; new fixtures use a separate tmpfs mount at /dev/shm.
The initial /tmp setup exhausted its inode limit before Linux preflight or timing. Only this
experiment's failed unmeasured fixture tree was removed; the error and original generator survive.
A second generator places new Linux fixtures on the separate RAM filesystem. No timing population
includes that failed setup. Native syscalls are the subject here; pure in-memory decoder correctness
is tested, but no filesystem-free performance claim is made.

All percentages are medians of within-session elapsed ratios: negative means faster, positive slower.
Absolute times are medians of the retained session means, so their ratio need not reproduce the
paired percentage. Copy percentages are controls, not subtracted corrections. All raw repetitions,
first attempts, failures, session values and identities are retained.

Local hosts: Apple M5 Pro (arm64, 18 logical CPUs), and AMD Ryzen 9 9950X 16-Core Processor (x86_64, 10 available logical CPUs). Builds use `--config=clang_release --config=xff_full`; exact commands, source hashes and host details are retained.

## Decision and limits

Retain the candidate. Large serial symlink CSV improves about 26% broad and 16% deep; mixed-tree
serial summaries improve about 27% broad and 20% deep. Deep mixed three-worker summaries improve
about 13%. Broad three-worker cases are near neutral under the unchanged capability policy.

Costs are not hidden: the 80 regular-file Mac controls range -3.19% to +1.43%; the largest new
default-mode cost is mixed deep 100k three-worker CSV at +1.98% (copy +0.04%). Follow-mode Mac
changes range -0.83% to +1.70%. Those small costs remain explicit rather than being averaged into
large default-mode wins. Linux's parent, candidate and rebuilt images are identical, so its elapsed
changes are unchanged-code noise controls, not evidence of a code speedup or regression.

Complete observations introduce temporary sidecar ownership where a symlink-only directory formerly
had none. The RSS table retains that cost; eliminating native segmented-to-vector materialization
remains independent O20j work. This is not a memory-saving claim or a new general parallel policy.

All 331 release targets pass on both hosts with every extension and real required Linux FUSE.
Five focused ASan suites pass on both hosts; the same five MSan and TSan suites pass on Linux.
Canonical changed coverage reaches 3/3 lines and 10/10 branches. Actual canonical clang-tidy checks
all four independently identified affected translation units on each host: decoder, decoder test,
local backend and local backend test. No new suppression or coverage exclusion is used.

Complete rebuilt release images exactly match their original frozen candidates on both hosts after
qualification. Mac remains 8,274,424 unstripped bytes; Linux remains 18,435,008. Linux parent and
candidate SHA are also exactly equal. These are unstripped release-config build outputs, not claimed
shipping stripped sizes. The later follow-mode measurements use these same unchanged images and
sources; adding timing controls does not call for repeating already-valid correctness runs.

## Representative 100,000-entry summary results

| Population | Tree  | Workers | Mac elapsed change | Mac copied parent |
| :--------- | :---- | ------: | -----------------: | ----------------: |
| symlinks   | broad |       1 |            -55.26% |            -0.22% |
| symlinks   | broad |       3 |             +0.05% |            +0.61% |
| symlinks   | deep  |       1 |            -33.83% |            -0.14% |
| symlinks   | deep  |       3 |            -26.13% |            +0.45% |
| mixed      | broad |       1 |            -27.27% |            -0.24% |
| mixed      | broad |       3 |             +0.96% |            +0.67% |
| mixed      | deep  |       1 |            -20.35% |            -0.15% |
| mixed      | deep  |       3 |            -12.72% |            -0.53% |

## All 200 native cases

| Case                                 | Mac parent / candidate [ms] | Mac change | Mac copy | Linux parent / candidate [ms] | Linux change | Linux copy |
| :----------------------------------- | --------------------------: | ---------: | -------: | ----------------------------: | -----------: | ---------: |
| mixed/broad/10/1/metadata            |               4.770 / 4.723 |     -0.99% |   -1.52% |                 0.849 / 0.846 |       -0.01% |     -1.59% |
| mixed/broad/10/1/names               |               4.551 / 4.552 |     +0.21% |   -1.59% |                 0.825 / 0.849 |       -0.51% |     -2.22% |
| mixed/broad/10/1/summary             |               4.461 / 4.484 |     +0.23% |   +1.06% |                 0.844 / 0.842 |       -0.24% |     -1.23% |
| mixed/broad/10/3/metadata            |               4.782 / 4.809 |     -0.10% |   +1.56% |                 0.894 / 0.909 |       +1.48% |     +0.41% |
| mixed/broad/10/3/names               |               4.544 / 4.553 |     +0.20% |   -0.27% |                 0.900 / 0.900 |       +0.04% |     -1.68% |
| mixed/broad/10/3/summary             |               4.542 / 4.541 |     -0.03% |   -0.06% |                 0.872 / 0.873 |       -0.45% |     -0.71% |
| mixed/broad/100/1/metadata           |               5.230 / 5.178 |     -2.81% |   -1.34% |                 0.943 / 0.938 |       -0.51% |     -1.42% |
| mixed/broad/100/1/names              |               4.594 / 4.621 |     +0.85% |   +0.35% |                 0.854 / 0.847 |       -0.38% |     -1.82% |
| mixed/broad/100/1/summary            |               4.779 / 4.694 |     -3.07% |   -0.87% |                 0.879 / 0.873 |       +0.12% |     -1.63% |
| mixed/broad/100/3/metadata           |               5.144 / 5.131 |     -0.25% |   -0.21% |                 0.997 / 1.003 |       +0.16% |     -0.84% |
| mixed/broad/100/3/names              |               4.512 / 4.521 |     -0.41% |   -1.11% |                 0.882 / 0.890 |       +0.94% |     -0.02% |
| mixed/broad/100/3/summary            |               4.802 / 4.731 |     -0.25% |   -0.57% |                 0.961 / 0.961 |       -1.04% |     -1.66% |
| mixed/broad/1000/1/metadata          |               9.887 / 9.399 |     -6.78% |   -2.00% |                 1.977 / 1.938 |       -0.97% |     -1.71% |
| mixed/broad/1000/1/names             |               5.444 / 5.497 |     +0.65% |   -0.95% |                 1.033 / 1.026 |       -0.41% |     -1.34% |
| mixed/broad/1000/1/summary           |               7.015 / 6.271 |    -10.44% |   -0.43% |                 1.389 / 1.371 |       -0.63% |     -1.72% |
| mixed/broad/1000/3/metadata          |               8.838 / 8.724 |     -0.45% |   +0.19% |                 2.204 / 2.253 |       +1.85% |     +0.29% |
| mixed/broad/1000/3/names             |               5.382 / 5.400 |     +0.33% |   -0.32% |                 1.086 / 1.079 |       -0.64% |     -1.37% |
| mixed/broad/1000/3/summary           |               6.364 / 6.290 |     -1.17% |   -1.22% |                 1.588 / 1.631 |       +0.60% |     +0.92% |
| mixed/broad/10000/1/metadata         |             58.831 / 51.867 |    -13.04% |   +0.15% |               12.780 / 12.629 |       -1.65% |     -1.58% |
| mixed/broad/10000/1/names            |             12.445 / 12.244 |     -0.86% |   -1.24% |                 3.005 / 3.010 |       +0.17% |     -0.42% |
| mixed/broad/10000/1/summary          |             33.925 / 25.307 |    -25.48% |   -0.08% |                 6.618 / 6.620 |       -0.45% |     +0.70% |
| mixed/broad/10000/3/metadata         |             45.238 / 45.538 |     +0.68% |   +0.38% |               12.327 / 11.894 |       -1.19% |     -1.07% |
| mixed/broad/10000/3/names            |             12.079 / 11.848 |     -1.01% |   -1.42% |                 3.096 / 3.111 |       +0.09% |     -0.30% |
| mixed/broad/10000/3/summary          |             21.323 / 21.408 |     +0.12% |   -0.00% |                 6.029 / 6.057 |       +2.89% |     +3.30% |
| mixed/broad/100000/1/metadata        |           561.808 / 467.545 |    -15.03% |   +0.07% |             197.476 / 194.652 |       -1.41% |     -1.34% |
| mixed/broad/100000/1/names           |             77.535 / 77.431 |     -0.13% |   -0.02% |               42.271 / 42.188 |       -0.17% |     -0.30% |
| mixed/broad/100000/1/summary         |           312.384 / 227.193 |    -27.27% |   -0.24% |             122.495 / 121.243 |       -0.85% |     -0.81% |
| mixed/broad/100000/3/metadata        |           414.788 / 408.877 |     -0.66% |   -1.06% |             161.427 / 157.974 |       -2.64% |     -2.00% |
| mixed/broad/100000/3/names           |             78.527 / 78.238 |     -0.37% |   +0.89% |               42.398 / 42.566 |       +0.91% |     +0.44% |
| mixed/broad/100000/3/summary         |           169.786 / 171.222 |     +0.96% |   +0.67% |               86.559 / 85.415 |       -1.40% |     -1.81% |
| mixed/deep/10/1/metadata             |               4.978 / 4.915 |     -1.27% |   -1.47% |                 0.883 / 0.883 |       -0.06% |     -2.20% |
| mixed/deep/10/1/names                |               4.745 / 4.682 |     -0.88% |   +0.25% |                 0.848 / 0.858 |       +1.06% |     +0.17% |
| mixed/deep/10/1/summary              |               4.702 / 4.698 |     -0.56% |   +0.43% |                 0.882 / 0.871 |       -0.75% |     -2.35% |
| mixed/deep/10/3/metadata             |               4.935 / 4.956 |     -0.22% |   -0.11% |                 0.939 / 0.934 |       -0.06% |     -0.95% |
| mixed/deep/10/3/names                |               4.746 / 4.735 |     -0.05% |   -1.04% |                 0.928 / 0.909 |       +0.66% |     -2.25% |
| mixed/deep/10/3/summary              |               5.281 / 5.193 |     -1.24% |   -2.08% |                 0.920 / 0.929 |       +1.07% |     +0.28% |
| mixed/deep/100/1/metadata            |               5.660 / 5.509 |     -3.60% |   -0.87% |                 0.999 / 0.997 |       +0.48% |     -0.61% |
| mixed/deep/100/1/names               |               4.964 / 4.987 |     +0.46% |   +0.49% |                 0.899 / 0.899 |       +1.01% |     -0.25% |
| mixed/deep/100/1/summary             |               5.079 / 4.973 |     -1.45% |   +1.71% |                 0.921 / 0.929 |       -0.22% |     -1.85% |
| mixed/deep/100/3/metadata            |               5.512 / 5.481 |     -1.13% |   -0.97% |                 1.288 / 1.290 |       -0.60% |     -2.03% |
| mixed/deep/100/3/names               |               5.132 / 5.050 |     -0.87% |   -0.78% |                 1.137 / 1.150 |       +0.68% |     -0.99% |
| mixed/deep/100/3/summary             |               5.162 / 5.123 |     -0.62% |   +1.45% |                 1.149 / 1.164 |       +1.40% |     +0.71% |
| mixed/deep/1000/1/metadata           |             12.249 / 11.547 |     -5.73% |   -0.59% |                 2.287 / 2.260 |       -1.13% |     -1.86% |
| mixed/deep/1000/1/names              |               7.375 / 7.407 |     +0.09% |   +0.43% |                 1.189 / 1.196 |       +0.22% |     -1.00% |
| mixed/deep/1000/1/summary            |               9.241 / 8.274 |    -11.08% |   -0.12% |                 1.579 / 1.587 |       +0.93% |     +0.01% |
| mixed/deep/1000/3/metadata           |               8.904 / 8.858 |     -1.10% |   -0.95% |                 2.972 / 2.968 |       -0.13% |     +1.18% |
| mixed/deep/1000/3/names              |               6.933 / 7.007 |     +1.06% |   +0.47% |                 1.528 / 1.538 |       +2.37% |     -2.21% |
| mixed/deep/1000/3/summary            |               7.763 / 7.264 |     -5.88% |   -0.68% |                 1.520 / 1.521 |       +0.52% |     -0.91% |
| mixed/deep/10000/1/metadata          |             77.568 / 69.537 |    -10.16% |   +0.00% |               17.009 / 16.871 |       -0.81% |     -0.87% |
| mixed/deep/10000/1/names             |             33.924 / 34.035 |     +0.33% |   +0.01% |                 4.565 / 4.583 |       +0.10% |     -0.89% |
| mixed/deep/10000/1/summary           |             51.577 / 41.740 |    -19.00% |   +0.10% |                 8.517 / 8.504 |       +0.00% |     -0.29% |
| mixed/deep/10000/3/metadata          |             38.940 / 38.733 |     +0.73% |   +0.49% |               21.184 / 20.725 |       -1.44% |     -1.79% |
| mixed/deep/10000/3/names             |             23.454 / 23.603 |     -0.13% |   +0.59% |                 3.848 / 3.962 |       +1.93% |     -0.09% |
| mixed/deep/10000/3/summary           |             30.204 / 27.285 |    -10.64% |   +0.01% |                 5.383 / 5.711 |       +4.32% |     +1.52% |
| mixed/deep/100000/1/metadata         |           749.570 / 669.368 |    -10.19% |   -0.29% |             211.795 / 209.868 |       -1.05% |     -1.01% |
| mixed/deep/100000/1/names            |           303.830 / 304.931 |     +0.36% |   -0.16% |               65.148 / 65.165 |       -0.57% |     -0.88% |
| mixed/deep/100000/1/summary          |           511.942 / 407.826 |    -20.35% |   -0.15% |             134.252 / 134.142 |       +0.17% |     +0.11% |
| mixed/deep/100000/3/metadata         |           319.688 / 326.887 |     +1.98% |   +0.04% |             186.058 / 184.653 |       -0.89% |     -4.32% |
| mixed/deep/100000/3/names            |           195.964 / 193.721 |     -0.33% |   -0.23% |               46.031 / 47.418 |       +0.74% |     +0.67% |
| mixed/deep/100000/3/summary          |           260.344 / 227.225 |    -12.72% |   -0.53% |               74.816 / 74.540 |       -0.17% |     -0.86% |
| regular/broad/10/1/eager-csv         |               4.589 / 4.526 |     -1.37% |   -1.68% |                 0.845 / 0.841 |       -0.14% |     -1.09% |
| regular/broad/10/1/eager-summary     |               4.578 / 4.578 |     -0.51% |   -1.43% |                 0.842 / 0.844 |       +0.30% |     -0.99% |
| regular/broad/10/1/files             |               4.537 / 4.465 |     -1.15% |   -0.28% |                 0.826 / 0.831 |       +0.44% |     -1.99% |
| regular/broad/10/1/native-name       |               4.600 / 4.560 |     -0.87% |   -0.57% |                 0.814 / 0.818 |       +0.46% |     -1.59% |
| regular/broad/10/3/eager-csv         |               4.433 / 4.408 |     -0.24% |   -0.83% |                 0.879 / 0.891 |       +1.37% |     +0.46% |
| regular/broad/10/3/eager-summary     |               4.561 / 4.511 |     -1.02% |   -2.11% |                 0.898 / 0.898 |       -0.22% |     -0.71% |
| regular/broad/10/3/files             |               5.059 / 5.030 |     -1.11% |   -0.29% |                 0.867 / 0.862 |       +0.22% |     -1.29% |
| regular/broad/10/3/native-name       |               4.547 / 4.449 |     -0.63% |   -1.13% |                 0.874 / 0.856 |       -1.95% |     -1.72% |
| regular/broad/100/1/eager-csv        |               4.980 / 4.995 |     -1.18% |   -0.30% |                 0.882 / 0.879 |       -0.32% |     -1.07% |
| regular/broad/100/1/eager-summary    |               4.710 / 4.661 |     +0.04% |   -0.28% |                 0.885 / 0.882 |       -0.27% |     -1.38% |
| regular/broad/100/1/files            |               4.631 / 4.580 |     -2.92% |   -2.62% |                 0.848 / 0.837 |       -0.99% |     -0.95% |
| regular/broad/100/1/native-name      |               4.717 / 4.739 |     +0.86% |   +0.63% |                 0.839 / 0.864 |       +0.57% |     -1.85% |
| regular/broad/100/3/eager-csv        |               4.705 / 4.724 |     +1.00% |   +0.17% |                 0.941 / 0.933 |       -0.57% |     -2.26% |
| regular/broad/100/3/eager-summary    |               4.821 / 4.754 |     -1.09% |   -1.75% |                 0.959 / 0.945 |       -1.39% |     -1.63% |
| regular/broad/100/3/files            |               4.630 / 4.609 |     -2.04% |   -3.26% |                 0.932 / 0.932 |       +0.37% |     -0.50% |
| regular/broad/100/3/native-name      |               4.759 / 4.655 |     -1.19% |   +0.13% |                 0.891 / 0.895 |       +0.28% |     -1.56% |
| regular/broad/1000/1/eager-csv       |               6.922 / 6.991 |     +0.89% |   +1.33% |                 1.376 / 1.373 |       -0.22% |     -1.47% |
| regular/broad/1000/1/eager-summary   |               6.977 / 7.050 |     -0.43% |   -1.42% |                 1.338 / 1.343 |       +0.36% |     -0.19% |
| regular/broad/1000/1/files           |               5.160 / 5.116 |     -0.85% |   +0.57% |                 0.978 / 0.976 |       -0.24% |     -0.77% |
| regular/broad/1000/1/native-name     |               7.014 / 7.054 |     +0.58% |   +0.45% |                 0.984 / 0.981 |       -0.33% |     -0.88% |
| regular/broad/1000/3/eager-csv       |               6.398 / 6.346 |     +0.28% |   -0.36% |                 1.580 / 1.645 |       +4.09% |     -3.06% |
| regular/broad/1000/3/eager-summary   |               6.326 / 6.259 |     -1.86% |   -0.04% |                 1.553 / 1.562 |       +1.09% |     -2.11% |
| regular/broad/1000/3/files           |               5.156 / 5.175 |     -1.30% |   -0.07% |                 1.040 / 1.035 |       -0.51% |     -0.04% |
| regular/broad/1000/3/native-name     |               6.226 / 6.283 |     +0.92% |   +0.24% |                 1.025 / 1.028 |       -0.48% |     -1.34% |
| regular/broad/10000/1/eager-csv      |             33.425 / 33.540 |     -0.67% |   -0.51% |                 6.895 / 7.020 |       +2.56% |     -2.05% |
| regular/broad/10000/1/eager-summary  |             34.334 / 33.239 |     -3.19% |   -2.89% |                 6.259 / 6.321 |       +0.15% |     -0.96% |
| regular/broad/10000/1/files          |             11.841 / 11.909 |     -0.50% |   -1.17% |                 2.524 / 2.532 |       -0.03% |     -0.96% |
| regular/broad/10000/1/native-name    |             33.211 / 33.189 |     +0.16% |   -0.09% |                 2.541 / 2.514 |       -0.45% |     -0.36% |
| regular/broad/10000/3/eager-csv      |             21.566 / 21.513 |     -0.70% |   -0.42% |                 6.221 / 6.378 |       +2.41% |     +0.50% |
| regular/broad/10000/3/eager-summary  |             20.206 / 19.860 |     -1.34% |   -1.17% |                 5.573 / 5.410 |       -4.24% |     +0.91% |
| regular/broad/10000/3/files          |             11.320 / 11.331 |     -0.57% |   -0.32% |                 2.595 / 2.599 |       +0.14% |     +0.10% |
| regular/broad/10000/3/native-name    |             20.075 / 20.223 |     +0.69% |   +0.96% |                 2.565 / 2.548 |       -0.35% |     -0.66% |
| regular/broad/100000/1/eager-csv     |           311.251 / 314.399 |     +0.80% |   +0.14% |             132.346 / 130.969 |       -1.22% |     -1.34% |
| regular/broad/100000/1/eager-summary |           302.034 / 302.486 |     -0.32% |   -0.42% |             125.319 / 125.155 |       -0.37% |     -0.49% |
| regular/broad/100000/1/files         |             76.738 / 76.012 |     -0.95% |   -0.04% |               40.124 / 40.136 |       +0.08% |     -0.19% |
| regular/broad/100000/1/native-name   |           301.726 / 305.946 |     +1.43% |   +0.23% |               38.448 / 38.463 |       +0.03% |     -0.14% |
| regular/broad/100000/3/eager-csv     |           174.401 / 177.835 |     -0.62% |   -0.57% |               90.472 / 89.074 |       -1.35% |     -1.14% |
| regular/broad/100000/3/eager-summary |           159.904 / 158.349 |     -0.56% |   -0.46% |               84.285 / 83.261 |       -1.22% |     -1.01% |
| regular/broad/100000/3/files         |             76.137 / 76.101 |     +0.12% |   -0.05% |               39.754 / 39.732 |       -0.06% |     +0.22% |
| regular/broad/100000/3/native-name   |           158.256 / 160.370 |     +0.54% |   +0.28% |               39.357 / 39.055 |       -0.70% |     -0.57% |
| regular/deep/10/1/eager-csv          |               5.302 / 5.353 |     +0.18% |   +0.04% |                 0.879 / 0.869 |       -1.13% |     -2.08% |
| regular/deep/10/1/eager-summary      |               4.831 / 4.819 |     -0.55% |   -0.83% |                 0.877 / 0.868 |       -1.25% |     -1.10% |
| regular/deep/10/1/files              |               4.847 / 4.833 |     -0.60% |   -1.11% |                 0.863 / 0.858 |       +0.34% |     -1.28% |
| regular/deep/10/1/native-name        |               4.781 / 4.808 |     +0.56% |   -0.26% |                 0.864 / 0.857 |       -1.02% |     -1.37% |
| regular/deep/10/3/eager-csv          |               5.319 / 5.151 |     +0.43% |   +0.68% |                 1.206 / 1.241 |       +0.62% |     -2.42% |
| regular/deep/10/3/eager-summary      |               5.137 / 5.025 |     -2.17% |   +0.31% |                 1.209 / 1.282 |       +1.83% |     -0.52% |
| regular/deep/10/3/files              |               5.087 / 5.147 |     +0.41% |   +0.26% |                 1.183 / 1.214 |       -0.64% |     -1.68% |
| regular/deep/10/3/native-name        |               5.251 / 5.231 |     -0.39% |   +0.59% |                 1.207 / 1.182 |       +0.62% |     -3.34% |
| regular/deep/100/1/eager-csv         |               6.708 / 6.701 |     -0.09% |   -1.19% |                 1.103 / 1.085 |       -0.34% |     -1.37% |
| regular/deep/100/1/eager-summary     |               6.465 / 6.473 |     -0.89% |   -0.87% |                 1.075 / 1.072 |       -0.20% |     -1.66% |
| regular/deep/100/1/files             |               6.475 / 6.414 |     +0.54% |   -0.06% |                 1.014 / 1.013 |       +0.04% |     -1.07% |
| regular/deep/100/1/native-name       |               6.482 / 6.547 |     +0.92% |   -0.03% |                 1.007 / 1.025 |       +0.80% |     -0.45% |
| regular/deep/100/3/eager-csv         |               6.164 / 6.203 |     +0.49% |   -0.42% |                 1.709 / 1.723 |       +0.81% |     -0.96% |
| regular/deep/100/3/eager-summary     |               6.322 / 6.286 |     -0.39% |   -0.42% |                 1.585 / 1.563 |       -1.20% |     -2.47% |
| regular/deep/100/3/files             |               6.128 / 6.120 |     -0.03% |   -2.28% |                 1.583 / 1.601 |       +1.13% |     -2.14% |
| regular/deep/100/3/native-name       |               6.108 / 6.156 |     -0.03% |   +0.48% |                 1.498 / 1.498 |       -1.02% |     +1.20% |
| regular/deep/1000/1/eager-csv        |             11.502 / 11.488 |     -0.12% |   +0.25% |                 1.832 / 1.833 |       -0.05% |     -0.33% |
| regular/deep/1000/1/eager-summary    |             11.448 / 11.299 |     -0.36% |   -1.13% |                 1.779 / 1.805 |       +0.63% |     -0.42% |
| regular/deep/1000/1/files            |               9.398 / 9.515 |     +0.86% |   +0.92% |                 1.417 / 1.437 |       +0.24% |     -0.75% |
| regular/deep/1000/1/native-name      |             11.131 / 11.006 |     -1.13% |   -0.55% |                 1.386 / 1.385 |       -0.04% |     -1.18% |
| regular/deep/1000/3/eager-csv        |               9.660 / 9.707 |     +0.54% |   +0.54% |                 2.283 / 2.335 |       -0.25% |     -1.22% |
| regular/deep/1000/3/eager-summary    |               9.493 / 9.553 |     -0.53% |   +0.18% |                 2.284 / 2.334 |       +1.66% |     -0.21% |
| regular/deep/1000/3/files            |               8.173 / 8.091 |     -1.74% |   +0.36% |                 2.010 / 1.986 |       -1.20% |     -2.71% |
| regular/deep/1000/3/native-name      |               9.551 / 9.508 |     -0.41% |   -0.69% |                 1.918 / 1.937 |       +0.98% |     -1.69% |
| regular/deep/10000/1/eager-csv       |             36.144 / 36.227 |     +0.23% |   -0.43% |                 7.171 / 7.504 |       +4.65% |     -1.19% |
| regular/deep/10000/1/eager-summary   |             34.021 / 33.858 |     +0.11% |   -0.31% |                 6.083 / 6.085 |       +0.88% |     +0.69% |
| regular/deep/10000/1/files           |             15.456 / 15.686 |     +0.80% |   -0.42% |                 2.778 / 2.800 |       +0.82% |     -0.21% |
| regular/deep/10000/1/native-name     |             33.779 / 33.584 |     +0.27% |   -0.87% |                 2.752 / 2.746 |       -0.19% |     -0.96% |
| regular/deep/10000/3/eager-csv       |             34.796 / 34.871 |     +0.21% |   +0.31% |                 4.717 / 4.785 |       +0.31% |     -0.57% |
| regular/deep/10000/3/eager-summary   |             34.997 / 35.043 |     -0.26% |   -0.28% |                 4.057 / 4.067 |       +0.23% |     -0.31% |
| regular/deep/10000/3/files           |             11.336 / 11.253 |     -0.30% |   -0.28% |                 3.512 / 3.548 |       +1.02% |     -2.49% |
| regular/deep/10000/3/native-name     |             34.660 / 34.407 |     +0.04% |   -0.30% |                 2.837 / 2.857 |       +0.72% |     -0.64% |
| regular/deep/100000/1/eager-csv      |           315.567 / 316.016 |     +0.18% |   +0.30% |             113.972 / 113.760 |       -0.17% |     -0.30% |
| regular/deep/100000/1/eager-summary  |           304.033 / 305.842 |     +0.61% |   -0.80% |             106.923 / 106.599 |       +0.04% |     -0.29% |
| regular/deep/100000/1/files          |             72.107 / 71.404 |     -0.02% |   +0.00% |               39.882 / 39.951 |       +0.37% |     -0.06% |
| regular/deep/100000/1/native-name    |           307.213 / 305.214 |     +0.09% |   -0.22% |               34.200 / 34.061 |       -0.41% |     -1.10% |
| regular/deep/100000/3/eager-csv      |           290.156 / 289.724 |     -0.20% |   -0.02% |               45.580 / 45.193 |       -1.12% |     +0.11% |
| regular/deep/100000/3/eager-summary  |           290.500 / 290.148 |     -0.12% |   +0.13% |               41.993 / 42.132 |       +0.41% |     -0.25% |
| regular/deep/100000/3/files          |             51.013 / 51.358 |     +0.26% |   +0.55% |               18.415 / 18.279 |       -0.07% |     +0.63% |
| regular/deep/100000/3/native-name    |           285.405 / 284.536 |     -0.30% |   +0.09% |               15.877 / 15.788 |       +1.37% |     -3.93% |
| symlinks/broad/10/1/metadata         |               5.181 / 5.214 |     -0.96% |   -0.69% |                 0.861 / 0.871 |       -1.12% |     -1.58% |
| symlinks/broad/10/1/names            |               4.715 / 4.576 |     -0.61% |   -1.29% |                 0.831 / 0.831 |       +0.35% |     -0.71% |
| symlinks/broad/10/1/summary          |               4.564 / 4.502 |     -1.36% |   +0.72% |                 0.841 / 0.849 |       +1.29% |     -0.33% |
| symlinks/broad/10/3/metadata         |               4.693 / 4.696 |     +0.10% |   -0.08% |                 0.905 / 0.909 |       -1.43% |     -1.70% |
| symlinks/broad/10/3/names            |               4.511 / 4.412 |     -1.41% |   -2.12% |                 0.880 / 0.868 |       -1.29% |     -2.11% |
| symlinks/broad/10/3/summary          |               4.588 / 4.556 |     -0.70% |   -1.13% |                 0.883 / 0.886 |       -0.38% |     -1.50% |
| symlinks/broad/100/1/metadata        |               5.218 / 5.146 |     -1.38% |   +1.01% |                 0.973 / 0.976 |       +0.13% |     -1.60% |
| symlinks/broad/100/1/names           |               4.567 / 4.535 |     -0.35% |   -0.54% |                 0.843 / 0.845 |       +0.25% |     -0.89% |
| symlinks/broad/100/1/summary         |               4.707 / 4.627 |     -3.28% |   -0.38% |                 0.877 / 0.879 |       +0.09% |     -0.71% |
| symlinks/broad/100/3/metadata        |               5.198 / 5.176 |     -0.48% |   -0.74% |                 1.029 / 1.021 |       -0.74% |     -1.48% |
| symlinks/broad/100/3/names           |               4.534 / 4.573 |     +0.71% |   -0.91% |                 0.872 / 0.880 |       +0.05% |     -1.09% |
| symlinks/broad/100/3/summary         |               4.875 / 4.881 |     +0.13% |   -0.73% |                 0.926 / 0.933 |       +0.78% |     -1.23% |
| symlinks/broad/1000/1/metadata       |              10.624 / 9.312 |    -11.76% |   +0.20% |                 2.136 / 2.125 |       -0.46% |     -1.42% |
| symlinks/broad/1000/1/names          |               5.188 / 5.117 |     -1.41% |   -0.21% |                 1.029 / 1.041 |       -0.40% |     -1.61% |
| symlinks/broad/1000/1/summary        |               7.083 / 5.638 |    -20.14% |   +2.65% |                 1.365 / 1.351 |       -1.03% |     -0.90% |
| symlinks/broad/1000/3/metadata       |               9.569 / 9.543 |     +0.63% |   -0.57% |                 2.398 / 2.354 |       -1.55% |     -1.42% |
| symlinks/broad/1000/3/names          |               5.209 / 5.147 |     -1.04% |   +0.10% |                 1.080 / 1.079 |       -0.68% |     -1.91% |
| symlinks/broad/1000/3/summary        |               7.108 / 6.737 |     -2.77% |   -0.57% |                 1.615 / 1.610 |       -0.30% |     -2.92% |
| symlinks/broad/10000/1/metadata      |             66.758 / 51.706 |    -22.74% |   -0.12% |               15.210 / 15.002 |       -1.40% |     -1.74% |
| symlinks/broad/10000/1/names         |             11.910 / 12.241 |     -0.84% |   -0.77% |                 2.991 / 3.011 |       +0.65% |     -0.36% |
| symlinks/broad/10000/1/summary       |             34.220 / 17.656 |    -48.70% |   -0.58% |                 6.704 / 6.666 |       -0.16% |     -0.64% |
| symlinks/broad/10000/3/metadata      |             55.715 / 55.259 |     -0.52% |   -0.68% |               14.691 / 14.196 |       -1.71% |     -2.08% |
| symlinks/broad/10000/3/names         |             11.603 / 11.907 |     +1.26% |   +0.20% |                 3.105 / 3.103 |       +0.47% |     -0.34% |
| symlinks/broad/10000/3/summary       |             21.562 / 21.780 |     -0.04% |   -0.32% |                 6.435 / 6.356 |       +2.38% |     -3.11% |
| symlinks/broad/100000/1/metadata     |           680.857 / 503.730 |    -26.02% |   +0.14% |             231.677 / 229.339 |       -1.01% |     -1.13% |
| symlinks/broad/100000/1/names        |             77.998 / 77.505 |     -0.63% |   -0.19% |               43.310 / 43.152 |       -0.29% |     -0.44% |
| symlinks/broad/100000/1/summary      |           311.506 / 139.059 |    -55.26% |   -0.22% |             125.427 / 124.485 |       -0.77% |     -0.67% |
| symlinks/broad/100000/3/metadata     |           514.059 / 517.847 |     +0.32% |   -0.45% |             196.394 / 193.199 |       -1.70% |     -1.52% |
| symlinks/broad/100000/3/names        |             77.632 / 77.723 |     -0.80% |   +0.24% |               43.585 / 43.448 |       +0.21% |     -0.57% |
| symlinks/broad/100000/3/summary      |           173.882 / 173.503 |     +0.05% |   +0.61% |               88.574 / 86.958 |       -1.76% |     -2.63% |
| symlinks/deep/10/1/metadata          |               4.871 / 4.801 |     -1.44% |   -0.70% |                 0.872 / 0.889 |       +1.90% |     +0.65% |
| symlinks/deep/10/1/names             |               4.752 / 4.660 |     -1.93% |   -1.30% |                 0.852 / 0.849 |       +0.24% |     -0.24% |
| symlinks/deep/10/1/summary           |               4.779 / 4.744 |     -0.92% |   -1.61% |                 0.864 / 0.857 |       -0.73% |     -1.51% |
| symlinks/deep/10/3/metadata          |               4.937 / 4.943 |     -0.15% |   -0.17% |                 0.926 / 0.922 |       +0.44% |     -1.48% |
| symlinks/deep/10/3/names             |               4.682 / 4.701 |     -0.53% |   +0.21% |                 0.909 / 0.908 |       +0.11% |     -1.32% |
| symlinks/deep/10/3/summary           |               4.749 / 4.668 |     -2.40% |   -1.95% |                 0.901 / 0.903 |       +0.23% |     -0.56% |
| symlinks/deep/100/1/metadata         |               5.675 / 5.508 |     -3.86% |   -0.31% |                 1.013 / 1.015 |       -0.04% |     -1.05% |
| symlinks/deep/100/1/names            |               4.978 / 4.965 |     -0.80% |   -1.63% |                 0.877 / 0.879 |       +0.23% |     -1.18% |
| symlinks/deep/100/1/summary          |               5.281 / 5.060 |     -3.81% |   -1.47% |                 0.917 / 0.920 |       +0.52% |     -1.53% |
| symlinks/deep/100/3/metadata         |               5.662 / 5.692 |     +0.34% |   +0.35% |                 1.285 / 1.318 |       +3.55% |     +0.52% |
| symlinks/deep/100/3/names            |               5.086 / 5.150 |     +1.26% |   +0.22% |                 1.148 / 1.145 |       +0.21% |     -0.60% |
| symlinks/deep/100/3/summary          |               5.328 / 5.255 |     -2.46% |   +1.23% |                 1.197 / 1.200 |       +0.24% |     -1.22% |
| symlinks/deep/1000/1/metadata        |             13.637 / 12.128 |    -10.98% |   -0.70% |                 2.522 / 2.510 |       -0.30% |     -1.01% |
| symlinks/deep/1000/1/names           |               7.793 / 7.634 |     -1.76% |   -1.61% |                 1.181 / 1.181 |       +0.64% |     -0.44% |
| symlinks/deep/1000/1/summary         |               9.506 / 7.718 |    -18.57% |   +0.33% |                 1.589 / 1.604 |       +0.56% |     -1.26% |
| symlinks/deep/1000/3/metadata        |               9.867 / 9.885 |     -0.37% |   -1.19% |                 3.314 / 3.265 |       -1.46% |     -0.45% |
| symlinks/deep/1000/3/names           |               6.919 / 6.867 |     -0.92% |   -1.62% |                 1.494 / 1.507 |       -1.80% |     +0.33% |
| symlinks/deep/1000/3/summary         |               7.635 / 6.938 |     -8.40% |   +0.71% |                 1.549 / 1.587 |       +2.47% |     -0.34% |
| symlinks/deep/10000/1/metadata       |             90.079 / 75.147 |    -17.15% |   +0.24% |               18.412 / 18.210 |       -0.89% |     -0.77% |
| symlinks/deep/10000/1/names          |             33.494 / 33.397 |     +0.07% |   +0.22% |                 4.539 / 4.553 |       +0.32% |     -0.05% |
| symlinks/deep/10000/1/summary        |             57.160 / 38.464 |    -32.75% |   -0.25% |                 8.339 / 8.332 |       -0.08% |     -0.21% |
| symlinks/deep/10000/3/metadata       |             50.766 / 49.898 |     +0.82% |   +0.08% |               23.927 / 23.753 |       -0.54% |     -0.85% |
| symlinks/deep/10000/3/names          |             22.949 / 23.302 |     +1.18% |   +0.50% |                 3.937 / 3.899 |       -1.22% |     +0.11% |
| symlinks/deep/10000/3/summary        |             30.772 / 24.087 |    -21.72% |   -0.75% |                 5.634 / 5.563 |       -0.95% |     -0.54% |
| symlinks/deep/100000/1/metadata      |           913.444 / 765.754 |    -16.00% |   -0.10% |             237.692 / 235.722 |       -0.83% |     -0.72% |
| symlinks/deep/100000/1/names         |           307.571 / 307.741 |     +0.22% |   +0.03% |               65.680 / 65.438 |       -0.37% |     -0.37% |
| symlinks/deep/100000/1/summary       |           559.733 / 371.158 |    -33.83% |   -0.14% |             132.719 / 132.773 |       -0.02% |     -0.17% |
| symlinks/deep/100000/3/metadata      |           435.563 / 437.518 |     +0.57% |   -1.07% |             235.991 / 233.116 |       -1.19% |     -1.05% |
| symlinks/deep/100000/3/names         |           192.064 / 191.682 |     +0.00% |   -0.17% |               48.268 / 48.341 |       +0.15% |     +1.31% |
| symlinks/deep/100000/3/summary       |           266.191 / 196.986 |    -26.13% |   +0.45% |               75.265 / 73.609 |       -1.46% |     +0.64% |

## All 16 followed-link controls

| Case                                    | Mac parent / candidate [ms] | Mac change | Mac copy | Linux parent / candidate [ms] | Linux change | Linux copy |
| :-------------------------------------- | --------------------------: | ---------: | -------: | ----------------------------: | -----------: | ---------: |
| follow/symlinks/broad/1000/1/metadata   |             12.459 / 12.468 |     +0.07% |   -0.71% |                 2.900 / 2.879 |       -0.59% |     +0.38% |
| follow/symlinks/broad/1000/1/summary    |               9.097 / 9.203 |     +1.16% |   +0.81% |                 2.122 / 2.141 |       -0.41% |     -1.12% |
| follow/symlinks/broad/1000/3/metadata   |             11.269 / 11.234 |     +0.92% |   +0.08% |                 2.848 / 2.957 |       +3.77% |     +2.04% |
| follow/symlinks/broad/1000/3/summary    |               8.082 / 8.240 |     +0.21% |   -0.70% |                 2.138 / 2.147 |       +0.39% |     -1.34% |
| follow/symlinks/broad/100000/1/metadata |           844.597 / 851.837 |     +1.07% |   +0.42% |             314.382 / 314.428 |       +0.01% |     +0.63% |
| follow/symlinks/broad/100000/1/summary  |           492.240 / 497.793 |     +0.78% |   +0.45% |             208.494 / 208.440 |       +0.04% |     -0.18% |
| follow/symlinks/broad/100000/3/metadata |           667.508 / 672.538 |     -0.09% |   -0.24% |             236.308 / 234.637 |       -0.71% |     +0.05% |
| follow/symlinks/broad/100000/3/summary  |           315.249 / 320.368 |     +0.44% |   +0.68% |             126.357 / 126.632 |       +0.22% |     +1.15% |
| follow/symlinks/deep/1000/1/metadata    |             15.553 / 15.553 |     +0.42% |   +0.19% |                 3.396 / 3.369 |       -0.43% |     -0.24% |
| follow/symlinks/deep/1000/1/summary     |             11.664 / 11.846 |     +1.70% |   +0.29% |                 2.414 / 2.420 |       -0.59% |     +0.38% |
| follow/symlinks/deep/1000/3/metadata    |             10.562 / 10.770 |     +1.14% |   -0.17% |                 2.818 / 2.669 |       -1.83% |     -3.00% |
| follow/symlinks/deep/1000/3/summary     |               9.042 / 9.137 |     -0.46% |   +0.06% |                 2.250 / 2.124 |       -4.02% |     -2.59% |
| follow/symlinks/deep/100000/1/metadata  |         1132.753 / 1124.590 |     -0.72% |   -0.50% |             329.950 / 329.317 |       -0.37% |     +0.14% |
| follow/symlinks/deep/100000/1/summary   |           794.356 / 792.570 |     -0.48% |   +0.76% |             242.041 / 241.562 |       -0.20% |     -0.28% |
| follow/symlinks/deep/100000/3/metadata  |           452.661 / 449.305 |     -0.83% |   -0.84% |             183.657 / 179.025 |       -1.34% |     -1.64% |
| follow/symlinks/deep/100000/3/summary   |           353.610 / 354.769 |     +0.33% |   +0.30% |             104.929 / 105.918 |       +0.94% |     -0.49% |

## Reported RSS for 100,000-entry symlink/mixed metadata and summaries

RSS is the per-child value returned in bytes by the established native launcher. Each cell gives
the median and maximum of 27 measured parent or candidate runs, in MiB. It is not allocation/live
byte accounting and includes process/index/output effects. Raw RSS for every case and copy remains.

| Host  | Case                             | Parent median / max [MiB] | Candidate median / max [MiB] |
| :---- | :------------------------------- | ------------------------: | ---------------------------: |
| macos | mixed/broad/100000/1/metadata    |             52.77 / 52.94 |                52.72 / 52.92 |
| macos | mixed/broad/100000/1/summary     |             52.44 / 52.86 |                52.44 / 52.89 |
| macos | mixed/broad/100000/3/metadata    |             37.83 / 38.58 |                37.92 / 38.39 |
| macos | mixed/broad/100000/3/summary     |             37.78 / 38.25 |                37.75 / 38.45 |
| macos | mixed/deep/100000/1/metadata     |             11.06 / 11.70 |                11.05 / 13.39 |
| macos | mixed/deep/100000/1/summary      |             10.91 / 11.61 |                10.80 / 11.69 |
| macos | mixed/deep/100000/3/metadata     |             11.73 / 13.09 |                11.73 / 12.89 |
| macos | mixed/deep/100000/3/summary      |             11.58 / 12.62 |                11.53 / 13.22 |
| macos | symlinks/broad/100000/1/metadata |             37.58 / 37.97 |                52.64 / 52.98 |
| macos | symlinks/broad/100000/1/summary  |             37.31 / 37.66 |                52.42 / 52.78 |
| macos | symlinks/broad/100000/3/metadata |             37.88 / 38.45 |                37.91 / 38.66 |
| macos | symlinks/broad/100000/3/summary  |             37.73 / 38.23 |                37.72 / 38.33 |
| macos | symlinks/deep/100000/1/metadata  |             11.06 / 12.12 |                11.09 / 12.62 |
| macos | symlinks/deep/100000/1/summary   |             10.92 / 12.05 |                10.91 / 11.69 |
| macos | symlinks/deep/100000/3/metadata  |             11.80 / 13.00 |                11.77 / 12.88 |
| macos | symlinks/deep/100000/3/summary   |             11.33 / 12.45 |                11.44 / 12.34 |
| linux | mixed/broad/100000/1/metadata    |             31.96 / 32.03 |                31.88 / 32.02 |
| linux | mixed/broad/100000/1/summary     |             31.79 / 32.03 |                31.85 / 32.02 |
| linux | mixed/broad/100000/3/metadata    |             31.81 / 32.01 |                31.96 / 32.14 |
| linux | mixed/broad/100000/3/summary     |             31.87 / 32.11 |                31.86 / 32.02 |
| linux | mixed/deep/100000/1/metadata     |             10.73 / 10.84 |                10.69 / 10.84 |
| linux | mixed/deep/100000/1/summary      |             10.70 / 10.85 |                10.71 / 10.84 |
| linux | mixed/deep/100000/3/metadata     |             11.05 / 11.26 |                11.04 / 11.34 |
| linux | mixed/deep/100000/3/summary      |             11.04 / 11.21 |                10.96 / 11.21 |
| linux | symlinks/broad/100000/1/metadata |             31.85 / 32.01 |                31.98 / 32.04 |
| linux | symlinks/broad/100000/1/summary  |             31.86 / 32.02 |                31.86 / 32.01 |
| linux | symlinks/broad/100000/3/metadata |             31.83 / 32.16 |                31.86 / 32.15 |
| linux | symlinks/broad/100000/3/summary  |             31.83 / 32.11 |                31.86 / 32.14 |
| linux | symlinks/deep/100000/1/metadata  |             10.85 / 10.98 |                10.82 / 11.00 |
| linux | symlinks/deep/100000/1/summary   |             10.81 / 10.98 |                10.81 / 10.97 |
| linux | symlinks/deep/100000/3/metadata  |             11.21 / 11.42 |                11.11 / 11.42 |
| linux | symlinks/deep/100000/3/summary   |             11.21 / 11.34 |                11.17 / 11.41 |

## Lossless evidence and next independent work

The [deterministic evidence archive](native-symlink-observations-20261105.json.gz) retains
1,861 original text records (944 Mac,
917 Linux), with 265 unique contents. It is
13,062,328 compressed bytes and 624,011,309 serialized bytes, reconstructing
1,969,060,917 original record bytes. Every record's full text, byte count,
SHA and per-host path provenance survive exact round-trip reconstruction. Repeats are indexed,
not dropped. Archive SHA-256: `c4dabaafb8972d6c430f79d99b5a6b7114bd20dd4211234b406d7c468f9c3376`.

Format `xff-lossless-text-evidence-v2` maps content hashes to exact text and retains each host's
record list with `path`, `bytes` and `sha256`. Sources, drivers, independent oracles, every raw
sample/session, source transitions, exact identities, failed fixture attempts and complete logs
are included. The idle follow watcher was corrected to the private working directory before any
dependent command launched; its record shows the original timing process was untouched. Generated
fixture trees, executables and full compilation databases are excluded. The established launcher
binary's identity is retained; no self-contained launcher executable distribution is claimed.

O20j storage remains open. Pinned MBO segmented slots do not initialize unused T objects; do not
invent a zero-fill explanation. The actual final vector materialization is the separate measured
representation question. The observed-listing early-exit scan and consumed-directory cleanup under
the queue mutex are additional inspection-only plans; neither is included or claimed faster here.
