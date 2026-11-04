<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Similarity word sets

Keep short normalized words in the existing batch, and insert long words directly from reusable
scratch storage. This avoids a vector of separately allocated long-word strings while preserving
the short-word behavior that both unconditional direct-insertion experiments made slower.

## Native decision

Ratios are candidate elapsed time divided by its paired baseline; below 1 improves. Each case
uses the median of three paired session ratios. Each session retains the mean fastest seven of
nine interleaved measured rounds. Cohort entries are medians across cases, without pooling hosts.
The original three-way screen and separate final-policy confirmation each have an identical
baseline-copy participant. Keep them as separate measurement cohorts.

| Host  | Cohort       | Policy          | All width-one cases | Large short-word cases | Long-word cases | Width-three controls | Raw/empty controls |
| :---- | :----------- | :-------------- | ------------------: | ---------------------: | --------------: | -------------------: | -----------------: |
| macos | screen       | direct_move     |              0.9945 |                 1.0561 |          0.9957 |               1.0013 |             1.0032 |
| macos | screen       | direct_copy     |              0.9868 |                 1.0747 |          0.9581 |               0.9965 |             1.0068 |
| macos | screen       | baseline_repeat |              0.9997 |                 1.0005 |          1.0002 |               0.9984 |             1.0073 |
| macos | confirmation | hybrid          |              0.9850 |                 0.9882 |          0.9572 |               0.9995 |             0.9951 |
| macos | confirmation | baseline_repeat |              1.0001 |                 0.9999 |          1.0004 |               0.9985 |             0.9998 |
| linux | screen       | direct_move     |              0.9815 |                 1.0473 |          0.9527 |               1.0019 |             1.0039 |
| linux | screen       | direct_copy     |              0.9308 |                 1.0580 |          0.9123 |               1.0085 |             0.9990 |
| linux | screen       | baseline_repeat |              0.9998 |                 1.0005 |          0.9998 |               0.9983 |             0.9915 |
| linux | confirmation | hybrid          |              0.9625 |                 1.0069 |          0.9204 |               1.0035 |             1.0007 |
| linux | confirmation | baseline_repeat |              1.0008 |                 1.0073 |          0.9999 |               0.9970 |             0.9979 |

Reject both unconditional direct policies: their large-short-word cohort costs about 5-7% on
macOS and Linux, beyond their identical-binary controls. The final policy preserves the batch
for inline-length strings and copies long keys directly from one reusable scratch string.
Its short-word cohort matches or improves the corresponding control; long-word native workloads
retain gains. All per-case regressions and control variation remain in the evidence.

## Implementation and ownership

`OwnedSingleByteShingles` keeps the existing byte-word classifier and ASCII lowercase operation.
Completed words whose length fits an empty `std::string` capacity are copied into the short-word
vector, then moved into the set in a batch. Longer words are inserted directly from scratch,
then scratch is cleared for reuse. The cutoff follows the standard library string representation,
rather than introducing a tuning flag or an arbitrary word-length constant.

All keys remain owned. A caller may move a reference or change its original source storage without
invalidating normalized keys. Scratch lives only during construction; a long key has its own copy
while scratch remains available for the next token. The temporary scratch capacity can therefore
coexist with an owned long key until construction returns. This is not a zero-copy API.

Width-zero/out-of-range threshold semantics, empty inputs, exact integer rounding, duplicate
handling, cache admission, fresh reference reads and binary/error checks remain unchanged.
The multiword borrowed-window representation stays in place. No worker, lock, I/O path, flag,
dependency, language catalog or user-visible output changes.

## Retained storage accounting

These are the existing `ByteWordShingleReference::StorageBytes()` counters, collected separately
with dry-run executions. They are not RSS, peak-memory measurements or malloc call counts.

| Host  | Word shape        | Words | Baseline tracked bytes | Final tracked bytes |
| :---- | :---------------- | ----: | ---------------------: | ------------------: |
| macos | short             |    32 |                  2,631 |               2,631 |
| macos | short             |  2048 |                 39,166 |              39,166 |
| macos | long, padding 256 |    32 |                 22,375 |              18,551 |
| macos | long, padding 256 |  2048 |                702,439 |             658,519 |
| linux | short             |    32 |                  2,631 |               2,631 |
| linux | short             |  2048 |                 39,166 |              39,166 |
| linux | long, padding 256 |    32 |                 22,375 |              18,791 |
| linux | long, padding 256 |  2048 |                702,439 |             659,319 |

Short-key tracked storage is unchanged. The long-key examples retain less capacity because keys
are copied to their owned storage rather than retaining each grown temporary word buffer. The
normal cache still uses its existing tracked-storage bound.

## Measurement and qualification

Implement baseline, direct-move, direct-copy and the final short-batch/long-copy policy. The
baseline and both direct strategies pass five affected release suites and actual source/header/
benchmark checks on both hosts. The final policy adds a mixed-inline/allocated-key test and
passes the same initial checks. All four builds pass 108 new manual correctness dry runs per host.
A move-preserving fallback was prepared outside the checkout but was neither built nor measured;
it is not part of the tested or selected policy.

The 108 new primitive cases cover one/32/2,048 words, one/32/2,048 distinct-word requests,
zero/32/256 padding bytes, and fresh/prepared percentage/threshold APIs. Requests larger than the
word count intentionally include equivalent tiny cases. Another 16 unchanged width-three cases
provide controls. Google Benchmark raw records retain their own units; ratios compare the same
case and unit. In-memory direct results are mechanism checks, not substitutes for native results.

Across both hosts, the original screen has 2,232 case/session/participant records; final-policy
confirmation has 1,488. Together these retain 33,480 raw timed repetitions. Native cohorts each
have 96 cases: 72 width-one, 16 width-three and eight raw/empty controls, with one/three jobs.
Original screen: 11,520 invocations per host including discarded warmups. Final confirmation:
8,640 per host. Total: 40,320 native invocations across both hosts. Every invocation checks its
exit and complete stdout against an independent byte-word regex/set and integer-rounding oracle.

Native shapes have 32 or 2,048 tokens, one/32/2,048 distinct-word requests, zero/32/256 padding,
and 1,000 or 100 files respectively. Candidates vary casing, separators, ordering and an extra
word, with thresholds 50/100. Linux uses shared-memory fixtures and recorded one/three-CPU
affinity; macOS uses local fixtures without claiming affinity. No host runs builds, tests, lint,
fuzz or heavy fixture creation during its release timing. Shuffle cases and rotate participants.

The selected source passes all 314 release targets on both hosts, required Linux FUSE, the five
affected ASan suites on both hosts and those five MSan/TSan suites on Linux. Each host also passes
a separate 30-second bounded ASan/libFuzzer campaign over arbitrary bytes with recorded seed 1083,
max length 4,096 and the existing platform runtime configuration. These are correctness checks,
not timing evidence. Dedicated opaque-byte fuzzing preserves the separation from ordinary ASCII
functional fixtures. Existing boundary/rounding/cache/isolation tests remain in place.

## Footprint and provenance

| Host  | Baseline full CLI bytes | Final full CLI bytes | Change |
| :---- | ----------------------: | -------------------: | -----: |
| macos |               8,206,808 |            8,206,808 |     +0 |
| linux |              18,269,920 |           18,270,952 | +1,032 |

Frozen release/ThinLTO measurements use stack checkout `b04a2e49e187bffb11ee75bd86aad536f5470f2d`.
Full selected qualification uses synchronized parent `7f73496b8bb7643c861b483d480166a7c9cb10c0`,
which adds the four reviewed CI-repair files from main `0d64bb3974f4e70bc5dfc36ea8647e7e7db3cbf5`.
Compiler/source/executable/configuration/link identities remain recorded per build. Do not relabel
a frozen executable as built from a later metadata-only revision.

Run the committed primitive cases with:

```sh
bazel run //xff/matching/similarity:similarity_benchmark --config=clang_release -- --benchmark_filter=^similarity/word-insertion/
```

## Evidence

Preserve all rejected strategies, separate cohorts, raw rounds, independent oracles, source
snapshots, full qualification/fuzz logs and capacity diagnostics. Generated fixture trees,
executable/object payloads and canonical compile databases are excluded.
`word-set-insertion-20261104.json.gz`: 6,415,610 bytes, 669 labeled files; SHA-256
`046d33745f7eed0e7477de4a293b54cd0d5d93dd63a256109881a1e00aa4764b`.
