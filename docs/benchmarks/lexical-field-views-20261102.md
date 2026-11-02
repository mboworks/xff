# Borrowed lexical field components (2026-11-02)

## Decision

Use the existing `StringOrView` return type to borrow filename, stem, final extension, all suffixes and core slices directly from the input. Apply the same helpers to path qualifiers and language/MIME filename lookup. Keep directory rendering through native `parent_path`: repeated-separator normalization is observable and does not always correspond to one input slice. No new path wrapper, per-entry cache, thread, ownership hierarchy, flag or dependency is introduced.

The input context owns or borrows a stable path for the duration of rendering. Compiled templates append borrowed values while that context and any owned transformed field value remain alive. Directory results remain owned when native path rendering constructs them. Backslashes remain ordinary filename bytes on both supported POSIX hosts. Embedded NULs remain length-delimited input, not C-string terminators.

## Measurement contract

Reference main `2b21790502fd6bf9b6e1afb53cc1444cf140f296` includes collection text sharing and prepared globs. Only `fields.cc` differs between runtime variants; both use the same test-only kernel source and build configuration. No unmerged fuzzy change is included. Source/binary identities and scripts are retained below.

Release LLVM 23 + ThinLTO on the local M5 Pro (warm APFS, requested 1/3/10 workers without affinity) and drogon Ryzen 9950X KVM guest (tmpfs, child affinity 1/3/10). Builds and tests finish before timing. Three sessions, discarded native warmups, nine interleaved rounds, fastest seven. Every fresh command checks exact complete output against an independent filename/component/directory oracle. Counts 10/1,000/10,000/100,000 with one/three workers plus 100,000 with ten: 4,320 invocations across both hosts including warmups. Commands use explicit `--template`, `--exact`, `--sort=dir`, UTC and no config/pager.

The in-memory kernel compiles templates before timing and renders 10/1,000/100,000 paths with short (8-byte) or long (160-byte) directory/filename components. It measures one field, six direct fields, six path qualifiers and an unchanged directory control. Nine repetitions retain seven, in three shuffled sessions. Path construction and template compilation are excluded from this steady-state kernel, so no preparation/startup improvement is claimed. Native commands include those real costs.

Tables show median session times and median within-session ratios. Negative means faster. Absolute times across hosts are not interchangeable.

## Native 100,000-file output

| Host  | Template    | Workers | Baseline ms | Candidate ms |  Change |
| ----- | ----------- | ------: | ----------: | -----------: | ------: |
| macos | name        |       1 |     228.628 |      225.766 |  -1.04% |
| macos | components  |       1 |     264.324 |      242.042 |  -8.54% |
| macos | qualified   |       1 |     263.991 |      241.427 |  -8.54% |
| macos | dir-control |       1 |     241.864 |      242.050 |  +0.32% |
| macos | name        |       3 |     104.010 |      104.073 |  -0.41% |
| macos | components  |       3 |     105.073 |      106.703 |  +0.95% |
| macos | qualified   |       3 |     104.237 |      105.056 |  +0.79% |
| macos | dir-control |       3 |     104.911 |      104.453 |  -0.90% |
| macos | name        |      10 |      74.524 |       74.184 |  -0.11% |
| macos | components  |      10 |      82.081 |       76.808 |  -6.42% |
| macos | qualified   |      10 |      82.977 |       76.849 |  -4.75% |
| macos | dir-control |      10 |      74.542 |       72.319 |  -1.33% |
| linux | name        |       1 |     102.180 |      100.281 |  -1.75% |
| linux | components  |       1 |     121.704 |      108.422 | -10.91% |
| linux | qualified   |       1 |     119.912 |      108.021 |  -9.59% |
| linux | dir-control |       1 |     103.248 |      104.082 |  +0.02% |
| linux | name        |       3 |      36.557 |       35.777 |  -2.13% |
| linux | components  |       3 |      49.582 |       40.223 | -18.88% |
| linux | qualified   |       3 |      48.022 |       40.219 | -16.20% |
| linux | dir-control |       3 |      37.430 |       37.763 |  +0.27% |
| linux | name        |      10 |      21.361 |       20.333 |  -5.71% |
| linux | components  |      10 |      40.752 |       27.924 | -31.24% |
| linux | qualified   |      10 |      39.747 |       28.795 | -26.01% |
| linux | dir-control |      10 |      23.296 |       23.781 |  +2.08% |

Linux multi-field output improves substantially. On macOS the one/ten-worker multi-field cases improve, while three-worker native results remain near baseline or slightly slower despite clear kernel gains. Traversal, coordination and output remain significant; do not turn a rendering-kernel percentage into a filesystem throughput claim.

## In-memory rendering of 1,000 paths

| Host  | Template    | Component bytes | Baseline us | Candidate us |  Change |
| ----- | ----------- | --------------: | ----------: | -----------: | ------: |
| macos | name        |               8 |      274.73 |       238.43 | -14.69% |
| macos | name        |             160 |      432.85 |       280.87 | -35.66% |
| macos | components  |               8 |      595.93 |       375.69 | -36.67% |
| macos | components  |             160 |     1438.05 |       582.34 | -59.97% |
| macos | qualified   |               8 |      620.72 |       400.64 | -34.88% |
| macos | qualified   |             160 |     1440.45 |       584.73 | -59.59% |
| macos | dir-control |               8 |      283.60 |       281.77 |  -0.64% |
| macos | dir-control |             160 |      507.00 |       510.62 |  -0.15% |
| linux | name        |               8 |       60.00 |        42.95 | -28.34% |
| linux | name        |             160 |      117.48 |        93.27 | -20.05% |
| linux | components  |               8 |      244.42 |       111.72 | -54.23% |
| linux | components  |             160 |      515.83 |       381.12 | -26.11% |
| linux | qualified   |               8 |      227.12 |       119.02 | -47.55% |
| linux | qualified   |             160 |      544.03 |       378.62 | -30.40% |
| linux | dir-control |               8 |       62.68 |        64.07 |  +2.43% |
| linux | dir-control |             160 |      149.16 |       150.91 |  +1.12% |

## Costs and counterexamples

- macos: slowest paired native median +1.74% at (10000, 3, 'name'); all small and directory-control samples remain in the evidence.
- macos: build-output executable 4,069,152 -> 4,068,752 bytes (-400), before release stripping/compression.
- linux: slowest paired native median +2.08% at (100000, 10, 'dir-control'); all small and directory-control samples remain in the evidence.
- linux: build-output executable 12,306,928 -> 12,287,256 bytes (-19,672), before release stripping/compression.

This removes temporary path/component construction, not final output storage. Long outputs still allocate the returned result string; directory fields retain their previous native path work. No claim of zero allocations for whole template rendering is made. There is no additional retained per-entry state.

## Correctness and verification

Differential tests compare every direct field and its path qualifier against native `std::filesystem::path` lexical results on both hosts: empty/root paths, repeated/trailing separators, dot/dot-dot, hidden names, trailing dots, compound suffixes, backslashes, virtual member names, embedded NULs, nonterminated slices and long components. Combined templates, owned definition inputs and chained transforms retain their lifetimes. Each C++ test source has its own test rule. The existing field and fuzz suites also pass. Complete release/extension suites pass 305/305 on macOS and Linux (with required working FUSE).
The added long-owned-parent lifetime case passes separately on both hosts. Scoped clang-tidy passes
the runtime, differential test and benchmark. Its benchmark-only unused loop token required a
`[[maybe_unused]]` annotation; the final executable code section is byte-identical to the measured
kernel on both hosts. Final normal hooks and published-head CI remain required.

## Lossless raw evidence

- [lexical-field-views-20261102-macos.json.gz](lexical-field-views-20261102-macos.json.gz): 115,070 bytes; SHA-256 `f3040e3bff4fb49a6c9d1b6716b9818e98f48c1742976fe5db56ec99be8b2bc7`.
- [lexical-field-views-20261102-linux.json.gz](lexical-field-views-20261102-linux.json.gz): 114,380 bytes; SHA-256 `6b2de8b3e8bd56c90770384a22aa8ad994afca87a7de6a300df852ae8daaf984`.
- [lexical-field-views-20261102-provenance.json.gz](lexical-field-views-20261102-provenance.json.gz): 47,600 bytes; SHA-256 `99581677c57cec0949ed9ad4140ed49edacca6dc28faed7e2d6090cb70326fc1`.
