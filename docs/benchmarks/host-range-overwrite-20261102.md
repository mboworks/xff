# Bounded host range reads without redundant zeroing (2026-11-02)

## Decision

Use C++23 `std::string::resize_and_overwrite` in `LocalFs::ReadContentRange`. The read-only POSIX adapter fills the allocation directly, then publishes only the initialized byte count. Preserve partial reads, EOF, offset limits, EINTR retry and error status behavior. Close the handle once after the overwrite callback. No new public API, pool, lock, exception cleanup, extra metadata query, dependency or user flag is added.

Keep `HostStream::Read` unchanged. Removing its zeroing is a separate tested prototype: it helps longer sequential reads but repeatedly regresses Linux early differences with ten workers. Preserve that source and all samples, and revisit cursor buffer ownership/reuse under O07 instead of adopting the combined change. The existing full-content stack buffer was already uninitialized and is not changed again.

## Measurement contract

Initial and split trials use main `9e4338a6ea3b47431e612a1fa24b818b3269193d` after the fuzzy optimization. Final selected qualification uses report-parent `4e8d2bc60d8cb9755e2fdc52e8ac7acc559e7d09`, whose runtime includes merged #1044/main `d546fcb3d864ed18776f642c1aa0fe904e9f8bfa`. The final two variants differ only in `local_fs.cc`; their `read_source.cc` is identical. Do not pool absolute times across these phases. Full source patches, actual binary identities and helper source are retained below.

Both hosts use release LLVM 23 + ThinLTO. macOS is the local M5 Pro with warm APFS and requested 1/3/10 workers without affinity. Linux drogon is the Ryzen 9950X KVM guest, tmpfs fixtures and child affinity 1/3/10. Builds, fixture creation and sanitizer diagnostics finish before timing. These are different storage/hardware contracts; compare variants within each host.

The changed code is an OS read adapter, so native cached-file workloads are required to measure it. An in-memory VFS would bypass the changed implementations. Deterministic fixtures have 10/100/1,000 files at 4 KiB/64 KiB and 10/100 files at 1 MiB. Cases are identical, first-byte different, last-byte different and different sizes (which avoid content reads). The fixture recipe creates about 1.54 GB per host, not an archived opaque corpus. Range comparison handles files through 64 KiB; larger files use cursors.

Each native case uses three sessions, a discarded warmup per variant/session, nine interleaved rounds and fastest seven retained. Every invocation checks all status/path records against an independent exact oracle. One/three workers cover all shapes, with ten-worker samples for 1,000 small files and 100 large files. Initial and final matrices each have 76 cases/session; split confirmation has 24 cases/session and three variants. Across phases/hosts, 22,560 fresh invocations include discarded warmups.

Commands explicitly use `--compare=status --compare-select=all`, `-type f`, `--exact`, `--sort=dir`, UTC and no config/pager. Actual read sizes, worker allowance and selected populations are identical between variants. Times are median session means; changes are medians of within-session ratios. Negative means faster.

## Final selected range-only results: 1,000 files of 64 KiB

| Host  | Case         | Workers | Baseline ms | Selected ms | Change |
| ----- | ------------ | ------: | ----------: | ----------: | -----: |
| macos | identical    |       1 |      40.689 |      38.382 | -5.79% |
| macos | late         |       1 |      40.172 |      37.628 | -6.94% |
| macos | early        |       1 |      36.721 |      35.744 | -2.66% |
| macos | size-control |       1 |       7.534 |       7.490 | -0.41% |
| macos | identical    |       3 |      40.270 |      37.951 | -5.60% |
| macos | late         |       3 |      40.303 |      37.777 | -5.59% |
| macos | early        |       3 |      36.530 |      35.897 | -1.73% |
| macos | size-control |       3 |       6.787 |       6.833 | +0.13% |
| macos | identical    |      10 |      41.473 |      38.953 | -6.08% |
| macos | late         |      10 |      42.291 |      39.925 | -6.08% |
| macos | early        |      10 |      38.045 |      37.290 | -1.20% |
| macos | size-control |      10 |       7.019 |       6.967 | -0.49% |
| linux | identical    |       1 |       9.813 |       9.229 | -4.80% |
| linux | late         |       1 |       9.499 |       8.983 | -5.87% |
| linux | early        |       1 |       8.644 |       8.160 | -5.60% |
| linux | size-control |       1 |       2.092 |       2.075 | +0.95% |
| linux | identical    |       3 |      10.222 |       9.640 | -5.69% |
| linux | late         |       3 |       9.347 |       9.018 | -4.09% |
| linux | early        |       3 |       9.177 |       8.506 | -6.81% |
| linux | size-control |       3 |       1.823 |       1.770 | -2.27% |
| linux | identical    |      10 |      10.111 |       9.499 | -6.06% |
| linux | late         |      10 |       9.771 |       9.132 | -6.54% |
| linux | early        |      10 |       9.528 |       8.893 | -4.83% |
| linux | size-control |      10 |       2.359 |       2.260 | -4.18% |

The 4 KiB cases are mostly near baseline because traversal, open/close and startup dominate. The 64 KiB read cases improve consistently on both hosts. Size-only mismatches remain controls and must not be counted as a zeroing optimization. Large-file cursor code is unchanged in the selected variant; do not claim a streaming improvement from it.

## Split experiment: 100 files of 1 MiB

| Host  | Case      | Workers | Combined change | Range-only change |
| ----- | --------- | ------: | --------------: | ----------------: |
| macos | identical |       1 |         -13.88% |            -0.24% |
| macos | late      |       1 |         -14.57% |            -0.49% |
| macos | early     |       1 |          -1.42% |            -0.23% |
| macos | identical |       3 |          -8.77% |            -0.02% |
| macos | late      |       3 |          -8.54% |            -0.51% |
| macos | early     |       3 |          -0.60% |            +0.46% |
| macos | identical |      10 |          -0.80% |            +0.12% |
| macos | late      |      10 |          -1.08% |            +0.67% |
| macos | early     |      10 |          -0.78% |            +0.41% |
| linux | identical |       1 |          -6.43% |            +0.00% |
| linux | late      |       1 |          -7.42% |            +0.74% |
| linux | early     |       1 |          -5.74% |            -0.77% |
| linux | identical |       3 |          -4.39% |            -0.78% |
| linux | late      |       3 |          -3.87% |            -1.22% |
| linux | early     |       3 |          -1.30% |            -1.26% |
| linux | identical |      10 |          -0.22% |            -2.95% |
| linux | late      |      10 |          +0.12% |            -1.33% |
| linux | early     |      10 |          +8.76% |            +2.39% |

The combined prototype improves one-worker large comparisons by about 14% on macOS and 6-7% on Linux, but Linux early differences at ten workers regress 6.28% in the first matrix and 8.76% in split confirmation. Range-only avoids that repeated cursor penalty. These data do not prove whether page faults, allocation reuse or scheduling explain it; do not present a guessed mechanism as established. Cursor zeroing stays unchanged pending a separately qualified ownership/reuse design.

## Counterexamples and footprint

- macos: slowest final paired median +0.83% at (100, 1048576, 10, 'early'). All slower controls and small cases remain in the raw evidence.
- macos: build-output executable 4,069,376 -> 4,069,376 bytes (+0), before release stripping/compression.
- linux: slowest final paired median +3.41% at (100, 1048576, 10, 'identical'). All slower controls and small cases remain in the raw evidence.
- linux: build-output executable 12,295,752 -> 12,295,504 bytes (-248), before release stripping/compression.

Some unchanged cursor/metadata control timings vary by a few percent; the report does not normalize those away or attribute them to read savings. No claim is made for eliminating range allocation, accelerating every invocation, changing archive size limits, or removing all read-buffer initialization. Read-source reservations and replay accounting stay unchanged.

## Correctness and validation

Only the callback ABI required by `resize_and_overwrite` uses a raw writable pointer, with its explicit adapter annotation. No xff-owned pointer interface is added. The callback may receive more storage than requested but reads at most the requested length and returns at most the initialized total. Errors remain errors, not successful partial output or EOF. No additional stat is performed merely to reserve storage.

Committed regression cases cover chunk/EOF boundaries, nonuniform byte patterns, zero/short/beyond-EOF ranges, native offset limits, read errors, zero-length cursors and ownership of earlier chunks. Reads/writes remain routed through the existing VFS and authorized temporary-output adapter. The original and selected paths pass focused release tests on macOS/Linux; initial ASan (macOS) and MSan (Linux) checks pass. Both complete suites pass 304/304 on the initial parent and 305/305 on the final parent (Linux requires its working FUSE support). The exact final range-only source also passes focused ASan on macOS and MSan on Linux. Scoped local clang-tidy passes all three changed translation units; normal commit/push hooks run before publication.

## Follow-up boundaries for O07

Cursor-owned reuse is the simplest first lifetime: its storage and existing reservation die together. A persistent worker buffer can outlive a source reservation, so retaining one requires explicit budget ownership across sources. A generic span-read fallback can add another allocation/copy for decoded or archived streams; preserve their existing path or expose an honest capability and measure it. Keep short non-EOF reads and offset/replay limits. Do not add a global pool or new generic source-size query just to reserve.

## Lossless raw evidence

- [host-range-overwrite-20261102-initial-macos.json.gz](host-range-overwrite-20261102-initial-macos.json.gz): 59,902 bytes; SHA-256 `6805adf3f7d6c69bade6056818e0ebecc3e33928f18e54643bb5a880b486abad`.
- [host-range-overwrite-20261102-initial-linux.json.gz](host-range-overwrite-20261102-initial-linux.json.gz): 62,098 bytes; SHA-256 `9417f7f465fa332fb3e545e80834d62792aed322d54dd88d2a553feec1f3a3ca`.
- [host-range-overwrite-20261102-split-macos.json.gz](host-range-overwrite-20261102-split-macos.json.gz): 31,360 bytes; SHA-256 `924e965ed81bad9a1974a911eac27b1c6bf97a9a07daef07f02c2a1d9ae2632e`.
- [host-range-overwrite-20261102-split-linux.json.gz](host-range-overwrite-20261102-split-linux.json.gz): 33,076 bytes; SHA-256 `bbded37bf5466bdab9911c67d965d2453d7ede2fa5280b0f8c8f5b5a70e058ce`.
- [host-range-overwrite-20261102-final-macos.json.gz](host-range-overwrite-20261102-final-macos.json.gz): 60,031 bytes; SHA-256 `250e62bbf77592b5408436df2cf2808512304ed68291a7a6947c8aa825aaea5a`.
- [host-range-overwrite-20261102-final-linux.json.gz](host-range-overwrite-20261102-final-linux.json.gz): 60,584 bytes; SHA-256 `414653b2ad1cda3d1d0e737c112b2d5e5c773cbe1a9d5f15bb2e4627a1ec26e1`.
- [host-range-overwrite-20261102-provenance.json.gz](host-range-overwrite-20261102-provenance.json.gz): 34,103 bytes; SHA-256 `9dba4c068eb55dc5f6c05b9033f64b5c4d1ccebe6816b7563f6e0a7fd953d139`.
