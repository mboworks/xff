# Historical benchmark backfill

Use one batch on one host to measure historical XFF revisions with the same current driver,
reference tools, generated fixtures and CPU/worker requests. This produces a separate measurement
series; it does not replace or relabel older hosted measurements.

The initial local grid is one/four workers. Four preserves the historical scaling target; eight
can be added explicitly for a scaling experiment. macOS does not provide the CPU pinning used by
our Linux driver, so its allocations are worker requests. A Linux Zen 5 series can enforce CPU
affinity, but remains separate from the Mac and hosted series.

## Local batch

Install Bazel, Python 3.13+, ripgrep and fzf. `find` must also be on PATH. Use a checkout containing
all selected commits; the driver never changes that checkout or includes its uncommitted edits.
Place the desired Git references in a whitespace-separated file, or repeat `--revision=REF`.

```sh
python3 tools/benchmark_backfill.py \
  --repo="$PWD" --output=/tmp/xff-backfill-macos --series=macos-m5-pro \
  --revisions-file=/tmp/xff-backfill-revisions.txt \
  --cpus=1 --cpus=4 --disk-cache="$HOME/.cache/bazel-disk"
```

Without `--run`, this resolves references to full commit IDs, sorts them by commit date, validates
capacity and tools, and writes `batch.json`. Inspect it, then repeat the command with `--run`.
The same driver is available as `bazel run //tools:benchmark_backfill -- ...`.
Use `--history-root=PATH/benchmarks` instead of a revision list to select all commits with retained
macOS comparison reports from a coverage-pages checkout. The selection is deduplicated.

For a Linux run, select a separate `--series=linux-zen5`, and add
`--require-cpu-affinity --fixture-parent=/dev/shm --require-memory`. The allowed CPU mask must
contain at least the requested count. CPU affinity restricts logical CPUs; select an appropriate
host mask before starting if the experiment requires distinct physical cores rather than SMT siblings.

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
is separate from measurement; this workflow does not rewrite hosted data or invent paired results.

## Manual GitHub workflow

**Benchmark backfill** is a manually dispatched, single-job macOS workflow. It defaults to all
retained macOS comparison commits, or accepts an explicit whitespace-separated revision list.
It installs reference tools once and measures the entire selection on the same hosted machine
with one/three workers. Its artifact contains the manifest, status, reports and build logs.

Use the local workflow for a complete campaign that may exceed a hosted job's time limit. The
hosted measurement step has a 345-minute ceiling within a 360-minute job, leaving time to upload
completed results after failure or timeout. A partial batch is visibly incomplete, never presented
as a complete history. Another hosted runner cannot resume the same host series.

## Presentation normalization

Raw results remain unchanged. Reference-window normalization belongs in the presentation layer,
separately from collection. Worker allocations and machine series remain separate. Multiplying
reference and XFF elapsed times by the same normalization factor does not change their same-run
ratio or compensate for different CPU allocations. This workflow retains all observations needed
for that presentation work; it does not normalize samples while collecting or resuming.
