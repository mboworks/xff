# Executor lifecycle and task-grain probe

This page retains the measurement evidence from [PR #1000](https://github.com/mboworks/xff/pull/1000)
without merging its executor or diagnostic C++ targets. The probe target is not
available on main: reproducing the experiment requires its exact measured source,
`c8929d16c77e318b15d87c5bfcbf07af0fa7058e`. The lossless bundle can instead be
verified offline from main without building or executing any measured binary.

At that source, `//xff/engine:executor_benchmark` is an explicit diagnostic, not a
shipping dependency or a new runtime admission rule. It separates thread-start calls,
first dispatch, warmed work and executor teardown. Use a fresh executable process
for every cold sample: a previous executor or other Abseil contention may already
have calibrated the process's clock. Ordinary in-process repetition cannot
establish fresh-process startup cost.

In a separate checkout of the measured source, build before measuring, then
invoke the resolved executable directly. Do not build this target from main or
relabel a new build as verification of the historical executable bytes:

```sh
git fetch origin audit/executor-dispatch-costs
git worktree add --detach /tmp/xff-executor-probe-repro c8929d16c77e318b15d87c5bfcbf07af0fa7058e
cd /tmp/xff-executor-probe-repro
bazel build --config=clang_release //xff/engine:executor_benchmark
bazel-bin/xff/engine/executor_benchmark leaf 3 8192 16 128 32
```

The six arguments are dispatch style (`leaf`, `claimable`, `drain`), caller-inclusive
participants (1..64), items per warmed sweep, sweep count, chunk grain and unsigned
mixing iterations per item. All dimensions except mixing iterations must be positive;
item and queued-job totals must fit `size_t`. A mixing count of zero keeps only
the seed and checksum accumulation, providing a cheap-work control.

Each run explicitly starts `N-1` threads; it does not apply the production
directory/stat/content admission heuristics. The first phase queues one trivial
job per background worker and performs one on the caller. The warmed phase uses
the same executor for all sweeps:

- `leaf` queues at most `N-1` chunks beside a reserved caller chunk, joins that
  batch, and repeats. Each chunk has its own producer/result state.
- `claimable` additionally uses the shared exactly-once callback and separate
  result/wrapper states from the caller-participation experiment. After its own
  chunk, the caller can claim each still-queued required chunk before joining.
- `drain` queues at most one leaf drain per background worker per sweep. An
  atomic chunk index assigns disjoint work to those drains and the caller.
  Workers never submit or wait for other executor jobs. Some drains can be empty
  if another participant claims the work first; observed identities count only
  participants that actually process items.

The work value for item `i` starts at `i+1`, then repeatedly applies unsigned
64-bit xor-shifts by left 7, right 9, and left 8, in that order. Each phase reports
the modulo-64-bit sum of item values, executed item count, queued-job count,
observed participant count and whether the caller processed work. These permit
an independent oracle, not just a successful exit. A single-worker run never
queues work. Correctness tests exercise all modes, partial chunks, budgets
1/2/3/10, independently known sums and rejected configurations.

Phase times include probe bookkeeping (including caller-side accumulation of
participant identities), queue/producer operations and synthetic work. Vector
preparation and final destruction are outside some phase boundaries, and the
native process's wall time includes executable startup and JSON output as well.
Do not treat the phase sum as the process wall time or infer an isolated cost for
every queue operation. Thread identities are not forced to rendezvous, which
would add synchronization to the costs being investigated.

This does not measure VFS reads, frontier path/storage accounting, lazy metadata,
expression evaluation, content scanning, output rendering, actual storage latency
or RSS improvements. In particular, `claimable` is not a complete walk benchmark.
Its batch join pattern differs from a sliding nested frontier. Allocation and
native full-workload profiles remain necessary before tuning those mechanisms.

Retain exact source/tree, compiler flags, binary/helper hashes, raw stdout, all
fresh-process samples and verified affinity. Finish every build, sanitizer and
lint before timing collection. Rotate dispatch styles and grain sizes in repeated
independent sessions; report cold and warm costs separately. Keep optional
clock-control builds separate from the default binary. The probe uses portable
C++/Abseil, but Linux results do not establish native M5 Pro safety or
P/E-core scheduling equivalence.

## Retained Linux diagnostic (2026-10-10)

The terminal collection used source `c8929d16c77e318b15d87c5bfcbf07af0fa7058e`
and tree `58bc0b9b3a4626c63ae859557551c93308e9e0a4`, before the evidence and
release-page mapping were added. It ran from `06:27:51` to `06:33:18` UTC.
Both binaries use the historical workspace `clang_release` configuration and
Clang 23.1.2. System/home RC files were disabled, the workspace RC was announced,
and no workspace user RC existed. The explicit retained ICU 70 build-library
path is recorded; a rejected missing-library attempt is retained, not measured.
No source, configuration, build, sanitizer or lint work overlapped collection.

The default executable SHA-256 is
`c730092bdc474154024799b1fa9348484853489bb682c6164ee14888113d2051`.
The separate clock-control executable SHA-256 is
`9faa695cda7dba14b2c0226d3b950b8927a18bc4c9e4bdc64f60a1d17fa69812`;
its only additional compilation option is
`--copt=-DABSL_USE_UNSCALED_CYCLECLOCK=0`. This is a diagnostic control, not a
proposed production default. Native resource accounting uses driver
`0e296e40ca6bf523215d80d1dc02deb3503b796c` and helper SHA-256
`60bcb395bb3d0eccccd0a3d0cb2407538b1f96c45d5e1e156a0bd4522f93328e`.
The original executable/helper bytes remain at the indexed local paths and were
rehash-verified on export; the public bundle excludes those binary bytes.

Each invocation executes 8 sweeps of 8,192 items. The matrix covers all three
dispatch styles, participant counts 1/3/10, grains 1/16/128 and mixing counts
0/32/512. Three complete independent orderings use declared, reverse-declared
and seeded-shuffled order (`20261010`). Default/control order alternates by
cell and repetition. Each version/cell has one fresh-process warmup and nine
fresh-process samples: 486 cells, 486 warmups, 4,374 samples and **4,860 checked
invocations**. The independent verifier recalculates checksums with a separate
unsigned-mixing implementation and checks every raw result, work/job total,
contributor bound, command/order, native resource field and estimator.

`taskset` restricts each invocation to the first N of ten distinct allowed
observer-reported `(package, core)` pairs, CPUs 0..9. These are guest-visible
topology observations, not independent verification of physical host cores or
QEMU thread affinity. The original manifest/index field `physical_cpus` is
preserved as recorded; its name does not establish physical-host pinning.
The manifest retains the allowed CPU list, topology and start/end load averages.
It does not record CPU model,
kernel version or per-sample thermal/load observations. This is repeated,
reordered diagnostic evidence on this host, not a host-independent calibrated
dispatch constant.

### Cold and warmed results

Across all retained per-cell nine-sample cold medians, median start-call plus
first-dispatch time is 3,242.70 us for three participants and 3,268.54 us for ten
with the default clock. Corresponding clock-control medians are 86.28 us and
298.70 us. These phase measurements are consistent with the separately profiled
one-time Linux/x86 Abseil clock-calibration cliff; they do not attribute 3 ms to
each `pthread_create` or isolate creation from dispatch/bookkeeping.

Selected default-clock warmed results follow. Times are the median of three
session estimates, each the arithmetic mean of the seven lowest warmed-phase
times out of nine. Selection uses the warmed phase, **not native process wall
time**; this is not the full-engine grid estimator. Each row uses its own
same-style/grain serial control.

| Mix iterations | Grain | Style     | Serial ms | 3 participants ms | 10 participants ms |
| -------------: | ----: | :-------- | --------: | ----------------: | -----------------: |
|              0 |    16 | leaf      |    0.0197 |           33.6261 |            10.9164 |
|              0 |    16 | drain     |    0.0195 |            0.1948 |             0.1852 |
|             32 |    16 | leaf      |    1.5625 |           34.3747 |            11.3201 |
|             32 |    16 | claimable |    1.5739 |           34.0290 |            11.1849 |
|             32 |    16 | drain     |    1.5595 |            0.6466 |             0.5831 |
|            512 |   128 | leaf      |   35.6879 |           16.6467 |             9.6011 |
|            512 |   128 | drain     |   35.7687 |           12.3751 |             5.0017 |

For the 32-iteration, grain-16, three-participant case, separate leaf batches
take 21.79..22.08 times their same-session serial controls; drains take
0.410..0.422 times theirs. Both patterns reuse the same threads. Conversely,
zero-work drains still lose to serial work. Enough useful work and dispatch
granularity matter independently of thread reuse and thread caps.

The 32-iteration drain's warmed saving alone is less than default cold startup.
An admission rule must consider the useful work **still available**, cold versus
warm executor costs and diminishing returns from extra participants. Completed
work is not remaining backlog. None of these synthetic mixing counts establishes
a file/stat/content threshold. Actual VFS latency, metadata and slow-storage
controls are still required. Long drains can also delay another queue class;
this probe does not justify replacing the bounded directory frontier with an
unbounded drain or weakening pruning, cancellation, ordered results or memory
accounting.

### Lossless review bundle

The [bundle index](native-executor-dispatch-controls.json) records compressed
and original byte counts/SHA-256 values for all nine archives:

- Raw stdout, warmups, samples, native resources and exact commands:
  [session 1](native-executor-dispatch-raw-1.gz),
  [session 2](native-executor-dispatch-raw-2.gz),
  [session 3](native-executor-dispatch-raw-3.gz).
- Original per-session analysis arrays:
  [session 1](native-executor-dispatch-analysis-1.gz),
  [session 2](native-executor-dispatch-analysis-2.gz),
  [session 3](native-executor-dispatch-analysis-3.gz).
- [Independently verified analysis](native-executor-dispatch-verified-analysis.gz)
  and [same-session ratio summary](native-executor-dispatch-summary.gz).
- [Provenance and reproduction scripts](native-executor-dispatch-provenance.gz):
  frozen manifest/configuration/source, compiler identity, build logs, helper and
  driver source, complete collection log, original verifier/summary/export
  scripts, offline verifier, and local validation logs.

To independently verify the published archives without building, executing a
measured binary, contacting GitHub or accessing the original `/tmp` paths, extract
the offline verifier from a checked-out bundle and inspect it before running:

```sh
python3.13 - <<'PY'
import gzip
import json
from pathlib import Path

bundle = Path("docs/benchmarks")
metadata = json.loads(gzip.decompress(
    (bundle / "native-executor-dispatch-provenance.gz").read_bytes()))
script = metadata["scripts"]["offline_verifier"]["text"]
with Path("/tmp/xff-executor-review-verifier.py").open("x") as output:
    output.write(script)
PY
python3.13 /tmp/xff-executor-review-verifier.py --bundle=docs/benchmarks
```

The offline verifier checks all archive hashes and independently recomputes the
4,860 output oracles, all three orderings, native-resource relationships, all
seven-of-nine estimates and the published ratio summary. It cannot verify the
excluded executable bytes. The collection script is Linux-specific (`taskset`
and sysfs); the C++ diagnostic uses portable APIs, but neither native M5 Pro execution
nor equivalent P/E-core scheduling has been established. No shipping threshold,
full-engine performance acceptance or completion of the audit is claimed.
