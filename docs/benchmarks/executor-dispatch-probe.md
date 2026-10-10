# Executor lifecycle and task-grain probe

`//xff/engine:executor_benchmark` is an explicit diagnostic, not a shipping
dependency or a new runtime admission rule. It separates thread-start calls,
first dispatch, warmed work and executor teardown. Use a fresh executable process
for every cold sample: a previous executor or other Abseil contention may already
have calibrated the process's clock. Ordinary in-process repetition cannot
establish fresh-process startup cost.

Build before measuring, then invoke the resolved executable directly:

```sh
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
clock-control builds separate from the default binary. The probe runs portably
on Linux/macOS, but Linux results do not establish native M5 Pro safety or
P/E-core scheduling equivalence.
