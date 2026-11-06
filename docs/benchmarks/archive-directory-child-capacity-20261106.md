# Archive directory child-count capacity hint, 2026-11-06

Use each archive directory's direct-child count to reserve the result vector in `ReadDirectory`.
The existing minimum capacity of 16 remains for small listings; wider directories reserve their
known direct-child count, capped by the indexed node count. This avoids repeated result and
observation-vector growth for wide archive roots without preallocating the whole subtree.

This is a targeted archive listing improvement. It is not a general traversal speedup: the full
88-case listing matrix is near neutral by median overall, and several deep/chain/root controls are
slightly slower. The largest and most repeatable benefits are flat roots with many direct members,
especially when metadata observations are requested.

## Scope and method

The runtime comparison uses the O19i2 parent implementation at `14520395c344903c3dfa247d4088c1f79cdfe0b7`
and the child-count implementation from this qualification. The production patch is retained as
`child-count.patch` in the evidence archive. It adds a
saturating `uint32_t` direct-child count to each archive node and a root count. Indexing increments
only when a new direct child is added; replacing a synthesized directory with its explicit archive
entry preserves its accumulated count. The root count is stored once on the filesystem.

The change does not alter traversal, ordering, metadata, paths, or output. Counts are only used to
choose initial vector capacity. The count saturates at `UINT32_MAX`, and the reservation is capped
at the indexed node count. A large listing can still grow beyond the estimate using normal vector
growth.

Both hosts ran the same 88 in-memory listing cases (ordinary and metadata-observing) and 28 archive
construction/index cases. Fixtures and full entry/stat preflights are outside the timed region.
Each host ran three balanced sessions, nine repetitions per case, retaining the fastest seven.
Every session measured the parent, candidate, and a byte-identical parent-repeat control. Per-case
session changes are candidate time divided by the geometric mean of the two parent times; negative
means faster. Reported case changes are the median of the three session changes. Full JSON, logs,
manifests, source patch, and hashes are in
[`archive-directory-child-capacity-20261106.json.gz`](archive-directory-child-capacity-20261106.json.gz).

The Mac measurement ran on the local Apple M5 Pro (arm64, 18 logical CPUs; unpinned). The Linux
measurement ran on drogon's AMD Ryzen 9 9950X (x86_64; pinned to one logical CPU). These are
controlled local comparisons, not CI runner measurements. Both variants used Clang release builds.

## Results

| Workload group             | Cases | Mac median | Mac faster / slower | Linux median | Linux faster / slower |
| :------------------------- | ----: | ---------: | ------------------: | -----------: | --------------------: |
| All listings               |    88 |     -0.54% |             55 / 33 |       +0.43% |               33 / 55 |
| Flat root, ordinary        |     5 |    -14.87% |               4 / 1 |      -10.64% |                 5 / 0 |
| Flat root, metadata        |     5 |    -26.77% |               5 / 0 |      -21.81% |                 4 / 1 |
| Wide root, ordinary        |     5 |     -6.23% |               3 / 2 |       -1.08% |                 3 / 2 |
| Wide root, metadata        |     5 |    -11.89% |               4 / 1 |       -7.31% |                 4 / 1 |
| Deep root, ordinary        |     5 |     +2.73% |               1 / 4 |       -0.15% |                 3 / 2 |
| Deep root, metadata        |     5 |     +1.89% |               0 / 5 |       -0.21% |                 3 / 2 |
| Archive construction/index |    28 |     -0.81% |             17 / 11 |       -2.30% |                26 / 2 |

The overall listing medians combine multiple tree shapes and sizes, so they intentionally dilute the
flat-root result. Representative 100k-member cases show the scale of the targeted improvement. For each case and
host, the displayed pair is from the session with the median candidate/reference ratio. The
reference is the geometric mean of that session's two parent controls after averaging the fastest
seven of nine samples; the candidate uses the same retained-sample mean. Percentages use those
paired values before rounding, so slower cases keep the same sign as their displayed times:

| Case                | Mac reference | Mac candidate | Mac change | Linux reference | Linux candidate | Linux change |
| :------------------ | ------------: | ------------: | ---------: | --------------: | --------------: | -----------: |
| Flat root, ordinary |       4.36 ms |       2.79 ms |    -35.93% |         4.03 ms |         2.48 ms |      -38.50% |
| Flat root, metadata |       8.60 ms |       3.58 ms |    -58.37% |         6.27 ms |         3.55 ms |      -43.38% |
| Wide root, ordinary |       1.66 ms |       1.33 ms |    -19.80% |         1.33 ms |         1.20 ms |       -9.50% |
| Wide root, metadata |       2.17 ms |       1.50 ms |    -30.78% |         1.47 ms |         1.21 ms |      -17.63% |
| Deep root, ordinary |       1.01 ms |       1.05 ms |     +3.90% |         2.32 ms |         2.28 ms |       -1.79% |

The construction/index benchmark is a check that maintaining counts does not impose a meaningful
indexing penalty. It measures public archive opening, parsing, indexing and destruction; fixture
generation and complete tree/stat validation are outside timing. Its median is slightly faster on
both hosts, with Linux faster in 26 of 28 cases. This is not evidence that indexing itself is
improved; it shows no detected construction regression in this matrix.

## Memory and correctness

Compile-time layout probes on both hosts found `Node` remains 152 bytes: the 32-bit count fits
existing padding. `ArchiveFileSystem` grows from 176 to 184 bytes, an 8-byte per-filesystem cost for
the root count. The directory benchmark executable sizes are unchanged on Mac (1,498,696 bytes);
Linux grows by 952 bytes. The separate construction benchmark is 464 bytes smaller on Mac and
1,136 bytes larger on Linux. These are benchmark executable sizes, not release CLI binary-size
measurements.

The original runtime qualification passed its 17 `@xff_archive` tests on both Mac and Linux,
including directory metadata/range, filesystem, and archive-member behavior. After that timed
qualification, the child-count regression fixture was changed from a host-written tar file to the
in-memory `MakeMemoryTar` / `ArchiveFileSystem::OpenBytes` path. This is test-only: it does not
change the measured production implementation or benchmark source. The final fixture version was
then run uncached with all 25 `@xff_archive` tests on Mac, and with the focused `archive_fs_test`
against the exact O19i2 production source on Linux in release and ASan configurations. The final
test source also passes changed-file pre-commit, including the host-I/O policy. The new construction
benchmark builds and runs on Mac. The full raw evidence retains all benchmark sessions and slower
cases; no unfavorable rows or control drift were subtracted.

## Decision and limits

The result supports adopting the direct-child count as a bounded capacity hint. It delivers large,
repeatable wins for the flat archive workload where repeated vector growth is material, improves
wide-root listings, and has neutral-to-small construction costs with no per-node size increase.
The overall listing median is close to zero, so describe this precisely as a wide archive-directory
listing optimization. Deep roots do not benefit consistently: Mac deep-root metadata at 100k costs
5.36%, and the ordinary listing costs 3.90%. The largest retained listing costs are 7.11% for Mac
chain-root 100 and 4.57% for Linux deep-ten metadata with 10 stored members. Callers still receive
correct behavior and ordinary vector growth remains available if a count saturates.

The published tables were independently recomputed from every original iteration using the
declared fastest-seven protocol. The original archived report remains unchanged as historical
evidence; it used inconsistent summary numbers and displayed independently aggregated times.
This is a report-only correction, with no sample replacement, source change or repeated timing.

The benchmark is an in-memory archive API test, not a full xff command benchmark. It isolates archive
indexing/listing from host filesystem speed and CLI startup; command-level impact depends on how
often and how widely archive directories are listed.
