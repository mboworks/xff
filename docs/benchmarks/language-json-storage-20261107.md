# Final language JSON storage, 2026-11-07

Decode the embedded language database directly into its final owning string, then
move that string into lazy static storage. The 109,261-byte database previously
passed through a zero-initialized byte vector, a separate string, and another owned
string copy. This is a small one-time ownership cleanup. It does not establish a
whole-command startup speedup, and ordinary listing does not initialize this database.

## Implementation and contracts

`std::string::resize_and_overwrite` gives the existing Brotli C decoder writable final
storage without first filling it with zeroes. The required C callback/output pointer
stays inside a narrowly annotated adapter. The callback returns only the successfully
written prefix, or zero on failure. Invalid data and exact decoded-length mismatch
retain their existing `DataLoss` statuses. Empty valid payloads and an undersized
destination are regression-tested. No unwritten byte is read or published.

The lazy initializer uses a mutable `StatusOr<string>` solely to move its owned value.
The static string still owns the returned database view. No environment/configuration,
public API or CLI behavior changes. There is no new runtime wrapper, permanent benchmark
target or benchmark module dependency.

## Current production decoder benchmark

Fresh baseline/candidate binaries call each tree's actual production `Decode` through
an identical harness and byte-exact copy of the existing compressed payload. Source,
configuration, corpus and image hashes are retained. Both use `clang_release` and
`xff_docs`, without sanitizers. The temporary root/manual target uses the existing
Google Benchmark dependency; its rule and sources are archived and were removed
from both worktrees before preparing the public change.

Each host completes three sessions of nine repetitions per case/participant, retaining
the fastest seven and averaging them per session. Participant order rotates by
session, with per-case random interleaving inside the benchmark. The control repeats
the exact baseline image. Linux is pinned to one allowed CPU; Mac is unpinned. No
build, lint or sanitizer runs concurrently with timing on the same host. Positive
percentages are slower.

| Same retained ownership behavior | Mac candidate | Mac baseline repeat | Linux candidate | Linux baseline repeat |
| :------------------------------- | ------------: | ------------------: | --------------: | --------------------: |
| Decode followed by owned copy    |        -6.05% |              -0.12% |          -2.87% |                -0.12% |
| Decode followed by move          |        -5.09% |              +0.79% |          -2.54% |                -0.62% |

The same-method comparisons isolate the decoder storage change. Comparing the old
production-style decode-plus-copy path with the new decode-plus-move path improves
9.87% on Mac and 3.90% on Linux. This latter comparison includes both storage and
owned-result transfer; it does not isolate one mechanism or claim a CLI speedup.
All raw repetitions, session values and both slower/faster control movements remain
in the artifact. No native RSS or peak-memory value was measured.

## Earlier decode-stage diagnostics

The corrected research harness compares the former vector/decode/string/copy sequence
with production direct decoding followed by either another owned copy or a move. The
November 6 populations below have nine repetitions per case on each host; these values
are the Google Benchmark median real times. They are not the separate three-session
zero-initialized-string experiment, which is also preserved under its own name.

| Diagnostic                                   | Mac microseconds | Linux microseconds |
| :------------------------------------------- | ---------------: | -----------------: |
| Former vector, decoded string, retained copy |          158.611 |            108.313 |
| Direct final-buffer decode, retained copy    |          151.908 |             95.547 |
| Direct final-buffer decode, retained move    |          146.704 |             93.534 |

Linux was pinned to CPU 2; Mac was unpinned. These are modeled initialization kernels,
not current-base CLI measurements. The retained v2 JSON identifies the executable path
and benchmark-library build but lacks a complete source/compiler/image-hash receipt;
its research harness and original plan are preserved as supporting diagnostics rather
than final current-head performance qualification. No native RSS or peak-memory value
was measured. The removed vector/owned copies are visible in the source change; the
kernel results do not quantify overall application memory use.

## Current-base whole-command controls

Fresh baseline and candidate full CLI binaries were built from `9bf1aad5` using
`clang_release` and `xff_docs`, with source and image hashes recorded. A native
fork/exec/wait launcher excludes Python launcher startup from the measured interval;
the complete xff invocation remains included. Each of three sessions has nine
rotating baseline/identical-baseline-repeat/candidate rounds after discarded warmups.
Retain the fastest seven per case/session, then report the median paired change.
Linux is pinned to one allowed CPU; Mac is unpinned. No build or sanitizer runs on
the measured host during timing. Every invocation passes a complete output SHA-256
oracle; the listing fixture has 100 known files, and the full type list includes C++.
Positive percentages are slower.

| Command         | Mac candidate | Mac same-binary repeat | Linux candidate | Linux same-binary repeat |
| :-------------- | ------------: | ---------------------: | --------------: | -----------------------: |
| type-list       |        -0.24% |                 +1.26% |          +0.58% |                   +0.36% |
| listing-control |        +1.28% |                 -2.07% |          +0.59% |                   -0.01% |

These controls establish no material whole-command improvement. The 500-launch
historical Mac summary was also inconclusive, but does not retain its individual
samples or complete measured-source identity, so it is preserved separately and
is not substituted for this current-base population. Every current raw sample,
per-session aggregate and oracle digest is included.

Mac full images are both 8,275,000 bytes. Linux changes from 18,443,224 to 18,442,904
bytes (-320). These unstripped experiment images are not release-download sizes.
The two binaries within each host have distinct hashes despite matching Mac sizes.

## Correctness and remaining integration

Both hosts pass all 323 core plus registered-extension release targets under
`clang_release` and `xff_docs`; Linux requires real FUSE tests. Both pass canonical
Clang+ASan language registration, language matching and language-database integration
targets. Linux also passes those three targets under MSan, checking the newly
uninitialized destination buffer path. Exact commands and complete logs are retained.
A sandbox-only Mac invocation failed before Bazel analysis because it could not
write the existing cache; the authorized invocation passed. That setup failure is
kept separately rather than described as a candidate test failure.

The new destination-boundary/empty-payload tests and existing invalid-data/size
checks pass. Test-only formatting was applied after development runs; current-head
PR CI, including changed-source clang-tidy, GCC and coverage, remains required.
No local clang-tidy was run. The successor integration on `c4d21aea2f6640386d49c4a2f66503472bd64f96`
(#1127) passes the three optimized language targets on Mac. Module implementation,
regression source and BUILD inputs match the qualified payload. This is a development
integration check, separate from the future pushed-head PR context. The current production-function results qualify a focused ownership cleanup;
no user-visible speedup or measured RSS gain is promised.

## Evidence

[`language-json-storage-20261107.json.gz`](language-json-storage-20261107.json.gz) is 154,700 bytes, SHA-256
`59d7645aea9397b4bba4443a903e5e3dfd295ca590ac2333a4f816288ab431d8`. Its 108 source/raw/log records retain exact byte counts and hashes.
The earlier zero-initialized-string artifact remains byte-exact, encoded separately
inside the bundle. Full executables stay in the local/remote task directories;
their recorded hashes identify the measured images.
