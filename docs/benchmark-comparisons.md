# Comparisons with other tools

Benchmark tables compare xff, find, ripgrep, and fzf on explicitly equivalent tasks. Every warm-up
and measured run is checked against an independent fixture oracle. Wrong output, duplicate rows,
unexpected exit status, diagnostics, and timeouts fail collection. Performance differences are
informational. A 15% advisory alarm identifies normalized slowdowns without blocking merges.

## Matrix and sampling

Each tree shape has one table. Rows identify the task and tool. Columns are grouped by allocation:
Linux uses **1 CPU** and **3 CPUs** with enforced affinity; hosted macOS uses **1 worker** and
**3 workers** without affinity. Each group has a 1-2-5 progression from 10 through 10,000 files
before merge, extended through 100,000 after merge. HTML uses spanning allocation headers;
Markdown repeats allocation/count labels and aligns numeric cells and their source text to the right.

- Both PR CI and main's post-merge benchmark workflow run nine measured repetitions and average
  the fastest seven, pooling three rounds from each of three shards.
- One correctness-checked warm-up round runs every participant and is discarded. The starting
  participant rotates across tasks and rounds; measured samples are interleaved by round.
- Selection is by elapsed time. Associated CPU, latency, memory, and output metrics use that same
  selected subset. All raw samples are retained, including the discarded slower observations.
- Tables explicitly state the sampling policy, normally `mean of fastest 7/9 runs`. Raw-sample
  variability remains available separately. Historical reports retain their recorded policy.

PR tables compare against the newest successful retained main measurement with compatible cells.
Cells show `current ms / main ms (change)`;
change is `(current / main - 1) * 100%`, so positive means slower. The baseline commit, run, attempt,
and policy are explicit. Historical sampling policies may differ; this is not a significance test.

The report stores invocation settings (file counts, CPU counts, depth, fixture mappings), tree and
source hashes, result hashes, tool versions, executable hashes, build identity, platform, runner
class, CPU affinity, and filesystem type. Compatibility checks exclude sampling count and temporary
paths, but require the same measurement semantics, build configuration, environment contract, and
per-participant commands and competitor identities. Each compared cell must also have the same CPU/file allocation, fixture tree,
and expected results. Adding a new size or fixture preserves comparisons for overlapping unchanged
cells; changed/new cells show `n/a`. If no compatible main report exists, the table says so explicitly.
The first run of a changed contract may therefore establish a new baseline.
Without enforced affinity, host CPU count must also match. A three-CPU macOS measurement cannot
use a five-CPU measurement as its baseline. Local measurements keep their own runner class and
must not replace hosted observations; remeasure selected revisions on one local machine when a
local history is needed.
Use the [historical backfill workflow](benchmark-backfill.md) to build and measure a frozen revision
list in one host session, with a common driver and reference-tool set.

## Relative performance and advisory alarm

Each shape also has a CPU-grouped ratio table for participants doing the same task. It shows
`xff elapsed / reference elapsed` and the percentage difference; a ratio above one means xff is
slower. Exact selected averages and absolute differences are retained in `relative_results` in JSON.
Standalone fzf list filtering is never compared with xff discovery.

When a compatible main cell exists, the ratio table also shows normalized change:
`(PR xff/reference ratio / main xff/reference ratio - 1) * 100%`. Historical reports retain the
same ratios and all tool versions, so both xff and competitor developments can be inspected.
Relative measurements reduce some shared runner effects; they do not eliminate all machine noise,
workload effects, or changes in the reference tool. Changed competitor identities suppress baseline comparisons for that participant while retaining
unaffected cells, rather than silently attributing a tool upgrade to xff.

`--alarm-percent=15` is the initial advisory policy. A comparable cell whose xff/reference ratio
worsens by more than 15% versus main is recorded in the report and produces a CI warning. It does
not change the command's exit status or block merging. All scales are shown, including startup-heavy
small cases; the recorded sampling policy remains explicit. New or incompatible cells cannot
trigger an alarm without a baseline. Correctness failures still fail CI. A future blocking policy
can use the same evidence with a confirmation run; no performance block is enabled now.

## PR previews and the performance overview

The benchmark page defaults to merged history. Its **Source** selector also offers the latest
available preview for each open PR's current head; **Platform** selects Linux or macOS. Previews
are labeled explicitly and do not become merged measurements. A new push removes an older head
from selection on the next publication. Closing or merging a PR also removes its preview from
selection on that refresh; the original observations remain archived at their immutable URL.

The trusted publisher starts watching when PR CI starts. Once both benchmark comparison jobs pass,
it publishes their existing JSON results without waiting for unrelated tests. It does not collect
another set of measurements. Publication still waits for any active Pages deployment. The browser
loads public data from the site and needs no GitHub credentials. Preview publication does not mean
the rest of CI passed.

The publisher checks the current PR head, repository, run attempt, successful comparison jobs,
artifact creation times, and the tested merge commit's parents. Reports preserve both the PR
branch head and tested merge commit, the base parent, and artifact identity. Only code checked out
from `main` executes with publication permissions. Uploaded HTML is ignored; bounded JSON is
validated and rendered again. Waiting happens outside the shared deployment lock. Preview updates
reuse merged pages and their catalog instead of rebuilding every historical chart.

The compact **Performance overview** appears before the detailed tables, in the selected chart
view, and in one bot comment updated on the PR after successful publication. It gives:

- The merged baseline commit, run, attempt, and sampling policy.
- A typical change: the equal-weight geometric mean of compatible XFF/reference change factors.
- Counts of faster, within-threshold, and slower cases, plus unavailable comparisons.
- The five largest regressions and a visible advisory warning when any case exceeds 15% slower.

For each matched case the factor is `(current XFF / current reference) / (main XFF / main reference)`.
This compares both XFF measurements on the same reference-time scale: multiplying each by a shared
window reference mean divided by its own reference time gives exactly the same factor. It does
not average XFF measurements across revisions. Positive change means slower; negative means faster.
All file counts participate, including startup-heavy small cases. An improved aggregate cannot
hide a warned individual case. Missing baselines are unknown, never zero regressions. The workload
grid contains correlated cases, so the counts and aggregate are descriptive, not a significance test.

Reference-window presentation remains separate: a preview can use compatible merged references
plus itself, but can never enter another preview's window or change a merged window. The first
five/current-plus-four rule still applies. Raw timings are unchanged. The PR comment updates only
for the current head and latest selected attempt; it never edits a human's comment. No performance
failure gate is enabled by this publication work.

## Fixture inputs

A manifest is self-contained UTF-8 text, one entry per line. Paths are relative to an isolated root;
parent directories are created automatically. Blank lines are ignored. There are no comments,
escape sequences, shell expansion, or sidecar files. Split at the first `=`; subsequent `=` bytes
belong to the content. Text forms preserve whitespace and add no newline. LF or CRLF terminates a
manifest line; encode embedded newlines or literal carriage returns with base64.

```text
empty.txt
empty-dir/
src/main.cc=:int main() {}
docs/note.txt=t:Hello world!
data/bytes.bin=b:AAECAwQ=
main.cc=l:src/main.cc
src/current.cc=l:main.cc
dangling=l:missing.txt
loop=l:loop
```

| Form            | Meaning                                                  |
| --------------- | -------------------------------------------------------- |
| `path`          | Empty regular file                                       |
| `path/`         | Explicit directory, including an empty directory         |
| `path=:text`    | Literal UTF-8 content                                    |
| `path=t:text`   | Explicit equivalent of `=:`                              |
| `path=b:BASE64` | Strict base64 decoding into exact bytes                  |
| `path=l:target` | Symlink; relative target resolves from the link's parent |

Unknown forms, including `=f:`, are errors. Duplicate entries and file/directory conflicts are
errors. Links are created after regular files and directories; fixture creation never follows them.
Absolute paths, parent traversal in entry names, and link chains escaping the root are rejected.
Internal dangling links and cycles are allowed. Excessively complex link expansion is rejected.

A tar archive, optionally compressed (`.tgz`, `.tar.gz`, etc.), is an alternative for larger content
or an existing tree. Extraction happens before timing through the same validated entry model;
files, directories, and symlinks are supported. Hard links, devices, FIFOs, duplicate names, and
escaping paths/links are rejected. Archive ownership, timestamps, and permissions are not restored:
these are normalized search fixtures, not metadata-preservation tests. Imports are bounded to
200,000 entries and 1 GiB of file content. Both the input checksum and canonical tree hash are stored.

Supply repeatable shape/count mappings to replace generated fixtures:

```sh
python3 tools/benchmark_compare.py --binary=/path/to/optimized/xff --report=report.json \
  --fixture broad:10=fixtures/broad-10.manifest \
  --fixture broad:100=fixtures/broad-100.tgz
```

The count is the exact number of regular files in that input. Directories and symlinks do not count;
inputs are never silently truncated or replicated. Missing shape/count combinations appear as `n/a`.
Without custom inputs, deterministic broad/deep trees are generated: the requested number of regular
files (including `.gitignore`), plus a file symlink and a directory symlink. Broad is flat; deep distributes files
across up to 40 nested levels, capped by the requested count. The `.gitignore` is included in
reported input counts and throughput. No third-party corpus is downloaded.

## Tasks and equivalence

| Task                   | Participants           | Contract                                              |
| ---------------------- | ---------------------- | ----------------------------------------------------- |
| File enumeration       | xff, find, rg          | All regular files, including hidden and ignored names |
| Safe enumeration       | xff                    | Same population with `--safe`                         |
| Basename `*.txt`       | xff, find, rg          | Case-sensitive suffix selection                       |
| Content selection      | xff, rg                | Literal `needle` and no-match `absent_marker`         |
| Discovery plus fuzzy   | xff, find piped to fzf | Case-sensitive subsequences `itm`, `hdn`, and `zzzz`  |
| Precomputed-list fuzzy | fzf                    | Same candidate paths and queries; excludes discovery  |

Results are unordered NUL-delimited path multisets. No symlinks are followed. Sorting, color,
pagers, rg configuration, and fzf defaults are disabled. xff requests `--no-config`; mandatory host
policy that changes results causes correctness validation to fail. The production VFS and safety
checks remain active. Safe enumeration measures read-only safe-mode overhead, not mutation-policy
costs. Content tasks are explicitly skipped for fixtures containing NUL bytes: xff and rg do not
have equivalent binary-skipping semantics. Other tasks still measure those binary fixtures.

Fuzzy tasks compare plain ASCII subsequence acceptance, not scoring, ranking, extended-query syntax,
or interactive responsiveness. Standalone fzf list filtering has no xff counterpart and must not be
compared with discovery time. The end-to-end pipeline includes both find and fzf. No cross-scope
speed ratios are generated.

## Storage, CPUs, and accounting

Hosted fixtures use verified Linux `/dev/shm` tmpfs; there is no silent disk fallback. This excludes
ordinary backing-device I/O from fixture reads while retaining filesystem calls, metadata, VFS
routing, and safety checks. Tmpfs can swap under memory pressure. Executables and system config
remain outside the fixture-storage contract. Fixtures are prepared before timing, warmed up, then
reused without cache flushes. These are not cold-storage benchmarks.

Linux workers bind to one/three CPUs from the runner's allowed affinity set before timing, and child
processes inherit that allocation. The entire find/fzf pipeline shares it. xff receives matching
`--jobs`, rg matching `--threads`, and fzf matching `GOMAXPROCS`. Find remains single-threaded.
Affinity does not reserve physical cores or eliminate hosted-runner contention.

GitHub documents four CPUs for public Linux runners and three for standard ARM64 macOS runners.
Both hosted grids request one/three workers. Linux leaves one vCPU outside the tools' affinity
mask for background work; this is headroom, not an exclusive reservation. macOS has no CPU
pinning and uses
ordinary temporary storage and scheduler placement. See
[GitHub's runner specifications](https://docs.github.com/en/actions/reference/runners/github-hosted-runners).
On every platform, collection rejects a requested allocation larger than the available CPU count
before creating any fixtures or measuring smaller allocations. Linux checks the allowed affinity
mask; other platforms check the host CPU count. An unknown capacity is an error. Requiring
affinity on an unsupported platform is also an error.

CI runs the same preflight before builds and tool installation. It can be run independently:

```sh
# Linux: use three CPUs, leaving headroom on the standard four-vCPU runner.
python3 tools/benchmark_compare.py --check-runner --cpus=1 --cpus=3 --require-cpu-affinity
# Standard hosted macOS: require capacity for three workers without claiming pinning.
python3 tools/benchmark_compare.py --check-runner --cpus=1 --cpus=3
```

The standalone comparison driver defaults to one/four requested workers. The local backfill
tool instead requires an explicit grid; use one/three/ten on a suitable host. Both validate capacity. A native macOS
worker limit does not bind threads to specific CPUs. Mach's affinity policy describes cache-sharing
hints, not a CPU mask, and is unsupported on the tested Apple Silicon host. See
[Apple's policy definition](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/mach/thread_policy.h).
A macOS VM can limit virtual CPU count but represents a different measurement environment.

Each invocation uses a fresh measurement worker. Commands run without a shell; stdout is drained
through a pipe and stderr captured. Validation is outside timing. Both pipeline processes contribute
CPU and exit status. Single-process RSS is its high-water mark; pipeline memory is the sum of
individual high-water marks, an upper bound rather than simultaneous peak memory. Missing stdout
latency is unavailable, not zero. Throughput divides input files by the selected mean elapsed time.
A 60-second deadline terminates the worker process group. These are leaf commands; arbitrary child
process trees are outside this accounting contract.

## Running and publication

Build with `--config=clang_release`. The driver is also available as `//tools:benchmark_compare`.

```sh
python3 tools/benchmark_compare.py --binary=/path/to/optimized/xff --report=report.json \
  --repetitions=9 --keep=7 --require-tools \
  --fixture-parent=/dev/shm --require-memory --require-cpu-affinity \
  --summary=report.md --html=report.html
```

Repeat `--files` or `--cpus` to choose other scales. Use `--baseline-root=PATH` for a retained
benchmark directory. `--build-identity` and `--runner-class` establish comparable invocation settings.
For a small local correctness check, use `--files=12 --depth=3 --cpus=1 --repetitions=1 --keep=1`.
Without required-memory/affinity options, macOS records ordinary temporary storage and null affinity;
such results are not comparable with the hosted tmpfs/affinity measurements. Missing competitors
are explicit skips unless `--require-tools` is set.

PR measurement is a job in the existing test workflow. Its Markdown table appears in the job summary;
JSON, HTML, and Markdown are artifacts. Correctness/measurement failures fail CI; timing deltas do
not. Main's 7/9 measurements join the existing post-merge history and publication workflow. No extra
cache generation is created. Historical reports and exact-commit release links remain supported.

```sh
bazel test //tools:benchmark_fixture_test //tools:benchmark_matrix_test \
  //tools:benchmark_compare_test //tools:benchmark_history_test
```

Rendered HTML ratios use dark red when xff is slower than the reference and dark green
when faster; equal results stay neutral. The numeric ratios remain visible in every
format, and the separate change versus main retains neutral styling.

HTML uses two numeric subcolumns per file count: elapsed milliseconds and the signed
time difference from main (with percentage change below), or ratio and percentage difference. Each numeric cell is right-aligned; task/tool names are
left-aligned. A ratio change versus main stays on its own line below the difference.

Broad and deep HTML sections share one table per metric family. Shaded `FS tree` rows identify
`Broad` and `Deep`, with slightly larger, left-aligned titles. Each section repeats column headers;
both datasets retain identical numeric column widths. Task names and `xff / reference` labels occupy
separate left-aligned columns.

Post-merge measurement runs on both Linux and macOS through the existing benchmark workflow.
Each platform retains its own HTML report and raw JSON under the workflow run and attempt;
release reference pages link to the exact tagged commit's retained results for each platform.
Missing platform results are stated explicitly. PR measurements remain previews.
Linux uses verified tmpfs and CPU affinity. macOS uses ordinary temporary storage and requested
worker counts without CPU affinity; the report records this distinction. Platform histories remain
separate and must not be compared as if their hardware or storage were equivalent.

HTML page titles and headings identify the recorded OS and architecture. Platform reports stay
separate to keep the scaling tables readable; release reference pages link to each platform.
Allocation headers say CPUs when affinity is enforced and workers otherwise. Rendering historical
results uses their recorded provenance, never the rendering host's identity.

Both PR and post-merge workflows retain separate macOS and Linux artifacts. Data and rendered
pages share a basename, for example `benchmark-pr-macos-arm64.json` and
`benchmark-pr-macos-arm64.html` (or `benchmark-report-linux-x86_64.json` and `.html` post-merge).
The task and tool/comparison occupy separate left-aligned columns; all measurements stay right-aligned.

Download a run's artifacts and render the saved observations with the current renderer:

```sh
gh run download RUN_ID --pattern 'benchmark-pr-*' --dir benchmark-artifacts
python3 tools/benchmark_compare.py --render-only \
  --report=benchmark-artifacts/benchmark-pr-macos-arm64/benchmark-pr-macos-arm64.json \
  --html=benchmark-artifacts/benchmark-pr-macos-arm64/benchmark-pr-macos-arm64.html
```

Repeat with the Linux artifact's basename for its page. `--render-only` never invokes a measured
tool or changes the input JSON. Historical platform identity comes from that JSON, so rendering
Linux observations on macOS does not relabel them. The same option can regenerate `--summary`
Markdown. CI artifacts are retained for 30 days; published post-merge history retains the raw JSON
alongside each platform's page for later rendering.

Measurement progress is flushed to stderr at most once every five seconds, with immediate
start/completion lines per scale. Updates put `Workers: N Files: N` first, followed by
scale X/Y, fixture preparation, or `Run: X/Y`
within the current task, including the tool and warm-up/sample index. Small scales therefore
produce little output. Progress output and correctness checks happen outside the timed child
commands; scale completion confirms all its output validation finished.

The macOS job has a 60-minute pre-merge budget. Post-merge, its build and measurement jobs each
have a 90-minute budget. Linux has 30 minutes before merge, then 45 minutes for its paired build
and 60 minutes per measurement shard after merge. These are job ceilings, not requested
measurement durations; individual benchmark invocations retain their timeout.

PR measurements use 10, 20, 50, 100, 200, 500, 1,000, 2,000, 5,000, and 10,000 files.
Post-merge adds 20,000, 50,000, and 100,000 files. Both retain the fastest 7 of 9 samples.
Baseline comparisons use matching cells at shared sizes; extra main-only scales
do not invalidate comparisons at the smaller sizes. Explicit repeated `--files` values override
the default grid for local experiments.

## Measurement shards

PR and main runs build once per platform, then distribute the exact binary and one shared plan
to three Linux runners and three macOS runners. Every shard measures the complete file-count,
worker-count and broad/deep matrix. All tools for each measured round run together on one runner,
with rotating participant order; the starting order also rotates between shards.

| Run  | Shards | Measured rounds per case per shard | Pooled rounds | Retained fastest rounds | Largest file count |
| ---- | -----: | ---------------------------------: | ------------: | ----------------------: | -----------------: |
| PR   |      3 |                                  3 |             9 |                       7 |             10,000 |
| Main |      3 |                                  3 |             9 |                       7 |            100,000 |

Each shard performs its own discarded, correctness-checked warm-up for each case. The aggregator
combines raw observations before selecting the fastest seven per participant. It never
averages shard summaries or discards a shard's slow rounds before pooling. All metrics use the
same selected observations as elapsed time. The raw JSON retains every measured observation,
its shard and round number, the shard's hashed host identity, and the original invocation paths.

Repeating the same matrix on all three runners gives approximately equal expected work and avoids
assigning one tree shape or worker count to a consistently faster host. It does not make hosts
identical: fastest-run selection may favor samples from a faster host, and the selected hosts
can differ between tools. Linux still pins one or three logical CPUs; macOS records worker
requests without claiming CPU affinity. Hardware, storage and background load still affect results.

Warm-ups and fixture creation are repeated on every shard. Both workflows execute twelve tool
rounds including warm-ups per case, versus ten in a serial nine-sample run. Each shard executes
four rounds including its warm-up, allowing an ideal 2.5-fold reduction in measurement wall time
relative to that serial run. Build, fixture setup, artifact transfer, aggregation and runner
availability limit the end-to-end gain.

Aggregation requires the complete planned matrix on every sample shard, exactly the planned raw
sample count, matching source, binary, reference tools, fixture contents, expected outputs and
commands, compatible CPU affinity, equal host CPU counts and identical sampling contracts.
Missing or incompatible shards fail the workflow rather than publishing a partial matrix. Each
merged report retains per-shard provenance. An identity mismatch reports its shard, field and
differing values, such as `incompatible shard 1: contract.cpu_count: expected 5, got 3`.
Both PR and merged-main aggregation load retained main reports before rendering the overview.
Only first-parent main revisions preceding the measured commit are eligible (for a PR merge
preview, this includes its main parent). The nearest compatible revision wins; later reruns of
older revisions cannot replace it, and a rerun never selects its own commit or a future merge.
The selected revision, run, attempt and sampling policy remain in the report.

Historical baselines and the advisory slowdown alarm are computed after pooling. JSON and HTML
share the platform-specific basename; only final merged artifacts are selected by the publisher.

`tools/benchmark_shards.py --partition=samples --count=3` creates these plans. `--repetitions`
sets the total measured rounds and must divide evenly among shards; `--keep` selects the final
fastest subset. The collector must retain every assigned round. The default `balanced` partition
continues to support disjoint, cost-balanced fixture plans and existing retained reports.
Historical CI backfill campaigns and local batches retain their documented one-host-per-revision
contract; their scheduling is separate from these ordinary PR and main workflows.

## Comparison landscape

`tools/benchmark_landscape.py` renders existing comparison JSON as a standalone interactive
HTML landscape. It requires no remeasurement. The 3D chart is open by default; its
show/hide disclosure collapses the chart, controls and explanation without hiding the tables.
Reopening resizes the chart to the available width. Absolute measurements are collapsed by default;
the comparison table stays visible below the plot. Benchmark publication inserts the landscape above all tables on retained reports with the
one-worker/one-larger-allocation matrix, including both hosted platforms' 1/3 groups and historical 1/4 groups.
Axis labels, hover cards and explanations use the recorded allocations. Older reports without
that matrix retain their existing presentation.
The benchmark overview also shows the same landscape with a **Platform** selector and **Version**
slider. Versions are retained measured commits (including branch/PR/tag labels), not only releases.
The slider runs oldest to newest and initially selects the newest measurement on the first listed
platform. It includes only reports with the required comparison matrix, and keeps the latest
run/attempt for each platform and commit. Platform labels include architecture when recorded.
Switching platforms preserves the selected commit when available; otherwise it selects that
platform's newest report. A single available version disables the slider.

The chart's upper-left panel holds the version slider above a compact table: version position,
linked commit, PR/release, revision date, measurement date when recorded, and platform. All rows
remain visible, using explicit placeholders for missing information. Platform details come last
in a reserved three-line area; their full text remains available in the tooltip.
Revision date identifies the code revision. Measured uses the recorded completion date for both
CI backfills and local runs; older revisions can therefore have newer measurement dates.
The commit opens the full report, including tables and raw data;
its tooltip retains the full branch, time and run/attempt information. The performance scale sits
in a separate outlined panel directly below it, with inset endpoints to keep long tick labels
inside the border. Scale, order and camera state survive version changes. Each chart retains its
own measurement contract and automatically scaled axes/colors: moving the slider is exploration,
not a paired cross-host or cross-version regression claim. Missing versions are not interpolated.
The overview downloads only the selected report's generated landscape JSON. While loading, the
previous chart and its report link remain visible; the new data replaces them in place without
collapsing the chart or resetting the camera. Interrupted requests cannot overwrite newer
selections. A reserved status line keeps the version card and the panels below it stationary
during loading and failure transitions. Longer errors remain available in the status tooltip.
On failure the panel explains which requested version could not load; the previous
chart and its matching metadata remain visible, and the slider remains usable. Generated payloads
expire with their raw reports under existing retention.

The version selection includes links to the matching PR and release when those references are
known. The chart reserves its full height before loading; changing versions, worker pairs, range,
or scale does not collapse the rendering area. Reports with three measured worker counts offer
all three pairs (`1/3`, `1/10`, and `3/10`); no unmeasured allocation is synthesized.

### View range and selection

**Range** offers Auto and symmetric limits of +/-20%, 50%, 100%, or 200% in percentage mode;
logarithmic mode uses +/-0.2, 0.5, 1, or 2. The default is +/-100% (+/-1 in logarithmic mode),
so changing versions retains the same vertical scale. **Cap** places outliers at the boundary and marks
them bright green or red. **Cut off** hides outlying observations and surface cells touching
them, leaving gaps rather than inventing measurements. Hover cards always retain actual values.

A black bar with a yellow outline marks the hovered measurement on the color legend. Capped
values pin it to the appropriate endpoint. Hovering over the legend temporarily emphasizes values
above the pointed-to performance level. **Below threshold** offers increasing transparency:
50% transparent (the default), 75% transparent, or Hide. **Threshold plane** shows a subtle
horizontal plane at that level and highlights its exact performance value on the vertical axis,
including between ticks. It is enabled by default. Leaving the legend restores the full view
and removes the plane and its axis highlight.
The two preferences persist across version, worker-pair, and scale changes. This preview changes
neither the selected range nor the source data.

### Reference-window timing normalization

**Timings** selects raw or reference-normalized hover information. For each compatible task,
tree shape, fileset size, worker allocation, platform/machine series and reference-tool identity,
take the arithmetic mean of five reference times. The first five measurements share the first
five-reference window; starting at measurement five the window is the current reference plus
the four preceding references. Fewer than five compatible measurements make normalization
unavailable; the card explains this and retains raw timings.

The correction is `window reference mean / this run's reference time`. Multiply only this
version's XFF timing by that factor: XFF results are never averaged across versions. Within-run
fastest-sample selection remains the recorded measurement policy. The card shows the normalized
XFF time, reference mean, correction, and separate rows for the window's measurement count
(or unavailability reason), window start date and window end date. Unavailable windows omit the date rows.
The card presents these values together with task, tree, allocation and file count in an aligned table.
Each report's generated `normalization.json` retains the full window identities and commit hashes.
Reference binaries, invocation arguments,
fixtures, machine series and CPU allocations must match; CI and local series remain separate.
Ordinary CI and its replacement backfills share reference history: the recorded runner labels
`github-hosted ubuntu-latest` / `github-ci-linux` and
`github-hosted macos-latest` / `github-ci-macos` identify the same respective hosted runner
classes. This label equivalence does not relax machine, reference, fixture or allocation checks.

This is presentation only. Raw report JSON and the measurement tables remain unchanged. The
percentage/factor landscape also remains unchanged because its same-run XFF/reference ratio
already applies the reference correction. Normalized absolute timings are useful for viewing
variation shared with the reference tool, but cannot eliminate XFF-specific noise.

One shared, pinned Three.js renderer bundle is published alongside the reports; no external CDN is used.
Standalone previews embed the same bundle for offline use.

The X coordinate mirrors log10 file counts: the 1-CPU group runs from large to small on the
left, and the 4-CPU group runs from small to large on the right. Reports without affinity use
worker labels instead. Y is the vertical percentage axis; Broad and Deep task/reference pairs
extend in opposite directions on Z. Each quadrant forms a separate surface, with missing cells
left empty. Connections between categorical tasks are visual interpolation, not a predictive
model. Colors use a shared symmetric scale. The near-neutral blue band spans -10 to +10 percentage
points, transitioning to dark red/green at -20/+20 points and bright red/green at the extremes.
The color scale extends to at least +/-20 points even for nearly neutral reports.

The plotted percentage is `100 * (1 - xff_time / reference_time)`: positive means xff is faster, negative means xff took longer. It reverses the sign of the existing table difference.
The Scale selector defaults to Percentage, with `%` on axis and color legend ticks.
The performance factor uses `reference_time / xff_time`: `2x` means twice as fast, `0.5x` means
half as fast, and `1x` is parity. Heights use log10 spacing, so reciprocal factors have
equal distances from parity. Logarithmic uses uniformly spaced signed log10 tick labels; a 1-2-5 sequence selects
the interval, not individual tick positions:
zero is parity, +1 means 10x, and -1 means 0.1x. Bounds are symmetric around zero. The color
mapping remains based on time saved in both views, with signed log10 labels on the factor legend. Hover cards retain actual factors.
Switching metrics preserves the camera and selected task order.
Hover cards show the original ratio and both absolute timings. Picking uses the nearest projected
corner of the intersected surface triangle, with its original row/column identity. Matching file
and task ticks are highlighted; dashed guides project the selected measurement to all three axes.
The exact vertical value is shown at the height guide, including between existing ticks.
The estimator comes from the report's recorded sampling policy.

Build the MIT-licensed Three.js renderer with its pinned npm dependencies. Node is needed only to
build and test the renderer; generating pages uses Python's standard library and the existing
benchmark matrix module:

```sh
npm ci --prefix tools/benchmark_web --ignore-scripts --no-audit --no-fund
npm run build --prefix tools/benchmark_web
python3 tools/benchmark_landscape.py /path/to/benchmark-report-linux-x86_64.json \
  --renderer-js=tools/benchmark_web/dist/landscape.js \
  --output=/path/to/benchmark-report-linux-x86_64.html
```

The trusted bundle is embedded with the complete Three.js MIT license. Offline pages need no CDN
or network access. Published pages share one bundle under the benchmark assets directory.
Drag to rotate, scroll to zoom, and right-drag to pan. Axis titles are printed on the base plane;
rotation remains unrestricted. Tick labels stay horizontal and extend outward from their axes.
Focus the canvas for arrow-key rotation, +/- zoom and Home reset, or use the Reset view button.
Hover cards occupy the third panel in the chart's upper-left information column, below the
version and performance-scale panels. Showing or hiding a hover card does not move the panels
above it or change the chart height; guides still highlight the selected measurement.
Missing WebGL2 leaves an explanatory message and the complete measurement tables available.
The chart resizes when its container changes or its disclosure reopens. Rendering is event-driven;
there is no idle animation loop. Single observations remain visible as points even when missing
neighbors prevent a surface quad.

Browser tests run in the normal release-site CI job, using Chromium. Run them locally with
`npm test --prefix tools/benchmark_web` after `npx playwright install chromium` from that directory.
`CHROME_PATH` can select an existing Chrome executable for these tests, and `PYTHON` selects the
Python interpreter. Tests cover offline loading, hover identity for all orders and scales, axis
highlights, variable color-stop counts, full rotation, resizing and hide/show behavior.

The task-order selector defaults to **Similarity, better toward center**. Each task/reference
pair has a profile of percentage values across matching file counts, CPU groups and tree shapes.
Distance is the mean absolute percentage-point difference over shared observations. The renderer
finds the minimum-total-distance neighbor chain exactly for up to 16 pairs, with deterministic
ties, and reverses it when necessary so the better endpoint faces the center. Broad and Deep use
the same mirrored order. This reduces surface jumps without claiming monotonic improvement.
For larger reports, a bounded multistart nearest-neighbor heuristic replaces the exact search.
If no complete chain with shared observations exists, the renderer falls back to average order.
Missing observations are never replaced with zero.

**Average performance, best at center** sorts descending by equally weighted mean time saved;
**Alphabetical** restores the task/reference name order. Changing order preserves the camera
and measurements. Every recorded cell has equal weight; file counts are not weighted by the
number of files or elapsed time. The average is descriptive, not aggregate throughput.

## Published data retention

Release and merged-PR measurements do not expire by report count. Raw observations are retained
losslessly as compressed JSON, and chart/report views share packed assets. The site-wide storage
review threshold is 200 MB; see [Published site storage](site-storage.md).
