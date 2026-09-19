# Benchmark history

The [published benchmark history](https://mboworks.github.io/xff/benchmarks/) retains paired
measurements for PRs, main merges, and releases. This is performance evidence, not a merge gate.
A failed measurement fails its own workflow and publishes no successful result; it does not change
xff's required test checks. There is no percentage threshold for a regression yet.

## What is compared

The independent `Benchmarks` workflow builds the PR head and its immediate base branch SHA,
including for stacked PRs. Main and release runs use the measured commit's first parent as the
baseline. Both revisions use `--config=clang_release`, the production optimization configuration,
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

The new workflow can be dispatched manually from its Actions page. It starts collecting automatic
PR/main/tag history when merged. There is no historical backfill of measurements that were never
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
