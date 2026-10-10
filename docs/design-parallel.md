# xff - Parallel Traversal Design

> Detailed spec for parallel directory traversal and `--sort` (issue #43).
> **Refines** `design.md` section"Cross-cutting concerns" (Determinism). Where the two
> differ, this document is authoritative for traversal/ordering specifically.
> Status: **Implemented** / 2026-06-27 / Org: MBO Works

## Purpose

Directory enumeration and metadata lookup are hot paths. Parallel reads can
hide I/O latency, especially on high-latency filesystems. Small or memory-backed
workloads can instead be dominated by startup, dispatch and per-entry CPU work;
admitting workers must be justified by useful remaining work, not file count
alone. Worker reuse removes repeated creation, but does not make dispatch free.

Parallelism must not cost correctness. Three things are trivially correct in the
current sequential walk and must stay correct under concurrency: deterministic
output (when asked for), the `-prune`/`-quit`/`-depth` control semantics, and the
exit-code model. The VFS layer is already contractually safe to call from many
threads (`vfs::FileSystem`), so the foundation is ready.

## v1 scope (what shipped first)

The first implementation (engine PR for #43) takes three deliberate simplifications
of the design below; each has a noted follow-up:

- **The worker pool parallelizes `readdir`+`lstat` only.** The visitor (evaluate +
  emit + exec + capture + summary) runs on the single coordinator thread in `--sort`
  order, so that whole pipeline stays single-threaded and unchanged (`run.cc`
  untouched) - the only new concurrent code is the pool, whose jobs are pure
  (path -> stat'd listing) and touch just the thread-safe VFS and a mutex-guarded
  queue. Per-worker parallel evaluation is a later option if profiling wants it.
- **The ordered modes are deterministic.** Siblings are consumed in sorted order
  (reads still overlap via prefetch), so `dir`/`subtree`/`tree` are reproducible
  across runs and machines. Streaming subtrees strictly by completion order (lower
  latency, nondeterministic) is a later refinement.
- **Parallelism and sort were opt-in in the first implementation.** The shipped
  style-scoped defaults now give `xff` bounded parallelism plus `dir` ordering;
  `find` and `rg` use every detected core plus `none`. Explicit `-j` / `--jobs`
  and `--sort` values override those defaults.

## Architecture

A command-owned executor with one calling coordinator and at most `N-1`
background workers under `--jobs=N`:

- Workers perform only `ReadDir` plus metadata lookup and return complete listings.
  The coordinator submits sibling-directory reads ahead, then consumes their
  result handles in the order required by `--sort`. Directory workers start only when at least
  64 sibling directories provide independent work. For a coordinator-read directory with at least
  512 children and eagerly required metadata, 128-entry stat chunks use the same executor. The
  coordinator also claims stat chunks. Directory reads use a bounded ordered window with
  command-wide execution and retained-storage limits; stat and content batches use at most one
  drain job per participating background worker. Worker jobs never wait on nested jobs. Metadata-free, lazy and
  owned/archive-source listings keep their serial per-directory path.
- The coordinator applies traversal controls and writes the output sink in traversal order.
  Independent content predicates and rg content searches reuse that executor, with the caller also
  claiming chunks; all stateful expressions, mutations and execution actions retain the coordinator evaluator.
- Content matching schedules chunks of four owned entries, so clustered expensive files can use
  several workers. Decision-only batches hold at most 8,192 entries; matching-line output retains
  the smaller 256-entry bound. Cold admission is described under Parallelism control; tails below
  16 run inline even after workers exist. Later larger batches can grow the executor up to the background-worker
  allowance. Completed results return in input order, including a trailing path-only print action.
  Cheap name/type matching stays inline to avoid scheduling overhead.
- Eligibility comes from audited descriptor capabilities, not names: only independent tests
  without metadata predicates, mutable run state or safety effects qualify. Reductions, templates
  and colored output can consume metadata on the coordinator after eligible filters run in workers;
  their eager metadata demand does not itself disable workers. Filesystem-native case probing and
  result-set controls still use the general evaluator. Archive-root probing
  does not disable host-file parallelism: mounted/archive-member entries flush pending host results
  and use the serial evaluator. Both paths keep the same VFS and safety preflight.
- Workers own each entry's content snapshot and rendered output. Predicates and match output reuse
  that snapshot; source bytes are released when evaluation finishes. The batch retains rendered
  records for ordered emission. Full line/context output buffers each input; filename/count/quiet
  selection streams where the VFS supports it, retaining the longest line and draining the source
  for binary/error checks. Neither the entry-count batch bound nor streaming limits record size.
- Each worker binds native and output RE2 state once to avoid shared DFA-cache contention. PCRE2
  workers share compiled patterns and reuse private match scratch, with bounded retained interpreter
  heap frames and independent reentrant leases. Entry content and lazy metadata have one owner and
  need no additional locks.
- A listing result may retain a completed directory read until the coordinator
  reaches it. Completed speculative results keep their storage charge but release
  execution slots, so cached siblings do not by themselves prevent deeper reads.
  No worker emits a match and no traversal sort collects all matches.

Comparison collects both trees sequentially on the same calling coordinator, reusing the
command's executor across the two walks. It then evaluates eligible large regular-file pairs
in batches of at most 64 on that executor and the caller, without creating a comparison-only
pool or asynchronous side coordinator. Each pair owns persistent read cursors; small patch inputs can be retained up to 64 KiB
per side. The coordinator publishes sorted results, summaries and patches. Archive/owned-source
pairs remain serial. This is not yet a completed-directory comparison pipeline.

## `--sort` modes

Six modes, separating root order from order below each root. `name` stays an alias for `dir`
(back-compat with the current `SortOrder::kName`).

| `--sort`  | Root order   | Order below each root                         | Result buffer |
| --------- | ------------ | --------------------------------------------- | ------------- |
| `none`    | command line | filesystem order                              | none          |
| `dir`     | command line | sorted child blocks, then their descendants   | none          |
| `subtree` | command line | sorted files, then contiguous sorted subtrees | none          |
| `tree`    | command line | sorted depth-first                            | none          |
| `roots`   | sorted       | filesystem order                              | none          |
| `global`  | sorted       | sorted depth-first                            | none          |

- **`none`** - retain command-line root order and each directory's `readdir`
  order. The coordinator, not a worker, visits entries, so workers do not race to
  emit; the filesystem order itself is simply unspecified.
- **`dir`** - each directory emits its complete direct listing (files **and**
  subdirectory entries) sorted as one block; the listing-blocks of different
  directories are then descended in that same order. A directory's direct children
  are ordered, but the result is not lexicographic depth-first.
- **`subtree`** - each directory emits its non-directory entries sorted, then
  inlines each descendable directory or container subtree contiguously in sorted
  child order. The coordinator emits each subtree directly; it does not retain a
  subtree's output in memory.
- **`tree`** - each root is path-ordered (subdirectories sorted into position
  too), fully reproducible across runs and machines. Root operands retain
  command-line order, so several roots do not form one global lexical order.
  This mode sorts directory listings; it does not collect all matching results.

- **`roots`** - stable-sort root operand spellings, then walk below each root
  exactly as `none` does. This makes root processing predictable without paying
  for sorted directory listings; roots are not canonicalized before sorting.

- **`global`** - stable-sort root operand spellings, then walk each root as
  `tree` does. The ordering key is hierarchical: operand spelling first, path
  within that root second. Duplicate and overlapping roots remain separate walks
  and may repeat paths.

All modes materialize the current directory's listing with the metadata its consumers need. With `-j > 1`, the
walker may additionally hold listings read ahead for its child directories. That
is traversal read-ahead, not matched-result buffering, and it does not change the
coordinator's visit order. Result formats have a separate buffering policy:
plain/nul/jsonl/csv/tsv stream, aligned/Markdown use `--buffer`, and tree-format
output builds its complete display tree. `--sort=score` is also separate: it
buffers and ranks the implicit listing after evaluation, while side-effecting
actions still occur in traversal order.

## Parallelism control

A single knob, `-j N` (long form `--jobs`), caps discovery, eager stat, eligible
content and file comparison at `N` participants for the entire command, including
its calling coordinator. The command owns one executor with at most `N-1`
background threads; standalone `Walk` calls likewise reserve one participant for
their caller.
The coordinator retains traversal and output ownership. The executor starts
lazily, grows only as useful work becomes available, and reuses its threads
across phases and mounted-filesystem walks until the command finishes.
Semicolon-form `-exec`/`-execdir` separately allows at most `N` outstanding child
processes; content matching does not run alongside execution actions in the same
expression. `-j 1` makes each part synchronous. `-j all` (`--jobs=all`) uses every
detected core (`hardware_concurrency()`) for these limits, regardless of the
active mode's default.

This ownership change remains an experiment, not an accepted performance result.
The caller claims independent stat, content and comparison chunks rather than
waiting idle while `N` additional workers run. Sequential comparison inventories
remove the second coordinator, but may reduce overlap when each tree has a
narrow frontier on slow storage; native A/B controls must quantify that tradeoff.
For directory reads, the first uncached required directory stays on the caller,
while the bounded frontier queues later siblings for background workers. A queued
read that becomes required can be claimed by its caller or its dispatched worker,
exactly once. The claim mutex protects callback ownership only; filesystem access
and result publication happen after unlocking. If a worker already owns that
required read, the caller waits for its result. It does not execute an unrelated
slow sibling merely to help the queue. This improves available participation, not
a guarantee of full utilization or sufficient useful work for each dispatch.
The bounded-frontier follow-up separates foreground stat/content/comparison jobs
from speculative directory reads. When both queues contain work, dispatch
alternates their classes, retaining FIFO order within each class. This is fairness
at job boundaries, not preemption: a large foreground drain or a blocking VFS call
still runs until it returns. Useful-work admission based only on remaining work,
task-size tuning and native timing/allocation validation remain required; the
thread-count and storage limits do not establish performance acceptance. The cold
matcher policy below is inherited for isolation, not endorsed by this change.

The coordinator currently admits directory read-ahead when at least 64 independent
sibling reads are available. This inherited count heuristic is provisional, not a
measurement of remaining cost. Each directory job performs one leaf VFS read plus
required metadata, publishes its result and yields before another directory.
Workers never recurse or wait on jobs in their own executor. Eager stat work still
uses drain jobs over 128-entry chunks after at least 512 entries are available.

Each admitted directory frontier keeps an ordered window of at most twice its
participating background-worker count. All nested and mounted-filesystem walks
also share a command-wide limit of twice the started background-worker count in
unfinished reads. A consumed parent request is not immediately refilled before
descent, allowing the deeper frontier to use available slots. Completion releases
an unfinished-read slot, while cached siblings keep their separate byte charges.

The experimental shared retained-storage allowance is 8 MiB. Every queued or
completed request first charges 512 bytes of bookkeeping plus its copied path;
completed speculative listings additionally charge vector/string capacities and
status storage. These conservative charges bound represented storage, not actual
allocator use or RSS; the 512-byte estimate and 8 MiB policy require native
allocation and performance controls. An unrepresentable or oversized speculative
listing is discarded and read on demand when visited. If the caller already needs
that result while its read is running, the required listing is transferred without
an additional lookahead charge, avoiding a second read. Required and ancestor
listings, in-flight VFS buffers and allocator overhead are outside this speculative
storage accounting; total traversal memory is not bounded by 8 MiB.

Reservations are released by their owners on consumption or destruction.
Per-request cancellation handles pruning, and a frontier-wide flag handles early
termination. Workers check both before and after each VFS read. Queued canceled
requests skip filesystem access; already running VFS calls cannot be interrupted.
The walker drains all its submitted jobs before its borrowed filesystem state
disappears, leaving the executor reusable by the next phase.

Content matching retains private evaluator and regex state for each logical
worker. Decision-only matching collects up to 8,192 entries before flushing;
rendered-content batches remain bounded at 256 entries because their output can
be large. A cold executor keeps cheap small batches on the coordinator until
8,192 entries have accumulated. A timed 16-entry prefix admits slower work
earlier when that prefix takes at least 500 microseconds. Once traversal or
matching has started the executor, batches of at least 16 entries may reuse it.
Each submitted matcher job drains entry chunks, and results are consumed in
original entry order.

Under `-j > 1` the serial `-exec ... ;` / `-execdir ... ;` form launches its child
on a bounded runner (at most `N` outstanding) instead of running it synchronously.
Because the child's exit status is not yet known when the action returns, the action
reports success on launch, so its truth value cannot gate a predicate to its right
(e.g. `-exec a \; -exec b \;` or `-exec test \; -print`). This matches find's exit
model: find's `;` form is a predicate whose nonzero exit makes only the action false
and never raises find's own exit status (verified against BSD and GNU find), so
nothing observable is lost at the exit-code level - the children are still reaped at
the end-of-walk drain. Use `-j 1` for find's strict synchronous, status-gating
semantics. The `+` batch forms are unaffected: they always accumulate and flush once
at end-of-walk, and a nonzero exit there does raise the exit status, as in find.

When `-j` is omitted the default is **style-scoped**. Invocation name and the
last explicit `--config=find|xff|rg` selector choose the style (see
design-config.md):

| Style  | Workers (no `-j`)          | Default `--sort` |
| ------ | -------------------------- | ---------------- |
| `find` | all cores                  | `none`           |
| `xff`  | `max(1, min(cores-1, 15))` | `dir`            |
| `rg`   | all cores                  | `none`           |

- The `find` and `rg` styles saturate cores and leave traversal order unspecified.
  All cores = `hardware_concurrency()`.
- `xff` is the good-citizen style: leave a core for the pipe consumer, cap
  at 15 to avoid oversubscription on many-core hosts, floor at 1 for single-core,
  and default to `dir` so output is nicely sorted out of the box.
- `-j N` overrides the worker count in every style; `--sort=...` overrides the
  default ordering in every style.

## Concurrency correctness

- **`-prune`** - the coordinator evaluates the entry and suppresses descent when
  the visitor returns prune. Known prune decisions are excluded from admission;
  destroying an unused speculative request cancels it. A read already started
  before that decision can still finish, but no entry from the pruned subtree is
  visited or acted upon.
- **`-quit`** - the coordinator stops visiting entries immediately. Workers only
  perform leaf directory reads and poll cancellation flags around each VFS call.
  Queued unused reads skip the filesystem; an in-flight call finishes before its
  walker is destroyed, even when the executor will be reused by a later phase.
  Their results are ignored, and no new expression actions are run. Exit status
  follows the normal model.
- **`-depth` (post-order)** - children before parent. The ordering layer holds a
  directory's own visit until its subtree has been emitted; under `none` this
  still means a parent waits on its descendants' completion.
- **`-maxdepth`/`-mindepth`/`-xdev`** - per-entry decisions, unaffected by
  concurrency.
- **Exit-code model** - per-path errors are reported through a thread-safe error
  sink and folded into the final status (design.md "Exit-code model"); a partial
  failure still yields the right nonzero code.
- **Thread-safety** - `vfs::FileSystem` is already documented thread-safe. The
  coordinator evaluates stateful expressions and writes the emit sink, capture map,
  and `--summary` accumulators. Matcher workers evaluate audited independent expressions and buffer
  entry output privately; directory-read workers never touch those objects.

## ThreadSanitizer

As soon as the walk is multithreaded we add a TSan run. TSan is mutually
exclusive with ASan, so it is a **separate** `--config=tsan` in `.bazelrc`
(`-fsanitize=thread` copt/linkopt, `TSAN_OPTIONS=halt_on_error=1`, the symbolizer,
mirroring the `asan` block) **and** its own `tsan` CI job wired into
the `done` gate. It lands in the same PR that introduces threads (it is a no-op on
single-threaded code). The `asan` config also runs UBSan as of #138.

## Implementation history

The traversal was delivered incrementally:

1. **Worker pool + `--sort=none`** - parallel walk behind the existing sink, plus
   the `tsan` config and CI job (threads arrive here, so TSan does too).
   The sequential walk stays available via `-j 1`.
2. **Ordering layer + `--sort=dir`** - generalize `SortOrder::kName` to `kDir`
   over the parallel walk (per-directory sorted listing blocks).
3. **`--sort=subtree`** - files-first contiguous-subtree traversal (`kSubtree`).
4. **`--sort=tree`** - sorted depth-first traversal within each root (`kTree`).
5. **`-j` / `--jobs` + style-scoped defaults** - the flag and the per-style
   worker-count and default-`--sort` wiring.
6. **`--sort=roots|global`** - stable root ordering, either alone or combined
   with tree ordering below each root.

## Metadata demand

The engine derives metadata demand once from descriptor capabilities and active output consumers:

- **Never:** names, known entry types and content predicates do not fetch full per-file metadata.
- **On demand:** a cheap predicate before a metadata predicate (for example `-name '*.cc' -size +1k`)
  fetches metadata only if evaluation reaches that predicate. Success or failure is cached per entry.
- **Always:** unconditional metadata predicates or consumers such as summaries, metadata templates,
  comparison and colored listings retain eager metadata, including parallel directory prefetch.

Roots, directories, unknown directory-entry types and followed symlinks still fetch metadata for
correct traversal, mount boundaries and loop detection. Metadata failures stop evaluation of that
entry; an OR branch cannot turn the failure into a match or execute an action. Missing entries honor
`-ignore_readdir_race`. A metadata-free listing can report an entry observed by directory enumeration
without an additional existence check, just as a name-only directory listing can race with removal.
New descriptors conservatively require basic metadata until explicitly audited; birth-time consumers
also declare `needs_birth_time`.

Field selection is independent of eager versus lazy demand. Ordinary traversal and size, owner,
mode and modification-time consumers request portable stat fields. Birth-time predicates and compiled
`{btime}` fields request creation time too; arbitrary entry callbacks retain complete metadata.
Linux basic requests use `stat`/`lstat`. Creation-time requests use one `statx` lookup for basic fields
and birth time, with a portable fallback when unavailable or incomplete. macOS already supplies birth
time in ordinary stat results, so it needs no extra call. The request is resolved once per walk;
a walk containing a birth-time consumer requests that field whenever its metadata is loaded.

Fuzzy predicates compile the extended query once. A truth-only lookup skips ranking work;
quality thresholds, `{fuzzy}`, result-set selection and score ordering retain scored evaluation.
Deferred-expression memoization is allocated only when the expression contains a deferred consumer.

Lazy metadata is exclusively owned by the coordinator after a directory-read future hands
off its listing. Its cached value and status need neither locks nor atomics. Matcher workers
receive owned entries without lazy loaders, and their eligibility excludes metadata predicates.
Presentation may consume the eager metadata carried by those entries after matching completes.
