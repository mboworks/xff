# Benchmark history

The [published benchmark history](https://mboworks.github.io/xff/benchmarks/) retains paired
measurements for main merges, attributed to their PRs. This is performance evidence, not a merge gate.
A failed measurement fails its own workflow and publishes no successful result; it does not change
xff's required test checks. There is no percentage threshold for a regression yet.

## What is compared

The informational `Benchmarks` workflow runs automatically only on pushes to main after merge.
It compares the merged commit with its first parent. PR pushes and release tags do not start
separate measurements. Manual dispatch can select a branch for an experiment against its first parent. Manual results remain Actions artifacts;
only successful main-push measurements are published. Both revisions use `--config=clang_release`, the production optimization configuration,
on the same hosted Linux runner. The head revision's measurement driver runs both binaries.
The report links both exact commits and records each executable's SHA-256 digest.

Five repetitions alternate base/head and head/base. Each repetition uses deterministic broad and
deep fixtures: 2,000 files per root, 1,024 bytes per file, and 40 directory levels in the deep case.
The six workloads are listing, extension summary, hashing, repeated hashing, hashing with line
counts, and directory comparison. One worker is requested from xff to reduce scheduler variability.
Build time is excluded. Full runs restore the existing default build cache without creating an
additional Actions cache generation.

Fixtures are freshly written for each driver invocation. No OS cache flush is performed, and
kernel cache reuse is uncontrolled: these are neither cold-disk measurements nor guaranteed
steady-state measurements. Alternating revision order reduces order bias but cannot remove noisy
neighbors or thermal effects on a hosted runner. Raw reports include commands and output byte
counts; the optional observer runs separately to report logical VFS reads, not storage-device I/O.

## Reading a result

Each workload shows base and head medians, minimum, maximum, sample standard deviation, sample
count, and head/base ratio for elapsed time, first stdout latency, user/system CPU time, peak child
RSS, and stdout bytes. No stdout means latency is unavailable, not zero. A zero baseline has no
ratio. Peak RSS comes from a fresh measurement process for each invocation, so repetitions do not
inherit an earlier maximum. Reported variability is descriptive, not statistical significance.

The versioned contract records fixture/scenario versions, fixture identity, build arguments,
build-setting/toolchain identities, Python/Bazel information, runner class, platform, CPU count,
worker count, repetition policy, and cache treatment. Different build identities omit ratios and
retain the two measured distributions. The identity conservatively hashes `.bazelrc` and the LLVM
module configuration; even a nonfunctional edit can therefore suppress a ratio. No ratios are
calculated between separate workflow runs or different hosts. Raw observations remain downloadable
as JSON so summaries can be reproduced.

## Publication and retention

The publisher executes trusted main-branch code and treats measurement artifacts as JSON data.
It checks the measured head against the source workflow SHA and derives run identity from GitHub.
It shares the `coverage-pages` deployment queue and retained branch with coverage and release
publication, preserving their pages.

The index groups the newest retained result for each PR phase and release. PRs use actual merge
time, newest first, with post-merge before pre-merge within a group. Release rows use the tagged
commit time. Open PRs use measurement creation time. Unassociated main runs are collapsed into
one row. Closed, unmerged PRs are omitted. Each result names its exact baseline; an aggregation
PR's post-merge measurements are not silently attributed to its constituent PRs.

Actions artifacts are kept for 30 days. The published site retains at most 100 successful run
attempts, including their raw observations. Old attempts remain distinguishable by run/attempt
identity but do not become extra index rows. Cleanup removes the oldest records and detail pages;
therefore an old PR or release eventually leaves this bounded history. Replaying the same attempt
is idempotent; changing its measurements is rejected. No generated measurements are committed to
the source branch. Missing, cancelled, or failed runs do not create fabricated measurements.

The workflow can be dispatched manually from its Actions page. Automatic collection and
publication occur after merge only. There is no historical backfill of measurements that were never
made. Initial runs should establish total build/measurement cost and runner noise before changing
fixture sizes or proposing any performance gate.

## Local measurement and tests

Build `//xff/cli:xff`, `//xff/engine:read_benchmark`, and `//tools:benchmark_resources` with the
chosen optimized configuration in each checkout. Run `//tools:benchmark_history` with the
`measure` arguments shown in `.github/workflows/benchmarks.yml`, using both executables and exact
commit IDs. A development-build smoke check verifies the harness only; do not publish it as an
optimized comparison. Small correctness tests are Bazel-owned and do not run performance workloads
in pre-commit:

```sh
bazel test //tools:benchmark_history_test //tools:measure_resources_test
```

Comparisons against `find`, `rg`, and `fzf` are deliberately a separate, deferred follow-up (F10).

## Initial hosted observations

The [first post-merge run](https://github.com/mboworks/xff/actions/runs/35470455554)
compared `0bcb461dbb08a1d3b14d1d73938a8b905aed81ea` with its first parent
`2bd76d6e682f7ca9c987f364fda485a2eb906d62` on September 19, 2026. The complete
job took 6 minutes 19 seconds: paired builds took 4 minutes 43 seconds and measurement took
68 seconds. All twelve workloads completed five repetitions for each revision. The
[raw report](https://mboworks.github.io/xff/benchmarks/runs/35470455554/1/report.json)
and [rendered comparison](https://mboworks.github.io/xff/benchmarks/runs/35470455554/1/)
were verified after publication. These links are subject to the bounded retention above.

Elapsed-time head/base medians ranged from 0.9941 to 1.0092. Across the twelve workloads,
the median sample-standard-deviation/median ratio was 0.52% for the base and 0.67% for the
head; maxima were 1.21% and 1.08%. This descriptive ratio uses the median denominator and
is not the conventional coefficient of variation. The earlier PR run had a 7.34% maximum
on the base side, demonstrating why one quiet run cannot establish a regression threshold.
No C++ behavior changed in this benchmark-infrastructure PR. These initial observations
validate the collection/publication path and support leaving performance checks advisory;
they are not evidence of a performance improvement.

## Required checks versus informational experiments

Harness correctness tests already run in the normal PR workflow through `bazel test //...`.
Any future required performance check must be integrated into that same PR test workflow,
reusing its build and test execution. It must not introduce a separate required benchmark run.
The post-merge workflow remains informational. Tagging a measured commit does not repeat it.
Historical pre-merge and tag records remain readable, but the current automatic schedule does
not generate new ones.

## Stable release and PR links

Release navigation links to `benchmarks/tag/VERSION/`, matching coverage's numeric version
convention (for example `benchmarks/tag/0.7.0/`). PR links use `benchmarks/pr/NUMBER/`. These
landing pages redirect to retained run/attempt details; they do not copy measurements or start
benchmark jobs. Annotated and lightweight tags resolve to their exact tagged commit. A PR prefers
its exact post-merge measurement and can fall back to its own retained pre-merge result.

A release with no retained measurement for its exact commit gets an explanatory page, not a
redirect to a different commit or a dangling run URL. Retention refreshes these pages when results
expire. The publisher refreshes references after measurements and completed release workflows;
manual `Publish benchmarks` dispatch refreshes existing data without downloading an artifact.
It remains serialized with all other site publishers.

To repair an older release after this change, first dispatch `benchmark_pages.yml` from main.
Then dispatch `pages.yml` with `tag=v0.7.0` and `config_path=docs/release-site-plain-notice.json` to regenerate its
navigation using the current tracked configuration. No release tag or measurement is rewritten.
