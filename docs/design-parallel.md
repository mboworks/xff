# xff - Parallel Traversal Design

> Detailed spec for parallel directory traversal and `--sort` (issue #43).
> **Refines** `design.md` section"Cross-cutting concerns" (Determinism). Where the two
> differ, this document is authoritative for traversal/ordering specifically.
> Status: **Implemented** / 2026-06-27 / Org: MBO Works

## Purpose

The directory walk is the hot path and it is IO-bound: most time is spent
blocked in `readdir`/`lstat`, not in CPU. Running it across several workers
hides that latency and is the single biggest performance lever for xff,
especially on deep trees and high-latency (network) filesystems.

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

A bounded directory-read worker pool with a single coordinator:

- Workers perform only `ReadDir` plus metadata lookup and return complete listings.
  The coordinator submits sibling-directory reads ahead, then consumes their
  futures in the order required by `--sort`. Directory workers start only when at least
  two sibling directories provide independent work; a flat directory or single-child
  chain does not pay for idle directory threads.
- The coordinator applies traversal controls and writes the output sink in traversal order.
  Independent content predicates can evaluate in a separate bounded worker pool; all stateful
  expressions, mutations and execution actions retain the coordinator evaluator.
- Content matching batches at most 256 owned entries and schedules chunks of 16, so one broad
  directory can use several workers. Fewer than 64 entries run inline without starting matcher
  threads. Completed results return to the coordinator in input order, including a trailing
  path-only print action. Cheap name/type matching stays inline to avoid scheduling overhead.
- Eligibility comes from audited descriptor capabilities, not names: only independent tests
  without full metadata, mutable run state or safety effects qualify. Reductions, templates,
  colored output, filesystem-native case probing and archive diving currently use the general
  evaluator. Both paths keep the same VFS and safety preflight.
- A listing future may retain a completed directory read until the coordinator
  reaches it. No worker emits a match and no traversal sort collects all matches.

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

A single knob, `-j N` (long form `--jobs`), configures independent limits: up to
`N` directory-read workers, up to `N` eligible content-matcher workers, and at most
`N` outstanding semicolon-form `-exec`/`-execdir` children. These are separate
limits, not one shared `N`-operation budget. Content matching does not run alongside
execution actions in the same expression. `-j 1` makes each part synchronous.
`-j all` (`--jobs=all`) uses every detected core (`hardware_concurrency()`) for
each limit, regardless of the active mode's default.

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
  the visitor returns prune. Depending on the ordering mode, batched read-ahead
  may read the pruned directory's listing before or after that decision, but no
  entry from the pruned subtree is visited or acted upon.
- **`-quit`** - the coordinator stops visiting entries immediately. Workers only
  perform leaf directory reads and do not inspect the walk's stop flag; already
  submitted reads finish while the pool is destroyed. Their results are ignored,
  and no new expression actions are run. Exit status follows the normal model.
- **`-depth` (post-order)** - children before parent. The ordering layer holds a
  directory's own visit until its subtree has been emitted; under `none` this
  still means a parent waits on its descendants' completion.
- **`-maxdepth`/`-mindepth`/`-xdev`** - per-entry decisions, unaffected by
  concurrency.
- **Exit-code model** - per-path errors are reported through a thread-safe error
  sink and folded into the final status (design.md "Exit-code model"); a partial
  failure still yields the right nonzero code.
- **Thread-safety** - `vfs::FileSystem` is already documented thread-safe. The
  coordinator alone evaluates expressions and writes the emit sink, capture map,
  and `--summary` accumulators; directory-read workers never touch those objects.

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
New descriptors conservatively require complete metadata until explicitly audited.

Fuzzy predicates compile the extended query once. A truth-only lookup skips ranking work;
quality thresholds, `{fuzzy}`, result-set selection and score ordering retain scored evaluation.
Deferred-expression memoization is allocated only when the expression contains a deferred consumer.

Lazy metadata is exclusively owned by the coordinator after a directory-read future hands
off its listing. Its cached value and status need neither locks nor atomics. Matcher workers
receive owned entries without lazy loaders, and their eligibility excludes metadata consumers.
