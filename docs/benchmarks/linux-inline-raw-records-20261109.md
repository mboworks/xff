# Linux inline raw record qualification (2026-11-09)

## Outcome

This follow-up to PR #1151 reduces raw filename-output work on Linux. Records up to
256 bytes, including their terminator, use uninitialized inline storage. Longer records retain
the existing bounded string fallback. Each buffer belongs to one coordinator, and consumers
must finish using its borrowed record before returning.

Linux measurements support a modest large-listing improvement and about 2.1% fewer userspace
instructions. The macOS portable experiment did not establish a benefit, so the final change
preserves that platform's previous implementation. No macOS speedup is claimed.

The measured final source is `90f8c8c0579b1132ea4d0bc714da9f855a77e012`; its qualified #1151
baseline is `1ea02a41d7dcf25c043e1079c98a9e60d6d73290`. The experiment parent `720fedab08`
and this baseline differ only in tooling, workflows and documentation before this output change.

## Final production executable comparisons

Both full (`//xff/cli:xff_full --config=clang_release --config=xff_full`) and minimal
(`//xff/cli:xff --config=clang_release`) CLIs were built from clean exact sources. Each build
has a matching configured query and immediate copy before any later Bazel build. Executable
SHA-256, source/tree identity, unchanged configuration hashes and actual ThinLTO link actions
are retained. Build servers were stopped before collecting measurements.

Final listing screens cover 100 and 100,000 files, broad/deep trees, one warmup and 27 measured
rounds retaining 21. The full-CLI screen uses three workers; the minimal screen covers workers
1/3/10. Every cell rotates candidate, baseline and GNU find. An identical-baseline population
provides separate controls. All 32 cells and 2,688 participant invocations passed on each host.

The table equally weights broad/deep trees at 100,000 files and three workers. Negative elapsed
changes mean less time. Candidate/find above one means XFF takes longer.

| Platform | CLI     | Elapsed change | Identical control change | Candidate/find |
| -------- | ------- | -------------: | -----------------------: | -------------: |
| linux    | full    |         -1.52% |                   +0.87% |         1.0942 |
| linux    | minimal |         -1.09% |                   +0.49% |         1.1055 |
| macos    | full    |         -0.60% |                   +0.15% |         0.9805 |
| macos    | minimal |         +0.57% |                   -0.01% |         0.9881 |

XFF still takes about 9-11% longer than GNU find in these Linux cases. Small-case changes and
macOS's sub-percent differences remain within observed timing variation. These point estimates
do not establish a universal speedup or performance on independent machines.

Separate final Linux counters use nine correctness-checked repetitions per binary/case, after
accepted timings finish. Large broad/single-worker listing uses 2.14% fewer instructions;
deep/three-worker listing uses 2.10% fewer. All counters have 100% scheduled coverage. Counter
cycles and task-clock samples remain diagnostics rather than additional elapsed-time samples.

## Broader qualification and rejected experiment

The initial portable implementation at `59132e3c2d86b21e8ec54e0a978297c85fb168b9` completed all
five extended workloads on both hosts: listing, native-case filtering, metadata/date output,
content regex and TAR filtering. Both shapes, all declared sizes and workers 1/3/10 give
90 candidate cells plus 90 identical-baseline controls per host. One warmup and nine measured
rounds retaining seven give 5,400 independently output-checked invocations per host.

Linux's descriptive full-grid ratio was 0.992727, with control 0.996151. Several individual
cells were initially slower. A separate 24-case, 27-round repetition included every slower
paired interval and its opposite shape, all five small workloads at three workers, and the
primary large-listing case. All 48 cells and 4,032 invocations passed. None of the repeat's
candidate paired intervals remained wholly above parity. Initial slower cells remain in the
retained summaries and raw data.

The portable macOS full-grid ratio was 1.001055, with control 0.998778. Its separate 81-round
small-listing repeat did not establish a benefit. Both populations are retained as rejected
macOS experiments, including their slower cells. They are not relabeled as final results.

Final ELF allocated sections match the measured Linux prototype except the excluded build-id
note. Final macOS full/minimal code/data sections and dyld bindings match their corresponding
baseline. Whole-file hashes differ: the comparison excludes the metadata listed in the
[qualification data](linux-inline-raw-records-20261109.json). The final executable screens
above measure the actual new files directly.

## Correctness, fixtures and retention

The final source passes four existing suites on both hosts under `clang_opt`:
`record_buffer_test`, `render_test`, `expression_execution_test` and `run_test`.
The new boundary case checks sizes immediately below/at/above inline capacity, embedded NULs,
alternating newline/NUL terminators and reuse after long records. Existing cases check queued
copies, growth and release of oversized storage.

Every measured invocation must match an independent multiset oracle, including duplicate counts,
exit status and diagnostics. Warmups, all measured rounds, commands, output counts/hashes, host
context and failed-attempt diagnostics are retained. Final fixture, binary, reference-tool and
native-launcher checks passed for every completed population.

Linux uses the existing verified tmpfs fixture on drogon with recorded guest-core affinity.
macOS uses newly owned 2 GiB case-sensitive HFS+ RAM images without CPU affinity. Four separate
creation/release receipts verify live device/image/UUID ownership before detach and absence
afterward. All four images were released; their device names are not authority for future cleanup.

Raw observations, original and final production bytes, configured link actions, native accounting
helper, source bundles and verification scripts are archived on drogon:

- `/home/marcus/Documents/xff-inline-records-linux-qualified-20261109.tar.gz`
- `/home/marcus/Documents/xff-inline-records-macos-qualified-20261109.tar.gz`

Sizes and independently checked SHA-256 values are in the qualification JSON. The original Linux
working evidence is `/home/marcus/Documents/xff-inline-records-20261109`; macOS originals are
`/private/tmp/xff-inline-records-evidence-20261109`. No sampling contract or fixture recipe changed.

## Coverage inventory

The refreshed [merged-PR coverage inventory](../benchmark-merged-pr-coverage.md) distinguishes
direct main-branch merges from development-branch merges and records current known missing work.
Chromium loaded PRs #914, #986 and #1143 on both platforms in the published viewer, including their
actual figure payloads, with no page errors. Their conservative binary verification state remains
unchanged. This does not claim that all missing merged-PR coverage has been recovered.
