<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Compile symlink target demand before traversal

The implicit listing now reads a symlink target only when a compiled output field
uses it. Templates such as `{name}` and CSV columns such as `path,type,size` do
not need that read. `{target}`, its qualifiers and transforms still do; repeated
target fields share the same read. Comparison equality continues to read targets
independently, with unchanged status output.

**Decision:** adopt this small runtime change. Full native measurements improve
target-free symlink metadata output on both systems while preserving every checked
output. The tested memory footprint is effectively unchanged. Preserve small slower
control cells and the independent Linux repeat rather than promising that every
command is faster.

## Implementation and functional contract

`fields::Template::NeedsLinkTarget()` inspects compiled field-function bindings,
including post-processing. The command combines this demand once before traversal.
The implicit-listing callback tests that prepared boolean before `ReadLink`.
There is no per-entry field-name lookup, template scan, new field allocation,
metadata read, lock, worker policy, or user-facing option.

The original exact-count regression test renders the correct `link` name but fails
because the backend records one `ReadLink` where zero is required. The retained
parent failure and candidate tests use a read-only synthetic VFS whose mutation
operation fails the test. Tests cover target-free templates, escaped literals,
plain/qualified/transformed/repeated target fields, regular entries, one/three
workers, CSV fields and the existing empty-field result for an unreadable target.

For example, these existing output forms can avoid the unnecessary read:

```sh
xff . -type l --template='{name}'
xff . --format=csv --columns=path,type,size
```

These continue to resolve the target:

```sh
xff . -type l --template='{target:name}'
xff . --format=csv --columns=path,target
```

The function-level synthetic tests isolate exact backend calls. Timings below are
native CLI measurements; they do not claim a separately measured in-memory VFS
speedup or a change to filesystem latency itself.

## Measurement protocol

Both private builds start from exact commit
`d148059dc631f15d9dcf6ca70ea256c8bb0658ac` (#1110), with only the five recorded
field/listing/test files changed. Use ordinary `--config=clang_release
--config=xff_full`, including O2 and actual ThinLTO. The candidate contains neither
PGO nor the separate short-target-buffer or input-path-storage prototypes.

There are 228 cases on each host: the original 200 independent native listing
cases, 24 explicit path/target CSV cases, and four symlink comparison controls
covering every status. Declared fixture sizes are 10, 100, 1,000, 10,000 and
100,000, with broad/deep trees and one/three workers. The target CSV subset uses
10/1,000/100,000; comparison controls use 128/4,096 entries. Regular, symlink and
mixed trees exercise names, metadata, summaries and target-bearing output.

The native metadata commands fix `--timezone=utc`; the original corpus also fixes
`--exact`, `--color=never` and directory ordering. Thus existing native-case and
civil-time startup policies do not silently vary between participants.

All 456 parent/candidate preflights per host pass complete independent output
length/hash, stderr and exit checks. Existing native expectations stay unchanged;
target rows combine their independently validated original order with Python
`readlink` values, and the four comparison statuses have an independent oracle.
No expected output is regenerated from the candidate.

An exploratory screen uses one balanced session, three measured repetitions and
the fastest two. Full confirmation uses three balanced sessions, nine repetitions
and the fastest seven, with a byte-identical copied-parent control. Each case has
an excluded warm-up per executable. Canonical pauses occur between invocations and
resume with checked, excluded rewarming. Every measured and warm-up invocation
checks full output, stderr and exit status.

Each full host population has 18,468 measured invocations and 2,052 excluded
warm-ups, plus any separately recorded pause/resume warm-ups. The native timer
excludes Python startup. Linux pins the launcher and children to the first one or
three allowed CPUs; macOS requests one/three workers without claiming pinning.
macOS fixtures are cached APFS, while Linux uses its retained RAM-backed native
fixtures. Compare within each host, not absolute CPU performance across hosts.

The first Mac screen briefly overlaps a separately logged 1.1-second evidence
validation. It remains exploratory evidence; the full three-session confirmation
has no such root packaging overlap. Full results, not that screen, drive this
decision.

## Full native results

Values are candidate/parent elapsed time minus one, so negative is faster. Take
each case's median percentage across its three sessions, then the median of those
case medians within the group. The copied-parent control remains separate; its
percentage is not subtracted to manufacture a gain.

| Group / output          | Cases | Linux elapsed change | macOS elapsed change |
| :---------------------- | ----: | -------------------: | -------------------: |
| compare-symlinks/status |     4 |              +0.766% |              +0.560% |
| mixed/metadata          |    20 |             -12.005% |              -9.046% |
| mixed/names             |    20 |              +0.724% |              -0.083% |
| mixed/summary           |    20 |              +0.524% |              +0.128% |
| mixed/target-csv        |    12 |              +0.426% |              -0.250% |
| regular/eager-csv       |    20 |              +0.535% |              -0.131% |
| regular/eager-summary   |    20 |              +0.313% |              -0.254% |
| regular/files           |    20 |              +0.706% |              +0.105% |
| regular/native-name     |    20 |              +0.073% |              -0.024% |
| symlinks/metadata       |    20 |             -20.711% |             -18.452% |
| symlinks/names          |    20 |              +0.794% |              -0.320% |
| symlinks/summary        |    20 |              +0.434% |              -0.147% |
| symlinks/target-csv     |    12 |              +0.313% |              +0.101% |

The symlink metadata group has 17/20 cases faster in every session on each host.
Mixed metadata has 15/20 such cases on Linux and 16/20 on macOS. Small fixtures
remain dominated by command setup. Required target reads, regular-file output,
summary-only output and comparison modes are controls, not claimed beneficiaries.

Mac's worst full case is mixed/broad/1,000/three-worker names: +2.254% (0.124 ms),
slower in all three sessions. Keep it alongside the favorable groups. Linux's
largest slower cells warranted a separate unchanged-image repeat:

| Linux control                       | Original full change | Independent repeat | Original difference |
| :---------------------------------- | -------------------: | -----------------: | ------------------: |
| regular/broad/10000/3/eager-csv     |              +6.010% |            -2.979% |          +0.3799 ms |
| mixed/deep/1000/3/target-csv        |              +5.825% |            -4.654% |          +0.1147 ms |
| symlinks/deep/1000/3/summary        |              +4.849% |            +2.672% |          +0.0769 ms |
| regular/broad/10000/3/eager-summary |              +4.282% |            -0.687% |          +0.2376 ms |
| symlinks/broad/10000/3/summary      |              +4.141% |            -1.630% |          +0.2713 ms |
| mixed/deep/100000/3/names           |              +4.597% |            +0.585% |          +2.1576 ms |

The repeat retains all original results and uses the same executable hashes,
three balanced sessions and fastest-seven-of-nine protocol. It also includes two
positive metadata controls. The original +6% CSV case does not reproduce; one
summary cell remains about +2.67% slower. This is measured variation and a small
retained counterexample, not evidence that the source fixes every output path.

| Footprint                        |        Linux |       macOS |
| :------------------------------- | -----------: | ----------: |
| Parent executable                | 18,435,008 B | 8,274,424 B |
| Candidate executable             | 18,437,376 B | 8,274,840 B |
| Executable increase              |      2,368 B |       416 B |
| Median case peak-RSS difference  |  +0.0039 MiB | +0.0156 MiB |
| Minimum case peak-RSS difference |  -0.1953 MiB | -0.3906 MiB |
| Maximum case peak-RSS difference |  +0.2617 MiB | +0.4688 MiB |

Peak RSS is an observed process measure, not an allocation counter. No RSS saving
is claimed from these small differences.

## Qualification

Both hosts pass all 331 root, extras and API release test targets, with 240
executed and 91 valid cached results in each final run. Linux requires its actual
FUSE tests. Five affected ASan suites pass on each host; the same five MSan and
TSan suites pass on Linux. Both hosts run the actual 47 affected translation
units selected through the changed field header, with zero clang-tidy failures.

Canonical Linux changed-coverage policy covers all seven changed coverable lines
and all ten branches: 100%/100%, with no exclusions or threshold changes. Final
ordinary CLI rebuilds exactly match the corresponding measured candidate hashes:

```text
Linux  63403a74dca040a3d9a2485eb9888cb73e523929ff70b27d91b7591fd9ffa616
macOS  273b4e0a1c503787a31db41f580fc1b43c53cd71f263a37a5dcfdd8fbe4677e8
```

An initial test-only compile error from implicit string-view-to-string conversion
is retained before the explicit construction fix. The parent count regression is
also retained. Neither is replaced by the final successful log. These private
qualification receipts are experimental evidence; public PR readiness still
requires checks on its actual pushed, synchronized head and merge context.

## Evidence and remaining work

[Lossless compressed evidence](symlink-target-demand-20261105.json.gz) includes
both complete native populations, contracts, oracle results, source snapshots,
initial failures, sanitizer/lint/coverage logs, exact image manifests and the
independent Linux counterexample repeat. It does not embed executable binaries;
their recorded hashes and retained private locations identify the measured images.

The archive is 1,645,133 bytes, with SHA-256
`37d5fd9f8e3a240218c580d722a10d79097c54b04c75a6fd299def4596776136`. Its 122 records reconstruct
23,126,910 original bytes from 97 unique text contents.

It uses `xff-lossless-experiment-group-text-evidence-v1`: each host record gives
a relative path, byte length and SHA-256; `contents[sha256]` is the exact UTF-8 text.
Decode gzip and reconstruct each record from that dictionary, checking byte length
and hash. Both original host archives and every record were independently verified
before grouping. The original records survive unchanged, including slower controls.

Input-path termination reuse, moving already-owned targets, the short-target-buffer
trial and PGO are separately measured policies. They are not part of this runtime
change and their percentages must not be added to these results. Broader target
preparation for other actions or summary contexts likewise requires its own
functional contract and measured case.
