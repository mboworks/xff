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
