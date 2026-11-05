<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Native-case experiments: retain the current runtime

Three implemented candidates remain unselected: admitting known native-case content filters to
workers, rejecting cheap nonmatching prefixes before queueing, and a first-device case-cache
shortcut. The worker trials have useful large Linux wins but repeat small/selective regressions.
The cache trial does not establish a useful practical gain over its unchanged-parent controls.
Retain the implementations and measurements for future work; none is enabled by this change.
Keep the mixed-device, failure-cache and deferred-query regression tests against the existing map.

## Common method and separate parents

Both hosts use three sessions, one discarded warmup, nine measurements and fastest seven.
Participants rotate per repetition; every native invocation validates exit status and complete
stdout. Ratios are medians of within-session elapsed ratios; below one is faster. Identical-parent
copies remain visible without silently correcting the results. Linux pins one/three allowed logical
CPUs; no Mac pinning is claimed. No competing same-host agent build/test/lint ran during timing.
These are cached native filesystem workloads, not in-memory filesystem measurements.

Worker trials predate the compact/eager/scoped stat-path changes. Their private parent has identical
C++ and Bazel production sources to the published #1091 runtime; that correspondence is recorded
with full baseline/candidate snapshots. The first-device cache trial uses the #1096 runtime as its
parent. Do not compare their absolute times as one before/after series or transfer an old worker
decision to a changed implementation without measuring again.

## Known native-case content workers (O20d)

The first policy passes an immutable fold decision into the existing worker contexts and removes
the native-case serialization gate only for backends with a uniform guarantee. Unknown backends
remain serial, and the independent content-work gate remains: pure name-only searches do not gain
workers from this change. Sensitive and insensitive guarantees are covered at coordinator/worker
batch transitions. Existing safety, required metadata and collection/deferred gates stay intact.

The native screen has 16 cases per host: 10/1k/10k files, roughly four-KiB bodies, all/selective names,
content regexes and one/three requested workers, plus exact-case and names-only controls. The direct
policy has parent/candidate/parent-copy participants: 1,440 invocations per host including warmups.
A second 1,920-invocation matrix adds the first policy as a fourth participant and prepares a cheap
snapshot-only coordinator prefix to reject nonmatching entries before queueing. The worker still
evaluates the complete expression. A total 3,360 invocations per host retain all original observations.

| Host  | Case                        | Direct worker ratio | Direct parent-copy | Prefilter worker ratio | Prefilter parent-copy |
| :---- | :-------------------------- | ------------------: | -----------------: | ---------------------: | --------------------: |
| macOS | 10/3/selective-regex        |              1.0115 |             0.9981 |                 1.0031 |                0.9956 |
| macOS | 1000/3/all-regex            |              0.9946 |             0.9966 |                 1.0011 |                1.0009 |
| macOS | 1000/3/selective-regex      |              1.0069 |             0.9943 |                 1.0039 |                1.0053 |
| macOS | 10000/3/all-regex           |              0.9990 |             0.9980 |                 0.9984 |                1.0015 |
| macOS | 10000/3/selective-regex     |              1.0071 |             1.0013 |                 1.0083 |                0.9994 |
| macOS | control/10000/3/exact-regex |              1.0029 |             0.9998 |                 0.9807 |                0.9997 |
| macOS | control/10000/3/names       |              1.0103 |             1.0100 |                 1.0032 |                0.9989 |
| Linux | 10/3/selective-regex        |              1.0099 |             1.0003 |                 1.0030 |                0.9841 |
| Linux | 1000/3/all-regex            |              0.8278 |             1.0036 |                 0.8345 |                0.9918 |
| Linux | 1000/3/selective-regex      |              1.1673 |             1.0096 |                 1.1222 |                0.9966 |
| Linux | 10000/3/all-regex           |              0.5659 |             1.0079 |                 0.5878 |                0.9975 |
| Linux | 10000/3/selective-regex     |              1.0526 |             0.9975 |                 0.8557 |                1.0168 |
| Linux | control/10000/3/exact-regex |              1.0005 |             1.0092 |                 0.8289 |                0.9955 |
| Linux | control/10000/3/names       |              1.0051 |             0.9941 |                 0.9997 |                0.9989 |

Direct admission improves Linux 10k all-match/three-worker content filtering about 43%, but costs
about 17% for selective 1k and 5% for selective 10k. The coordinator-prefix trial improves selective
10k about 14% and all-match 10k about 41%, while selective 1k still costs about 12%. Mac native-case
controls remain effectively unchanged because its volume policy has no uniform guarantee; its
exact-case 10k result improves about 2%. These mixed outcomes reject both general runtime policies.

Explicit useful-work admission, startup amortization and snapshot-only eligibility need more
review. Do not invent a millisecond delay or lifetime file-count threshold to fit these fixtures,
and do not move delayed host/content reads to the coordinator merely to avoid worker overhead.
The private implementations, exact source stages and raw evidence remain available for a narrower
future experiment. Four relevant release targets and the explicit fold-state transition regression
pass on both hosts; these early rejection trials do not claim full release/sanitizer qualification.

## First-device case cache (O20h)

The prototype keeps one optional {device, sensitivity} value outside the existing hash map and
uses hashed overflow for other devices. Ordinary and deferred visits share the same command-owned
cache. It preserves first query order, cached errors defaulting to sensitive, and uniform/exact/find
fast paths. It adds no lock, timer, global state or new query. Four relevant release targets and the
mixed-device tests pass on both hosts; the same new tests also pass against the unchanged parent.
The experimental production code stays in a private branch rather than entering the runtime.

The premise requires care: the pinned Abseil implementation's small-object insertion path compares
the single slot before hashing, while an empty slot can consult its sampling gate. A one-device
cache therefore does not necessarily allocate or repeatedly hash. These experiments do not establish
that sampling causes the Mac startup gap. See the [pinned Abseil source](https://raw.githubusercontent.com/abseil/abseil-cpp/20260817.0/absl/container/internal/raw_hash_set.h).

There are 23 cases per host: broad/nested 10/1k/100k names with one/three workers, relative roots,
long names, exact metadata-free controls and eager CSV. Parent/candidate/identical-parent participants
produce 2,070 invocations per host including warmups. The many-file Linux controls mostly bypass the
cache through the existing uniform-case capability.

| Host  | Case                                | First-device cache ratio | Parent-copy ratio |
| :---- | :---------------------------------- | -----------------------: | ----------------: |
| macOS | native/broad/10/1/name-txt          |                   0.9964 |            1.0090 |
| macOS | native/broad/1000/1/name-txt        |                   0.9890 |            0.9921 |
| macOS | native/broad/100000/1/name-txt      |                   0.9935 |            0.9924 |
| macOS | native/broad/100000/3/name-txt      |                   1.0005 |            1.0057 |
| macOS | native/deep/100000/3/name-txt       |                   0.9960 |            0.9975 |
| macOS | native/relative/broad/10/1/name-txt |                   0.9836 |            0.9924 |
| macOS | native/relative/broad/10/3/name-txt |                   0.9956 |            0.9860 |
| macOS | long/100000/1/native-name           |                   0.9957 |            0.9986 |
| macOS | exact/eager/broad/10000/3/csv       |                   0.9965 |            1.0038 |
| Linux | native/broad/10/1/name-txt          |                   1.0047 |            1.0158 |
| Linux | native/broad/1000/1/name-txt        |                   1.0004 |            0.9983 |
| Linux | native/broad/100000/1/name-txt      |                   0.9991 |            1.0037 |
| Linux | native/broad/100000/3/name-txt      |                   1.0084 |            1.0070 |
| Linux | native/deep/100000/3/name-txt       |                   1.0211 |            0.9796 |
| Linux | native/relative/broad/10/1/name-txt |                   1.0075 |            1.0013 |
| Linux | native/relative/broad/10/3/name-txt |                   0.9983 |            0.9936 |
| Linux | long/100000/1/native-name           |                   1.0052 |            1.0034 |
| Linux | exact/eager/broad/10000/3/csv       |                   0.9329 |            0.9316 |

Mac broad 100k/one-worker cache ratio is 0.9935 while its copy is 0.9924; three workers stay neutral.
The strongest small relative-root cell improves 1.64%, but a different relative cell improves less
than its copy. Linux eager CSV improves 6.71% while its identical copy improves 6.84%, exposing
shared drift in a case that does not exercise native-name caching. Linux nested 100k/three-worker
names cost 2.11% while the copy gains 2.04%; retain that unfavorable control as well.

The results do not justify an extra optional record, cache representation and lookup path.
Keep the current runtime. More targeted startup/locale profiling or actual multi-device workloads
could motivate a new experiment; do not claim a discovered hashing/clock tax from these observations.

## Retained regression coverage and next decisions

The public change only expands the case-policy test fixture and adds black-box regressions:
interleaved devices, folding/sensitive/error answers, one query per device, ordinary/deferred visits,
and query-free exact/find operation. The baseline runtime passes release/ASan on both hosts and
MSan/TSan on Linux. Prototype tests and public baseline tests remain separately labeled. Normal
hooks and remote current-head CI still gate publication and merge; no prototype production change
is smuggled in with its evidence.

The companion JSON.gz contains both hosts' worker/cache sources, drivers, full observations,
copy controls, fingerprints, baseline correspondence, qualification/rejection logs and decision
records. Frozen binaries, generated fixture trees and complete native stdout remain local; the
recorded drivers and hashes allow reproduction. No sanitizer timing is used.

Keep these separate follow-ups:

- O20d: a useful-work and snapshot-only worker-admission design that removes the selective regression.
- O02b: investigate the prepared glob fallback used for folded names, preserving active locale,
  NUL boundaries and non-ASCII/complex-pattern behavior. Inspection alone establishes no gain.
- O20: optional bulk metadata with checked decoding and precise field/link/mount fallbacks; retain
  cheap name-only traversal and unchanged Linux fallback behavior.
- Benchmark normalization provenance: derive a stable semantic launcher identity before combining
  differing binary fingerprints. The shared task-order rendering change does not relax strict
  reference-window compatibility.
