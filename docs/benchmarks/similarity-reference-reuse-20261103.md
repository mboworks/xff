<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Literal similarity reference reuse

A repeated literal `-similar` reference can reuse its byte-word shingle representation while still
reading and validating the current reference bytes for every evaluated entry. On these fixtures,
native 4 KiB repeated-reference commands take about 27% less time on macOS and 38% less on Linux.
This is a targeted improvement, not a general traversal or parallel-matching speedup.

## Selected mechanism and limits

The immutable prepared plan identifies literal field templates from compiled segments, without
adding a field to every template. A worker lazily remembers the first eligible reference bytes,
then prepares stable, immutable shingle storage after a second equal observation. Every reuse
requires the same freshly read bytes and width. Changed bytes invalidate the prior representation
before a replacement is built. Content reads, target rendering, errors, NUL rejection, width-zero
scores and byte-equality shortcuts remain observable in their original order.

Dynamic targets retain the original evaluator and no similarity slot. The command's coordinator
gets a persistent prepared worker only when its plan needs reference state; ordinary coordinator
evaluation remains unchanged. Original coordinator regex matchers remain unforked, while concurrent
workers retain independent lazy forks. The callback's state span does not grow; the private slot
record gains an owner. Similarity's compact operand stays inside the existing variant envelope.

A reference larger than 1 MiB is fully compared through the existing implementation rather than
cached. More than 8 MiB of tracked retained capacity releases the cache after the comparison.
These are admission/retention policies, not file-size limits, peak-memory limits or a run-wide
memory budget. Raw bytes, normalized bytes, key storage and string capacities are counted;
allocator metadata and hash-control bytes are not. Each evaluated similarity slot owns its own
cache. Width one keeps the previously qualified owning-word algorithm; wider windows borrow
stable normalized backing storage. Empty and moved-from owners remain safe to query.

No locks, clocks, new threads, filesystem mutations, dependencies or flags are introduced.
Near-duplicate candidate indexing and grouping are separate open work.

## Native command results

Times are milliseconds. Changes are the median of three within-session elapsed-time ratios;
negative means faster. Both one-worker and three-worker requests are included, without implying
that a coordinator-only similarity predicate became parallel.

| Workload                    | Workers | macOS original ms | macOS change | Linux original ms | Linux change |
| :-------------------------- | ------: | ----------------: | -----------: | ----------------: | -----------: |
| Repeated 64 B, 1,000 files  |       1 |            29.250 |       -1.80% |             3.408 |       -5.91% |
| Repeated 64 B, 1,000 files  |       3 |            29.200 |       -1.18% |             3.479 |       -5.48% |
| Repeated 4 KiB, 1,000 files |       1 |            70.017 |      -27.16% |            27.343 |      -38.60% |
| Repeated 4 KiB, 1,000 files |       3 |            69.611 |      -26.58% |            27.527 |      -38.20% |
| Repeated 128 KiB, 10 files  |       1 |            15.490 |      -26.64% |             7.729 |      -37.79% |
| Repeated 128 KiB, 10 files  |       3 |            15.398 |      -26.98% |             7.944 |      -37.26% |
| Dynamic 4 KiB, 1,000 files  |       1 |            71.815 |       +0.04% |            27.782 |       +1.92% |
| Dynamic 4 KiB, 1,000 files  |       3 |            72.089 |       +0.02% |            28.128 |       +2.12% |
| Plain listing, 10,000 files |       1 |            11.803 |       +0.01% |             3.295 |       +2.22% |
| Plain listing, 10,000 files |       3 |            11.870 |       +0.16% |             3.318 |       +2.47% |

The entire 48-case population per host is retained. The slowest selected native macOS cell is
about +1.4%; the slowest Linux cell is about +2.5% (plain listing at 10,000 files). Dynamic Linux
4 KiB comparisons are about +2%, while the macOS equivalent stays near baseline. Ten-file/small
reference commands are dominated by startup and can become slightly slower. These costs remain
visible rather than being averaged away against the repeated-reference gains.

## In-memory engine results and controls

Three sessions cover 50 cases per host, including cold/empty cases, dynamic unique references,
64 B / 4 KiB / 128 KiB bodies, widths 1/3/5, ordinary name/regex output and one/three worker
requests. The width-one and width-five in-memory controls use empty and 1,000-file cases;
the ordinary width-three cases additionally include single-entry cold calls. Native width-one
and width-five cases use 1,000 entries. Reference bodies differ in ASCII case from candidates,
so the repeated-reference gains do not come from byte equality. Dynamic fixtures append unique
punctuation-only suffixes without changing their word-shingle score.

Repeated 4 KiB, 1,000-file engine cases improve about 46% on macOS and 48% on Linux; width-one
and width-five cases improve about 44-48%. The complete table and all raw rounds are in the data.
One initial three-worker Linux regex control showed +11.45%. A separate three-session confirmation
with an identical-baseline copy did not reproduce a stable regression: selected changes were
-5.61%, +0.97% and +0.13%; the identical baseline ranged from -6.50% to +8.55%. Keep both
populations; do not pool them or discard the unfavorable initial result.

The original, state-only and selected implementations share the fixture and reference API code;
the original uses the parent's evaluator and coordinator sources. A state-only variant isolates
slot/factory costs without reusing shingles. Native commands and the separate ordinary-control
confirmation also include a byte-identical baseline copy.

## Earlier prototypes retained

- The initial worker-only placement did not improve production similarity because the coordinator
  used stateless evaluation. The corrected implementation includes real coordinator reuse tests.
- Eager rebuilding on every changed reference regressed the large macOS changing-reference case
  by about 21%; reject that admission policy.
- A two-observation cache for all target templates removed that large penalty but still regressed
  a small changing-reference Linux case by about 9%. Keep dynamic templates on the original path.
- The preceding adaptive experiment compares 64 KiB / 1 MiB tracked-budget admission against
  1 MiB / 8 MiB admission. The selected literal policy keeps the larger admission and its
  128 KiB shared-reference gains without limiting the size of files that can be compared.

One reduced pilot driver accidentally repeated successive prefixes of its case list. Those runs
were stopped, marked malformed and retained separately. Canonical fixed drivers assert case and
participant/session counts and invoke the measurement driver once. Malformed samples do not enter
any selected result.

## Qualification and reproduction

Both hosts pass all 314 enabled release targets, with FUSE required on the mount-capable Linux host.
Seven affected ASan targets pass on each host; seven affected MSan and TSan targets pass on Linux.
Scoped clang-tidy uses the real canonical compile database, affected translation units and explicit
XFF header filters. Additional mixed regex/similarity cases cover both primary orders and retain
capture/output/error observations across executor choices. The seeded independent byte-word oracle
and fuzz harness also compare the reusable reference, including opaque high-bit bytes. Functional
fixtures remain ASCII.

Fresh-read tests cover changed bytes, dynamic paths, missing/binary references and recovery, worker
isolation, moved backing storage, equality and width-specific scores. Resource cases cover exactly
1 MiB, one byte beyond it, oversized normalized/key storage release and subsequent small references.
The expanded expression-contract target needs a moderate timeout under MSan. An initial 60-second
timeout and the stronger four-observation diagnostic run are retained; the final overflow case
needs only the first admission and second representation/release observations.

ASan allocator-hook probes measure requests in actual one-worker engine runs, excluding fixture
construction. Repeated-reference cases roughly halve allocation requests and requested bytes;
these are diagnostic counts, not timing, peak usage or RSS. Hash/debug instrumentation can vary
individual counts. All per-variant counts and exact probe source are preserved.

The release build uses the same `--config=clang_release --config=xff_full`, LLVM identity and
ThinLTO configuration for every participant. Native inputs use Linux shared memory and macOS's
local filesystem; in-memory engine cases isolate filesystem speed. Each case/session has a
separate discarded warmup, nine interleaved measured rounds and the fastest seven retained.
Case order is deterministic-shuffled; participant order rotates. Linux records and pins the
requested CPU allowance. Native outputs and exit codes are checked independently for every run.
Google Benchmark's registered `MinTime(0.01)` controls each in-memory measurement.

| Host  | Original full CLI bytes | Selected full CLI bytes | Increase |
| :---- | ----------------------: | ----------------------: | -------: |
| macOS |               8,176,264 |               8,185,912 |   +9,648 |
| Linux |              18,165,320 |              18,220,984 |  +55,664 |

The frozen timing parent is `6db2de9e49f19107f49e257486b50694a764cf23`. Publication is based on
#1074's isolated hosted-runner disk-space fix; it changes no runtime source. Exact sources,
compiler/binary hashes, link actions, commands, every raw timing population, failed preflights,
allocation probes and validation logs accompany the report in
[the compressed evidence](similarity-reference-reuse-20261103.json.gz). Binary executables and
repeated compile-database copies are excluded from that archive.

Archive: 7,016,330 bytes, containing 979 labeled evidence files. SHA-256:
`9f1d5930ac4f7f968b87d6e61a977a66733463432eb79bf2ad1b4943d405ac91`.
