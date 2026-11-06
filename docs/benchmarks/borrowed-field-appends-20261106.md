<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Append borrowed field values directly

`-printf` appends each compiled field template into its owning output string. Borrowed values are
consumed synchronously while the entry context, fallback snapshot and transformation source remain
alive, avoiding the intermediate owning string in the render-then-append path.

## Decision evidence

Ratios are direct-append elapsed time divided by render-copy elapsed time; below 1 is faster. Each
cell is the median of three independently ordered sessions. Every session uses the mean fastest 7
of 9 measured invocations. These are isolated in-memory template-rendering kernels, not complete
command timings. The first table reports 1,000 and 100,000 renders with 160-byte directory and
basename components; the generated path also includes its prefix, separator, index and suffix.

| Context  | Field shape | macOS 1,000 | macOS 100,000 | Linux 1,000 | Linux 100,000 |
| :------- | :---------- | ----------: | ------------: | ----------: | ------------: |
| fallback | path        |       0.539 |         0.508 |       0.756 |         0.737 |
| fallback | dir         |       0.946 |         0.950 |       0.953 |         0.959 |
| fallback | name        |       0.806 |         0.951 |       0.917 |         1.055 |
| fallback | transformed |       0.733 |         0.948 |       0.892 |         0.869 |
| fallback | composite   |       0.836 |         0.862 |       0.946 |         0.957 |
| supplied | path        |       0.532 |         0.517 |       0.756 |         0.752 |
| supplied | dir         |       0.935 |         0.942 |       0.965 |         0.956 |
| supplied | name        |       0.804 |         0.991 |       0.931 |         0.905 |
| supplied | transformed |       0.761 |         1.009 |       0.859 |         0.930 |
| supplied | composite   |       0.856 |         0.853 |       0.948 |         0.943 |

Nineteen of the 20 displayed long-path cases improve on each host. Direct `{path}` is 46-49% faster on macOS and
24-26% faster on Linux. Most other fields improve by 4-20%; the strongest gains are for transformed
fields. Two host-specific cases regress: macOS supplied `{path:stem}` at 100,000 renders is 0.9%
slower, and Linux fallback `{name}` at 100,000 renders is 5.5% slower. Both exceptions should stay
visible in the tradeoff. These microbenchmarks do not establish an end-to-end `xff` speedup.

The full retained population also includes 10-render and short-path controls: 60 cases per host
(two contexts, five shapes, three render counts and two path-component lengths). Of these, 53 are
faster on macOS and 54 on Linux. The following medians retain every slower case, including those
outside the displayed long-path subset. No slower cell was replaced by another measurement.

| Host  | Context  | Field       | Renders | Component bytes | Direct / render-copy |
| :---- | :------- | :---------- | ------: | --------------: | -------------------: |
| macOS | fallback | dir         |      10 |               8 |               1.0223 |
| macOS | fallback | dir         |   1,000 |               8 |               1.0540 |
| macOS | fallback | dir         | 100,000 |               8 |               1.0412 |
| macOS | supplied | dir         |      10 |               8 |               1.0340 |
| macOS | supplied | dir         |   1,000 |               8 |               1.0419 |
| macOS | supplied | dir         | 100,000 |               8 |               1.0080 |
| macOS | supplied | transformed | 100,000 |             160 |               1.0092 |
| Linux | fallback | name        | 100,000 |             160 |               1.0546 |
| Linux | fallback | path        |      10 |               8 |               1.0130 |
| Linux | supplied | name        |      10 |               8 |               1.0410 |
| Linux | supplied | name        |   1,000 |               8 |               1.0220 |
| Linux | supplied | name        | 100,000 |               8 |               1.0177 |
| Linux | supplied | path        |      10 |               8 |               1.0213 |

## Implementation and lifetime rules

`Template::AppendTo` appends literals, field values and transformations into a caller-owned string.
The owning `Template::Render` API keeps its direct single-field fast path, which moves owned values
into the returned string; composite and transformed rendering use `AppendTo`. The append path can
also move an owned result when the destination is empty, avoiding a second allocation for fields
such as `{dir}`. The `-printf` evaluator uses `AppendTo` for each compiled placeholder, so no
returned view or reference survives the call. An existing content snapshot is used directly;
otherwise the fallback snapshot remains local and alive for the entire append operation. Transforms
append their result before the source view's owner can expire. No view is stored in a template, row,
callback, or returned result.

The benchmark compares `Template::Render` followed by appending its owning result with direct
`Template::AppendTo`; both sides use the same compiled format, entry data, output-string creation,
and consumed output. Each timed render starts with an empty destination; the measurements do not
quantify reuse or appending after earlier `-printf` placeholders. Those cases have output/lifetime
goldens but no separate performance claim. It covers `{path}`, `{dir}`, `{name}`, `{path:stem}`, and `{dir}/{name}`, both
supplied and fallback snapshot contexts, 1,000 and 100,000 renders, and long paths. Nine repetitions
form each session; participants run in varied order across three sessions per host.

## Validation and provenance

Committed goldens cover borrowed, owned, transformed and composite values appended to empty,
nonempty and preallocated outputs, with local and supplied snapshots. An in-memory digest test
checks ownership after snapshot invalidation. Existing integration tests also exercise multiple
`-printf` placeholders. On the original private candidate, targeted release and ASan tests passed
on macOS and Linux, and the complete root release suite passed 253 tests with 10 skipped on each
host. Public integration adds the independent goldens and digest-lifetime checks above; its own
validation is recorded separately in the PR.

The complete raw benchmark JSON is retained in
`borrowed-field-appends-20261106.json.gz` (397,002 bytes, SHA-256
`e1ec4266347fd4ca1c5747f48501df33ca1e0f7255c00829c683d5795a85d966`). It records the base revision,
host sessions, benchmark parameters and candidate source-patch SHA-256
`1dc8564eecaf91f0c845f9e6a5d455dc2e16af7a280e989fea4be7855ef0e05d`. The benchmark fixture and
complete change are microbenchmark evidence; required PR CI remains the final cross-platform
integration gate.

Public integration is based after #1122 and adds the independent output/lifetime tests and site
index above without changing the privately measured production or benchmark source bytes. The
private measurements and full root test runs retain their recorded `c7bb70cc6f3e6441ff25c999667d76f48ffe9acb`
base; current pushed-head integration requires its own normal CI. The 253-test root scope excludes
separately nested extension modules. No end-to-end, allocation-count or release binary-size claim
is inferred from these renderer kernels.
