# Uncached native ZIP seeking: evaluation and rejection

The uncached seekable-reader prototype is rejected as a general performance change. It makes
some large compressed workloads faster, but repeatedly slows ordinary first-member workloads.
The strongest native gains are 8.4% on macOS and 40.7% on Linux; the worst native regressions
are 138.1% and 91.7%. The current product reader remains in place.

The manual archive benchmark now covers ZIP names-only listing, first/last member reads and
three-target cycles, with stored/deflate, native-memory, explicitly sequential and nested inputs.
It also retains all existing TAR controls. The rejected implementation, tests, admission policies,
qualifications and every measured round are preserved in the evidence rather than shipped.

## Native whole-process results

Ratios are paired within each of three sessions; the displayed change is the median session ratio
minus one. Every session rotates participant order over nine measured rounds, retaining the fastest
seven after one discarded warmup per participant/case. An identical baseline-copy participant
records ordinary variation. Negative changes mean faster.

| Fixture / requested workers / action | macOS elapsed change | Linux elapsed change |
| :----------------------------------- | -------------------: | -------------------: |
| stored-10000-128 / 1 / first         |             +138.07% |              +90.49% |
| stored-10000-128 / 3 / first         |             +137.67% |              +91.73% |
| stored-10000-128 / 1 / list          |              +66.85% |              +44.06% |
| stored-10000-128 / 1 / last          |              +80.06% |              +54.81% |
| stored-100-4096 / 1 / all-content    |              +79.97% |              +53.74% |
| deflate-10000-4096 / 3 / list        |               -8.38% |              -36.46% |
| deflate-10000-4096 / 1 / last        |               -7.25% |              -40.74% |
| deflate-10000-4096 / 3 / first       |              +30.13% |              -17.73% |
| ordinary / 1 / list                  |               -0.36% |               -0.27% |
| tar-control / 1 / last               |               -0.26% |               -0.11% |
| stored-40-1048576 / 1 / list         |               -0.16% |               -7.52% |

Fixture names encode compression, member count and bytes per member. The stored 40-member / 1 MiB
case exceeds the prototype's conservative index allowance and exercises sequential fallback.

| Host / case                           | Baseline median ms | Prototype median ms | Paired change |
| :------------------------------------ | -----------------: | ------------------: | ------------: |
| macos / stored-10000-128 / 1 / first  |             31.796 |              75.434 |      +138.07% |
| linux / stored-10000-128 / 3 / first  |             12.033 |              23.091 |       +91.73% |
| macos / deflate-10000-4096 / 3 / list |             59.566 |              54.573 |        -8.38% |
| linux / deflate-10000-4096 / 1 / last |             50.813 |              30.084 |       -40.74% |

Times are medians of session means; ratios are paired before taking their median, so a ratio
need not equal the quotient of the two separately rounded time columns.

The native matrix contains 98 cases and 8,820 checked invocations per host, including discarded
warmups (17,640 across both hosts). Independent Python ZIP writers deliberately use data descriptors.
Every archive is read back independently; all process exits and complete path-output bytes are
checked on every invocation. Inputs cover 10/100/1,000/10,000 members, 128/4,096-byte payloads, a
40 MiB stored fallback, first/last/all-content selection, requested workers 1/3, an ordinary tree
and a compressed-TAR control. Linux uses shared-memory fixtures and recorded CPU affinity; macOS
uses local files and makes no affinity claim.

## In-memory reader results

The direct matrix has 150 ZIP and 96 existing TAR cases, two participants and three sessions:
1,476 case/participant/session records per host, each with nine raw repetitions. Google Benchmark
uses real time, in microseconds; fixture creation is outside the timed loop. Names-only listing
validates names/counts; every timed content read checks complete bytes. Full fixture-name validation
also runs before timing.

| Host / direct case                                 | Baseline median us | Prototype median us | Paired ratio |
| :------------------------------------------------- | -----------------: | ------------------: | -----------: |
| macos / `zip/stored/memory/first/10000/128`        |              6.561 |           20866.583 |   3252.49209 |
| macos / `zip/deflate/memory/names-only/10/1048576` |           2548.011 |              45.629 |      0.01799 |
| linux / `zip/stored/memory/first/10000/128`        |              2.853 |            5202.757 |   1825.17537 |
| linux / `zip/deflate/memory/names-only/10/1048576` |          13203.764 |              13.152 |      0.00100 |

A first-member open can stop at the first local header in the sequential reader. The prototype
constructs the complete central-directory index before that same read, even after a separate
listing already built and destroyed its own reader. The direct benchmark makes that cost visible
without whole-program startup and filesystem construction. Conversely, listing large compressed
members can avoid decoding their payloads once central-directory metadata is available.

The names-only workload does not assert that the parent's descriptor sizes are correct. The
`descriptor_size_differences` counter records those mismatches explicitly; the independent exact-size
regression fails on the parent and passes only for admitted native sources. Sequential and
budget-fallback sources retain the existing unknown/zero-size issue. This remains an open correctness
task. No size check was removed from existing TAR benchmarks.

All 96 unrelated TAR cases remain in the raw matrix. Their median paired ranges are about -21.7%
to +14.8% on macOS and -6.6% to +7.7% on Linux. These small-control observations are retained
without assigning every variation to a particular cause. The rejection does not depend on them.

## I/O diagnostic and architectural implication

A separately run, untimed counter diagnostic exercises 10,000 members of 128 bytes using the same
implementation with native seeking enabled or explicitly unsupported. Both hosts produce the
same logical read/seek counts. These are cursor operations, not allocator counts or elapsed timings.

| Stored ZIP operation | Sequential bytes read | Native bytes read | Sequential seek calls | Native seek calls |
| :------------------- | --------------------: | ----------------: | --------------------: | ----------------: |
| Listing              |             1,835,012 |         2,465,844 |                     1 |                13 |
| First-member read    |                65,540 |           696,372 |                     1 |                13 |

Both paths open two source cursors, including the existing four-byte format probe. The pinned
libarchive source consumes adjacent local headers forward; the diagnostic does not show a native
seek per member. A simple last-buffer cache is therefore not a demonstrated solution to the
upfront indexing regression. Immutable index/locator reuse, metadata demand and API support for
reusing parsed directory information need separate design and qualification. No such improvement
is claimed by this rejected candidate.

## Implemented safeguards and qualification scope

The archived prototype adds optional typed `ReadStream::Seek` with memory and read-only owned-host
implementations. Unsupported seeking never consumes data. It preserves XAR's existing `Size`/`OpenAt`
contract: the first trial bypassed source-wrapper errors, and the Apple tests caught that regression.

For ZIP it reserves 16 times the actual owned-cursor source length plus 256 KiB before admitting
libarchive's index, and holds that reservation until the reader is freed. Insufficient budget or
estimate overflow preserves sequential reading; it does not impose an archive-size refusal. This
is deliberately conservative whole-source admission, not exact allocator/RSS accounting. Large
stored payloads can fall back even when their directory itself is small. The source extent bounds
native reads/skips/seeks; changed lengths and invalid cursor returns remain errors.

The implementation audit is pinned to libarchive 3.8.1.bcr.3, with a compiler guard requiring a new
audit for a library-version change. Native ZIP disables Apple resource-fork interpretation so
`__MACOSX` entries remain ordinary members, matching the existing streaming VFS on both hosts.
No native XAR-policy change is retained in the final trial.

Both hosts pass all 314 enabled release targets and the affected ASan suites. Linux additionally
passes affected MSan and TSan suites; FUSE is required in its complete release run. Real canonical
compile-database checks cover the changed source/header consumers and the new manual benchmark.
A final test-only trailing comma fixes the expanded enum's lint finding; its affected release test
and scoped lint pass on both hosts. Guarded source substitutions restore only the experiment's
owned files after each trial.

New tests cover seek arithmetic, beyond-EOF behavior, mixed reads/skips, descriptor identity,
unsupported operations, changing capabilities, malformed positions, read overshoot, overflow and
small-budget fallback, reservation lifetimes, independent readers, long names, 10,000 empty entries,
Zip64, resource-fork paths and exact descriptor sizes/content. Passing these tests is not a claim
that the rejected policy is an acceptable general optimization.

## Footprint and reproduction

| Host  | Baseline full CLI bytes | Prototype full CLI bytes |  Change |
| :---- | ----------------------: | -----------------------: | ------: |
| macOS |               8,186,040 |                8,187,128 |  +1,088 |
| Linux |              18,220,928 |               18,244,000 | +23,072 |

The measurements use parent `653f5ed25b68e84e0b292afa9671289eade3b534` and frozen LLVM 23 release/
ThinLTO configurations, with actual compiler, source, executable and link-action identities. The
later counter diagnostic also runs on `35b810bb806aa169c075c01a449a408cd3758248`; its archive sources
are unchanged. Neither host performs builds, tests, lint, allocation probes or heavy fixture work
concurrently with its release timings.

Run the retained manual ZIP matrix on the current reader with:

```sh
bazel run //tools:archive_source_benchmark --config=clang_release --config=xff_full -- --benchmark_filter='^zip/'
```

The committed fixture helper keeps its borrowed backing storage and byte-count record alive until
its C writer is destroyed, including error returns. Retained timing runs use the frozen successful
fixture paths recorded in their source snapshots; those identities remain unchanged.

The benchmark's sequential and nested controls remain explicit; the memory source exercises
whatever capability the checked-out implementation provides. The archived variants reconstruct
the rejected prototype and its complete qualification/measurement protocol.

[The compressed evidence](native-zip-seek-evaluation-20261103.json.gz) retains all measured rounds,
frozen source variants, original failures, fixes, dry runs, validation logs and I/O diagnostics.
Generated fixtures, executables/objects and compile-database payloads are excluded; reconstruction
sources and identities are retained. O19b index reuse/replay/admission and the existing streamed
descriptor-size issue remain open.

Archive: 5,680,410 bytes, 932 labeled files. SHA-256:
`088df0f6a7ce7c116a46857158f04954302ab47540264261e911446a5bbdffea`.
