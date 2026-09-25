# Traversal performance analysis

## Measured compiler and allocator decisions

Experiments used identical xff source within each comparison, broad/deep trees, 10, 100, 1,000
and 10,000 files, and one/four workers. Linux ARM64 runs used tmpfs and enforced CPU affinity;
macOS ARM64 runs used local storage without affinity pinning. Tools ran in rotating order after
one discarded, correctness-checked warmup. Pilots retained the fastest two of three observations;
compiler confirmations at 10,000 files retained the fastest seven of nine. Percentages below are
medians across the tested cases, not guarantees for arbitrary workloads.

- `-O2` with ThinLTO replaced `-Oz`: the confirmed 10,000-file median improved 8.8% on macOS and
  24.4% on Linux ARM64. Linux metadata-light cases improved 21-30%; content cases improved 2-7%.
  The macOS experiment executable grew from 1,966,936 to 2,795,000 bytes with release stripping
  (`strip -S -x`). This deliberately trades about 0.83 MB for execution speed.
- `-O3` offered no additional macOS benefit: its confirmed median improvement was 8.7%, versus
  8.8% for `-O2`, with almost identical executable size. It was not selected.
- Modern Google TCMalloc `0.0.0-20260818-15db23d`, statically linked on Linux ARM64, had median
  slowdowns of 80.7%, 66.7%, 53.5% and 11.8% at the four sizes. It was not selected.
- Mimalloc 3.5.3 was approximately neutral in the macOS static pilot. On Linux ARM64 its medians
  were 8.8%, 9.0% and 4.0% slower at the three smaller sizes, and 2.2% faster at 10,000 files.
  The startup penalty does not justify replacing the default system allocator. Dynamic injection
  was also tried on macOS, but its loader overhead does not represent static integration.

Allocator entry points were verified in the actual executables. Linux mimalloc was built with
hermetic Clang 22 and passed all seven upstream tests. Allocator pilots do not establish behavior
above 10,000 files. Raw JSON includes commands, executable hashes, samples, CPU allocation and
fixture identity; HTML is derived from that data. Measurements exclude build time.

The compiler trials used the traversal implementation from PR 902; the Linux source was synced
through main commit `80d75576c32b364d08a30a5b1f0c5206ecc07d51`. All comparisons retained identical
optimization flags except the variable under study. Inspection of compiler actions confirmed the
last optimization argument takes effect; a transitive allocator dependency is not evidence that
its allocator is linked.

## Metadata and filesystem costs

`LocalFs::ReadDir` already consumes `dirent.d_type`; the walker uses it for known regular-file
and symlink types when metadata is unnecessary. Unknown types, directories needed for loop or
filesystem checks, and followed symlinks retain metadata lookup. `MetadataDemand` distinguishes
never, on-demand and always; the per-entry lazy value has exclusive coordinator ownership and
requires no synchronization.

Linux basic consumers use `stat`/`lstat`; birth-time consumers request basic fields plus birth time
in one `statx` call, with a portable fallback for incomplete/unsupported responses. The Linux
[statx interface](https://man7.org/linux/man-pages/man2/statx.2.html) defines explicit field masks.
Directory entries provide names, inode identifiers and types, not complete sizes/timestamps;
[the getdents interface](https://man7.org/linux/man-pages/man2/getdents.2.html) also requires handling
unknown types. Replacing libc enumeration with raw getdents would not remove required stat calls.

macOS offers [getattrlistbulk](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/bsd/man/man2/getattrlistbulk.2),
which can return selected attributes for many entries per call. It is a candidate for workloads
that require metadata for most files, not a replacement for cheap name-only traversal. It needs
an optional VFS listing-with-metadata capability, a checked decoder for variable records and
fallbacks for unsupported attributes, per-entry errors, followed links, mount points and firmlinks.
Apple documents that bulk attributes describe links themselves and underlying mount points, so
blindly substituting them for the current followed-target metadata would change behavior. This
larger backend change is deferred until a dedicated correctness and syscall-count experiment.

## Further implementation candidates

1. **Directory scheduling allocations (`xff/engine/walk.cc`).** Unprefetched reads currently pass
   through a packaged task/future even when immediately waited on. A future vector also reserves
   a slot for every child when no parallel reads are scheduled. A local prototype bypasses both;
   initial Linux timing changes are small and noisy (about 0.8% faster median at 10,000 files).
   The fastest-7/9 confirmation improved only 0.2% overall, within timing noise, so it was not promoted.
2. **Content buffer initialization (`xff/vfs/local_fs.cc`).** Every `ReadContent` zero-initializes
   a 64 KiB stack buffer, then consumes only the byte count returned by `read`. At 10,000 small
   files this requests roughly 625 MiB of unnecessary initialization. The fastest-7/9 Linux confirmation improved content cases by 3.7-10.5%. The change is
   selected, with empty/full/partial-chunk tests and sanitizer validation of the returned-byte boundary.
3. **Metadata demand for presentation (`xff/engine/run.cc`, `xff/presentation/fields`).** Any
   compiled output template currently selects full metadata. Path-only templates could declare
   a field-level demand just as birth time does. This needs a complete field dependency inventory,
   including composed templates and extensions, plus exact stat-count tests; guessing from flag
   names would be incorrect. Retain the conservative default for unknown/custom consumers.
4. **Content reuse and bounded reads (`xff/engine/evaluate.cc`).** Multiple predicates can each
   call `ReadContent` for the same entry. An entry-owned content cache could avoid repeated reads
   without locks. Binary sniffing could use a prefix read, but unreadable trailing data and strict
   text flavors must preserve documented behavior. Streaming regex matching also needs boundary,
   multiline and capture semantics. These require dedicated fixtures beyond the small-file pilot.
5. **Parallel content scheduling (`xff/engine/parallel_match.cc`).** Batches of 256 and chunks of
   16 bound memory and preserve output order. The coordinator waits for a batch before continuing
   traversal; a two-buffer pipeline might overlap those phases. It must preserve quit/prune,
   error ordering, cancellation and stateful actions, so it is deferred until profiles show the
   wait is a meaningful fraction of time. Cheap name/type predicates should stay serial.
6. **Path ownership and conversion (`xff/vfs/local_fs.cc`, `xff/engine/walk.cc`).** Each listing
   stores both full paths and names, then converts entries to walker records. Moving strings
   avoids a second allocation, but the two listing vectors coexist. An internal visitor or a
   shared value record could reduce peak memory. Borrowing names requires stable storage and
   must not introduce dangling views in prefetched or archived entries.
7. **Safety and output.** Safety policy is resolved before traversal; mutation checks must stay
   at the operation boundary. Neither checks nor VFS routing may be bypassed for speed. Profile
   pure filtering separately from output formatting and safety-enabled runs before changing
   policy representation or buffering emitted records.

## ISA policy

The inspected project release configuration has no explicit `-march`, `-mcpu` or `-mtune`.
The available experiment hosts are ARM64; emulated x86 timing would not establish native x86
benefit. Keep the portable x86 baseline until a native x86-64 experiment measures generic versus
v3 with identical source, allocator and fixtures. Record CPU features and effective compiler
commands. v4 is not a portable default; separate generic/v3/v4 distributions remain deferred.

## Measurement overhead

Fixture identity is now computed once per fixture, rather than repeatedly for every task.
Post-merge comparison work is split across three complete-fixture shards per platform, preserving
all tools and repetitions on one host per cell. Shards share the same compiled binary and retain
provenance; mismatched or incomplete reports fail aggregation. These changes shorten orchestration
without changing the measured child processes or relaxing correctness checks.

## PCRE2 JIT/SIMD verification and RE2 comparison

Source baseline: `cb882b639a` (PR #910). The baseline investigation below describes that revision.
The JIT follow-up at the end records the implementation and measurements that supersede its
capability findings; release compiler options and the default RE2 grammar remain unchanged.

### Verified capability and dispatch

- The pinned PCRE2 10.48 Bazel package copies `config.h.generic`, leaves `SUPPORT_JIT` undefined,
  and does not add that define in its `LOCAL_DEFINES`. Building and running its own
  `@@pcre2+//:pcre2test -C` reports **No just-in-time compiler support**. Merely compiling
  `pcre2_jit_compile.c`, or publishing the SLJIT license, does not establish usable JIT.
- Independently, `extra_modules/pcre2/pcre2_backend.cc` never calls `pcre2_jit_compile`.
  `PartialMatch`, `FindFirst`, captures and substitution therefore use interpreted PCRE2 today.
- `FullMatch` and `FullMatchCaptures` pass `PCRE2_ANCHORED | PCRE2_ENDANCHORED` at match time.
  Those options force interpreter fallback even for a JIT-compiled pattern. Supporting JIT for
  these operations needs a semantics-preserving anchored compilation strategy, not merely a
  new call after compilation. [PCRE2 JIT documentation](https://pcre2project.github.io/pcre2/doc/pcre2jit/).
- PCRE2's explicit SIMD fast-forward implementation is inside its JIT path. The pinned source
  includes ARM64 NEON and x86 vector implementations; its AVX2 fast-forward path is explicitly
  disabled in `pcre2_jit_simd_inc.h`. None of that JIT-generated scanning is active in the current
  xff build. This does **not** claim that compiler-generated code or libc uses no SIMD.
- RE2 is pinned to `2025-11-05.bcr.1`. Its source has an AVX2 prefix-search path guarded by
  `__AVX2__`; this ARM64 measurement does not establish native x86 behavior. JIT and SIMD are
  separate capabilities, and a library's build support does not prove a particular match used it.
- This Mac's external ripgrep is 15.2.0, reports compile/runtime NEON and PCRE2 10.45 with JIT
  available. Its version and execution model differ from xff, so its times are a tool comparison,
  not an isolated measurement of JIT's benefit. Its availability report alone also does not prove
  per-match JIT dispatch.

### Reproducible measurement contract

`//tools:benchmark_regex` compares the same xff full binary with `--regextype=RE2` and
`--regextype=PCRE2`, plus `rg --pcre2`. It uses an ASCII log-record pattern with alternation,
character classes, bounded repetition, literal punctuation and an optional field. Each 16 KiB
file contains repeated near misses; the mixed population has one late match in every third file,
while the absent population has no matches. The pattern cannot match across lines, so xff's
whole-content search and rg's line-based search have equivalent accepted file sets here.
An independent Python regex oracle tests the fixture labels. Every measured command, including
warmups, must return exactly the expected NUL-delimited path multiset and a valid exit status.

The matrix uses 10, 100, 1,000 and 10,000 files with one/four requested workers, a discarded warmup,
rotating participant order, and the mean of the fastest seven of nine samples. macOS worker counts
are **not** CPU affinity limits. Storage is an explicitly labeled host filesystem with warm-cache
reuse, not a claimed in-memory filesystem. Raw JSON records commands, binary hashes, tool versions,
fixture hashes, sample times, CPU accounting, RSS and the measurement contract. Compilation and
other benchmark runs are not intentionally run alongside the final measurements.

`//xff/matching/regex:regex_benchmark` separately exercises the production Matcher backends on
in-memory subjects. It times 100 pattern compilations independently from 1,000 matches against two
reused 16 KiB subjects (half matching), checks every result, rotates engine order over ten rounds,
and outputs raw CSV. Round zero is warmup. This isolates repeated matching from filesystem and
process startup, but does not isolate allocator costs inside each backend: PCRE2 currently
allocates/frees match data on every call. It is single-threaded and is not a traversal benchmark.
Timing is informational; neither benchmark adds a CI performance gate or automatic workflow run.

```sh
bazel build --config=clang_release --config=xff_full //xff/cli:xff_full
python3.13 tools/benchmark_regex.py --binary "$PWD/bazel-bin/xff/cli/xff_full" \
  --build-label 'SOURCE_REV; clang_release; xff_full' --output /tmp/regex-macos-arm64.json
bazel run --config=clang_release //xff/matching/regex:regex_benchmark \
  > /tmp/regex-inmemory-macos-arm64.csv
```

The same harness supports Linux affinity and `--fixture-parent=/dev/shm`; verify the JSON storage
identity says `tmpfs`. Linux was not measured in this investigation: the available local container
service has no configured kernel. Native Linux/x86 results remain required before generalizing.

### macOS ARM64 results (2026-09-24)

Host: Apple M5 Pro, 64 GiB RAM. Release configuration: `--config=clang_release --config=xff_full`, `-O2` with ThinLTO.
The in-memory target uses `--config=clang_release` and links the same PCRE2 backend directly.
Raw [end-to-end samples](measurements/regex-macos-arm64.json),
[in-memory samples](measurements/regex-inmemory-macos-arm64.csv), and
[PCRE2 capability output](measurements/pcre2-config-macos-arm64.txt) are retained here.
The following compile and matching means independently retain the fastest seven post-warmup rounds.

| Engine | Compile per pattern (us) | Match 1,000 subjects (ms) |
| :----- | -----------------------: | ------------------------: |
| RE2    |                    28.03 |                     28.58 |
| PCRE2  |                     2.69 |                     38.04 |

RE2 saves about 25% of repeated matching time in this bounded near-miss workload; interpreted
PCRE2 compiles substantially faster. These are production-backend costs, including their different
allocation behavior, not guarantees for every regular expression or input distribution.

| Files | Workers | Population | xff RE2 ms | xff PCRE2 ms | rg PCRE2 ms | PCRE2 / RE2 |
| ----: | ------: | :--------- | ---------: | -----------: | ----------: | ----------: |
|    10 |       1 | mixed      |      14.20 |        12.48 |        8.21 |        0.88 |
|    10 |       4 | mixed      |      21.28 |        23.06 |       18.34 |        1.08 |
|    10 |       1 | absent     |      30.08 |        25.66 |       13.30 |        0.85 |
|    10 |       4 | absent     |      28.88 |        31.15 |       21.87 |        1.08 |
|   100 |       1 | mixed      |      23.40 |        25.47 |       17.34 |        1.09 |
|   100 |       4 | mixed      |      14.16 |        14.99 |        9.48 |        1.06 |
|   100 |       1 | absent     |      14.00 |        14.86 |        8.09 |        1.06 |
|   100 |       4 | absent     |      14.09 |        15.14 |        9.40 |        1.07 |
|  1000 |       1 | mixed      |      55.26 |        64.28 |       37.19 |        1.16 |
|  1000 |       4 | mixed      |      55.23 |        64.27 |       21.53 |        1.16 |
|  1000 |       1 | absent     |      54.38 |        64.10 |       32.23 |        1.18 |
|  1000 |       4 | absent     |      54.26 |        64.51 |       18.87 |        1.19 |
| 10000 |       1 | mixed      |     840.95 |      1045.92 |      654.74 |        1.24 |
| 10000 |       4 | mixed      |    1269.15 |      1649.13 |      451.18 |        1.30 |
| 10000 |       1 | absent     |    1177.64 |      1488.68 |      876.81 |        1.26 |
| 10000 |       4 | absent     |     495.79 |       575.45 |      122.91 |        1.16 |

PCRE2 / RE2 above is elapsed time, so values above one favor RE2. The smallest file counts are
startup-heavy; increasing requested workers does not guarantee more active matcher threads or
better throughput. The external rg result includes its different read/match scheduling and PCRE2
version. It cannot be attributed to JIT alone. This matrix covers a flat file tree and one complex
pattern with two result populations; it does not replace broad/deep or multi-pattern coverage.

A separate [confirmation run](measurements/regex-confirm-macos-arm64.json) repeated the larger
populations. Both runs are retained because absolute times varied materially on this shared host;
these are informational samples rather than confidence-bounded throughput claims. Confirmation:

| Files | Workers | Population | xff RE2 ms | xff PCRE2 ms | rg PCRE2 ms | PCRE2 / RE2 |
| ----: | ------: | :--------- | ---------: | -----------: | ----------: | ----------: |
|  1000 |       1 | mixed      |      54.01 |        63.90 |       36.80 |        1.18 |
|  1000 |       4 | mixed      |      54.82 |        64.86 |       21.78 |        1.18 |
|  1000 |       1 | absent     |      54.32 |        64.06 |       31.92 |        1.18 |
|  1000 |       4 | absent     |      54.43 |        64.08 |       18.88 |        1.18 |
| 10000 |       1 | mixed      |     478.03 |       577.43 |      340.18 |        1.21 |
| 10000 |       4 | mixed      |     477.21 |       574.71 |      151.38 |        1.20 |
| 10000 |       1 | absent     |     478.23 |       577.13 |      292.86 |        1.21 |
| 10000 |       4 | absent     |     476.06 |       572.97 |      124.42 |        1.20 |

### Follow-up implementation criteria

1. Enable JIT in the dependency build for supported targets, then explicitly compile eligible
   patterns. Verify capability, nonzero `PCRE2_INFO_JITSIZE`, and an actual match-time JIT callback;
   retain interpreter fallback when executable memory or pattern support is unavailable.
2. Measure startup/compile overhead and repeated matching before choosing eager versus lazy JIT.
   Do not introduce locks into the shared matching hot path. Preserve concurrent matching and
   bounded per-thread execution state.
3. Preserve resource protections: JIT does not enforce the interpreter depth limit, and its match
   limit accounting differs. Test bounded JIT-stack exhaustion, match limits, fallback and error
   behavior explicitly before enabling it in production.
4. Cover full matches, captures and rewrites as well as unanchored content matches. Treat
   match-data allocation reuse as a separate measured optimization, not part of a claimed JIT gain.

### JIT implementation follow-up

The PCRE2 extension enables upstream JIT for ARM64 and x86-64. Other architectures retain the
interpreter. SLJIT is bundled in the pinned PCRE2 source; no additional module or shared library
is introduced. The 33 SLJIT C/header files used by the build carry the BSD-2-Clause notice, already
included in xff's full notices. PCRE2 remains BSD-3-Clause WITH PCRE2-exception. Neither GPL nor
LGPL code is added. The ARM64 JIT includes upstream NEON scanning; x86 uses upstream SIMD support,
without enabling its deliberately disabled AVX2 path or changing the portable compiler target.

Eligible patterns compile JIT code eagerly. Full matching uses a second pattern compiled with
both anchors, because match-time anchoring bypasses JIT. This preserves alternatives, directives,
and capture numbering without textual pattern rewriting. The compiled patterns and context are
immutable during matching; match data is invocation-local and the default 32 KiB JIT stack is
thread-local machine-stack storage. No matching lock is added.

Unsupported patterns, unsupported targets, and executable-memory allocation failures retain the
interpreter. Explicit `(*LIMIT_DEPTH=...)` or `(*LIMIT_HEAP=...)` patterns stay interpreted because
JIT does not enforce those limits. Both engines retain the configured match-work limit, though
PCRE2 accounts work differently in JIT and interpreter execution. JIT stack exhaustion retries
with the bounded interpreter, including substitutions. Other match errors retain existing
no-match/unchanged-replacement behavior. RE2 remains the default and offers its existing
linear-time guarantee; JIT does not make backtracking linear-time.

Tests observe PCRE2's match-time JIT callback for partial matching, full matching, spans, captures,
and rewriting. They also exercise `(*NO_JIT)`, explicit interpreter limits, match limits,
backtracking alternatives, a large backtracking stack, and concurrent use of one backend.
Production registration installs no callback. Direct LLVM coverage reports 164/164 backend lines
and 55/58 branches covered. Local LLVM coverage confirms stack-limit retries
for matching and rewriting. Coverage builds use atomic profile counters in this backend so the
concurrent test does not corrupt counts. PCRE2 already carries MSan annotations; span lookup
allocates the full capture vector to avoid its unannotated `rc == 0` case, and capture extraction
does not read trailing nonparticipating offsets beyond the reported capture count.

#### macOS ARM64 JIT measurements

Measured on the same Apple M5 Pro / 64 GiB host with `clang_release` (O2 + ThinLTO), against
`aebd53b799` plus this JIT implementation. Each result is the fastest seven of nine samples after
one discarded warmup. Binary identity, fixture hashes, commands, and raw samples are retained in
[the end-to-end report](measurements/pcre2-jit-macos-arm64.json). These are warm host-filesystem
measurements, not verified tmpfs; workers on macOS are requested concurrency, not CPU affinity.

| Engine            | Compile per pattern (us) | Match 1,000 subjects (ms) |
| :---------------- | -----------------------: | ------------------------: |
| PCRE2 interpreter |                     2.43 |                     38.30 |
| PCRE2 JIT         |                    14.37 |                      5.25 |
| RE2 control       |                    26.84 |                     28.74 |

The JIT compile cost includes both search and anchored patterns. The in-memory fixture is one
complex pattern over 1,000 reused 16 KiB subjects, half late matches and half near misses.
[Interpreter raw samples](measurements/pcre2-jit-baseline-inmemory-macos-arm64.csv) and
[JIT/control raw samples](measurements/pcre2-jit-inmemory-macos-arm64.csv) retain all rounds.

| Files | Workers | Population | xff RE2 ms | xff PCRE2 ms | rg PCRE2 ms | PCRE2 / RE2 |
| ----: | ------: | :--------- | ---------: | -----------: | ----------: | ----------: |
|    10 |       1 | mixed      |      10.02 |         9.73 |        5.86 |        0.97 |
|    10 |       4 | mixed      |      10.03 |         9.66 |        6.71 |        0.96 |
|    10 |       1 | absent     |      10.01 |         9.72 |        5.71 |        0.97 |
|    10 |       4 | absent     |      10.05 |         9.74 |        6.88 |        0.97 |
|   100 |       1 | mixed      |      14.31 |        11.84 |        8.77 |        0.83 |
|   100 |       4 | mixed      |      14.12 |        11.82 |        9.11 |        0.84 |
|   100 |       1 | absent     |      14.17 |        11.82 |        8.22 |        0.83 |
|   100 |       4 | absent     |      14.16 |        11.81 |        8.78 |        0.83 |
|  1000 |       1 | mixed      |      54.67 |        31.00 |       37.17 |        0.57 |
|  1000 |       4 | mixed      |      54.70 |        31.06 |       21.02 |        0.57 |
|  1000 |       1 | absent     |      54.71 |        31.02 |       32.18 |        0.57 |
|  1000 |       4 | absent     |      54.64 |        30.83 |       18.74 |        0.56 |
| 10000 |       1 | mixed      |     482.34 |       242.55 |      342.48 |        0.50 |
| 10000 |       4 | mixed      |     480.14 |       243.70 |      151.40 |        0.51 |
| 10000 |       1 | absent     |     482.60 |       243.85 |      294.26 |        0.51 |
| 10000 |       4 | absent     |     484.20 |       244.74 |      124.08 |        0.51 |

At 10,000 files JIT roughly halves xff's elapsed time compared with RE2 for this pattern. It does
not fix traversal/scheduling scaling: four requested workers provide little benefit here, while
ripgrep benefits substantially. These results do not justify changing the default grammar or
claiming PCRE2 is faster for every pattern.

The stripped full macOS ARM64 binary grows from 6,251,328 to 6,648,288 bytes: **396,960 bytes
(6.35%)**. Both builds use `strip -S -x`, the same compiler, optimization, and extension selection.
Their dynamic-library dependency sets are identical. The final release build, including generated
help and capture-handling refinements, retains this size. Linux/x86 performance and size remain
to be measured independently.

## MBO segmented collection storage and arenas

The Git module override pins MBO `ac4c11113de868b27226c0fe74028f818dc6b4b8`, the head evaluated
for this change. Both new APIs work without upstream changes. `SegmentedSequence` supplies stable
addresses, random-access iterators and explicit empty-directory reservation. `Arena` supplies raw
aligned bytes and bulk lifetime, with no internal locks. Neither supplies contiguous element spans;
`Arena` does not construct or destroy arbitrary C++ objects.

The first adoption is `Collections`, the population retained by `-collect` for later summaries,
histograms and shard grouping. An unknown-size vector previously relocated complete records while
growing; each record owned three strings. Records now occupy 64-element segments, with zero initial
directory reservation, and their text views refer to a collection-owned byte arena. The arena grows
from 4 KiB to 64 KiB normal blocks. These are initial measured policies, not universal optimums.
Many small named collections can waste tail slots; ordinary runs without collection allocate none
of these blocks. Metadata and filesystem owners remain real, destructed C++ members. Arena text is
released only with its owning collection, and moves preserve references. Consumers may not retain
these views after the collection dies. Row/byte budgets continue to count the same logical text;
they are not physical allocator-memory caps. VFS routing and safety enforcement are unchanged.

### In-memory storage measurements

The committed `//xff/engine:collect_benchmark` constructs paths outside timing, then measures
collection append, full iteration and destruction. Cases use 10 through 100,000 records, root
lengths 1 and 128, and one/four independent benchmark threads. This excludes filesystem I/O and
engine traversal. Four threads do not represent four CPUs assigned to a single XFF walk; there is
no affinity on this Mac. Compiler configuration is identical `clang_release` (O2 + ThinLTO), including
the MBO pin, for the vector baseline, sequence-only variant and sequence-plus-arena variant.

One discarded warmup precedes nine rounds with rotating variant order. Each invocation uses
`--benchmark_min_time=0.02s`; the table averages the fastest seven CPU-time observations per case.
The unchanged vector baseline uses the parent collection implementation. The sequence-only variant
changes collection storage to `SegmentedSequence<CollectedEntry>` with the same segment options,
retaining owned strings. The final variant is the implementation in this PR.
[Raw samples, hashes, configuration and memory observations](measurements/collection-storage-macos-arm64.json)
retain the full measurements. These are local Apple M5 Pro/macOS results, not cross-platform or
whole-command speedup claims. Linux confirmation and traversal-inclusive measurements remain open.

| Entries | Root bytes | Threads | Vector us | Sequence us | Sequence + arena us | CPU time reduction |
| ------: | ---------: | ------: | --------: | ----------: | ------------------: | -----------------: |
|      10 |          1 |       1 |      0.51 |        0.29 |                0.36 |              30.0% |
|      10 |          1 |       4 |      0.52 |        0.30 |                0.36 |              30.1% |
|     100 |          1 |       1 |      3.58 |        2.61 |                2.96 |              17.2% |
|     100 |          1 |       4 |      3.60 |        2.64 |                2.99 |              17.1% |
|   1,000 |          1 |       1 |     37.14 |       29.30 |               32.61 |              12.2% |
|   1,000 |          1 |       4 |     36.73 |       29.48 |               32.59 |              11.3% |
|  10,000 |          1 |       1 |    745.34 |      310.64 |              322.57 |              56.7% |
|  10,000 |          1 |       4 |    922.45 |      309.06 |              316.34 |              65.7% |
| 100,000 |          1 |       1 |   5325.74 |     4669.87 |             5049.80 |               5.2% |
| 100,000 |          1 |       4 |   9735.54 |     6666.86 |             6916.11 |              29.0% |
|      10 |        128 |       1 |      0.86 |        0.62 |                0.41 |              52.6% |
|      10 |        128 |       4 |      1.04 |        0.61 |                0.40 |              61.3% |
|     100 |        128 |       1 |      8.65 |        7.30 |                3.47 |              59.9% |
|     100 |        128 |       4 |     12.27 |       11.61 |                3.46 |              71.8% |
|   1,000 |        128 |       1 |    128.69 |       88.48 |               43.39 |              66.3% |
|   1,000 |        128 |       4 |    183.60 |      150.23 |               44.74 |              75.6% |
|  10,000 |        128 |       1 |   1413.85 |      924.07 |              465.05 |              67.1% |
|  10,000 |        128 |       4 |   2619.48 |     1783.85 |              520.21 |              80.1% |
| 100,000 |        128 |       1 |  17202.86 |    13451.64 |             7858.08 |              54.3% |
| 100,000 |        128 |       4 |  29769.00 |    26798.29 |            11813.75 |              60.3% |

Sequence-only wins every measured case. Arena text improves long-path cases substantially but is
slower than sequence-only for short paths, where owned strings often fit their inline buffers.
The combined implementation still improves every measured case versus the existing vector baseline.
One-shot peak RSS for 100,000 long-root entries was 120,471,552 bytes (vector), 91,373,568 (sequence),
and 86,933,504 (sequence plus arena). A repeated 0.05-second campaign instead peaked at 174,047,232,
121,290,752 and 185,761,792 bytes, respectively. These process-wide samples include fixtures and
allocator retention; arenas are not a promise of lower long-running RSS, despite lower one-shot
peak memory here. Neither campaign identifies a leak. An embedded application repeatedly creating
collections needs its own lifetime/retention measurement.

Run the benchmark with:

```sh
bazel run --config=clang_release //xff/engine:collect_benchmark -- \
  --benchmark_min_time=0.02s --benchmark_repetitions=9 --benchmark_out=collection-storage.json
```

For inter-variant comparisons, rotate separate executable invocations as in the recorded experiment;
a single executable's consecutive repetitions do not provide that interleaving.

### Other candidates and API boundaries

| Site                                      | Assessment                                                                                              | Next step                                                                                                     |
| ----------------------------------------- | ------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------- |
| Deferred `-top` / shard-status candidates | Large records appended without known count; sequence avoids relocating maps and owned state             | Benchmark whole result-set rounds before adoption                                                             |
| `LocalFs::ReadDir`                        | Unknown count, but the public VFS API currently returns a vector                                        | Measure a shared listing abstraction; do not materialize a sequence only to copy it into a vector             |
| Walker `Stated` children                  | Exact size is known and already reserved; indexed and sorted traversal benefits from contiguous storage | Keep vector until traversal-level measurements justify a change                                               |
| Parallel matcher batches                  | Bounded 256-entry batches with indexed worker access and publication boundaries                         | Test buffer reuse first; a shared arena must not mutate while workers read it                                 |
| Ranked output                             | Appends then sorts; stable addresses are not required                                                   | Measure sorting/cache costs as well as append time                                                            |
| Parser AST                                | Nontrivial nodes currently have individual ownership/destruction                                        | Raw Arena is not an object-lifetime layer; design explicit destruction/ownership before replacing allocations |
| Collection text                           | Raw bytes have collection lifetime and are copied once                                                  | Implemented through a narrow adapter to MBO's byte API; no upstream API blocker                               |

No blanket vector replacement is warranted. A known size plus `reserve`, range construction, sorting,
contiguous spans, or short-lived small batches can favor vector. Segmented storage is most promising
where unknown-size append growth currently moves large records or invalidates references.
