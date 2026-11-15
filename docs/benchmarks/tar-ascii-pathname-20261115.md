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

Seven affected suites pass on macOS: both pathname suites, archive reader, archive filesystem,
directory metadata, archive member source and package-format fixtures. Linux builds, regression
tests and timing are deferred while the 130-revision dense
backfill is running on drogon; this change does not modify its frozen drivers or executables.

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
and test invocations are retained locally; Linux verification is still pending.

The [compressed evidence](tar-ascii-pathname-20261115.json.gz) retains build identities,
contract, RAM ownership/release receipts, commands, all warm-up and measured samples, output
hashes and per-cell summaries. Local executables and logs are retained under the isolated
checkout's ignored `bazel-pathname-investigation/` directory. An initial driver setup error
occurred before any timing samples and its RAM disk was successfully detached; its evidence is
kept separately rather than included in the accepted population.
