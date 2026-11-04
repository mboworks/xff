<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Scoped native stat-path storage

Avoid a heap-backed string copy solely to terminate ordinary native stat paths. A scoped
512-byte uninitialized buffer copies the exact view and adds one terminator; longer paths retain
an owning string fallback. Selected eager/native cases improve modestly on both hosts, commonly
about 0.5-1.6%, with long-name macOS eager cases improving about 0.8-1.8%. Some controls remain
neutral or slower. This removes one allocation opportunity; it does not make stat cheap or replace
bulk metadata, worker scheduling or the public string-view contract.

## Implementation and correctness

A private TerminatedStatPath owns the short buffer, long fallback and input length. It neither
reads past the supplied view nor probes an assumed terminator. Only copied bytes plus their
terminator are initialized; the unused buffer tail is untouched. Copies and moves are deleted so
an accidental copy cannot read an uninitialized tail. The short/long decision uses size < capacity,
with no size-plus-one overflow. The view ends immediately before the owned terminator and stays
valid through stat/lstat/statx, error construction and metadata conversion.

Only LocalFs::StatFields uses the adapter. Basic and birth-time masks, statx/fallback selection,
followed-link handling, timestamps, errors and every mutation route remain unchanged. The new
fixture exercises 255/256/257, 511/512/513 and 767-byte spellings with basic and birth fields, both
an exact-sized non-terminated vector and a view followed by unrelated path bytes. Empty/missing
paths remain errors. Existing suites retain symlink, special-type and metadata behavior.

At the two POSIX call sites, a narrow bugprone-suspicious-stringview-data-usage suppression records
the owned NUL-termination guarantee that clang-tidy cannot infer through the value adapter.
The existing host-I/O annotations remain on the calls. There is no raw observer API, global or
thread-local scratch, shared owner, lock, new host-I/O route, dependency, public option or threshold.
The fallback retains ordinary string allocation behavior for long inputs.

## Measurement protocol

The parent contains eager names-only stat-path reuse. Native fixtures and all compared release
executables retain their identities and source snapshots. The first matrix compares parent,
identical parent-copy and 256/512/1,024-byte policies across 16 cases. A second matrix compares
parent/copy/256/512 across 12 cases and adds 100,000-file eager summaries. Each host uses three
sessions, rotating participants per repetition, one discarded warmup, nine measured runs and the
fastest seven. The totals are 2,400 + 1,440 = 3,840 invocations per host including warmups. Every
invocation checks exit status and complete stdout against its parent. Raw copy drift is retained,
not subtracted to manufacture a result. Ratios below one are faster.

Cases include metadata-free 10/1k/100k listings, one/three workers, eager CSV, on-demand size,
default-native case and 180-character-component names. Native enumeration, stat, workers and output
are timed together. An in-memory VFS bypasses this host adapter and would not demonstrate its gain,
so no in-memory speedup is claimed. Linux uses one/three allowed logical CPUs; no Mac pinning is
claimed. No competing same-host agent build/test/lint ran during timing. Fixtures are unchanged
through each measurement, and complete output is checked outside the native timing interval.

## Policy screen

The columns are medians of within-session elapsed ratios, not ratios of displayed medians.

| Host  | Case                                   | 256 bytes | 512 bytes | 1,024 bytes | Parent-copy |
| :---- | :------------------------------------- | --------: | --------: | ----------: | ----------: |
| macOS | exact/broad/10/1/files                 |    0.9976 |    0.9953 |      1.0008 |      1.0031 |
| macOS | exact/broad/1000/1/files               |    0.9810 |    0.9875 |      1.0027 |      0.9852 |
| macOS | exact/broad/100000/3/files             |    1.0092 |    1.0003 |      1.0052 |      0.9987 |
| macOS | exact/eager/broad/10000/3/csv          |    0.9976 |    0.9963 |      0.9961 |      1.0011 |
| macOS | exact/eager/deep/10000/3/csv           |    0.9886 |    0.9950 |      0.9974 |      1.0016 |
| macOS | default-native/broad/100000/1/name-txt |    0.9826 |    0.9937 |      0.9852 |      0.9994 |
| macOS | long/100000/1/eager-summary            |    0.9983 |    0.9820 |      0.9840 |      0.9980 |
| macOS | long/100000/3/eager-summary            |    1.0045 |    0.9924 |      0.9937 |      1.0034 |
| Linux | exact/broad/10/1/files                 |    0.9954 |    0.9944 |      0.9939 |      1.0045 |
| Linux | exact/broad/1000/1/files               |    0.9854 |    0.9887 |      1.0066 |      0.9847 |
| Linux | exact/broad/100000/3/files             |    0.9975 |    0.9972 |      0.9995 |      0.9955 |
| Linux | exact/eager/broad/10000/3/csv          |    0.9540 |    1.0225 |      1.0074 |      1.0315 |
| Linux | exact/eager/deep/10000/3/csv           |    0.9853 |    0.9994 |      0.9970 |      0.9948 |
| Linux | default-native/broad/100000/1/name-txt |    1.0010 |    0.9998 |      1.0003 |      0.9986 |
| Linux | long/100000/1/eager-summary            |    1.0110 |    0.9955 |      0.9960 |      1.0067 |
| Linux | long/100000/3/eager-summary            |    1.0100 |    1.0025 |      0.9897 |      1.0169 |

The 256-byte policy falls back for the long-component fixture: Linux long eager cases cost about
1% in the screen and 1.2-2.9% in confirmation, although the latter three-worker copy also costs
2.18%. Larger storage does not show a consistent advantage beyond 512 bytes. Select 512 rather
than adding a configurable size or increasing the stack buffer without evidence.

## Confirmation and controls

| Host  | Case                                   | Parent ms | 512-byte ms | 256-byte ratio | 512-byte ratio | Parent-copy |
| :---- | :------------------------------------- | --------: | ----------: | -------------: | -------------: | ----------: |
| macOS | exact/broad/10/1/files                 |     4.368 |       4.385 |         1.0065 |         0.9998 |      1.0054 |
| macOS | exact/broad/100000/3/files             |    72.013 |      72.037 |         1.0011 |         1.0003 |      0.9999 |
| macOS | exact/eager/broad/10000/3/csv          |    19.591 |      19.504 |         0.9995 |         0.9949 |      1.0018 |
| macOS | exact/eager/deep/10000/3/csv           |    18.295 |      18.183 |         0.9979 |         0.9930 |      1.0086 |
| macOS | default-native/broad/100000/1/name-txt |   249.787 |     246.105 |         0.9883 |         0.9841 |      0.9968 |
| macOS | default-native/broad/100000/3/name-txt |   172.481 |     169.966 |         0.9902 |         0.9932 |      0.9916 |
| macOS | long/100000/1/native-name              |   547.757 |     542.634 |         0.9943 |         0.9915 |      1.0001 |
| macOS | long/100000/1/eager-summary            |   382.582 |     377.767 |         1.0012 |         0.9880 |      1.0009 |
| macOS | long/100000/3/native-name              |   412.678 |     411.711 |         1.0023 |         0.9996 |      1.0083 |
| macOS | long/100000/3/eager-summary            |   245.942 |     242.655 |         0.9966 |         0.9856 |      0.9998 |
| macOS | exact/eager/broad/100000/1/csv         |   236.600 |     233.211 |         0.9855 |         0.9854 |      1.0012 |
| macOS | exact/eager/broad/100000/3/csv         |   155.705 |     154.814 |         0.9921 |         0.9943 |      1.0033 |
| Linux | exact/broad/10/1/files                 |     0.848 |       0.840 |         0.9951 |         0.9910 |      1.0141 |
| Linux | exact/broad/100000/3/files             |    40.971 |      40.896 |         0.9950 |         0.9964 |      0.9959 |
| Linux | exact/eager/broad/10000/3/csv          |     6.447 |       6.043 |         0.9769 |         0.9972 |      0.9946 |
| Linux | exact/eager/deep/10000/3/csv           |     4.333 |       4.390 |         0.9718 |         0.9928 |      1.0009 |
| Linux | default-native/broad/100000/1/name-txt |    40.011 |      40.055 |         1.0035 |         1.0012 |      1.0020 |
| Linux | default-native/broad/100000/3/name-txt |    40.150 |      40.099 |         1.0005 |         0.9987 |      1.0024 |
| Linux | long/100000/1/native-name              |    63.543 |      63.375 |         1.0013 |         0.9974 |      1.0044 |
| Linux | long/100000/1/eager-summary            |   157.807 |     157.071 |         1.0117 |         0.9953 |      1.0055 |
| Linux | long/100000/3/native-name              |    63.054 |      63.942 |         0.9980 |         1.0051 |      1.0036 |
| Linux | long/100000/3/eager-summary            |   108.528 |     107.806 |         1.0291 |         0.9925 |      1.0218 |
| Linux | exact/eager/broad/100000/1/csv         |   123.388 |     122.855 |         1.0073 |         0.9935 |      1.0123 |
| Linux | exact/eager/broad/100000/3/csv         |    83.949 |      82.964 |         1.0126 |         0.9857 |      1.0118 |

macOS long eager one-worker gain changes from 1.80% to 1.20%; three-worker gain changes from
0.76% to 1.44%. The added 100k eager summaries improve 1.46%/0.57% on macOS and 0.65%/1.43% on
Linux. Linux native-name controls stay mostly neutral because their existing uniform-case policy
avoids eager leaf stat. Metadata-free controls likewise perform little work in this adapter.

Retain slower cells: Linux broad 10k eager CSV costs 2.25% in the screen (copy +3.15%) and gains
0.28% in confirmation (copy -0.54%); do not call its initial cost a proven regression or silently
drop it. Linux long native names with three workers costs 0.51% in confirmation (copy +0.36%).
macOS native-case three-worker gain is 0.68%, while its parent copy gains 0.84%. These observations
limit causal claims for small differences. This is a modest local improvement, not a solution to
the remaining eager syscall cost. Native high-water RSS is retained in raw observations, includes
pre-exec footprint and is not an allocation count; no peak-memory reduction is claimed.

## Footprint and verification

With the same enabled extras, the Mac release CLI remains 8,250,504 bytes; Linux changes from
18,401,176 to 18,401,048 bytes (-128). The final Linux rebuild differs from its frozen measured
512-byte executable only in debug-line metadata and dependent build identity; every allocated
runtime section is exact. The Mac runtime instructions, data and resolved symbols are exact after
sandbox debug-path and dependent symbol/signature normalization. Both original signatures validate.
No binary is modified to produce a matching fingerprint. Final edits only document/suppress the
verified terminated-view warning and preserve source line counts; final source and binary proofs
are retained before publication.

All 316 enabled release test targets pass on both hosts, including required Linux FUSE tests.
Eight affected targets pass ASan on both hosts and MSan/TSan on Linux. Actual scoped Linux lint
passes the implementation and test; the final runtime annotation is checked again. Normal Mac
signed-commit/push hooks run changed-file lint and repository policy; remote current-head CI remains
the merge gate. The lossless companion JSON.gz retains every policy, source stage, raw sample,
control, exact-output hash, driver, qualification diagnostic and strict binary proof. The initial
invalid formatting of a generated policy literal is retained as a failed build diagnostic, never
counted as a timing observation. Fixtures, full native stdout goldens and frozen binaries remain local
and reproducible from the recorded generators and fingerprints. No sanitizer timing is used.
