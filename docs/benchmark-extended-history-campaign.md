# Extended benchmark history qualification

This campaign qualifies additional CLI workloads against the last 130 merged PRs at
`ae8c5af3eb11dc8571f11db9b307b0db948f78c6` (PR #1130). Its oldest revision is
`21c7f3656b005b23113d936c1cd2ca0fd2221ab2` (PR #977). The manifest selects first-parent
commits with PR numbers; PR numbers are not a contiguous range.
The [frozen revision manifest](benchmarks/extended-history-revisions-20261107.json) is retained
with the implementation.

It follows the [chart sensitivity audit](benchmark-chart-sensitivity-audit.md). These workloads
have their own recipe, `extended-production-v1`, and must remain a separate dataset from core
measurements. They use the existing 3D visualization framework. The stale local Linux v2 dataset
is outside this investigation.

## Workloads fixed before collecting timings

| Task            | Operation                                                  | Equivalent traditional tool | Reason for inclusion                               |
| :-------------- | :--------------------------------------------------------- | :-------------------------- | :------------------------------------------------- |
| `files-exact`   | List regular files with NUL termination                    | GNU find                    | Simple traversal control                           |
| `name-native`   | Native-case `*.txt` match under an absolute root           | GNU find                    | Native-case discovery and absolute-root work       |
| `metadata-time` | Type, name and size filters; print path, size and UTC date | GNU find                    | Metadata and timestamp formatting                  |
| `content-regex` | Match a status regex in text files                         | ripgrep                     | Traversal, filename filtering and content matching |
| `archive-tar`   | List matching regular-file members in a TAR                | GNU tar                     | Archive indexing and virtual directory traversal   |

Each task has broad/deep fixtures and one/three allowed CPUs. Native tasks use
100, 10,000 and 100,000 files; archives use 100, 1,000 and 10,000 members: 60 cells per revision,
7,800 historical cells in total. Missing sizes remain missing in the renderer.

Fixtures contain deterministic ASCII names and text, fixed UTC modification times and modes,
matching and nonmatching files, and 256-byte/8-KiB contents. Archive fixtures contain regular
members without explicit directory entries. Fixture contents and metadata are verified before
and after collection. Native fixtures require Linux case-sensitive tmpfs; this qualification
does not establish the same case semantics on a macOS filesystem.

Drogon runs this campaign in a KVM guest. The guest reports an AMD Ryzen 9 9950X CPU model and
ten cores; the measurement affinity uses guest CPUs `0`, `1`, and `2`, selected from distinct
core IDs reported by Linux. This does not establish how the host pins those virtual CPUs to
physical cores. The guest kernel is `7.0.0-38-generic`, with glibc 2.43. The independently read
virtualization/DMI details are retained in `machine-environment.json` beside the campaign.

The TAR reference transforms displayed names to the same `fixture.tar/member` spelling as XFF.
Native references use identical roots and output delimiters. Every invocation must succeed
without diagnostics and produce the exact expected multiset of records. Duplicate, missing or
extra records invalidate the invocation. Expected outputs come from the fixture recipe, rather
than another measured tool's output.

## Build and measurement contract

- Build every exact historical checkout with its own configuration and pinned dependencies:
  `//xff/cli:xff_full --config=clang_release --config=xff_full`.
- Disable system/home Bazel rc files; reject dirty trees and workspace `.bazelrc.user` overrides.
- Resolve the configured executable with `cquery` using the same options, then immediately copy
  it to its revision directory. Retain source/tree identity, SHA-256, build logs and link actions.
- Require ThinLTO link evidence. Historical compiler/dependency changes are part of this
  production-history comparison; results cannot automatically be attributed solely to XFF source.
- Finish every build, including the native resource launcher, before accepting timing samples.
- Keep fixtures on `/dev/shm` tmpfs. The disk holding binaries/logs/results is not the measured
  fixture filesystem. No disk-storage throughput conclusion follows from these measurements.
- Rotate historical XFF, fixed PR #1130 XFF, and the traditional tool through each execution
  position. Use one correctness-checked warm-up round and nine measured rounds per cell.
- Use the existing chart estimator, mean of the fastest seven of nine. Retain all nine samples
  and warm-ups. The existing native launcher measures command CPU, RSS, wall time, output bytes
  and first-output time. Python validation/serialization occurs after the timed interval.
- Record affinity, commands, locale, timezone, fixture identity, output hashes/counts, host
  pressure/load/memory context, binary hashes and accounting method. Timeouts terminate the
  worker process group. Failed attempts remain separate from completed cells.
- Resume only an identical contract. Interrupted cells restart as new attempts. Executable or
  fixture identity drift invalidates a campaign instead of silently combining populations.

The fixed-current participant provides a nearby control for host load. A historical binary that
is byte-identical to current provides an identical-binary noise control. Interleaving reduces
shared host effects; workload-dependent interference can remain. The analysis also computes a
paired-round bootstrap interval using all nine rounds. That interval describes within-cell
variability, not independent-machine reproducibility.

## Reproduction

The tools have Bazel owners and support direct Python 3.13 invocation. Building requires an
isolated checkout at `BUILD_ROOT/checkout` and a JSON revision list containing `sha`, `pr`, `date`
and `subject`. Retain this manifest.

```sh
python3.13 tools/benchmark_extended_build.py \
  --root=BUILD_ROOT --revisions=revisions.json --disk-cache=BAZEL_DISK_CACHE

python3.13 tools/benchmark_extended_history.py \
  --root=MEASUREMENT_ROOT --build-root=BUILD_ROOT --revisions=revisions.json \
  --fixture=/dev/shm/xff-extended-fixtures --resource-launcher=NATIVE_LAUNCHER

python3.13 tools/benchmark_extended_report.py \
  --campaign=MEASUREMENT_ROOT --output=REPORT_DIRECTORY \
  --renderer-js=THREE_LANDSCAPE_JS
```

The report exports every cell to CSV, endpoint summaries/control noise to JSON, and two datasets
using the existing 3D history viewer: traditional tools and fixed-current XFF comparisons.
The cell CSV includes user/kernel CPU, peak child RSS and first-output latency, taken from the
same samples selected by wall time. `history-by-task.csv` equally weights the shape/worker
combinations using geometric means at each task/size; it omits incomplete groups. These grouped
ratios supplement the individual measured cells rather than replacing them.
`current-versus-reference.csv` uses the fixed-current participant from every historical block
to describe the remaining reference-tool gap for each individual case. Its 10th/90th percentiles
describe the observed spread across those blocks, not confidence across independent machines.
Serve the report directory over HTTP for JSON fetches. `--allow-partial` is for monitoring; the
default refuses incomplete campaigns.

The historical LLVM 22 linker on drogon requires ICU 70. System installation required interactive
sudo authentication, so the official Ubuntu `libicu70_70.1-2ubuntu1_amd64.deb` was extracted into
the campaign's `compat` directory. `--build-library-path=PATH` supplies those libraries to build
actions and records their hashes. XFF itself does not link ICU. The package SHA-256 is
`223c1e6cebf2a6748637303227d8b84337c14a2d30eacbc46a68d02557dcff3e`.
The package, download URL, hashes and library list are retained in `installed-dependencies.json`.

## Completed results and workload selection

All 130 revisions and 7,800 cells completed successfully, followed by a separate 120-cell
endpoint repetition with 27 measured rounds retaining 21. Across the two populations, all
244,080 invocations, including warm-ups, passed their output checks. The
[compact results](benchmarks/extended-history-results-20261107.json) retain every task/size
endpoint comparison, the selected adjacent-source transitions, and the small regex cases.

The ratios below are geometric means over broad/deep layouts and one/three allowed CPUs.
Old/current greater than one means current is faster; current/tool greater than one means XFF
takes longer than the traditional tool. Endpoint repetition is a separate population, not
additional samples merged into the historical estimates.

| Task                       | Files or members | Old/current, history | Old/current, repeat | Current/tool, repeat |
| :------------------------- | ---------------: | -------------------: | ------------------: | -------------------: |
| Exact file listing         |          100,000 |                 1.09 |                1.09 |                1.159 |
| Native-case name filtering |          100,000 |                 2.64 |                2.45 |                1.090 |
| Metadata and UTC date      |          100,000 |                 1.08 |                1.08 |                1.137 |
| Content regex              |          100,000 |                 1.09 |                1.11 |                0.721 |
| TAR member filtering       |           10,000 |                 1.60 |                1.63 |                0.860 |

Retain these workloads as a separate full-production suite. Native-case name filtering, TAR
member filtering, and small content searches with multiple workers expose substantial changes
that the existing exact-case, non-archive tasks cannot demonstrate. Keep exact listing and
metadata formatting as controls and as remaining performance targets. A positive historical
change is not sufficient reason to hide a slower comparison against a traditional tool.

The measured transitions agree with specific source changes:

- PR #1091's uniform filesystem case policy removes unnecessary leaf metadata lookups. At
  100,000 files, the immediately preceding/current-control ratio changes from 2.409 at #1090
  to 1.075 at #1091, while exact listing and metadata controls remain nearly unchanged.
- PR #1105's bounded archive-directory lookup changes the 10,000-member ratio from 1.446 at
  #1104 to 1.010 at #1105. Native listing/name/content controls remain close to parity.
- PR #1034 removes Abseil hardware-clock calibration from worker startup. The small broad
  regex case with three allowed CPUs changes from about 2.74 times current to about 1.20;
  the one-CPU control barely changes. In the separate endpoint repeat, current completes
  100-file searches 2.75 times as fast on the broad layout and 2.52 times as fast on the deep
  layout with three allowed CPUs. One-CPU results do not show that improvement.

The remaining simple-task gap is reproducible. At 100,000 files, current takes about 16% longer
than find for exact listing, 9% longer for native-case filtering, and 14% longer for metadata/date
output. At 10,000 files, exact listing takes about 35% longer. Small 100-file listing/metadata
cases show no historical improvement beyond the observed control variation. Content regex is
about 28% faster than rg in the largest repeated case; TAR listing is about 14% faster than tar
at 10,000 members, although the original current endpoint consumes about 3% more total CPU than
tar there. These are workload-specific results from this Linux guest.

All three byte-identical current revisions provide 180 control cells. Their 95th percentile
multiplicative distance from parity is 1.0305. This describes the observed noise and does not
make it a universal significance threshold. Native-case speedup changes from 2.64 to 2.45 between
populations, and small deep regex speedup also varies: interleaving does not eliminate every
effect of shared-machine load. Preserve the individual cells and host snapshots.

## Evidence and status

On `drogon.boerger.online`, the root is `/home/marcus/Documents/xff-chart-campaign-20261107`:

- `xff-extended-history-revisions.json`: frozen 130-revision selection.
- `qualified/binaries/SHA/`: production executable and build/link evidence.
- `qualified/build-status.json`: build progress; initial failed dependency attempts are retained
  separately under the campaign root's `binaries` directory.
- `preflight-full.json`: 240 full-size output checks across both endpoints and their references.
- `preflight-native-worker.json`: checks through the actual accounting worker; preparation
  timings are explicitly excluded because builds were running concurrently.
- `measurements/contract.json`, `fixtures.json`, `progress.json`: frozen population and progress.
- `measurements/cells/SHA/TASK/SHAPE/FILES/CPUS/complete.json`: accepted raw cell data.
- `measurements/cells/.../attempts/`: warm-ups, samples and failure evidence.
- `qualified/logs/build-campaign.log` and `logs/measurements.log`: progress logs.

The 240 endpoint functional checks and native-worker checks passed. All 130 production builds
completed before timing collection. The
[build summary](benchmarks/extended-history-builds-20261107.json) records 77 distinct executable
hashes; 53 adjacent revision pairs have identical executable bytes. PRs #1128, #1129 and #1130
also share the current executable hash, providing identical-binary controls in the campaign.

The independent post-collection audit rehashed all 130 retained binaries and their build/link
evidence successfully. The endpoint repeat used every predeclared case, including slower and
unchanged cases, with fresh tmpfs fixtures and the same frozen production binaries. Its own
contract and directory (`confirmation/`) remain separate from the nine-round history.

The [completion audit](benchmarks/extended-history-complete-audit-20261107.json) records independent
raw-sample replay of all 93,600 historical plotted values and 31,200 missing positions, plus
1,440 values and 480 missing positions in the repeat. Maximum floating-point difference is
1.12e-16. Chromium checked 96 historical viewer states and 24 repeated-endpoint states across
both dataset views, all task orders and both metrics, without JavaScript or loading errors.
The viewer includes all 130 revisions and direct CSV download links.

After confirmation, ten diagnostic CPU profiles compared the retained current binary with GNU find
on small and large listings and metadata formatting. Profiled commands write to `/dev/null`;
their results describe CPU attribution and never enter the timing population. In the broad
100,000-file XFF listing, approximate self shares of sampled user cycles are 19.8% in prepared
expression evaluation, 13.6% in the per-entry driver callback, 11.2% in string append, and 8.8%
in `EvalPrint0`; readdir contributes 16.8%. These point to evaluation, entry setup and output
construction as further targets. They are not percentages of total wall time. The short find
profile has only 48 samples and is insufficient for a useful startup breakdown. Perf also
reports an event-description-header warning; raw recordings, commands, sample counts and
reports are retained, and these profiles serve only as diagnostics.

A separate experiment follows those profiles: construct the per-entry content snapshot's cache
state only on demand, using inline `std::optional<State>` storage. Its source is
`90abd2b3ef63b35833e8f9d96745475c6b9f182a`, based on the unchanged PR #1130 baseline. The
experiment completed all 60 cases for the candidate and an identical-baseline control, with
27 measured rounds retaining 21: 120 cells and 10,080 validated invocations. A separate targeted
repeat adds 16 cells and 1,344 validated invocations. Broad 100,000-file listing is about 1% faster
in both populations; broad metadata/date output is about 0.5% slower in both. Instruction counters
show approximately 66 fewer user-space instructions per file in broad listing, a 2.75% reduction
at 100,000 files. The snapshot grows from 88 to 96 bytes in the macOS construction probe. The
measured implementation and its regression tests are now adopted unchanged on
`benchmark/extended-history-20261107`: the repeatable listing gain is worth retaining, with the
metadata tradeoff documented in the [experiment report](benchmarks/lazy-content-state-20261107.md).
macOS whole-command performance and hash-heavy workloads have not been qualified for this change.
The candidate has no PR number; its standalone measurement manifest uses zero as an explicit
experiment label. It must not enter the merged-PR history or be combined with either earlier
population. Source, tests, build identities and samples live under `lazy-content/` in the same
campaign root. The empty map previously allocated nothing: the hypothesis concerns construction,
destruction and movement of unused cache state, not removing a per-file map allocation.

The next basic-operation qualification applies four changes to startup, expression results,
explicit output storage and directory path construction. The direct Linux comparison reduces
large-listing userspace instructions by 6-10%, with elapsed gains smaller than those instruction
reductions. Results, remaining find gaps, build identities and next investigation targets are in
[the basic-operation report](benchmarks/find-listing-optimization-20261107.md). Its experiment
catalogs remain separate from merged history. A fifth change defers native filesystem case
resolution until it can change a match. On the Mac RAM fixture, 100,000-file name filtering
takes 71% less time than the four-change binary and 25% less time than GNU find; the complete
Linux controls, smaller cases and remaining listing gaps are retained in the same report.

The [synthetic viewer check](benchmarks/extended-history-viewer-check-20261107.json) verifies
both exported dataset views in Chromium. Both historical versions produce their exact expected
heights; 60 measured points remain present and all 20 unavailable cells remain empty. This
artifact contains synthetic renderer checks only, not performance measurements.

The [November 8 follow-up](benchmarks/lazy-evaluation-shard-setup-20261108.md)
qualifies lazy deferred-entry state, cached predicate descriptors and conditional
shard setup against merged PR #1149. It includes Linux/macOS production builds,
the 1/3/10-worker grid, identical controls, rejected experiments and small-task
regression repeats. Its experiment populations remain separate from the original
130-revision history and the core CI benchmark contract.
