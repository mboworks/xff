# Avoid wide pathname conversion for ASCII TAR entries

## Change

The pinned libarchive TAR reader converts regular members' full pathnames to wide characters
to check for a trailing slash. That slash means a directory in old or malformed TAR headers,
even when the stored type is regular. The conversion is eager for every regular member.
The [upstream implementation](https://github.com/libarchive/libarchive/blob/v3.8.1/libarchive/archive_read_support_format_tar.c)
shows the check in `archive_read_format_tar_read_header`.

The local dependency patch first checks an ASCII byte pathname directly. It excludes high-bit
bytes and ESC, which may introduce a stateful encoding. Non-ASCII and escape-bearing names keep
the existing wide-character conversion and narrow fallback. Directory type, remaining bytes and
padding are updated exactly as before. A later request for the wide pathname can still perform
that conversion lazily.

The patch applies only to the optional archive dependency. The production XFF archive reader,
expression evaluator, traversal and output implementation are unchanged. No format or charset
support is disabled. This is independent of the dense benchmark history change in PR #1153.

## Correctness

The new tests construct USTAR headers in memory so an archive writer cannot normalize away
the deliberately inconsistent regular-file/trailing-slash cases. They cover ordinary ASCII,
spaces, tabs/newlines, a full 100-byte name field, explicit directories, trailing-slash
regular headers, escape-bearing names, UTF-8 and invalid multibyte names. Unicode checks cover
both C and UTF-8 locales and restore the prior locale after each test. No host file I/O is used.

Seven affected suites pass on both macOS and Linux: both pathname suites, archive reader, archive
filesystem, directory metadata, archive member source and package-format fixtures. Linux
qualification ran after the 130-revision dense campaign finished, in separate checkouts and
new owned tmpfs fixtures. Its original drivers, fixtures and executables were preserved.

## Production comparison

The baseline is current main `42d87175e0e8261493f3bfcf1f9b82c5626edc9f`.
Both executables use `//xff/cli:xff_full --config=clang_release --config=xff_full` with LLVM
23.1.5 and ThinLTO. The configured binary is resolved by `cquery` and copied immediately after
each corresponding build, before changing configuration. Both frozen executables are 8,322,568
bytes. Their SHA-256 values are retained with the raw evidence.

Fixture preparation, builds and correctness tests finish before timing. A disposable 2 GiB
case-sensitive HFS+ RAM disk holds deterministic TAR fixtures. The device, RAM backing image and
volume UUID are checked before use and release; only the owned disk is detached. Worker counts
1/3 are requests on macOS, with no CPU-affinity claim.

Each cell rotates candidate, baseline, an identical baseline control and GNU tar. One warm-up
is discarded; 27 measured rounds retain the fastest 21 for wall-time estimates. Every output
is checked against the independently generated exact record multiset. Wall time includes the
native accounting launch boundary; CPU and first-output metrics are retained separately.
The experiment is separate from benchmark viewer history and does not append samples to it.

The initial population covers 100/1,000/10,000/100,000 members, broad/deep layouts and both
worker counts: 16 cells and 1,792 checked invocations. The larger-case repetition measures
10,000/100,000 members in a separate population. No sample pooling is used between phases.

Initial wall-time reductions range from 3.50% to 8.01% at 10,000 members and 4.02% to 11.25%
at 100,000 members. Identical-baseline controls vary from -1.81% to +1.54% across the complete
initial population. The 100-member changes overlap that variation and are not claimed as a
demonstrated speedup. Results describe this macOS host; the Linux profile that motivated the
change does not establish the speedup there.

The independent eight-cell repetition also passes every output check: another 896 invocations,
for 2,688 total. Its wall-time reductions range from 2.36% to 7.26% at 10,000 members and 5.86%
to 10.19% at 100,000 members. Deep 100,000-member cases repeat at about 10% faster on both worker
counts. One repeated broad 100,000-member/three-worker control differs by -4.44%; its individual
6.53% candidate reduction must therefore be interpreted with that observed variation. The other
repeated controls differ by -0.37% to +0.54%. No samples or unfavorable controls are removed.

Both RAM disks were detached successfully after fixture, binary, reference-tool and accounting
launcher rechecks. Changed-file pre-commit policy and formatting checks pass. Production build
and test invocations are retained with the qualification evidence.

The [compressed evidence](tar-ascii-pathname-20261115.json.gz) retains build identities,
contract, RAM ownership/release receipts, commands, all warm-up and measured samples, output
hashes and per-cell summaries. Local executables and logs are retained under the isolated
checkout's ignored `bazel-pathname-investigation/` directory. An initial driver setup error
occurred before any timing samples and its RAM disk was successfully detached; its evidence is
kept separately rather than included in the accepted population.

## Linux qualification after PR #1153

The Linux baseline is merged main `40aa47615dbcdccd94ea3941b6893ab858132d4e`; the
rebased candidate is `4ce229dca5e706b1651d76c1259b66b6155e7706`. No production source,
dependency or build-configuration changes separate the original macOS baseline from this
baseline: #1153 changed the benchmark tooling and viewer. These Linux executables were
built independently and frozen immediately from their respective configured paths.
Baseline is 18,578,472 bytes, SHA-256
`158de92534269d4994a02bd654a6530a87cf727d757b717dd655976637eddd0b`;
candidate is 18,578,584 bytes, SHA-256
`2398d392e3a79119ce8ad9acc7cf7db97e4c2a0c1c692b719233865b1ac62edd`.
Their manifests, full production ThinLTO link actions and build logs are retained.

Both independent Linux populations use all 16 cells (100/1,000/10,000/100,000 members,
broad/deep layouts, 1/3 workers), rotating candidate, baseline, identical baseline control
and GNU tar. Each discards one warm-up and retains the fastest 21 of 27 measured rounds.
All 3,584 invocations pass the independent exact-output oracle. Three distinct allowed
CPU topology cores are selected; each participant inherits the requested 1/3-core mask.
Cell order uses seed 20261115 initially and 20261116 for the repetition. Builds, tests and
Bazel shutdown finish before either timed population. Each uses a fresh owned directory
on the verified `/run/user/1000` tmpfs; the directory is removed after integrity checks.
No disk-backed fixture or emulated filesystem is measured.

| Members | Initial wall-time reduction | Repeated wall-time reduction |
| ------: | --------------------------: | ---------------------------: |
|     100 |            -5.56% to -2.64% |             -3.98% to -2.94% |
|   1,000 |             3.48% to 10.43% |               2.76% to 9.74% |
|  10,000 |            15.47% to 26.17% |             15.68% to 26.22% |
| 100,000 |            18.35% to 27.10% |             17.00% to 27.19% |

Negative reductions are regressions. The 100-member Linux slowdown repeats; it is not
removed as noise or hidden by the large cases. Identical-baseline controls range from
-0.37% to +2.91% initially and -0.23% to +1.83% on repetition (same reduction convention).
This is a tradeoff: reduced decoding cost for larger archives, with a small-archive Linux
cost. The mechanism alone does not establish why the small case regresses.

[All Linux raw evidence](tar-ascii-pathname-linux-20261116.json.gz) includes both populations,
commands, warm-ups, per-round timings, fixture manifests, executable/tool/launcher rechecks,
seven-suite correctness output, exact build/link identities and cleanup receipts. Its
SHA-256 is `221eb67e56b9295e50ce42be152ff8d6f8a171341d729933b300bdd99af28c74`.
The original Linux binaries and drivers remain on drogon at
`/home/marcus/Documents/xff-performance-follow-up-20261116/linux/`.
