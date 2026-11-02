# Cursor-owned comparison read buffers (2026-11-02)

## Decision

Reuse one 64 KiB buffer per native comparison cursor instead of allocating and clearing an owned string for every chunk. Keep the buffer inside its existing source-budget reservation and destroy it before releasing that reservation. Clear once when allocating the cursor buffer; subsequent reads overwrite only the requested range. Native measurements favor this policy over an entirely uninitialized allocation on Linux. Neither policy relies on precleared bytes for correctness: compare only the initialized byte count returned by the read.

Add an optional span-based `ReadStream::ReadInto` capability, implemented only by the existing read-only host adapter. The default returns Unimplemented without consuming input, allocating or copying. `HostStream::Read` remains unchanged for other consumers. Memory, archive and decoder streams retain their owning-string path. Select that legacy comparison loop once from the left cursor capability; a mixed legacy-left/native-right pair conservatively uses owned reads for both sides. A native-left/legacy-right pair keeps the legacy bytes only until that comparison step completes. No archive admission or parallelism guard changes.

The initial shared native/legacy loop is rejected: an unchanged memory-source early-difference control slowed about 29% on macOS. The refined implementation instantiates separate loops at compile time so the legacy path uses local owning chunks as before. Full original and refined source, measurements and slower controls remain below. Do not infer a proven allocator or compiler mechanism from those timing differences.

## Measurement contract

Parent `de31e680114aad8d91977c7c05d9bdd0cd4382dd` includes merged #1044/#1045 and the synchronized #1046 range-read change. Both phases compare independently built LLVM 23 release + ThinLTO binaries against that exact parent. Source and executable hashes are in each manifest; the refined selected sources match the retained implementation. No sanitizer or allocation instrumentation enters the timing executables.

Native fixtures use deterministic nonuniform ASCII bytes with identical, first-byte-different, last-byte-different and different-size cases. Initial qualification measures 1,000 files of 64 KiB and 100 files of 1 MiB at 1/3/10 workers. Refined qualification also adds ten 1 MiB files at those worker counts. The 64 KiB range path and size-only mismatches do not use the new cursor buffer and remain controls.

Each native case has three sessions, a discarded invocation per variant/session, nine interleaved repetitions and fastest seven retained. Cases and variants use reproducible shuffled order. Every invocation validates every comparison status/path record against an independent exact oracle. Initial plus refined qualification totals 10,800 fresh invocations across both hosts, including discarded warmups. Compare paired session ratios, not absolute times across phases or hosts.

macOS uses the local M5 Pro, warm APFS and worker allowances without CPU affinity. Linux drogon uses Ryzen 9950X KVM, tmpfs fixtures and child affinity to the first 1/3/10 available virtual CPUs. Builds and diagnostics finish before timing on each host. Commands use `--compare=status --compare-select=all --jobs=N --exact --sort=dir --no-config --no-pager --timezone=UTC LEFT RIGHT -type f`.

The memory-source fallback control is an independent Google Benchmark release executable: 100/1,000 files, 1/4 workers, small identical and large identical/early-different comparisons. Three variant sessions, three repetitions, 0.02-second warmup and 0.03-second minimum kernel time retain full samples and context. Linux pins each process to its requested kernel worker allowance. These controls deliberately retain their existing 1/4 contract rather than pretend they are native 1/3/10 measurements.

## Refined native results: 100 files of 1 MiB

Negative change means faster. Values are median session means; changes are medians of within-session ratios.

| Host  | Case         | Workers | Baseline ms | Reuse ms |  Change |
| ----- | ------------ | ------: | ----------: | -------: | ------: |
| macos | identical    |       1 |      25.560 |   22.154 | -13.81% |
| macos | late         |       1 |      25.389 |   21.781 | -14.28% |
| macos | early        |       1 |       7.285 |    7.203 |  -0.60% |
| macos | size-control |       1 |       4.281 |    4.235 |  -1.02% |
| macos | identical    |       3 |      12.795 |   11.698 |  -8.57% |
| macos | late         |       3 |      12.728 |   11.636 |  -8.75% |
| macos | early        |       3 |       6.197 |    6.208 |  -0.09% |
| macos | size-control |       3 |       4.257 |    4.228 |  -0.24% |
| macos | identical    |      10 |       9.479 |    9.351 |  -1.53% |
| macos | late         |      10 |       9.277 |    9.115 |  -1.40% |
| macos | early        |      10 |       6.905 |    6.918 |  +0.47% |
| macos | size-control |      10 |       4.268 |    4.266 |  -0.92% |
| linux | identical    |       1 |       8.844 |    8.220 |  -7.57% |
| linux | late         |       1 |       8.176 |    7.236 | -10.09% |
| linux | early        |       1 |       1.377 |    1.374 |  -0.12% |
| linux | size-control |       1 |       0.852 |    0.861 |  +0.43% |
| linux | identical    |       3 |       3.810 |    3.611 |  -5.22% |
| linux | late         |       3 |       3.534 |    3.309 |  -6.85% |
| linux | early        |       3 |       1.285 |    1.297 |  +0.66% |
| linux | size-control |       3 |       0.839 |    0.829 |  +0.90% |
| linux | identical    |      10 |       3.078 |    2.907 |  -4.95% |
| linux | late         |      10 |       2.903 |    2.776 |  -5.90% |
| linux | early        |      10 |       1.624 |    1.637 |  +0.68% |
| linux | size-control |      10 |       0.867 |    0.852 |  -2.62% |

Complete reads improve most with one or three workers. Early mismatches often stop after one chunk, so they cannot amortize buffer reuse; keep those slower/small results visible. Thread startup and traversal can dominate ten-file and many-worker cases. This does not claim that larger worker counts improve all workloads.

## Initialization and fallback decisions

The entirely uninitialized cursor prototype improves some one-worker cases but repeatedly slows Linux ten-worker early differences: +6.88% initially and +8.84% after separating the loops. The selected one-clear policy is +1.32% and +0.68% respectively in the corresponding 100-file cases. Keep the one-time initialization; do not claim that all zeroing should be removed or attribute this result to a proven page-fault explanation.

| Memory-source control        | macOS change | Linux change |
| ---------------------------- | -----------: | -----------: |
| large-early-different/100/1  |       -0.12% |       +2.17% |
| large-early-different/100/4  |       -1.23% |       -6.00% |
| large-early-different/1000/1 |       +1.15% |       +1.29% |
| large-early-different/1000/4 |       -4.46% |       +2.24% |
| large-identical/100/1        |       -0.11% |       +1.68% |
| large-identical/100/4        |       -0.15% |       -5.23% |
| large-identical/1000/1       |       -0.28% |       +1.43% |
| large-identical/1000/4       |      -10.16% |       -2.16% |
| small-identical/100/1        |       +0.44% |       +0.02% |
| small-identical/100/4        |       +0.30% |       -0.70% |
| small-identical/1000/1       |       +0.17% |       -0.69% |
| small-identical/1000/4       |       +0.11% |       -1.20% |

The initial macOS large-early-different/100/1 control is +29.36%; the refined control is -0.12%. The table preserves remaining small regressions and noisy multi-worker improvements. The fallback algorithm is unchanged, so do not count those fluctuations as an archive or memory-source speedup. Existing archive/decoder integration tests pass, but no new archive throughput improvement is claimed.

## Allocations, retained storage and counterexamples

Isolated ASan hooks record counts only, never timing. Comparing two chunks and sixteen chunks uses 13 allocations in the selected implementation on both hosts. The old implementation uses 13 and 43 respectively. Total requested bytes for the sixteen-chunk pair fall from about 2.10 MB to 132 kB. This measures cumulative allocation traffic, not a reduction of the same size in peak RSS. Fixture creation, writes, assertions and warmup occur outside recording. The baseline diagnostic intentionally fails its constant-allocation assertion; the selected implementation passes. Both raw XML/logs are retained.

Each native pair retains at most two 64 KiB buffers, under the same two source reservations as before. Buffers are cursor-owned, not a global pool or idle per-worker cache; a buffer never outlives its reservation. No extra source-size scan or stat is added. EOF, short non-EOF reads, errors, metadata-sized comparison and left-read-before-right-open order remain unchanged.

The additive virtual capability is source-compatible with existing decoders. It requires rebuilding statically linked modules and does not promise a stable external binary ABI. There are no new dependencies, flags, locks, execution threads, shared owners or exception wrappers. File access remains inside the authorized VFS adapter; mutations in tests use only the existing temporary-output interface.

- macos: slowest native paired median +0.93% at (1000, 65536, 10, 'identical'); all small/control cases remain in the raw data.
- macos: build-output executable 4,069,408 -> 4,070,368 bytes (+960), before release stripping/compression.
- linux: slowest native paired median +3.37% at (10, 1048576, 10, 'early'); all small/control cases remain in the raw data.
- linux: build-output executable 12,295,504 -> 12,309,616 bytes (+14,112), before release stripping/compression.

## Validation

Both all-extension release suites pass 305/305, with FUSE integration required on Linux. Focused ASan on macOS and MSan on Linux pass the comparison and source tests. Regression tests cover native span caps, zero/short reads, offsets, EOF, preservation of unused buffer tails, mixed capabilities, repeated chunks, invalid returned counts, errors, source-budget exhaustion/release and left-side error precedence. The committed isolated allocation diagnostic verifies that chunk count no longer grows allocation traffic. Raw-pointer and host-I/O policy checks pass. Explicit local lint resolves Bazel include-prefix mappings and checks all 117 affected translation units; TODO records the current scope-selector gap. Final test-only style corrections pass macOS ASan and Linux MSan/allocation checks without changing the measured production sources. Normal commit/push hooks run before publication.

## Lossless raw evidence

- [cursor-owned-read-buffers-20261102-initial-macos-native.json.gz](cursor-owned-read-buffers-20261102-initial-macos-native.json.gz): 31,699 bytes; SHA-256 `08c96b5b8e0f692d022283c1f7860ae5e6852e189c57bdf9d42a75ea2af3cad7`.
- [cursor-owned-read-buffers-20261102-initial-linux-native.json.gz](cursor-owned-read-buffers-20261102-initial-linux-native.json.gz): 32,617 bytes; SHA-256 `40fe6ca25e204edab71d222414587a8d07ededd5bc137f964b624c2be7780b05`.
- [cursor-owned-read-buffers-20261102-refined-macos-native.json.gz](cursor-owned-read-buffers-20261102-refined-macos-native.json.gz): 45,704 bytes; SHA-256 `ef358c9e1634a6af8e3a0deef8b01681098a8acbe2539035682ac032aa2967ef`.
- [cursor-owned-read-buffers-20261102-refined-linux-native.json.gz](cursor-owned-read-buffers-20261102-refined-linux-native.json.gz): 46,743 bytes; SHA-256 `0feeaced6e803a199df3e59a1ab182527dfe4f5c28d1b37d748f86a4480b5091`.
- [cursor-owned-read-buffers-20261102-initial-macos-fallback.json.gz](cursor-owned-read-buffers-20261102-initial-macos-fallback.json.gz): 27,451 bytes; SHA-256 `825bedde52ce3a3c5293333bac310d0fc8d4d46aebd49e1b54c05d050242878b`.
- [cursor-owned-read-buffers-20261102-initial-linux-fallback.json.gz](cursor-owned-read-buffers-20261102-initial-linux-fallback.json.gz): 30,145 bytes; SHA-256 `5487789fa7b6651b57d7dcbd63eb55041d575f5eab45423d70f5bfac02f7576d`.
- [cursor-owned-read-buffers-20261102-refined-macos-fallback.json.gz](cursor-owned-read-buffers-20261102-refined-macos-fallback.json.gz): 28,674 bytes; SHA-256 `958775ee4bd76a862bad1580ae54eeb6f425d0dc0811cc22ddec4eae2ce81e0d`.
- [cursor-owned-read-buffers-20261102-refined-linux-fallback.json.gz](cursor-owned-read-buffers-20261102-refined-linux-fallback.json.gz): 30,128 bytes; SHA-256 `e7489710481832ac609eb1c95c9d8ce8921dc7383b4e259ce09a7a39d8a996e1`.
- [cursor-owned-read-buffers-20261102-provenance.json.gz](cursor-owned-read-buffers-20261102-provenance.json.gz): 33,126 bytes; SHA-256 `9d90ce26d65b7efee6ee63d663987ef4beb8a7aee3f5158415a4d35e74afb80f`.
