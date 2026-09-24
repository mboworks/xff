# Comparisons with other tools

Benchmark tables compare xff, find, ripgrep, and fzf on explicitly equivalent tasks. Every warm-up
and measured run is checked against an independent fixture oracle. Wrong output, duplicate rows,
unexpected exit status, diagnostics, and timeouts fail collection. Performance differences are
informational. A 15% advisory alarm identifies normalized slowdowns without blocking merges.

## Matrix and sampling

Each tree shape has one table. Rows identify the task and tool. The eight standard columns are
grouped by CPU allocation: **1 CPU** with a 1-2-5 progression from 10 through 10,000 files, then **4 CPUs** with the
same sizes. HTML uses spanning CPU headers; Markdown repeats CPU/count labels and aligns numeric
cells and their source text to the right.

- Normal PR CI runs three measured repetitions and averages the fastest two.
- Main's post-merge benchmark workflow runs nine and averages the fastest seven.
- One correctness-checked warm-up round runs every participant and is discarded. The starting
  participant rotates across tasks and rounds; measured samples are interleaved by round.
- Selection is by elapsed time. Associated CPU, latency, memory, and output metrics use that same
  selected subset. All raw samples are retained, including the discarded slower observations.
- Tables explicitly say `mean of fastest 2/3 runs` or `mean of fastest 7/9 runs`. Raw-sample
  variability remains available separately. Historical median-based reports remain readable.

PR tables compare against the newest successful retained main measurement with compatible cells,
even though main uses a different sampling policy. Cells show `current ms / main ms (change)`;
change is `(current / main - 1) * 100%`, so positive means slower. The baseline commit, run, attempt,
and policy are explicit. The larger main sample may favor its timing; this is not a significance test.

The report stores invocation settings (file counts, CPU counts, depth, fixture mappings), tree and
source hashes, result hashes, tool versions, executable hashes, build identity, platform, runner
class, CPU affinity, and filesystem type. Compatibility checks exclude sampling count and temporary
paths, but require the same measurement semantics, build configuration, environment contract, and
per-participant commands and competitor identities. Each compared cell must also have the same CPU/file allocation, fixture tree,
and expected results. Adding a new size or fixture preserves comparisons for overlapping unchanged
cells; changed/new cells show `n/a`. If no compatible main report exists, the table says so explicitly.
The first run of a changed contract may therefore establish a new baseline.

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
small cases; the differing 2/3 and 7/9 policies remain explicit. New or incompatible cells cannot
trigger an alarm without a baseline. Correctness failures still fail CI. A future blocking policy
can use the same evidence with a confirmation run; no performance block is enabled now.

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

Linux workers bind to one/four CPUs from the runner's allowed affinity set before timing, and child
processes inherit that allocation. The entire find/fzf pipeline shares it. xff receives matching
`--jobs`, rg matching `--threads`, and fzf matching `GOMAXPROCS`. Find remains single-threaded.
Affinity does not reserve physical cores or eliminate hosted-runner contention. Insufficient CPUs
or unavailable affinity enforcement fail the hosted run.

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
start/completion lines per scale. Updates identify scale X/Y, fixture preparation, or run X/Y
within the current task, including the tool and warm-up/sample index. Small scales therefore
produce little output. Progress output and correctness checks happen outside the timed child
commands; scale completion confirms all its output validation finished.

The macOS job has a 60-minute pre-merge budget and a 90-minute post-merge budget, including
compilation and fixture creation. Its first 100,000-file CI matrix exceeded the old 30-minute job
budget after an eight-minute build. Linux keeps its 30/45-minute budgets. These are job ceilings,
not requested measurement durations; individual benchmark invocations retain their timeout.

PR measurements use 10, 20, 50, 100, 200, 500, 1,000, 2,000, 5,000, and 10,000 files, retaining
the fastest 2 of 3 samples. Post-merge adds 20,000, 50,000, and 100,000 files and retains the
fastest 7 of 9. Baseline comparisons use matching cells at shared sizes; extra main-only scales
do not invalidate comparisons at the smaller sizes. Explicit repeated `--files` values override
the default grid for local experiments.

## Post-merge measurement shards

Main runs build the paired binaries once per platform, then distribute the exact head binary to
three measurement runners. A fixture consists of its file count, CPU allocation and broad/deep
shape. All tools, tasks, warmups and repetitions for that fixture stay on one runner; samples for
one cell are never pooled across runners. The post-merge estimator remains the fastest seven of
nine observations, while PR runs retain the fastest two of three.

A shared plan balances estimated fixture costs from the latest retained main report for the
architecture. Unmeasured sizes extrapolate from the nearest measured size. Without retained data,
file counts provide the initial weights. These estimates exclude build and fixture setup time, so
three shards do not guarantee a threefold wall-time improvement.

Aggregation requires every planned fixture and task exactly once, matching source and executable
identities, compatible CPU affinity, and identical sampling contracts. Missing or incompatible
shards fail the workflow rather than publishing a partial matrix. Each merged report retains
per-shard provenance. Historical baselines belong on the complete merged report, not individual
shards. JSON and HTML share the platform-specific basename; only final merged artifacts are
selected by the publisher.

## Comparison landscape

`tools/benchmark_landscape.py` renders existing comparison JSON as a standalone interactive
HTML landscape. It requires no remeasurement. The 3D chart is open by default; its
show/hide disclosure collapses the chart, controls and explanation without hiding the tables.
Reopening resizes the chart to the available width. Absolute measurements are collapsed by default;
the comparison table stays visible below the plot. Benchmark publication inserts the landscape above all tables on retained reports with the
1/4 allocation matrix. Older reports without that matrix retain their existing presentation.
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
