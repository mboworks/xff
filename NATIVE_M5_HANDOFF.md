# Native M5 Pro scheduler audit handoff

Run the same scheduler controls measured on Linux on the native Apple M5 Pro.
The immediate task is three independent full-grid timing sessions. Then collect
native CPU/allocation profiles and the boundary and slow-storage controls below.
Return the complete evidence, including failed runs. Engine adoption is outside
this task; hosted macOS CI cannot substitute for this machine's measurements.

## Fetch the handoff

This document is also available on drogon at
`/tmp/xff-native-m5-handoff/NATIVE_M5_HANDOFF.md`:

```sh
scp marcus@drogon.boerger.online:/tmp/xff-native-m5-handoff/NATIVE_M5_HANDOFF.md .
```

From your existing XFF clone on the Mac, create a separate worktree. Leave your
current branch and tracked/untracked changes untouched:

```sh
git fetch origin audit/native-m5-handoff audit/native-scheduler-candidate
git worktree add --detach ../xff-native-m5-audit origin/audit/native-m5-handoff
cd ../xff-native-m5-audit
git rev-parse HEAD
```

`audit/native-m5-handoff` contains the runner and the frozen Linux audit driver.
`audit/native-scheduler-candidate` retains the experimental candidate for exact
cross-machine reproduction. These are communication branches, not merge requests.
Record the exact fetched handoff commit before running; do not rebase or sync it
during collection.

## Prepare the Mac

Use the native M5 Pro, on AC power, with its power mode recorded. Stop unrelated
builds, profiling, indexing and other heavy work. Keep CPU sampling and allocation
profiling out of the timing sessions. Do not change host security settings.

Required commands are `git`, `bazel`, `python3.13`, `find`, `rg`, `fzf` and GNU
`sort` (`gsort` from coreutils is preferred). The runner verifies NUL sorting
before expensive builds. Install missing prerequisites deliberately; it does not
install packages itself. Instruments/Xcode is needed for the later profile step.

Choose an existing directory on the actual storage you want to measure, with
enough space for disposable full-grid fixtures. Ordinary local APFS storage is
fine for the first run. It is not a verified RAM disk or slow-storage control.
Keep at least several GB free for retained output, plus build/toolchain caches.

## Run the timing collection

For example, create a fixture parent on your normal local volume and run:

```sh
mkdir -p "$HOME/xff-benchmark-fixtures"
bash tools/native_scheduler_handoff.sh "$HOME/xff-benchmark-fixtures"
```

The wrapper prints a fresh output directory, builds the native accounting helper
and all three release executables, then measures only after all builds finish.
Preserve that directory before temporary-directory cleanup or a reboot. The
default is beneath the Mac's `TMPDIR`; set `TMPDIR` to an existing persistent
directory before launching if you prefer to retain it there.

The exact source controls are:

- Baseline: `772994ab7905be77915978f06ea90ae5934aaeef`.
- Previous scheduler: `af967729eeaf9689367cf19c20ec66eed60ee2ec`.
- Completion repair: `7e0abb52547a337e78b52ef8ee86cad90063b9a8`.
- Frozen benchmark driver: `0e296e40ca6bf523215d80d1dc02deb3503b796c`.

Each executable uses its historical `clang_release` configuration. Source trees,
configuration hashes, executable hashes, launcher identity and driver hashes are
checked and retained. New Mac builds do not verify historical Linux executables.

Each session uses both complete prepared v2 layouts and all thirteen file counts:
`10 20 50 100 200 500 1000 2000 5000 10000 20000 50000 100000`, with worker
requests `1 3 10`. Do not reduce that grid: its topology affects the workload.
The fifteen tasks include published comparisons plus tree-order, collection,
lazy-stat and eager-stat controls. There are 1,170 cells per session and 3,510
across declared, reversed and seeded-shuffle order. Participant order rotates;
each participant has one warm-up and nine retained samples, estimating the mean
of the fastest seven. Output counts/hashes and tree order are checked per run.

macOS worker requests are allowances, not verified Linux-style CPU affinity.
The records keep that distinction, performance/efficiency core observations,
first-output latency, direct native process RSS/CPU accounting, raw timings and
losslessly compressed stdout. The storage label is descriptive, not proof of
physical or slow storage.

If a run fails, stop and retain its full directory and logs. Do not overwrite it,
resume its incomplete contract, silently reduce workers/grid, replace a failed
measurement, or declare success from a partially populated manifest. A retry
gets a new output directory and an explicit explanation of what changed.

## Collect native profiles after timing

Use Instruments Time Profiler and Allocations, or a native equivalent with raw
artifacts retained. Consult the locally installed tool's help for exact launch
and export syntax. Do not run these tools concurrently with accepted timing.

Recreate the complete v2 fixture models using the pinned handoff's
`benchmark_layouts` and `benchmark_fixture` modules. Timing fixtures are disposed
after each session, so old recorded working-directory paths will no longer exist.
Verify recreated fixture/anchor hashes against the timing manifests; reuse the
same relative commands and correctness oracles on the recreated roots.

Profile fresh, correctness-checked invocations with their source/command/binary
identities recorded. Aggregate enough fresh invocations to resolve short phases;
do not replace fresh-process startup with one indefinitely warm process.

- Broad 10/20 and deep 1,000/2,000 file boundaries, jobs 1/3/10: first thread/pool
  activation, thread creation, wakeups, queue/result allocation, mutex work,
  coordinator progress and teardown. Compare the three pinned sources.
- Single-worker 20k/50k/100k files, tree order, collection and content tasks:
  directory enumeration, entry/path construction, metadata calls, collection
  allocation, sorting, expression dispatch and output formatting.
- Content and stat-heavy tasks: matcher startup, batching, worker reuse,
  coordinator work and any nested-pool oversubscription.

Return the original `.trace`/native profile files, their exported samples/stacks,
symbols and the exact measured executable. Record profiler version and settings.
Keep unresolved frames explicit; do not infer missing callers or convert sampled
CPU periods into wall-time shares. If a separate diagnostic/debug build is
necessary, retain its different flags/hash and keep it out of accepted timing.
Check direct native peak RSS against a separate native measurement before making
arena/capacity claims; report the launch method and units for both observations.

## Additional correctness and storage controls

Read `docs/performance-analysis.md`, especially P08 and adaptive worker activation,
and `docs/benchmark-correctness-audit.md` before drawing conclusions. Retain the
four-entry matcher chunks, 64-entry startup threshold, inline tails below 16 and
existing decision/rendered batch policies as controls. Do not presume that the
previously rejected 128-entry startup, one-entry chunks or async overlap improve
performance. Activation evidence must describe pending work, not a lifetime count.

As separately labelled diagnostic fixtures, cover 0/1/10 entries, 15/16/17 and
63/64/65 matcher batches, 511/512/513 metadata entries, two empty siblings, many
tiny directories, a few large files, clustered/interleaved mixed-size content,
early quit and prune. Record workers actually created/active, queued work,
first-result latency and retained bytes. Use deterministic injected observers
for correctness assertions rather than wall-clock thresholds. Keep these distinct
from the published full-grid contract.

Repeat the A/B controls on genuinely different, identified storage if available.
Record the mount, filesystem, backing device/network server and cache/warm-up
policy. Explain why it represents slower I/O rather than merely relabelling the
same APFS SSD. Preserve the stat-heavy/content/deep/single-worker controls. Do not
flush global caches, create mounts or change machine settings without approval.
If suitable slow storage is unavailable, report that gate as unavailable rather
than replacing it with an unrelated fixture or claiming cross-storage acceptance.

Run affected correctness/concurrency tests and appropriate supported sanitizers
in a separate phase. Preserve commands and logs. Prefer `ABSL_*` macros and their
correct dependencies; do not replace them with unprefixed macros.

## Exactly what to return

Return one archive containing each complete wrapper output directory, plus the
native profiles and a concise findings file. Include failed/partial directories
as separate runs. At minimum retain:

1. `prepare.log`, `collect.log`, `fixture-df.txt`, `host-mounts.txt`,
   `fixture-diskutil.plist` and its diagnostics, plus the exact handoff commit.
2. `run/build-state.json`, `run/helper-build.log` and `run/resource-launcher`.
3. All three `run/{baseline,parent,candidate}/binaries/COMMIT/` directories,
   including the actual `xff`, `build.json` and `build.log`. Do not send only a
   new rebuild or only its hash.
4. The complete `run/collection/`: `manifest.json`, source snapshots, all three
   session reports, every `cell-*.json` and every referenced `stdout/*.gz` blob.
   Do not prune outliers, warm-ups, reference-tool results or unchanged outputs.
5. Original native profile/trace artifacts, exports, profiler settings, captured
   symbols and binary identities, fixture hashes, exact commands, invocation
   counts and correctness checks. Include any separate diagnostic binaries.
6. Boundary/storage-control raw results and fixtures/contracts, worker/queue/
   retention observations, direct RSS cross-checks and correctness/sanitizer logs.
7. `findings.md`: host/OS/power mode and storage identity; completion/failure
   status per phase; absolute XFF and reference times and same-run ratios for
   each session; repeated regressions as well as improvements; unresolved frames;
   and an explicit list of unavailable or unverified acceptance gates.

For a transfer, replace `task_output` with the exact path printed by the wrapper:

```sh
task_output='/absolute/path/printed/by/the/wrapper'
task_package=$(mktemp -d "$HOME/xff-m5-return.XXXXXX")
tar -czf "$task_package/native-m5-audit.tgz" -C "$task_output" .
shasum -a 256 "$task_package/native-m5-audit.tgz" > "$task_package/native-m5-audit.tgz.sha256"
```

Add separately stored profiles/failures to a staging directory before packaging,
or return additional archives with their hashes. Check archive contents before
transfer. Send back the archive(s), SHA-256 file(s), exact source branch/commit,
and a message listing which phases completed. You can upload them to an agreed
destination on drogon; do not overwrite existing evidence. Alternatively give
the accessible paths and transfer instructions. A screenshot or summary alone
is insufficient to independently reconcile timings and provenance.

## Goal to paste into Codex on the Mac

```text
Complete the native Apple M5 Pro portion of the XFF benchmark correctness audit.
Fetch audit/native-m5-handoff and audit/native-scheduler-candidate from mboworks/xff
into an isolated worktree; preserve my existing checkout and dirty files. Read
NATIVE_M5_HANDOFF.md, docs/benchmark-correctness-audit.md and
docs/performance-analysis.md completely, then follow their native investigation
and evidence-return requirements. Pin the exact handoff/source commits. Run the
prepared three-source, full-grid, three-order timing handoff on real local storage,
with no profiling/build overlap and no grid/worker/sample reductions. Then gather
native CPU/allocation profiles, independent RSS, boundary/heterogeneous/quit/prune
controls, affected correctness/sanitizer results, and identified slow-storage A/B
where available. Keep engine merges and benchmark publication outside this task;
never weaken correctness checks, alter host security/settings or infer missing evidence. Keep
all raw samples, outputs, binaries, manifests, profiles, logs and failed runs.
Return the complete archive(s), SHA-256 hashes and findings.md exactly as requested
by the handoff. Report unavailable gates honestly and ask before actions requiring
new authority. Timing collection alone is not completion of the full native audit.
```
