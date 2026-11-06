# Archive directory child separator, 2026-11-06

In `ArchiveFileSystem::ReadDirectory`, the bounded ordered-index range already restricts entries
to one subtree. A direct child can therefore be recognized by searching for the next `/` after the
known directory prefix, rather than reverse-scanning the entire member key to find its parent.
Searching from byte one at the archive root preserves a leading slash in absolute member names.
When the root's direct-child count equals the indexed node count, every indexed member is a direct
child and the root listing can skip separator checks entirely.

The root fast path is important to the result. The initial forward-search-only version improved
most cases overall, but caused a repeatable Linux regression for flat-root metadata listings. The
selected forward-search plus flat-root shortcut removes that regression while retaining the flat
root gain. A hybrid that kept reverse-searching at the root lost the overall Linux gain.

## Scope and method

The comparison uses the O19i2 implementation at parent commit `45f4c5e65bfb7de5d0facf8bcbdca1d28320ae4a`
and the candidate in this branch. It measures 24 in-memory archive-directory cases: ordinary and
metadata-observing listings across flat, wide, deep, and chain-shaped trees, including large root
and nested listings. Each of three sessions alternated the parent and candidate, with three
Google Benchmark repetitions per case. Within each participant and session, use the median of
its three iteration samples. The table reports the median of the three paired session-level
candidate/parent changes for each case; negative means faster. This exploratory protocol does not
use fastest-seven-of-nine retention or a third unchanged-binary control. The archived JSON retains all raw
repetitions for the initial, hybrid, and selected variants.

The Mac run used the local Apple M5 Pro (arm64, 18 logical CPUs, unpinned). Linux used drogon's
AMD Ryzen 9 9950X, pinned to one logical CPU. Both variants used Clang release builds. Timed cases
used `--benchmark_repetitions=3`, `--benchmark_min_time=0.020`, and
`--benchmark_min_warmup_time=0.010`; fixture creation and validation were outside the timed region.

## Results

Across the 24 selected cases, the median per-case change is **-1.80% on Mac** (21 faster, 3 slower)
and **-3.36% on Linux** (22 faster, 2 slower). The gains are small outside flat roots, so this is an
archive-directory scan improvement rather than a broad command-level speedup.

| Listing case                   | Mac change | Linux change |
| :----------------------------- | ---------: | -----------: |
| Ordinary, flat root, 10k       |    -11.10% |      -10.77% |
| Ordinary, flat root, 100k      |    -11.47% |       -9.21% |
| Ordinary, wide root, 1k        |     -1.03% |       -9.27% |
| Ordinary, wide root, 100k      |     -2.39% |       +3.77% |
| Ordinary, wide one-level, 100k |     -1.93% |       -3.46% |
| Ordinary, wide ten-level, 100k |     -1.09% |       -2.46% |
| Ordinary, deep root, 1k        |     -6.05% |       -4.89% |
| Ordinary, deep root, 100k      |     +6.75% |       -4.36% |
| Ordinary, deep one-level, 100k |     -0.01% |       -2.50% |
| Ordinary, deep ten-level, 100k |     -0.18% |       -2.84% |
| Ordinary, chain root, 1k       |     -0.79% |       -2.79% |
| Ordinary, chain one-level, 1k  |     -7.25% |       -4.14% |
| Metadata, flat root, 10k       |     -6.24% |       -6.32% |
| Metadata, flat root, 100k      |     -8.15% |       -7.24% |
| Metadata, wide root, 1k        |     -1.33% |       -8.35% |
| Metadata, wide root, 100k      |     -5.28% |       -2.43% |
| Metadata, wide one-level, 100k |     -0.20% |       -3.27% |
| Metadata, wide ten-level, 100k |     -0.61% |       -2.67% |
| Metadata, deep root, 1k        |     -2.13% |       -4.18% |
| Metadata, deep root, 100k      |     +7.43% |       +0.96% |
| Metadata, deep one-level, 100k |     -4.67% |       -0.27% |
| Metadata, deep ten-level, 100k |     -1.68% |       -1.94% |
| Metadata, chain root, 1k       |     +0.36% |       -1.53% |
| Metadata, chain one-level, 1k  |     -9.82% |       -3.71% |

The Mac 100k deep-root results are slower at the median in both listing modes, although their
session estimates varied widely; retain them as an explicit limitation rather than explain them
away. Linux's two slower medians are +3.77% for ordinary wide-root 100k and +0.96% for metadata
deep-root 100k. The rejected forward-only variant had a +15.20% Linux regression in metadata
flat-root 100k; adding the direct-child-count root shortcut removed that regression and made both
flat-root 100k cases faster. The hybrid variant was neutral overall on Linux (12 faster, 12 slower)
and is not selected.

The complete raw sessions are in
[`archive-directory-child-separator-20261106.json.gz`](archive-directory-child-separator-20261106.json.gz).
The archive includes all three variants and preserves the parent and candidate benchmark JSON for
each session. Its 193,834-byte gzip has SHA-256
`06d37b5885dc89a9184b051b7fc662f3d659e867ce78543f3d335ff87dbc4d55`. All 36
JSON runs were independently checked for release context, 24-case populations, repetition counts
and errors. The paired estimator reproduces every selected table value. The archive does not
contain source or executable hashes for each timed variant; the separately frozen final patch
identifies the submitted source, without establishing measured-image identity for every run.

## Correctness and limits

The change only classifies direct children inside the already bounded subtree range. It does not
change index ordering, member normalization, recursion, or returned paths. The forward search skips
the leading root slash when present. Existing absolute-member tests cover that path; a new flat-root
test covers the count-based shortcut. The preceding direct-child-count change supplies the in-memory fixture covering synthesized-parent
replacement, first-wins duplicate handling, exact capacities and metadata observations; this
follow-up adds the flat-root shortcut case.

All 25 `@xff_archive` tests passed on Mac and Linux; focused `archive_fs_test` passed under ASan on
both. Changed-file pre-commit checks, including formatting, lint, and I/O policy, passed. This
qualification does not establish a full xff command-level speedup. Keep the deep-root Mac regression
visible in review and use PR CI on the then-current base as the final merge gate.
