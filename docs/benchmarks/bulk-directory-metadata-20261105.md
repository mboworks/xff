<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Optional bulk directory metadata

Use complete directory metadata when the backend can supply it more cheaply than separate stats.
The final Mac matrix improves 100k-file serial CSV workloads about 34% and the deep-tree
three-worker case about 43%. Linux retains ordinary native listing/stat operations. Keep the
narrow costs visible: Mac serial all-symlink CSV costs about 10% in final confirmation and up to 18% in the prior screen, and a wide 100k-file
serial regular listing uses about 9.5 MiB more peak RSS. This is a workload tradeoff, not a
universal traversal improvement.

## Implementation and boundaries

`FileSystem` has an optional directory-metadata capability and an owning result with aligned
entry/observation arrays. Unsupported backends default to false. An empty observation array
requests ordinary stats; an aligned failed observation also requests ordinary stat. A hint failure
never replaces the eventual user-visible stat result. Transfer complete records into the sorted
batch before retaining references into them. No per-file shared owner, lock, timer, new thread,
flag or dependency is added.

The native Mac adapter uses bounded 64 KiB uninitialized stack scratch and read-only directory
handles. Only complete regular-file observations replace `lstat`. Directories, mount/firmlink-
sensitive paths, links, other types, missing fields and failed observations retain ordinary stats.
Followed links keep the normal followed stat. Malformed, unsupported or failed bulk reads discard
the entire partial result and reopen ordinary enumeration from the beginning. Embedded-NUL
directory spellings keep the ordinary path: native truncation changes the meaning of joined child
paths, so attaching metadata for a different spelling would be incorrect.

The pure bounded decoder validates record length/alignment, name offsets/ranges/terminators,
embedded NUL/slashes, returned masks, timestamp nanoseconds, signed sizes and allocation units.
SDK static assertions check Darwin widths and constants. Logical size comes from the data fork;
allocated blocks include all forks and use stat's 512-byte units. Native equivalence tests cover
ordinary, empty, sparse, resource-fork and hardlinked files. Directories omit file-attribute slots
even with packed invalid attributes; their metadata stays on the ordinary stat path.

A backend preference receives the stat concurrency already available at that call site. Mac bulk
metadata is selected for serial stats and independent directory-read workers. The coordinator keeps
parallel stats when it has multiple workers. This avoids serializing wide directories that already
benefit from the existing stat pool. Cheap other backends can still accept either worker hint.
Missing observation batches retain ordinary chunk admission; directory-read workers remain leaves
and never submit/wait for work in their own pool. Metadata-never/on-demand traversal does not ask
for bulk capability. Safety, rg input and read-observer wrappers forward capability and fields;
content-read counters remain unchanged. Cached read metadata does not authorize mutations: the
central mutation checks still run on fresh controlled operations.

The selected final refinement delays allocating observation storage until the first complete
record. Earlier entries are backfilled with missing statuses at that point; entirely ineligible
directories retain only names. This saves about 1 MiB in the 10k-symlink screen without a repeatable
throughput gain there. Keep ordinary symlink semantics rather than accepting incomplete metadata
to make a benchmark faster.

Measurements use cached local APFS/ext4 trees. No network-filesystem speedup is claimed. An
unsupported or incomplete native filesystem can pay for an attempted bulk read before ordinary
stats; its conservative fallback preserves results rather than promising free capability discovery.

## Measurement and provenance

The exact parent runtime is #1098, `fc31e2d4e43e572154aa4aa83617ae2a7b25869d`.
The publication parent is #1099, `63769a6f2dd0f4afc1b7abc64800c2d2a0580463`; its two
cache-tool Python files do not alter runtime sources and the scoped cache test passes on both hosts. Frozen parent,
candidate and identical-parent copy preserve sizes and SHA-256. Three sessions rotate participant
order for every repetition. Each case has one discarded warmup, nine measurements and a mean of
the fastest seven. Report medians of within-session candidate/parent ratios; independently medianed
milliseconds need not divide to that ratio. Every native invocation checks exit status and complete
stdout against the parent outside timing. Parent-copy drift remains visible, without correction.
Linux pins one/three allowed logical CPUs; Mac counts describe workers and do not claim CPU pinning.
Builds, tests and lint do not compete with timings on the same host. Public sync hooks can pause
between cases/processes through the shared guard.

Four policies and all rejected observations are retained:

| Policy                         | Native cases per host |         In-memory cases per host | Participants | Decision                                      |
| :----------------------------- | --------------------: | -------------------------------: | -----------: | :-------------------------------------------- |
| Copy sidecar                   |                    80 |                              160 |            3 | Reject wide parallel and ownership costs      |
| Transfer owner                 |                    14 |                               32 |            4 | Retain ownership; reject unchanged scheduling |
| Backend stat-worker preference |                    88 |                              160 |            3 | Retain scheduling; expose nonregular costs    |
| Delayed sidecar allocation     |             14 on Mac | Reuse preceding generic controls |            4 | Retain lower nonregular memory                |

Each case repeats across three sessions. The first two policies use 7,200 + 1,680 native
invocations per host, including warmups; the third uses 7,920, and the final Mac screen adds 1,680.
The generic in-memory backend does not advertise bulk metadata. Its 160 cases measure fallback
and scheduling overhead across broad/deep trees, eager/hint demand, full/name-only records,
selection, 10/100/1k/10k/100k entries and one/three workers. Nine random-interleaved repetitions
retain seven per process/session; calibration is not a discarded native warmup. The final
allocation refinement changes only the native Mac adapter, not generic traversal code.

Linux's 80 regular native cases completed before the eight nonregular fixtures were created, so
those eight run separately against the same frozen binaries. Fixture creation overlapped the first
Linux memory matrix. Preserve that matrix as ineligible for acceptance and use the clean repeat;
its protocol note records the reason, driver/fixture hashes and completed replacement. Assert actual
case cardinality rather than labeling an 80-case result as 88. Sources and qualified binaries did
not change during that correction.

## Selected Mac confirmation

This table uses the four-participant final screen, including the preceding scheduling policy.
The previous policy column retains it as an in-session control.

| Case                          | Parent ms | Selected ms | Selected / parent | Previous / parent | Parent-copy / parent |
| :---------------------------- | --------: | ----------: | ----------------: | ----------------: | -------------------: |
| broad/10/1/files              |     4.403 |       4.423 |            1.0041 |            1.0000 |               1.0085 |
| broad/100000/1/eager-csv      |   243.676 |     167.542 |            0.6899 |            0.6892 |               0.9987 |
| broad/100000/3/eager-csv      |   169.727 |     169.698 |            0.9988 |            0.9992 |               0.9967 |
| deep/100/1/files              |     6.043 |       6.076 |            1.0028 |            1.0020 |               0.9979 |
| deep/100000/1/eager-csv       |   233.596 |     158.689 |            0.6831 |            0.6862 |               1.0069 |
| deep/100000/3/eager-csv       |   112.145 |      66.121 |            0.5896 |            0.5848 |               0.9994 |
| directories/1000/1/eager-csv  |    18.975 |      18.665 |            0.9810 |            0.9793 |               1.0022 |
| directories/1000/3/eager-csv  |    16.005 |      15.561 |            0.9722 |            0.9589 |               1.0017 |
| directories/10000/1/eager-csv |   177.228 |     172.586 |            0.9738 |            0.9812 |               0.9999 |
| directories/10000/3/eager-csv |   121.949 |     117.292 |            0.9619 |            0.9597 |               0.9997 |
| symlinks/1000/1/eager-csv     |     7.618 |       8.127 |            1.0648 |            1.0567 |               0.9945 |
| symlinks/1000/3/eager-csv     |     7.589 |       7.637 |            0.9993 |            0.9953 |               1.0048 |
| symlinks/10000/1/eager-csv    |    39.695 |      46.998 |            1.1738 |            1.1841 |               0.9982 |
| symlinks/10000/3/eager-csv    |    35.335 |      35.506 |            1.0037 |            1.0019 |               0.9988 |

The prior full Mac matrix showed about 30-36% gains in large serial metadata/name workloads and
40-44% for deep three-worker trees. Broad 10k/100k three-worker metadata cases were within about
0.1-1.2% of the parent after preserving parallel stats. Metadata-free native controls were mostly
within 1%; they do not benefit from bulk calls. The final screen preserves the large gains and
has about 0.3-0.4% costs on its two small native controls.

## Final complete native confirmation

After qualification, both final binaries run all 88 native cases and all 160 in-memory controls.
The final immutable drivers check their own hashes, production source hashes and actual case
cardinality. These fresh runs replace any assumption that a changed release hash was harmless.
The earlier matrices remain separate policy evidence. The additional native confirmation contributes
7,920 invocations per host, including warmups; final complete virtual controls add 12,960 iteration
records per host. Across four policies, retain 26,400 native invocations on Mac and 24,720 on Linux,
plus the explicitly ineligible earlier Linux memory matrix.

| Mac case                      | Parent ms | Final ms | Final / parent | Parent-copy / parent |
| :---------------------------- | --------: | -------: | -------------: | -------------------: |
| broad/10/1/files              |     4.287 |    4.309 |         1.0050 |               0.9988 |
| broad/100000/1/eager-csv      |   246.057 |  162.266 |         0.6607 |               1.0017 |
| broad/100000/1/eager-summary  |   230.222 |  141.707 |         0.6163 |               0.9938 |
| broad/100000/3/eager-csv      |   168.845 |  169.249 |         1.0027 |               1.0001 |
| deep/100000/1/eager-csv       |   233.724 |  153.233 |         0.6556 |               1.0035 |
| deep/100000/3/eager-csv       |   111.473 |   63.301 |         0.5682 |               0.9970 |
| deep/100000/3/eager-summary   |   110.029 |   62.138 |         0.5652 |               0.9961 |
| directories/10000/1/eager-csv |   174.328 |  169.093 |         0.9698 |               1.0003 |
| symlinks/1000/1/eager-csv     |     7.395 |    7.710 |         1.0409 |               1.0052 |
| symlinks/10000/1/eager-csv    |    41.673 |   45.538 |         1.0952 |               1.0006 |

Wide three-worker metadata remains near the parent (+0.27% CSV, +0.22% summary), while deep
three-worker metadata improves about 43%. The final serial symlink-only case costs 9.52% at 10k
and 4.09% at 1k; retain the earlier 11-18% readings as well. Do not report only the fastest trial.
The final generic Mac controls include a +10.77% cell with +21.69% identical-parent-copy drift,
and several roughly +5% three-worker cells with +3-7% copy drift. Those process/session shifts
limit attribution. The earlier +9.39% serial 1k cell remains an independent slower observation.

## Costs, rejected policies and fallback controls

Copy-sidecar accumulation increased broad 100k peak RSS from about 37 to 58 MiB. Ownership transfer
reduced it to about 46 MiB, but wide parallel metadata still regressed 6.6-7.7% at 10k files.
The backend's stat-worker preference fixes that scheduling cost without a time/count heuristic.
The remaining serial wide regular-file footprint is explicit: parent about 36.7 MiB, selected
46.1 MiB; three-worker wide and deep cases stay much closer to the parent. Avoid blindly enlarging
worker stack buffers. Segmented ownership through the public result is a separate possible follow-up.

Mac symlink-only serial CSV remains slower: the full matrix costs 5.46% at 1k and 10.72% at 10k;
the simultaneous final screen measures 6.48%/17.38%, with the preceding policy also slower. The
allocation refinement lowers 10k-symlink RSS from about 9.23 to 8.33 MiB (parent 8.16 MiB), but
it does not remove bulk-read overhead. Three-worker symlink controls remain near neutral because
the coordinator keeps ordinary stats. Directory-only controls improve about 2-4% on Mac. A future
planner could avoid bulk reads when it can prove only ineligible types are needed; do not infer
that from arbitrary expression spellings or a timer threshold.

Generic Mac controls also have costs: broad eager/name-only 1k/one-worker traversal is 9.39% slower
(about 0.268 ms parent versus 0.289 ms candidate, paired sessions +2.21/+11.19/+9.39%; parent-copy
median -0.46%). Wide 100k metadata-free name-only controls cost about 2.6-2.8%. Keep these cells;
native syscall savings do not explain them. Most large eager virtual controls are near neutral.
The clean Linux memory matrix's largest slower cell is deep eager/name-only 100/one worker,
+4.15% with a +2.10% parent-copy control. Other listed slower cells are about 1.6-2.2%; retain
per-session variation rather than declaring all virtual controls free of overhead.

## Linux ordinary native controls

Linux declines native bulk metadata. These are actual ordinary-path controls, including the
separately completed nonregular cases. A copy shift is not silently attributed to the implementation.

| Case                          | Parent ms | Candidate ms | Candidate / parent | Parent-copy / parent |
| :---------------------------- | --------: | -----------: | -----------------: | -------------------: |
| broad/10/1/files              |     0.809 |        0.812 |             1.0039 |               1.0051 |
| broad/100000/1/eager-csv      |   131.997 |      130.393 |             0.9878 |               0.9867 |
| broad/100000/3/eager-csv      |    91.640 |       90.357 |             0.9882 |               0.9850 |
| broad/100000/3/eager-summary  |    84.912 |       83.627 |             0.9831 |               0.9800 |
| deep/100000/1/eager-csv       |   112.794 |      113.437 |             1.0057 |               0.9986 |
| deep/100000/3/eager-csv       |    45.111 |       45.202 |             1.0020 |               0.9953 |
| directories/1000/1/eager-csv  |     2.355 |        2.365 |             1.0041 |               1.0006 |
| directories/1000/3/eager-csv  |     2.817 |        2.758 |             0.9695 |               1.0226 |
| directories/10000/1/eager-csv |    18.521 |       18.562 |             1.0019 |               0.9998 |
| directories/10000/3/eager-csv |    18.906 |       19.391 |             1.0012 |               0.9971 |
| symlinks/1000/1/eager-csv     |     1.808 |        1.801 |             1.0106 |               1.0004 |
| symlinks/1000/3/eager-csv     |     2.039 |        2.052 |             1.0065 |               1.0070 |
| symlinks/10000/1/eager-csv    |    12.223 |       12.207 |             1.0040 |               0.9974 |
| symlinks/10000/3/eager-csv    |    11.434 |       11.549 |             1.0100 |               0.9728 |

The final full Linux confirmation is mostly near neutral. Its 100k deep serial summary is +1.47%
with a +0.05% copy control; broad metadata shifts of about -1.2 to -1.7% accompany similar parent-copy
shifts. The final virtual matrix retains a +4.82% deep/eager/name-only 100/one-worker cell with
+0.30% copy drift, and a +4.82% deep name-only 10k/three-worker cell with +2.28% copy drift.
Do not substitute the earlier virtual matrix or describe every fallback workload as unchanged.

## Footprint

| Host  | Parent bytes | Final bytes |  Change |
| :---- | -----------: | ----------: | ------: |
| Mac   |    8,250,616 |   8,273,096 | +22,480 |
| Linux |   18,404,960 |  18,431,008 | +26,048 |

The final Mac release is byte-identical to its frozen measurements. The final Linux release is
measured afresh after a strict ELF comparison rejected a read-only-data difference; that check
was not weakened. Original qualified binaries and their SHA-256 remain intact. The final extra
fallback regression changes only its test translation unit and checks that production sources and
qualified release files stay unchanged.

## Validation and retained evidence

Final focused added-code coverage is 230/232 lines (99.14%) and 137/156 branches
(87.82%), before canonical exclusions. The extra empty-sidecar regression checks serial/parallel
stat policy, requested fields, missing/permission errors and visitor output; its release/sanitizer
and actual single-TU lint checks pass on both hosts. Production source hashes and both qualified
release files remain unchanged by that test-only addition.

The scheduling policy passes all 329 release targets on both hosts, including all nine extension
packages and the API package, plus eight focused ASan targets on each host. MSan/TSan and focused
coverage pass Linux. Actual clang-tidy passes all 14 changed C++ translation units on both hosts.
Known lint findings were collected without canceling healthy checks, corrected once and requalified.
The final two-file allocation refinement also passes all 329 Mac release targets, eight focused
ASan targets and its two actual TUs. The final qualified Mac release is bit-identical to the frozen
measured candidate. The final Linux allocation policy also passes all 329 release targets, eight
focused ASan/MSan/TSan targets and both changed actual TUs. Its final native/memory matrices use
the exact qualified release binary: the strict ELF identity check found a changed read-only-data
section, so preceding frozen timings are not substituted for the final confirmation. Unchanged actual lint
results stay tied to exact source hashes rather than being repeatedly run.

Linux coverage exercises the portable decoder and generic observation behavior; it cannot instrument
the native Apple adapter. Full field equivalence, resource-fork allocation and native fallback tests
run on Mac. Keep that boundary explicit rather than claiming universal adapter coverage.

The accompanying `bulk-directory-metadata-20261105.json.gz` retains source snapshots, build/verification/lint logs,
all native and virtual observations, rejected policies, binary/driver identities, output hashes,
protocol corrections and raw LCOV. Frozen executables and generated fixture trees remain outside
the repository; fixture drivers and output hashes make their construction auditable.

The ABI comes from Apple's [getattrlistbulk manual](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/bsd/man/man2/getattrlistbulk.2),
[getattrlist manual](https://raw.githubusercontent.com/apple-oss-distributions/xnu/main/bsd/man/man2/getattrlist.2)
and SDK definitions. No third-party implementation is copied and no dependency or license is added.

The deterministic lossless archive contains 958 text records, 16,913,757 compressed bytes
(173,242,396 uncompressed bytes). Its SHA-256 is
`f2db159596ab015c3c76cedb4dd029651c76a14ada9f8002c29eabf834012861`. Every embedded text record
was checked against its original byte count and SHA-256 before packaging.
