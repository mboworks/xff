# Execution resources

Treat retained state, file-content reads, and concurrency as separate costs. `--buffer` controls
particular output buffers; it is not a process memory limit. `--jobs` controls directory workers
and eligible child commands; it is not a limit on bytes read or retained results.

## What a command retains and reads

| Operation                                         | Retained state                                                               | Content reads                                         | First relevant output                |
| ------------------------------------------------- | ---------------------------------------------------------------------------- | ----------------------------------------------------- | ------------------------------------ |
| Filename and metadata filtering                   | Directory listings and traversal state                                       | No file-content read required by the predicate itself | During traversal                     |
| Aligned listing                                   | Rows used to determine column widths                                         | Depends on requested fields                           | After the selected buffer window     |
| Fuzzy score ordering                              | Matching results for ranking                                                 | Depends on the expression and fields                  | After traversal                      |
| Summary or histogram                              | Aggregation keys and values; some scopes retain per-entry data               | Hash keys and line measures require content           | After aggregation                    |
| Tree comparison                                   | Both complete matched-entry inventories, plus comparison and reduction state | Paired regular files may require content comparison   | Comparison records follow both walks |
| Content predicates, grep, hashes, and line fields | Operation-specific file content or working state                             | Can require full file reads                           | Depends on the consumer              |

The table describes ordinary execution, not every backend's implementation. Archive access can
add decompression and retained container state. Configuration discovery, ignore handling, and
archive probing can also read data even when the expression only tests names.

In comparison mode the two walks run concurrently. Their inventories are merged after both walks
finish; this is not a directory-at-a-time comparison. Actions inside the expression can run during
the walks, so their output can precede comparison records and interleave between sides. A small
`--buffer` value does not bound either inventory.

Repeated content consumers need separate attention. Do not assume `-grep`, `{hash}`, `{lines}`, and
summary measures share one read. For example, the hash and line fields each request file content.
A warm operating-system cache can hide storage latency while the program still reads and processes
the bytes repeatedly. Network filesystems and archive members can make this distinction especially
important.

## Choosing controls

- Use filename and metadata predicates before expensive predicates when their logical meaning
  permits short-circuiting. Do not reorder actions or predicates that produce values consumed later.
- Use `--buffer=off` to avoid column-sizing buffering when alignment is unnecessary. This does not
  disable ranking, aggregation, comparison inventories, or directory read-ahead.
- Lower `--jobs` when directory read-ahead or concurrent children put pressure on resources.
  Directory workers and eligible semicolon-form execution have independent concurrency budgets;
  the setting is not one shared pool. Comparison has a walk on each side.
- Select only needed fields and reductions. Displaying a hash or line count adds work even when
  the matching expression itself is cheap.

## Inspecting an effective command

`--explain` includes an execution-resource view after configuration composition. It does not
run the search or evaluate actions. Configuration discovery still reads config files and,
with recursive `.xffrc` autoload enabled, may traverse directories. This is not a promise of
zero configuration I/O.

```sh
xff LEFT RIGHT --compare=summary --summary=ext --buffer=off --jobs=2 --explain
xff ROOT --format=aligned --columns=hash,lines --buffer=1MiB --explain
```

The first command reports two walks, two directory workers per walk, both complete comparison
inventories, and per-entry summary contributions. Turning alignment buffering off does not bound
those inventories. Eligible semicolon-form child commands have an independent per-walk worker
limit; this is not one shared limit across all worker pools.

The second reports two compiled content-reading field occurrences and a cell-byte alignment
buffer. The occurrence count describes active `hash` and `lines` field segments, not predicted
read calls: branch selection, matched entries, grep lines, and field evaluation determine actual
calls. Escaped literals do not count. Suppressed listing templates, templates replaced by columns,
and grep templates disabled by count mode do not count either. Config selections and CLI overrides
use the same resolved command as execution.

Other rows identify aggregation, collections, deferred selection, ranking, tree output, command
batches, shard grouping, and archive packing when applicable. Collection row/byte caps describe
stored entries and path/name/root text, not a process-memory ceiling. In particular, `--buffer=off`
disables column buffering but leaves an active collection without a row or byte cap.

The expensive-primary list comes from the registry's advisory cost tier. It is not a classification
of file reads: matching, computation, and process work can be expensive for different reasons.
The view gives no byte, peak-memory, or latency estimate; backends, archive probing, ignore/config
discovery, and runtime conditions contribute work that static inspection cannot measure.

## Measuring changes

A useful benchmark records the exact binary, command, dataset, backend, job count, and cache state.
Measure these separately:

| Measurement              | Meaning                                          | Caution                                                                           |
| ------------------------ | ------------------------------------------------ | --------------------------------------------------------------------------------- |
| Time to first output     | Invocation to the first byte read from stdout    | Disable paging and identify whether that byte is an action or a comparison record |
| Total elapsed time       | Invocation to process completion                 | Include exit status and reported errors                                           |
| Peak resident memory     | Maximum resident memory of the measured process  | Child-process memory is a separate measurement                                    |
| Logical content bytes    | Bytes delivered by instrumented VFS reads        | Distinguish repeated reads from unique file bytes                                 |
| Storage or network bytes | Backend traffic observed by the operating system | Page caches, compression, and read-ahead prevent equivalence with logical bytes   |

Use both broad directories and deep trees, local and network storage, and a repeated-content-consumer
case. Report warm-cache and cold-cache conditions honestly; do not label a run cold merely because
xff was restarted. Compare identical selected populations and output sinks. A slow consumer of
stdout can dominate elapsed time.

`python3 tools/measure_resources.py -- COMMAND...` measures one invocation in a fresh process
and emits a JSON record. It drains stdout without retaining the output, records its byte count
and first-byte latency, and retains the last 32 KiB of stderr for diagnosis. An invocation without
stdout has a null first-byte latency. The wrapper exits nonzero when the measured command fails,
while preserving its actual exit status in the record.

For example, use the same binary and roots for these workloads:

```sh
python3 tools/measure_resources.py -- ./xff --no-pager ROOT -type f
python3 tools/measure_resources.py -- ./xff --no-pager ROOT -type f --summary=ext
python3 tools/measure_resources.py -- ./xff --no-pager ROOT -type f --template='{hash} {lines}'
python3 tools/measure_resources.py -- ./xff --no-pager --compare=summary LEFT RIGHT
```

Run each invocation separately and repeat it across broad/deep fixture trees. The JSON records
include the exact argument vector, working directory, platform, elapsed/CPU time, and peak child
RSS in bytes. Peak RSS is the operating system's child high-water measurement, not a summed
process-tree peak or retained bytes attributable to a particular feature. The wrapper supports
macOS and Linux and normalizes their different RSS units. Its stderr spool and stdout draining
add measurement overhead; use the same sink and harness when comparing runs.

`--cache-state=warm` labels a run; it does not warm or flush caches. Record binary version/build,
fixture generation parameters, filesystem/backend, and cache preparation alongside the JSON.
The harness does not yet measure logical VFS bytes or storage traffic; runtime read accounting
remains audit work. Directory-at-a-time comparison is a separate design
question because ordering, filtering, virtual filesystems, and error handling affect correctness.

A [10,000-file broad/deep measurement example](benchmarks/execution-resources.md) records
repeated content consumers, time to first output, and peak RSS with those limitations.

## Directory-at-a-time comparison investigation

The current implementation has two explicit barriers in `RunTreeCompare`: both `RunFindCore`
invocations finish, then the ordered maps of matched entries are merged. `Walk` exposes entry
visits and traversal errors, but no callback saying that a directory's complete child listing
has been evaluated. A directory entry being visited is therefore not a completion signal.

A future streaming implementation needs a paired-directory coordinator. Each side may read ahead,
but the coordinator must know when both corresponding listings are complete before deciding
that an entry is present on only one side. An empty listing, an excluded or pruned subtree, an
unreadable listing, and a listing still in progress are different states. A failure must never
be interpreted as an empty directory. Matching remains based on each side's selected population;
unmatched parent directories can still contain matching descendants.

A bounded prototype should start with pure filters and status output, without changing existing
behavior for other commands. It would retain complete sibling listings and a bounded traversal
frontier, compare corresponding entries, and release completed entries and archive owners once
no pending comparison needs them. This reduces whole-tree retention, but does not establish a
fixed memory bound: one huge directory, deep traversal, content buffers, and backend state still
matter. Read-ahead needs backpressure so that a faster side cannot accumulate the entire tree
while waiting for the slower side.

Several contracts need separate decisions before replacing the current path:

- **Ordering:** current comparison records follow relative-path map order. Emitting all siblings
  before descending can change that order. For example, a path-ordered sequence can put `a/x`
  before sibling `b`; a directory-listing sequence puts `b` before `a/x`. Reducing memory must
  not silently change the chosen output contract.
- **Errors:** the current walk barrier suppresses comparison records if either walk reports an
  error, although expression actions may already have emitted output. Streaming records before
  a later traversal failure changes that behavior. Preserving the barrier requires deferred
  output, potentially with a separate spool; allowing partial output needs an explicit contract.
- **Actions and termination:** expressions can mutate entries, emit their own output, prune, or
  stop traversal. Reading paired content earlier can observe a different state from today's
  post-walk comparison. The first implementation should retain the existing path for commands
  whose semantics have not been proved equivalent.
- **Collected operations:** ranking, shard classification, and category-dependent reductions
  can retain data independently of traversal. Streaming the directory inventory alone does not
  remove those costs. Category reduction inputs must be attributed exactly once after the pair's
  status is known, preserving physical-entry and logical-result accounting.
- **Virtual filesystems:** archive members must keep their owning filesystem alive through content
  comparison and any deferred field reads. Paired paths may have different types or archive
  structures; comparison cannot assume identical traversal shapes.

Acceptance should compare records, ordering, counts, sizes, errors, and exit status against the
existing implementation on broad/deep trees, empty directories, one-sided subtrees, filtered
parents, pruning, type mismatches, archive members, and injected traversal/read failures. Measure
first-record latency and peak RSS separately from final summary latency. Network measurements remain outstanding. The isolated logical VFS read counts in the
[measurement report](benchmarks/execution-resources.md) complement local timings; neither
measures network or physical storage traffic. This investigation proposes a bounded prototype, not an enabled execution mode.
