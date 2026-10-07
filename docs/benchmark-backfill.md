# Historical benchmark backfill

The CI workflow collects replacements for historical CI measurements. The standalone tool also supports additional
local machine histories. These are distinct purposes, recorded explicitly in each batch and report:

| Collection          | Grid                                  | Purpose                                           |
| ------------------- | ------------------------------------- | ------------------------------------------------- |
| macOS CI            | 1 and 3 workers                       | Replace macOS CI history for the measured commits |
| Linux CI            | 1 and 3 pinned logical CPUs           | Replace Linux CI history for the measured commits |
| Local Mac, M5 Pro   | 1, 3 and 10 workers (host maximum 18) | Add a separate M5 Pro history                     |
| Local Mac, M2 Ultra | 1, 3 and 10 workers (host maximum 24) | Add a separate M2 Ultra history                   |
| Local Linux, Zen 5  | 1, 3 and 10 physical cores            | Add a separate Zen 5 history                      |

Keep every measurement of one replayed revision on one host, with a common benchmark driver,
reference tools and fixtures. Different historical revisions may use different machines. The CI
workflow schedules those revisions concurrently; a standalone local batch can keep its complete
revision list on one host. Record each host and reference-tool identity: raw elapsed times from
different hosts are not a paired comparison, and same-run XFF/reference ratios do not remove
every hardware or storage difference.
The local grid must be selected explicitly. The recommended 1, 3, 10 grid shares the
three-worker comparison with CI and adds a larger scaling measurement; it can extend to 30
on a suitable future host. This approximately logarithmic progression does not make timings
from different hosts interchangeable. The two Mac models
remain separate histories. macOS allocations are worker requests because the driver cannot pin
Mac CPUs. Apple's `container --cpus=N` caps a Linux VM, so it would measure Linux ARM64,
not macOS. A macOS VM can cap guest vCPUs but would be a separate series and does not
select physical host cores. Local Linux uses one hardware thread per physical core and enforces affinity.

Replacement means selecting the backfilled CI data as the active history for the corresponding
platform and commit. Superseded raw observations remain available for provenance. A local series
must never replace a CI series, and different CPU allocations must not be pooled or normalized
into equivalence. The collection command records this intent. After a complete successful CI
campaign, the trusted publisher validates and imports the whole campaign before selecting any replacements.

## Automatic local backfill

Use the local benchmark command to discover this machine's existing measurements and fill gaps:

```sh
bazel run //tools:benchmark -- help
bazel run //tools:benchmark -- list
bazel run //tools:benchmark -- backfill
```

Bazel supplies the pinned Python runtime and declared dependencies. Direct execution with
`./tools/benchmark.py COMMAND` or `python3 tools/benchmark.py COMMAND` is also supported
with Python 3.13+ installed. Use one invocation form consistently within a resumable batch,
since the Python runtime is part of its frozen environment.
`bazel run` forwards standard input, so interactive dataset selection and confirmation work normally.
`help backfill` and `backfill --help` explain the options. This maintenance tool uses subcommands;
the XFF search executable continues to use flags.

`list` refreshes `origin/main` and downloads the site's small `datasets.json` catalog. It shows
the published series, workload grids, sampling settings and compatibility with this machine.
It prefers an unambiguous compatible dataset belonging to this machine. For a selected dataset,
it shows each main revision since comparison benchmarks were introduced: its date, SHA, PR/subject,
and whether this machine's requested measurements exist. It checks saved batches under
`~/xff-benchmarks` and downloads only the selected machine dataset's published observations,
including packed reports. Downloads must match the metadata's canonical JSON identities before
their observations are reused. It does not build, measure, or change saved results.

`backfill` offers a numbered selection when several datasets are compatible. An existing machine
continues its series; a new machine can reuse a compatible published workload as a new independent
series. File counts, CPU allocations, depth and sampling are taken from the selected recipe, so
the user does not need to reconstruct its flags. `--dataset=ID` accepts a unique ID prefix for
scripts; `--yes` requires that selection when more than one dataset fits. Explicit grid/sampling
flags override recipe defaults and will produce a distinct workload description on publication.

`--no-fetch` uses existing local Git refs and metadata, which can be stale.
`--history-root=PATH/benchmarks` reads a Pages checkout, and `--catalog-url=HTTPS_URL` selects another
published catalog. Before a site publishes dataset metadata, a 404 falls back to the batch discovery
in `origin/coverage-pages`. Other network errors fail rather than treating existing data as absent.
Only local-observation JSON is extracted in that fallback; rendered site pages are not checked out.

After dataset selection, `backfill` shows the revision inventory and its proposed batches, then asks
`Build and measure the missing revisions? [y/N]`. Only `y` or `yes` proceeds; empty input or EOF
cancels. `-Y` / `--yes` supplies that confirmation for unattended runs. Discovery can refresh Git
refs before confirmation; building, measuring and saving new batches happen only after confirmation.
Nothing uploads automatically. See [Publishing completed local observations](#publishing-completed-local-observations).

To keep long histories readable, `list` and `backfill` show the newest ten revisions individually and group
older revisions into adaptive day, ISO-week, month, or year ranges. A range never crosses a status or
diagnostic-detail transition. Each aggregate reports its date span, commit count, PR range, and counts by
status and diagnostic detail. Pass `--full` to show every revision.

Machine identification hashes the macOS platform UUID or Linux installation machine ID; raw IDs
are never stored. New machines receive distinct series names automatically. Existing batches
with the stable ID remain recognizable after a hostname change. For older batches, a matching
hostname hash, architecture and CPU count identifies the existing series; its historical batches
then remain available even if older records have a different hostname. This legacy identification
cannot distinguish machines cloned with the same hostname and hardware configuration. It does not
guess a match from the processor model alone. If multiple series match, select one with `--series`.
A series carrying another stable machine ID cannot be adopted. Cloned OS machine IDs likewise
must be made unique before collecting separate machine histories.

Without a published recipe, a recognized local series supplies its latest saved settings. For a new
series without a recipe, the default workload is 1/3/10 workers or cores, file counts 10 through
100,000, the prepared `broad/v2` and `deep/v2` layouts, and the fastest seven of nine measurements.
Override it with repeated `--cpus` / `--files` and `--depth`, `--repetitions`, or `--keep`.
`--layout-revision=legacy` explicitly selects the old independently generated fixtures; selecting a
published legacy dataset does the same automatically. A small host must select an allocation it can support;
the tool never silently reduces the requested grid. On Linux, fixtures default to `/dev/shm` and
must be memory-backed; one logical CPU per physical core is pinned. macOS requests workers.

A saved observation counts only after validation of its complete task/participant/sample matrix,
revision, batch and binary identity. Its file/CPU grid must cover the requested grid, and its depth
and sampling settings must match. CI measurements and other machines never satisfy local coverage.
Missing files, incomplete samples, failed runs and incompatible settings remain missing, with the
reason shown per revision. Driver or reference-tool updates do not erase valid historical
observations; their recorded identities remain available for comparison eligibility and normalization.

Every main revision is considered, including older gaps rather than only commits after the newest
measurement. Existing batches remain immutable. An interrupted batch with the exact current
environment and driver resumes; otherwise a new batch contains only still-missing revisions.
Completed reports from any batch are reused without rerunning them. New batches live under
`~/xff-benchmarks/batches/IDENTITY/`; use `--root` to change local storage. Two invocations of this
command cannot collect simultaneously for the same machine and storage root. An older manual
collector does not use this lock; finish it before starting automatic collection.

## Published dataset metadata

The benchmark page's **Measurement datasets and methods** section explains the available series
and links to `datasets.json`. The publisher derives this catalog from retained raw observations,
including packed history; it does not invent missing provenance or modify measurements. PR preview
reports are excluded from the backfill catalog. Publishing a new measurement refreshes the metadata.

Schema version 1 separates:

- **Dataset identity:** local or CI origin, named series, OS, architecture and a workload recipe.
  The recipe records file counts, CPU counts and allocation policy, tree shapes/depth, fixture
  version/source, complete ordered layout definitions and anchor hashes, tasks and reference participants,
  warm-up/order, estimator and sampling, and storage.
  Changing the recipe creates another dataset ID within the series.
- **Machine identity:** stable hashed IDs where available, plus explicitly recorded legacy host
  fingerprints. Reusing another host's recipe never adopts that host's observation history.
- **Observation provenance:** measured and revision dates, commit, raw report and batch links with
  canonical JSON identities, and a method reference. Methods retain driver hashes, reference-tool
  identities, build details, actual platform/CPU count, affinity, storage and shard sampling.

The client interprets only the known recipe fields. It never executes downloaded commands.
Compatibility means that the current driver can collect the workload on this OS/architecture with
sufficient capacity. Unsupported custom fixtures, unknown fixture versions, changed task sets and
allocation/storage policies are listed with reasons instead of being guessed. In particular,
Linux CI's logical-CPU allocation is not interchangeable with local physical-core allocation.

A runnable recipe is not a claim of identical performance conditions. New runs use the installed
reference tools and current measurement driver, and build each XFF revision with that revision's
release configuration. The original and new identities remain attached to their observations;
comparison and reference-window normalization still enforce their own compatibility rules.

## Explicit local batch

Install Bazel, Python 3.13+, ripgrep and fzf. `find` must also be on PATH. Use a checkout containing
all selected commits; the driver never changes that checkout or includes its uncommitted edits.
Place the desired Git references in a whitespace-separated file, or repeat `--revision=REF`.

```sh
python3 tools/benchmark_backfill.py \
  --repo="$PWD" --output=/tmp/xff-backfill-macos --series=macos-m5-pro \
  --revisions-file=/tmp/xff-backfill-revisions.txt \
  --purpose=local-addition --cpus=1 --cpus=3 --cpus=10 --disk-cache="$HOME/.cache/bazel-disk"
```

Without `--run`, this resolves references to full commit IDs, sorts them by commit date, validates
capacity and tools, and writes `batch.json`. Inspect it, then repeat the command with `--run`.
The same driver is available as `bazel run //tools:benchmark_backfill -- ...`.
Use `--history-root=PATH/benchmarks` instead of a revision list to select retained comparison commits
from a coverage-pages checkout. Selection defaults to the local OS; `--history-platform=macos|linux`
chooses another historical platform's commit list. The selection is deduplicated. This only chooses
revisions to build; it does not relabel where the new measurements run.

For a local Linux run, select `--series=linux-zen5`, an explicit grid such as `--cpus=1 --cpus=3 --cpus=10`,
and `--fixture-parent=/dev/shm --require-memory`. Pinning is mandatory for local Linux even without
`--require-cpu-affinity`: the driver reads package/core IDs from sysfs, selects one allowed logical
CPU per physical core, and rejects requests exceeding the available physical-core count. Missing
or unknown topology is an error, not permission to count SMT siblings as separate cores.

The measurement worker and all tool processes inherit the selected mask. Each allocation uses
its requested subset of that pool. The driver restores its original mask after measurement, also
on failure; builds run with the original allowance. The frozen contract records the selected pool
and each result records the actual per-allocation CPU IDs. Respect an externally restricted mask;
the driver never expands it. Core pinning does not reserve those cores against unrelated processes.

The standard grid includes the 1-2-5 file-count progression from 10 through 100,000, both tree
shapes and all current comparison tasks, retaining the fastest seven of nine samples. Repeated
`--files` and `--cpus` flags select a smaller pilot or another allocation. `--repetitions` and
`--keep` control sampling. Workload changes create a different batch contract.

## Execution, failures and resuming

Progress lines begin with the local time and the revision's one-based position in the batch:

```text
20261004 130136 5/9 Workers: 1 Files: 100,000 Run: 5/10; broad/fuzzy-list-hdn; fzf; sample: 4/9
```

The timestamp uses `YYYYMMDD HHMMSS`; `10/17` means the tenth of seventeen selected revisions.
The initial batch summary uses `0/17` before any revision starts. Build, measurement, nested
fixture/sample progress, failures, reused reports and completion messages carry this prefix.
Nested progress retains its five-second throttle, with scale boundaries always shown.
Raw build-tool logs remain available separately in each revision's `build.log`.

All selected binaries are built before measurement begins. An isolated shared Git clone under the
output directory checks out each exact revision and uses its own `clang_release` configuration,
Bazel version and dependency pins. Binaries and build logs are retained outside the checkout.
Compilation does not overlap measurements. Historical build changes remain visible in each
record; they are not silently replaced with today's compiler configuration.

Measurements then run sequentially, with the existing discarded warm-up and interleaved reference
tool samples. Reference executable hashes, versions, driver hashes, Python, OS, architecture,
host identity, available CPUs and storage settings are frozen. Host identity uses a hash of the
hostname rather than publishing the hostname. Same-host collection reduces variation; it does
not eliminate thermal drift or unrelated background activity.

Repeat the same command to resume. A changed host, driver, tool or invocation contract is rejected;
use another output directory for a new series. Moving Git references that resolve to new commits
also fail the frozen-contract check. Use the recorded full SHAs when resuming after a branch advances.
Saved binaries are verified by hash, and completed reports are validated and reused. An interrupted
revision restarts from the beginning; partial samples are never pooled with a later invocation. Failed
builds and measurements are retried by the identical command, while completed observations are reused.
If a historical build tool needs a compatibility runtime absent from the host, pass its library directory
with `--build-library-path=PATH`. The path is frozen in the batch contract and reaches build actions through
Bazel's `LD_LIBRARY_PATH` action environment; it does not affect benchmark execution. Later collections for
the same local series automatically reuse its newest recorded non-empty build-library path and reject it
before building if the directory is no longer available. An explicit flag overrides the inherited path.

A historical build failure or unsupported CLI workload is recorded with its actionable diagnostic and
build-log link and does not prevent later
revisions from running. Any missing revision makes the batch exit nonzero. Environment drift
stops the entire batch. Do not change historical commands just to obtain a passing comparison:
that would change the task being measured.

The output contains:

- `batch.json`: immutable selection and measurement contract.
- `status.json` and `index.html`: completion state, report links and failure diagnostics.
- `reports/COMMIT/benchmark-report-OS-ARCH.json` and `.html`: raw measurements and rendered tables.
- `binaries/COMMIT/`: saved executable, build identity and build log.
- `checkout/`: disposable build checkout; keep it and the source repository available while resuming.

The shared clone borrows source Git objects. Do not prune those objects during the campaign.
Completed result JSON and HTML are independent of the source checkout. Keep them even when
another series becomes the preferred presentation. Publication into the shared benchmark history
is separate from measurement and does not invent paired results for historical commits.

## Manual GitHub workflow

**Benchmark backfill** is a manually dispatched CI-only workflow. Its planning job freezes every
retained comparison commit for each platform, the source and history commits, driver hashes,
task/participant set and full sampling grid in `campaign.json`. It creates one job per platform
and historical revision, with at most eight jobs running concurrently. A revision is never split
across hosts. This avoids putting the entire history into a single six-hour hosted job.

Each job installs reference tools once, builds its revision with that revision's release settings,
then measures the complete matrix on the same machine: one/three workers on macOS, one/three pinned logical CPUs
on Linux. Linux fixtures use verified tmpfs, matching the ordinary hosted measurement policy. Using three
of a four-vCPU Linux runner's CPUs leaves one outside the tools' affinity mask for background
work. This provides headroom without reserving CPUs or preventing background work on the
measured CPUs; reduced contention is expected, not guaranteed.

The tool's `--purpose=ci-replacement` requires a GitHub-hosted environment and the exact platform
grid. Its `replacement_target` is `github-ci-macos` or `github-ci-linux`. Local runs default to
`--purpose=local-addition`, have no replacement target, and require an explicit grid. The workflow
has no dispatch inputs; arbitrary local host labels or allocations cannot change its CI targets.

Artifacts contain the campaign plan plus each revision's host manifest, status, reports and build
logs. The build/measurement step has a 165-minute ceiling within a 180-minute job, leaving time to
upload completed results after failure or timeout. Failed jobs do not cancel the other revisions.
Retrying a failed job replaces that revision's artifact with a complete new measurement; it never
pools partial observations across hosts. Successful jobs from an earlier attempt remain usable
when they belong to the same frozen campaign, source commit and workflow run. Each batch records
its actual collection attempt separately from the successful campaign attempt.

## Publication

**Publish benchmarks** imports a successful main-branch **Benchmark backfill** dispatch. Publisher
code comes from trusted main; artifacts are read as data. It verifies source workflow identity,
the complete frozen job set, report and binary identities, driver hashes, host/tool consistency,
the full files/CPUs/tree/task/participant matrix, every repetition, and Linux affinity/tmpfs.
Missing, duplicate, failed or incompatible data rejects the campaign before the site changes.
Local additions cannot enter this replacement path.

The complete campaign is retained atomically under `backfills/RUN/ATTEMPT/`, with the plan and
per-platform/commit raw reports, batch contracts and completion states. Re-importing identical
data is harmless; changing a retained campaign is an error. The index, version slider, and exact
PR/release references prefer a validated CI backfill for its measured platform and commit. They
use the historical commit/merge time, not the replay date or the publisher's source commit.

Each replacement links its original measurements, including the paired base/head results that
the backfill does not reproduce. These original reports are protected from ordinary retention;
backfill records are also kept outside that bounded run cache. New commits remain independent
entries. Host and reference-tool differences between revisions remain visible in the raw contracts;
publication does not normalize, pool or relabel their observations.

## Publishing completed local observations

Local collection writes reports on the measurement machine. Upload its completed observations; the
benchmark publisher then includes it in the main page's platform/version selector and report table.
Local series have their own names and never replace the CI series. The landscape's Workers selector
chooses any measured pair (for a 1/3/10 grid: 1/3, 1/10 or 3/10); detailed tables retain all allocations.

Use the automatic uploader from a current source checkout:

```sh
bazel run //tools:benchmark -- upload
```

The command fetches `origin/coverage-pages`, creates a temporary detached worktree, and lists every
local batch with completed observations. Each row shows whether its raw observations are ready or
already uploaded and how many revisions completed. Pick one numbered batch, choose `a` to upload every
listed ready batch, or cancel. The all-ready choice excludes batches already present in the Pages
checkout; a numbered already-uploaded selection still dispatches publication, so retrying after an
authentication or dispatch failure repairs the missing step. The selection authorizes validation,
import, commit, push, and publication for all selected batches as one operation.

For unattended operation, select batches explicitly by directory, batch JSON path, or unique identity
prefix; repeat `--batch` to publish several batches together and pass `-Y` to confirm the push:

```sh
bazel run //tools:benchmark -- upload --batch=0957eb2cce8f -Y
```

After the push, the command dispatches `benchmark_pages.yml` from trusted `main`. Use `--no-publish`
to push the raw observations without dispatching the site publication workflow.

The equivalent manual procedure uses a separate checkout of the `coverage-pages` branch:

```sh
python3 tools/benchmark_local.py --batch="$HOME/xff-benchmarks/macos-m5-pro" \
  --root=/path/to/pages-checkout/benchmarks
git -C /path/to/pages-checkout add benchmarks/local
git -C /path/to/pages-checkout commit -m "benchmarks: retain completed local series"
git -C /path/to/pages-checkout push origin HEAD:coverage-pages
gh workflow run benchmark_pages.yml --repo mboworks/xff --ref main
```

The importer validates every completed revision, allocation, task, participant and sample before retaining
anything. Failed or pending revisions do not prevent completed observations from being retained. It preserves
raw reports and the frozen host/build contract under
`local/SERIES/BATCH_ID/`. Re-importing identical observations is harmless; changing an existing
batch fails. Binaries, source checkouts and build logs are not uploaded. There is no automatic
upload from the measurement machine.

Publication is restricted to `main` by the Pages environment and uses trusted main code.
Every deployment fetches the public benchmark index and checks its digest, so a successful
Pages response alone cannot mark stale content as successfully published.

## Presentation normalization

Raw results remain unchanged. Publication calculates reference-window corrections for the
presentation layer, separately from collection. Worker allocations and machine series remain
separate. The first five measurements share their five-reference mean; subsequent measurements
use the current reference plus four prior ones. Scale each individual XFF time by that window
mean divided by its own reference time. XFF results are not averaged across versions.
The **Timings** selector exposes this information in hover cards; raw reports and ratios remain
unchanged. See [Reference-window timing normalization](benchmark-comparisons.md#reference-window-timing-normalization).
