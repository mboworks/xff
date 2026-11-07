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

Each task has broad/deep fixtures and one/three allowed physical CPU cores. Native tasks use
100, 10,000 and 100,000 files; archives use 100, 1,000 and 10,000 members: 60 cells per revision,
7,800 historical cells in total. Missing sizes remain missing in the renderer.

Fixtures contain deterministic ASCII names and text, fixed UTC modification times and modes,
matching and nonmatching files, and 256-byte/8-KiB contents. Archive fixtures contain regular
members without explicit directory entries. Fixture contents and metadata are verified before
and after collection. Native fixtures require Linux case-sensitive tmpfs; this qualification
does not establish the same case semantics on a macOS filesystem.

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

Measurement collection is in progress. A separate endpoint repeat is queued after successful
completion: every one of the 60 predeclared cases at both endpoints, fresh tmpfs fixtures,
27 measured rounds retaining the fastest 21. It uses the same frozen production binaries and
does not select only favorable cases. The repeated data has its own contract and directory
(`confirmation/`) and must not be merged with the nine-round historical population. Before the
repeat, an independent pass rehashes all 130 retained binaries and their build/link evidence.
Select CI workloads from completed results, retain the simple control, and report flat or
regressing cases alongside improvements. A historical improvement alone does not establish that
XFF is competitive with its traditional reference.

The [synthetic viewer check](benchmarks/extended-history-viewer-check-20261107.json) verifies
both exported dataset views in Chromium. Both historical versions produce their exact expected
heights; 60 measured points remain present and all 20 unavailable cells remain empty. This
artifact contains synthetic renderer checks only, not performance measurements.
