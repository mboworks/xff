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

## Local batch

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
revision restarts from the beginning; partial samples are never pooled with a later invocation.

A historical build failure or unsupported CLI workload is recorded and does not prevent later
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

## Publishing a completed local series

Local collection writes reports on the measurement machine. Upload a completed batch once; the
benchmark publisher then includes it in the main page's platform/version selector and report table.
Local series have their own names and never replace the CI series. The landscape's Workers selector
chooses any measured pair (for a 1/3/10 grid: 1/3, 1/10 or 3/10); detailed tables retain all allocations.

Use a current source checkout and a separate checkout of the `coverage-pages` branch:

```sh
python3 tools/benchmark_local.py --batch="$HOME/xff-benchmarks/macos-m5-pro" \
  --root=/path/to/pages-checkout/benchmarks
git -C /path/to/pages-checkout add benchmarks/local
git -C /path/to/pages-checkout commit -m "benchmarks: retain completed local series"
git -C /path/to/pages-checkout push origin HEAD:coverage-pages
gh workflow run benchmark_pages.yml --repo mboworks/xff --ref main
```

The importer validates every revision, allocation, task, participant and sample before retaining
anything. It preserves raw reports and the frozen host/build contract under
`local/SERIES/BATCH_ID/`. Re-importing identical observations is harmless; changing an existing
batch fails. Binaries, source checkouts and build logs are not uploaded. There is no automatic
upload from the measurement machine.

Publication is restricted to `main` by the Pages environment and uses trusted main code.
Every deployment fetches the public benchmark index and checks its digest, so a successful
Pages response alone cannot mark stale content as successfully published.

## Presentation normalization

Raw results remain unchanged. Reference-window normalization belongs in the presentation layer,
separately from collection. Worker allocations and machine series remain separate. Multiplying
reference and XFF elapsed times by the same normalization factor does not change their same-run
ratio or compensate for different CPU allocations. This workflow retains all observations needed
for that presentation work; it does not normalize samples while collecting or resuming.
