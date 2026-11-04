<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Reuse field contexts and transfer owned scalar results

Render against the caller's existing content snapshot without copying its complete rendering context.
When a template is exactly one untransformed field, return its owning string directly or copy its
borrowed view into the returned string. Keep the existing composite and transformed-field paths.

## Native decision

Ratios are candidate elapsed time divided by its paired baseline; below 1 improves. Each case uses
the median of three session ratios. A session retains the mean fastest seven of nine interleaved
measured invocations after one discarded warmup per participant. Cohorts take medians across cases,
without pooling hosts. The identical-baseline executable estimates variation.

| Host  | Policy              | >=1,000-file columns | 10-file columns | Raw/NUL/template/action controls | 10,000 files, 16 columns |
| :---- | :------------------ | -------------------: | --------------: | -------------------------------: | -----------------------: |
| macos | context_ref         |               0.9946 |          0.9988 |                           0.9971 |                   0.9826 |
| macos | context_ref_new_mbo |               0.9957 |          0.9973 |                           0.9990 |                   0.9839 |
| macos | combined_plain      |               0.9878 |          0.9988 |                           0.9959 |                   0.9468 |
| macos | baseline_repeat     |               0.9975 |          0.9949 |                           0.9987 |                   0.9999 |
| linux | context_ref         |               0.9923 |          1.0032 |                           0.9967 |                   0.9908 |
| linux | context_ref_new_mbo |               0.9960 |          1.0046 |                           1.0019 |                   0.9914 |
| linux | combined_plain      |               0.9855 |          1.0055 |                           1.0024 |                   0.9610 |
| linux | baseline_repeat     |               1.0009 |          1.0073 |                           1.0023 |                   0.9986 |

Select `combined_plain`: larger column cases improve about 1.2% macOS and 1.4% Linux at the median,
and 10,000-file, 16-column cases improve about 5.3% and 3.9%. Small Linux cases are about 0.6%
slower while the identical-baseline control is about 0.7% slower; keep both measurements.
Control ratios alone do not establish an implementation effect on paths the optimization never runs.

Supplemental composite templates contain either seven ordinary fields or six component-qualified
fields. Both retain all original work. Results below are median per-case three-session ratios.

| Host  | Policy              | Composite fields | Component qualifiers |
| :---- | :------------------ | ---------------: | -------------------: |
| macos | context_ref         |           1.0030 |               0.9933 |
| macos | context_ref_new_mbo |           0.9984 |               0.9961 |
| macos | combined_plain      |           0.9985 |               0.9970 |
| macos | baseline_repeat     |           0.9972 |               0.9946 |
| linux | context_ref         |           0.9899 |               0.9839 |
| linux | context_ref_new_mbo |           0.9898 |               0.9906 |
| linux | combined_plain      |           1.0008 |               0.9865 |
| linux | baseline_repeat     |           1.0057 |               1.0026 |

## Mechanisms, ownership and tradeoffs

- `baseline`: original dependency and renderer.
- `context_ref`: skip the context copy when a snapshot is supplied; original dependency.
- `context_ref_new_mbo`: the same renderer with the merged MBO revision, isolating dependency effects.
- `combined_plain`: context reuse plus the owning scalar-result path.
- `baseline_repeat`: an exact byte-for-byte executable copy, not another implementation.

Normal CLI coordinators already supply a snapshot. Direct callers without one still create a local
snapshot and copied context, preserving repeated hash reads and all fallback lifetimes. There is
no retained caller reference: every returned value remains an owning `std::string`. A borrowed
hash result is copied before a local snapshot dies; an owning string transfers through the new
rvalue `StringOrView::str()` overload. Rewrites, extraction and component transforms keep their
existing implementation. No new lock, thread, allocator wrapper, flag or output byte is introduced.

| Host  | Policy              | Supplied snapshot kernels | Local fallback kernels |
| :---- | :------------------ | ------------------------: | ---------------------: |
| macos | context_ref         |                    0.9592 |                 1.0016 |
| macos | context_ref_new_mbo |                    0.9613 |                 1.0044 |
| macos | combined_plain      |                    0.9591 |                 1.0004 |
| macos | baseline_repeat     |                    1.0023 |                 1.0061 |
| linux | context_ref         |                    0.9519 |                 1.0229 |
| linux | context_ref_new_mbo |                    0.9477 |                 1.0307 |
| linux | combined_plain      |                    0.9669 |                 1.0266 |
| linux | baseline_repeat     |                    1.0002 |                 1.0001 |

The combined fallback kernel median is about 2.7% slower on Linux. This is a real retained tradeoff
for direct library callers; do not describe every rendering path as faster. Earlier unconditional
single-field transform returns (`single_all`) had worse composite costs and are not selected.
All owning-only policies, context-only screening runs, and slower primitive cells remain in evidence.

## Protocol and provenance

The final matrix has 144 in-memory cases: twelve templates, supplied/local snapshots,
10/1,000/100,000 renders, and short/long paths. Each host measures five participants in three
sessions with nine raw rounds, retaining fastest seven: 2,160 case/session/participant records
per host, 4,320 total and 38,880 raw rounds. Context preparation and timezone selection occur
outside the timed render loop. The kernel consumes results through `DoNotOptimize`; correctness is
covered by committed tests and the independent native output oracles, not a kernel golden check.

Native matrices have 82 cases per host, five participants, three sessions and ten invocations
including the discarded warmup: 12,300 per host, 24,600 total. Twelve supplemental composite
cases add 1,800 per host, giving 28,200 final native invocations. Every invocation verifies full
stdout and exit status against independent Python path/CSV/TSV/template oracles. Linux uses
recorded CPU affinity and `/dev/shm` fixtures. macOS uses local fixtures without claiming affinity.
Do not infer release performance from sanitizer runs. No same-host build, test, lint, fuzz or
heavy fixture generation overlaps the final timing windows.

The earlier owning-only experiment also retains 28,200 native invocations, its 72-case kernels,
all rejected variants, and a macOS background-formatter event. Its potentially overlapping eight-case
batch was repeated cleanly for every participant in three sessions; original and confirmation data
are both preserved. Initial Linux context-only screening retains another 7,380 native invocations.

Frozen source revisions `301d70c45168fe8c98e4d630e2683a37ee049c12` and
`5960fb55f9738cde65d007589de859a9a73aef83` have identical complete Git trees; the latter
includes the current parent/main ancestry. MBO API head `31742109b82b5d8a88feea75c5974e1d0b124d93`
and merged revision `8dc6ff8290e6b12559a79f69cdd41a529e51ba83` likewise have identical trees.
Final builds use the merged MBO revision. Source hashes, compiler/build/link identities and binary
hashes remain attached to their actual builds rather than relabeling old measurements.

## Qualification

The selected source passes the complete 314-target release suite on each host, with the working
Linux FUSE configuration required. Eleven affected targets pass ASan on both hosts and MSan/TSan
on Linux. All four changed C++ translation units pass scoped clang-tidy on both hosts, using the
canonical compile database and an explicit XFF header filter. Both 30-second ASan/libFuzzer field
campaigns pass with a recorded seed and disconnected execution/mutation capabilities.

Committed regressions cover owning results after caller backing is replaced, components, rewrites,
extraction, and hash results after local or supplied snapshots expire or invalidate. Existing
copy/move/default/hash assertions remain intact. Actual compiler identities, commands, result codes,
release binary identities and link-action output are retained. A first sandbox-only Mac launch failed
before Bazel could access its cache; that diagnostic is retained separately from the successful run.

## Footprint

| Host  | Baseline CLI bytes | Selected CLI bytes | Increase |
| :---- | -----------------: | -----------------: | -------: |
| macos |          8,206,792 |          8,206,936 |     +144 |
| linux |         18,270,952 |         18,272,872 |   +1,920 |

Run the manual kernel cases with:

```sh
bazel run //xff/presentation/fields:fields_benchmark --config=clang_release -- --benchmark_filter=^fields-context/
```

The lossless compressed evidence retains full raw rounds, all implementations, source snapshots,
oracles, failures and corrections, controls and qualification logs. Generated fixtures, executable
payloads, objects and canonical compile databases are excluded; their identities remain in manifests.

`field-context-owned-results-20261104.json.gz`: 14,978,786 bytes, 2,267 labeled files; SHA-256
`c6d18b2019fd205f90c2cd2f76f483adf75cb8b293c3e4078d4f816b99be5036`.
