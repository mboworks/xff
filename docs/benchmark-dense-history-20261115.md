# Dense extended history and TAR investigation

## Acceptance

PR #1153 needs an extended continuous surface made from actual measurements. The original
60-cell sparse campaign and a synthetic viewer example do not satisfy that requirement.

- All five workloads use the same 13 counts, from 10 through 100,000, including the intermediate
  20/50 and 200/500 steps. TAR must measure the same counts as the native workloads.
- Both broad/deep shapes and 1/3 CPU allocations are complete: 260 cells per revision.
- Each cell has a discarded warm-up and nine interleaved rounds, retaining the fastest seven.
  Every output is checked against the independent fixture oracle.
- Original raw observations, hashes and sampling stay unchanged. The dense population has its
  own versioned contract, `extended-production-v2`; v1 samples are not appended to its timing sets.
- The merged CI viewer presents the extended workloads beside core workloads for the same
  platform, architecture and revision. Local measurements retain their actual machine identity.
- The final historical import requires all 130 revisions and 33,800 cells. Empty historical
  cells are not evidence of measured performance. Full local surfaces do not establish CI or
  macOS measurements.

The sparse recipe remains readable for audit and provenance. A matching complete dense local
observation supersedes its sparse selectable view. Ongoing CI collects the dense recipe;
sparse CI artifacts cannot activate the new surface. macOS fixture capacity increases to 4 GiB
to hold both native files and TAR fixtures across the complete grid.

## Running replacement history

On `drogon.boerger.online`, the new task root is:

```text
/home/marcus/Documents/xff-dense-benchmark-20261115
```

The original campaign at `/home/marcus/Documents/xff-chart-campaign-20261107` remains intact.
The replacement uses its frozen revision manifest, retained production ThinLTO executables and
native resource launcher. It uses new fixtures at `/run/user/1000/xff-dense-history-20261115`, verified
as tmpfs, and new raw sample directories. No build runs during collection. The oldest and anchor
revisions run first, then the remaining revisions in the frozen shuffled order. Cells are also
shuffled, and participant order rotates every round. Endpoint cells belong to the final history
and do not need measuring again in a separate endpoint run.

- `measurement-process.json`: exact command, PID and driver hashes.
- `revisions.json`: copied original 130-revision manifest.
- `measurements/contract.json`: complete frozen recipe, allocation and binary evidence.
- `measurements/progress.json`: running/completed/failed state and cell count.
- `measurements/cells/SHA/TASK/SHAPE/COUNT/CPUS/complete.json`: completed raw cells.
- `measurements.log`: progress and failure output.

Resume using the exact recorded command and unchanged drivers. A contract or binary mismatch
must fail. The collector uses a lock, retains failed attempts and verifies fixtures and tools
after collection. Do not publish the campaign before completion and those final checks.
Collection completed at 2026-11-16 00:33 UTC: all 130 revisions and 33,800 cells, with final
fixture/tool/launcher/binary checks and no invalid-data marker. The measurement phase took
25,896 seconds. All records were copied to this Mac and exported into complete 13-count
surfaces; both views and the oldest/latest versions loaded without browser errors.
The importer subsequently validated all 130 observations against the original retained
production executables and ThinLTO link evidence. Staging is separate from publication.
The [publication qualification](benchmarks/extended-dense-publication-20261116.json) records
405,600 checked chart coordinates, lossless compaction, 650 unchanged prior catalog entries,
807 unchanged packed reports and successful browser loads of all seven available datasets.

These are local Linux measurements on Drogon. They do not populate the GitHub CI datasets,
and no equivalent macOS historical extended population has been collected. Existing local
core layout names (`broad/v1`, `broad/v2`) are independent of the extended suite's recipe version.

There are 30 further main merges after PR #1130 through `42d87175e0` (#1159), requiring
7,800 additional extended cells. This PR's own merge must be included once its exact commit
exists. The current 130-revision campaign remains immutable; later observations must retain
their own complete collection contracts and use the same fixed reference for that local series.
No tail collection has started and the original measurements do not need repeating.

## GitHub historical continuation

PR #1153 supplies continuous extended Linux/macOS collection and combines those CI workloads
with the corresponding original platform/revision in the viewer. Its historical local importer
does not turn local observations into CI measurements. The existing GitHub backfill workflow
still collects core benchmarks; historical extended CI scheduling uses the separate
`Extended benchmark backfill` workflow.

It walks main's first-parent merges newest first, back to the oldest retained CI commit
(currently PR #868, `c092e8245ff843ab2f622cc39a0f226ff95d9f61`). Missing commits between
catalog entries are included. Linux/macOS completion is independent. The default batch is
ten missing revisions, validated between 1 and 100; each revision requires at most two jobs,
with at most eight active jobs. A 100-revision batch therefore has at most 200 matrix jobs.

After the workflow is merged onto main, start or resume it with:

```sh
gh workflow run benchmark_extended_backfill.yml --repo mboworks/xff --ref main \
  -f batch_size=10 -f platform=both
```

Repeat that command for the next missing batch, or select `platform=linux`/`platform=macos`.
Queued batches plan after their predecessor finishes. Planning first recovers valid retained
artifacts from completed continuous/backfill runs, including failed workflows and successful
earlier attempts; it does not wait for their Pages publication. Published catalog entries and
validated raw checkpoints both satisfy completion. Transport failures during checkpoint
recovery fail planning instead of silently scheduling expensive duplicate measurements.
Invalid/missing/expired reports remain missing and are eligible for retry. An empty plan exits
without starting measurement jobs. Retry failed jobs or start a new batch; successful partner
platforms are preserved. No automatic whole-history dispatch is performed by merging this code.

Each job builds the exact historical full-production head and actual first parent with ThinLTO,
copies each configured executable immediately after its build, records link/configuration
evidence, uploads/downloads the original bytes and shuts down build servers before timing.
The current trusted driver collects the complete 260-cell head grid and identical-parent
control grid using RAM fixtures. The recipe, fixture oracle, interleaving and fastest-7/9
sampling remain identical to continuous CI. Both historical builds and raw reports are retained
as 30-day artifacts, with attempt-specific names; no historical script is executed as a driver.

The report binds the historical target separately from the workflow source, frozen campaign,
job, repository and attempts. Publication downloads the frozen plan, checks trusted main-only
workflow metadata, actual GitHub first-parent identity and ancestry, then validates the complete
measurement/build proof. Report self-claims cannot substitute for those independent checks.
Every valid revision/platform in a partially failed batch is retained independently. The shared
publisher processes the batch after workflow completion and can also recover it on schedule;
publishing every individual revision is unnecessary. Existing core and local data are preserved.

Begin with ten revisions and inspect actual runner duration, artifact size and total
`storage-report.json` before increasing batch size. The 200 MB warning remains a review point;
the existing site is already substantially larger. Full-history storage growth and lossless
representation work remain tracked in [site storage](site-storage.md). Do not remove or round
observations to make the backfill fit. A 100-revision input is a supported bound, not a claim
that its full cost or Pages deployment has already been qualified on hosted runners.

Keep the current measurement contract and original core observations. New CI results add
the five workload slots to the existing CI history; unmeasured older slots remain empty.
Native Mac and Drogon history remain useful independent local evidence, not CI replacements.

### First hosted batch and Linux runner correction

[The first hosted batch](https://github.com/mboworks/xff/actions/runs/44177510657)
selected ten revisions on both platforms. All ten macOS jobs succeeded. All ten Linux jobs
stopped during runtime preparation, before building or measuring: the planner selected
`ubuntu-latest`, which supplied Ubuntu 26.04, while `prepare_benchmark_runtime.sh` requires
Ubuntu 24.04 for historical compiler dependencies. New Linux plans use `ubuntu-24.04`.
The actual-distribution guard remains enforced.

The original immutable mixed-platform plan still validates for artifact recovery; changing
new runner selection must not invalidate its ten successful macOS results. Resume with a new
main-branch dispatch after the runner correction merges. The planner recovers those reports
before selecting missing jobs. Rerunning the original run uses its original workflow source
and repeats the Linux setup failure.

The first preparation attempt hit the per-user quota on `/dev/shm`, despite free filesystem
space. It collected no timing cells. Its contract, log and process receipt are preserved in
`failed-preparation-shm-quota/`; its partial fixture remains separate. The replacement runtime
directory is an existing tmpfs with 6.1 GiB free and no configured user quota. Fixture I/O remains
in memory; no fallback to a disk filesystem is allowed.

## What the TAR comparison measures

The existing TAR case compares one XFF process with one GNU tar process. Both filter archive
member names and print the same selected paths. GNU tar performs wildcard matching and prefix
transformation internally. No find, grep, shell pipeline or per-member executable is involved.
Consequently this case measures archive listing/filtering overhead; it cannot establish the
benefit of fusing a multi-process workflow. A genuine compound workload needs its own equivalent
commands, independent output oracle and qualification before entering the chart. The reference
must not acquire unnecessary processes merely to favor XFF.

XFF currently reads the archive headers, builds a virtual filesystem index and walks that index.
Its first output can therefore occur much later than a streaming TAR listing, even when total
wall times are similar. This is work in XFF and its linked archive library, not a conclusion about
disk throughput.

## Separate TAR diagnostics

The [retained summary](benchmarks/extended-tar-diagnostic-20261115.json) contains 12 cases:
100/1,000/10,000 members, both shapes and 1/3 CPUs. It uses the retained PR #1130 full-production
binary, SHA-256 `c47c904b693c8c114bb87284497b98a10e4925f25afdc3f5ec4f26896ad1a2e1`.
There are 27 measured rounds retaining 21, with identical-binary controls. Every invocation's
complete output passed the oracle. These samples are diagnostic and never join chart samples.

Selected times in milliseconds:

| Shape | Members | CPUs | XFF wall | TAR wall | XFF first output | TAR first output |
| :---- | ------: | ---: | -------: | -------: | ---------------: | ---------------: |
| Broad |     100 |    1 |    1.497 |    1.189 |            1.337 |            0.906 |
| Broad |   10000 |    1 |    8.903 |   10.309 |            7.071 |            1.120 |
| Deep  |    1000 |    3 |    2.807 |    2.315 |            2.177 |            0.976 |
| Deep  |   10000 |    1 |   11.571 |   11.616 |            9.231 |            1.109 |

Separate perf runs repeat the checked 10,000-member commands 200 times for each tool and shape.
On deep XFF listing, approximately 20.89% of sampled user cycles are in `__mbrtowc` and 15.22%
in ASCII conversion. Call stacks lead through `archive_mstring_get_wcs`, libarchive's TAR header
reader and XFF `ReadMembers`. String comparison accounts for about 8.24%, including tree-index
operations. Broad listing shows about 20% in the two conversion functions and 12.78% in string
comparison. These are sampled user-CPU shares from this executable and host, not wall-time shares
or proof that the entire cost is removable. Conversion correctness for non-ASCII names must be
preserved when investigating a fast path.

The diagnostic evidence is under `tar-diagnostics/` in the new task root: accepted raw rows,
`summary.json`, `complete.json`, profile specifications, `perf.data`, `self.txt`, `flat.txt` and
`strace.txt`. Syscall tracing is separate from accepted timing samples. The timing and profiling
phases completed before the dense campaign started.

Next performance checks must reproduce the hotspots against current main, separate header
decoding from index construction/traversal, and qualify a real compound archive/content workflow.
The PR #1130 profile identifies concrete investigation targets; it does not prove current-main
performance or justify promising a particular speedup.
