<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Move owned single-word shingle keys

Width-one similarity already builds owning words before inserting unique keys into its set.
Making that local vector mutable allows insertion to move each word's existing storage.
The word-building algorithm, deduplication, byte folding, scores and wider shingle windows remain
unchanged. Long-word native workloads improve about 6-7% on macOS and 9% on Linux in this matrix;
short-word commands remain near baseline.

## Ownership and memory tradeoff

This does not change the previously qualified vector-first algorithm or reintroduce the rejected
all-view width-one representation. The result owns its keys after the temporary vector is destroyed.
Duplicate insertion remains valid whether the set consumes the argument or retains the existing key.
No new interface, dependency, thread, lock, clock or filesystem operation is introduced.

Moving a heap string also retains the capacity accumulated while tokenizing it. Copying could instead
allocate a smaller final key. One 2,048-word disjoint reference with 256 padding bytes grows from
1,203,223 to 1,432,599 bytes of tracked capacity, about 19%. This is not an RSS measurement.
The reference cache still accounts for actual string capacity and releases storage exceeding its
existing per-slot bound. Lower temporary allocation traffic does not promise lower retained memory.

## Native commands

Each row uses 1,000 files with 256 words per body. Padding is appended to each distinct token.
The eight-key case repeats long words rather than silently assuming every word is unique.
Reference bodies are ASCII uppercase, so successful comparison does not use byte equality.
Dynamic references add unique punctuation-only suffixes without changing their word-shingle sets.

Times are milliseconds. Changes are medians of three within-session ratios; negative means faster.

| Workload                             | Workers | macOS original ms | macOS change | Linux original ms | Linux change |
| :----------------------------------- | ------: | ----------------: | -----------: | ----------------: | -----------: |
| Short words, repeated reference      |       1 |            38.794 |       -0.06% |            12.176 |       -0.22% |
| Short words, repeated reference      |       3 |            38.696 |       -0.06% |            12.229 |       -0.73% |
| 64-byte padding, repeated reference  |       1 |           103.280 |       -7.15% |            54.387 |       -9.10% |
| 64-byte padding, repeated reference  |       3 |           102.762 |       -6.48% |            54.187 |       -8.73% |
| 64-byte padding, dynamic reference   |       1 |           169.685 |       -6.35% |           100.586 |       -8.83% |
| 64-byte padding, dynamic reference   |       3 |           168.698 |       -6.68% |            99.959 |       -8.76% |
| 256-byte padding, repeated reference |       1 |           279.074 |       -4.72% |           147.516 |       -4.40% |
| 256-byte padding, repeated reference |       3 |           279.117 |       -4.83% |           147.613 |       -4.63% |
| Eight repeated long keys             |       1 |            86.631 |       -0.55% |            39.709 |       -0.27% |
| Eight repeated long keys             |       3 |            86.795 |       -0.71% |            39.609 |       -0.30% |
| Width-three control                  |       1 |            93.785 |       -0.04% |            45.773 |       +0.06% |
| Width-three control                  |       3 |            93.703 |       +0.01% |            45.899 |       -0.16% |

The full 50-case native population also contains ten-file, listing, name and regex controls,
width-three controls, both worker requests and an identical-baseline copy. Each host checks exact
output and exit status in 4,500 invocations including warmups. The worst native macOS cell is about
+0.83%. Linux's duplicate-heavy dynamic cases can cost about +0.7%; the favorable noisy listing cell
also improves by almost the same amount for the identical baseline. Complete populations are kept,
including small direct-kernel slowdowns rather than averaging them away against long-word wins.

## Direct and whole-engine controls

The initial three-session pilot covers 78 cases per host, with the original and move implementations.
Direct verification and warmed immutable-reference cases include 32/256/2,048 words, widths 1/3/5,
equality, partial overlap, disjoint sets, short words and 64/256 padding bytes. Larger inputs include
repeated keys as well as high-cardinality references. Whole-engine controls include ordinary name
and regex output, empty runs and short width-one reference workloads.

Long-word direct cases typically improve a few percent; some warmed-reference cases remain neutral.
Small macOS short-word kernels can cost about 1-3%, although their native command counterparts remain
near baseline. Original equality and wider-window paths are unchanged; their samples remain controls.
The committed benchmark now exposes the longer-word fixtures and immutable-reference operation.

## Allocator diagnostics

An isolated ASan probe counts one verifier call, including call temporaries and excluding fixture and
cached-reference construction. Instrumented timing is never used as performance evidence. Normal
sanitizer qualification retains full checks.

Abseil enables generation tracking and artificial rehashes in sanitizer builds. `NDEBUG` alone does
not disable these. The initial debug and NDEBUG-only exploratory counts therefore remain noisy; an
early NDEBUG-only scope annotation was incorrect and is explicitly corrected in `allocator-policy.json`.
The final counting probe additionally defines Abseil's supported `NDEBUG_SANITIZER` switch to suppress
that container instrumentation only in the probe, while keeping ASan allocator hooks and address checks.

For 256 long words with 64 padding bytes, macOS request counts fall from 1,572 to 1,060 for ordinary
verification and 786 to 530 for a cached-reference comparison. Corresponding requested bytes fall
from 188,504 to 149,088 and 94,224 to 74,544 for the nearby fixture. Short-word requests/bytes are
unchanged. These are allocator requests, not live/peak memory or an application-wide guarantee.
The sources, flags, earlier observations and final diagnostic are retained separately.

## Correctness and CI test stabilization

All 314 enabled release targets pass on each host, with FUSE required on drogon. Five affected ASan
targets pass on both; the same five MSan/TSan targets pass on Linux. Scoped clang-tidy checks all
three changed C++ translation units using the real canonical compile database. Existing independent
byte-word and opaque-byte oracles remain active; an added test covers duplicate/unique keys and
padding around short-string storage boundaries through both exact-score and reusable-reference APIs.

A separate test-only commit fixes the existing Python progress-order flake observed in #1075's MSan
job. The real-process test no longer assumes that a 10 ms sleeper completes before a 50 ms sleeper.
A deterministic mocked-completion test separately proves that progress follows completed futures
rather than submission order. The integration test still checks real subprocess success/failure,
diagnostics and aggregate progress. Tests pass with Python 3.9/3.13 locally and through Bazel on both
hosts. No sanitizer finding caused that CI failure.

## Protocol and footprint

Timing uses parent `75c04b61c32012ae0e0b60949cde3dcecf632be4`, the same hermetic LLVM identity and
release/ThinLTO settings per participant. The separate Python test commit changes no runtime source.
Native inputs use Linux shared memory and macOS's local filesystem; direct strings and in-memory
engine fixtures isolate filesystem speed. Cases are shuffled, participant order rotates, each
case/session has a discarded warmup and nine measured rounds, and the fastest seven are retained.
Ratios are paired within sessions. Linux records and pins the requested CPU allowance.
Direct benchmark time units/calibration and engine registration overrides remain in the raw data.

| Host  | Original full CLI bytes | Selected full CLI bytes | Change |
| :---- | ----------------------: | ----------------------: | -----: |
| macOS |               8,185,912 |               8,185,912 |     +0 |
| Linux |              18,220,984 |              18,220,928 |    -56 |

No release timing is taken during same-host builds, tests, lint or allocation probes. Raw snapshots,
identities, commands, all measured rounds, controls, diagnostic revisions and validation records
are retained in [the compressed evidence](owned-word-moves-20261103.json.gz). The initial bundle
includes every raw measured round on both platforms, frozen sources and build/compiler identities,
full local logs, and a receipt of the successful Linux validation. Additional original Linux
build/test console logs and its allocator-counter JSON remain at the documented experiment path
on drogon; their auxiliary copy was blocked by the approval-review service. No missing log is
represented as a reconstructed raw log. The published count table above uses collected macOS data.

Archive: 1,612,673 bytes, containing 218 labeled evidence files. SHA-256:
`38989938e6ad2aaba6325ac3f813c77d0b6904b633cc5a367ddd4e5b808d8c3a`.
