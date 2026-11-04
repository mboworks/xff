<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Uniform native-case capability

Keep the explicit capability. It removes unnecessary per-file metadata reads for native-case
name matching on Linux without changing its existing case policy. Broad 100,000-file selected-name
commands improve from about 129 ms to 44 ms with one worker and 87 ms to 44 ms with three workers.
The macOS implementation keeps volume-specific queries and is effectively unchanged. This does
not make all commands faster: exact-case and eager-metadata controls retain small costs below.

## Contract and scope

`FileSystem::UniformCaseSensitivity()` returns an optional answer. A present value guarantees the
answer `IsCaseSensitive` would give for every path of that backend instance. It is never inferred
by sampling a root, device, path or volume. The default is unknown. LocalFs advertises the existing
constant case-sensitive fallback only on platforms without `_PC_CASE_SENSITIVE`; macOS/BSD keep
unknown and their current per-volume probing. Linux per-directory case-folding detection is not
added here: its existing conservative policy is unchanged.

The safety wrapper forwards the capability. The rg stdin adapter forwards it only when compatible
with stdin's explicitly case-sensitive rule: an insensitive host plus stdin is mixed, so reports
unknown. A sensitive host remains uniform. Custom and archive backends retain unknown by default.
Adding this virtual method requires rebuilding extensions with the current source interface.

Only expressions needing filesystem-native case consult the capability, once per run. An explicit
`--exact` does not consult it. A known answer removes native-case matching's demand for complete
metadata; other consumers still determine Never/OnDemand/Always demand as before. Directory,
unknown-type, followed-link, size, summary, color and deferred requirements remain in force.
Known-sensitive and known-insensitive answers both avoid the device cache and per-path case query.
Unknown backends keep the original per-device cache and conservative exact fallback on query error.
The uniform fold decision is computed once per run. Ordinary and deferred evaluation reuse it;
the existing unknown-backend lookup/cache code remains unchanged.

This preserves matching and required metadata errors. Like existing metadata-free traversal,
a known type hint need not be restatted merely to discover an otherwise unused leaf-stat failure.
A predicate requiring metadata still loads it and reports its failure. There is no new lock,
worker policy, public flag, output format, dependency, host-I/O path or encoding rule. Matcher-worker
admission remains unchanged; these gains are from admission and case queries, not new parallelism.

## Measurements

The parent is the native names-only listing implementation. Each host has 92 cases, three
balanced sessions, four participants (parent, invariant-hoisted candidate, earlier per-visit candidate,
identical parent copy), one discarded
warmup and nine measured invocations per participant/case/session. Retain the fastest seven
per session and report the median of session ratios: 11,040 invocations per host. Every invocation
checks exit status and complete stdout against the parent. An initial 16-case pilot, the earlier 92-case three-participant matrix (8,280 invocations per host),
and a separate eight-case Linux confirmation remain in the raw evidence, including slower observations.

The matrix covers broad and nested 10/100/1k/10k/100k fixtures, one/three requested workers,
absolute/relative roots, selective names, files-only output, on-demand size, eager summaries,
CSV/JSON-lines/NUL/Markdown and 16 explicit-exact controls. The fixture called `deep` has two
nested component levels (`i % 8`, `i % 31`); it is not a long directory chain. Fixtures are the
same 222,220 zero-byte paths verified in the parent report. Linux pins invocations to one/three
allowed logical CPUs. macOS CPU pinning is not claimed. Sessions interleave participants; no
same-host build, test or lint runs during timing. Ratios are candidate / parent; below one is faster.
Parent-copy drift is retained, not normalized away.

| Host  | Case                                   | Workers | Parent ms | Candidate ms |  Ratio | Parent-copy ratio |
| :---- | :------------------------------------- | ------: | --------: | -----------: | -----: | ----------------: |
| macos | native/broad/100000/name-txt           |       1 |   253.728 |      255.003 | 1.0048 |            1.0017 |
| macos | native/broad/100000/name-txt           |       3 |   181.081 |      180.941 | 0.9964 |            0.9987 |
| macos | native/deep/100000/name-txt            |       1 |   243.384 |      243.017 | 1.0016 |            1.0020 |
| macos | native/relative/broad/100000/name-txt  |       1 |   221.760 |      221.410 | 0.9984 |            1.0006 |
| macos | native/on-demand/broad/10000/name-size |       3 |    21.521 |       21.253 | 0.9876 |            0.9917 |
| macos | native/eager/broad/10000/csv           |       3 |    20.051 |       19.968 | 0.9966 |            0.9941 |
| macos | control/broad/100000/name-txt          |       1 |    71.349 |       71.426 | 0.9972 |            0.9976 |
| linux | native/broad/100000/name-txt           |       1 |   128.956 |       43.756 | 0.3403 |            1.0058 |
| linux | native/broad/100000/name-txt           |       3 |    87.491 |       43.704 | 0.4997 |            1.0208 |
| linux | native/deep/100000/name-txt            |       1 |   106.144 |       39.573 | 0.3728 |            1.0010 |
| linux | native/relative/broad/100000/name-txt  |       1 |   112.406 |       43.477 | 0.3868 |            1.0017 |
| linux | native/on-demand/broad/10000/name-size |       3 |     6.912 |        3.818 | 0.5496 |            1.0207 |
| linux | native/eager/broad/10000/csv           |       3 |     6.206 |        5.853 | 0.9448 |            0.9820 |
| linux | control/broad/100000/name-txt          |       1 |    44.320 |       44.375 | 1.0012 |            1.0023 |

Final slowest controls:

- macos: `native/eager/broad/10000/1/csv`, ratio 1.0093; identical-parent ratio 1.0063.
- linux: `native/relative/broad/10/1/files`, ratio 1.0157; identical-parent ratio 1.0089.

The earlier per-visit implementation's expanded Linux run had an eager CSV control 4.53% slower
(about 0.28 ms), compared with -0.34% for its parent copy. A separate balanced confirmation was
1.22% slower (about 0.07 ms), with -0.15% copy drift. Retain both observations and the earlier
implementation in the final matrix; do not attribute every difference between runs to the invariant
hoist. The final Linux eager CSV control is faster in this run. Acceptance retains small slower
controls against large default-native-case gains; it does not claim every command improves.

A separate Linux syscall-count diagnostic over broad 10k selective-name files records 10,015
stat-family calls for the parent and 15 for the earlier candidate: exactly 10,000 fewer. Both include
the same four startup errors. This confirms avoided leaf metadata work; strace timing is not used
as a speed measurement. The diagnostic precedes the invariant-only hoist, whose metadata admission
is identical.

## Binary footprint

The measured full CLI grows from 8,246,712 to 8,247,352 bytes on macOS
(+640 bytes), and from 18,357,984 to 18,359,008 bytes on Linux
(+1,024 bytes). These are the retained timed executables,
not inferred sizes from an unrelated CI artifact.

## Validation and retained evidence

The complete 316-target release suites passed on both hosts before the final test-only additions.
The focused suite includes capability/default behavior, sensitive/insensitive/unknown backends,
explicit exact bypass, required metadata, errors, deferred replay, stdin mixing and platform policy.
Eight relevant targets passed ASan on both hosts and MSan/TSan on Linux. Scoped clang-tidy
checked 23 affected translation units per host; a Mac-only unused matcher import was corrected
and its file rechecked together with the API default-capability assertion. Release and sanitizer
regressions cover both final test files. The canonical database contains 320 expected first-party
sources with none missing. The exact release-site build and its unit test pass on Mac.
After the invariant hoist, the three affected engine targets pass applicable sanitizers again;
Linux rechecks the changed runtime translation unit and focused coverage. The normal Mac commit
hook performs final scoped lint on the exact complete change.
Development checks do not replace the resulting PR's own current-head CI.

Both final rebuilt CLIs are byte-identical to their final timed executables. An earlier per-visit
Mac rebuild differed only in sandbox numeric IDs in debug/object names and dependent Mach-O
metadata: its strict parser proves equal runtime code/data and normalized resolved symbols,
with both original signatures valid. Retain that earlier identity failure and proof separately;
no binary was modified to make a hash match.

The lossless companion JSON.gz retains source stages, drivers, raw pilot/full/confirmation samples,
complete-output hashes, fixture identity, qualification logs and binary fingerprints. The timed
binaries and full golden-output files remain in the local evidence directories. This report makes
no claim about an unmeasured filesystem, persistent CPU pinning on macOS, physical storage traffic,
comparison streaming or a later representation change. Compact traversal metadata, optional bulk
stat, first-device cache allocation and coordinated pipeline work remain separate candidates.

Artifact: `uniform-native-case-20261104.json.gz`, 3386460 bytes,
SHA-256 `1f9f4eea1d18bb14af7d4349d34a5ec24a8c1740cbf50b8127a7bd6853c34742`.
