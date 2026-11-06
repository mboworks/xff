# Archive directory allocation decisions, 2026-11-05

Reserve a bounded initial result capacity for nonempty archive directory ranges. This reduces
small-directory vector reallocations without retaining another index or reserving space for the
whole subtree. The qualified source change is seven added lines in `archive_fs.cc`; it introduces
no flag, worker, lock, filesystem operation, dependency, or change to listing results.

The two independently measured borrowed-key variants are held. Their lookup microbenchmarks improve,
but a repeated Linux flat-archive traversal regression prevents calling them a general winner. Their
sources, lifetime tests, complete measurements and diagnostic failures remain in the same lossless
archive. Neither borrowed-key implementation is included in this runtime change.

## Scope and method

Private experimental parent is published PR #1105, commit
`5f1def75b9bfe751469b3be8c65783dec120deeb`. This identifies provenance; normal signed publication,
hooks and the successor's own current-head CI are still required for merge readiness.

Measured local hosts: Mac Apple M5 Pro (arm64, 18 logical CPUs) and Linux AMD Ryzen 9 9950X 16-Core Processor (x86_64, 10 logical CPUs). These are local experiments, not CI runner results. Build commands use `--config=clang_release` and `--config=xff_full`; exact configuration, module sources, OS details and CPU affinity are retained with the commands.

The result vector reserves `min(indexed_node_count, 16)` only when the selected map range is
nonempty. Empty ranges keep zero initial allocation. Ordinary growth continues after that capacity.
A one-child result can have up to 15 spare initial slots; no new `Entry` objects are constructed in
those slots. The 16-entry setting is an internal allocation choice, not a file-count policy or an
assumption that every directory has 16 children. Root and chain controls measure its costs.

Forty-four existing in-memory cases cover flat, wide, deep and chain-shaped archives, 10 through
100,000 members (chains through 1,000). `one` and `ten` mean one or up to ten directory queries per
iteration; wide/deep leaf directories each contain ten files. Fixture construction, indexing and
complete independent Entry-field preflights are untimed. Native tests cover 66 complete commands:
ordinary broad/deep trees, TAR/ZIP, 10 through 100,000 members, one/three workers, names and CSV
path/type/size/mode/mtime output. The existing immutable fixtures are reused, and both new frozen
CLIs must match every previously checked complete output, exit code and empty stderr before timing.

Every case uses three balanced sessions and nine repetitions retaining the fastest seven. A
byte-identical copied-parent binary measures drift. Linux pins one/three allowed logical CPUs;
there is no macOS affinity claim. Native timing checks all output bytes and includes startup,
archive indexing, traversal and output. Each host performs 5,940 checked native invocations,
including ordinary warmups; any extra pause/resume warmup is retained separately. No sanitizer
elapsed result is used as performance evidence.

Percentages below are the median of three within-session candidate/parent elapsed changes;
negative is faster, positive slower. Copy percentages are independent unchanged-binary controls,
not subtracted corrections. Absolute columns use session medians, so their ratio need not equal
the median of paired ratios. All sessions and raw samples remain available.

## Decision and limits

This is a small listing-allocation improvement, not a broad CLI speedup. The wide/deep multi-query
microbenchmarks generally improve about 11-16% on Linux and 15-22% on Mac. The 66-case full-command
median is approximately neutral on both hosts. Complete 100,000-member wide/deep TAR names runs
improve about 2.5%/1.7% on Mac; Linux wide improves about 0.9% and deep costs about 0.35%.

Costs remain explicit. Mac wide/root/10, which returns one directory, costs 32.92%, about 26 ns;
deep/root/10 costs 6.78%. Other tiny/root/chain changes and copied-parent drift are in the full
tables. Some large microbenchmarks show several-percent copy drift, so those isolated results do
not establish a uniform large-root gain. Native medians range roughly -2.5% to +1.7% on Mac and
-2.9% to +2.4% on Linux. Small three-worker metadata costs are retained rather than masked by a
cohort average or an invented worker threshold. Peak-RSS samples accompany every native command;
the source bounds initial spare storage rather than promising a measurable whole-process RSS drop.

## Qualification

All 330 release tests pass on both hosts, including every extension and required real Linux FUSE.
Seven focused ASan suites pass on both; seven MSan and TSan suites pass on Linux. Canonical focused
coverage reaches every changed coverable line and branch: 4/4 lines and 2/2 branches. The actual
canonical clang-tidy runner checks the sole changed translation unit on both hosts without new
suppressions. Existing complete-entry, ordering, empty-directory, error and path-normalization
tests cover the behavior; no test mirrors the vector's chosen capacity.

After qualification, complete rebuilt CLI bytes match the frozen measured candidate on both hosts.
Mac remains 8,273,608 bytes; Linux is 18,431,464 bytes, 520 bytes above its parent. The archive
contains exact hashes, commands, source snapshots, complete logs, LCOV and all measurements.

## Borrowed normalized keys: retained, not adopted

Both variants replace a transient private owned key with a caller-borrowed view and explicitly
copy at retained content-source boundaries. Five dedicated tests cover pending sources and open
cursors after the caller path is overwritten and the filesystem destroyed, long relative keys,
cached content, links, normalized/custom/URI/nested/absolute paths and errors. These tests pass
against parent and candidates on both hosts alongside three existing archive suites. All ten
actual affected translation units pass local canonical lint for the first variant; the narrower
variant's only changed runtime TU passes again. No full release/sanitizer/coverage qualification is
claimed for these held implementations.

Each variant has a complete 28-case lookup/index memory matrix and 66-case native matrix per host,
with the same three-session 9/7 protocol and copied-parent controls. The first shares normalization
for owned index construction; the second retains the original IndexKey body. All 24 hot lookup
cases improve, roughly 6-60% on Mac and 7-43% on Linux. Restoring the original owned-index body
brings direct cold-index costs closer to neutral, but does not remove the repeated 5.6-6.2% Linux
flat-TAR-10k native cost. A separate 14-case access-predicate matrix per host for the second variant
usually gives only 0-3% complete-command gains, with ordinary controls retained.

Separate Linux `perf` sampling and 31-repeat hardware counters are diagnostic only. Flat-TAR user
instructions are almost unchanged (107,683,384 parent versus 107,643,382 candidate), while cycles
increase from 27,475,116 to 29,784,998. This confirms a real cost but does not prove an inlining,
allocator, scheduling, cache or layout cause. Do not turn that uncertainty into an unmeasured fix,
pool only favorable rows, or claim the microbenchmark gains as end-to-end wins. O19g remains open.

## Next independent investigation

Archive nodes already hold exactly the metadata returned by Stat. O19j will investigate returning
those values with the existing directory-observation API, avoiding repeated per-entry path parsing
and index lookup when eager metadata is actually needed. The walker currently couples that API to
name-only native listings and excludes mounted owners. A candidate must preserve full virtual
paths, the native archive-all path convention, followed-link fallback, lifetime transfer, lazy
demand and leaf-worker isolation. This is a plan, not an implemented or measured feature.

## All 44 memory cases

| Case             | Mac paired change | Mac copy | Linux paired change | Linux copy |
| :--------------- | ----------------: | -------: | ------------------: | ---------: |
| chain/one/10     |            -6.93% |   +0.32% |             -10.74% |     -6.71% |
| chain/one/100    |            -4.88% |   -2.19% |              +3.58% |     +2.40% |
| chain/one/1000   |            +1.02% |   -0.83% |              -0.60% |     -0.35% |
| chain/root/10    |            +0.86% |   -0.08% |              -2.18% |     -0.01% |
| chain/root/100   |            -0.58% |   -0.07% |              -9.68% |     -2.70% |
| chain/root/1000  |            -0.45% |   +0.68% |              -1.24% |     -0.05% |
| chain/ten/10     |            -5.76% |   -1.64% |              -7.24% |     -2.28% |
| chain/ten/100    |            -4.74% |   -1.34% |              -5.56% |     -0.94% |
| chain/ten/1000   |            -3.06% |   +0.02% |              -0.33% |     -0.17% |
| deep/one/10      |           -17.32% |   +0.41% |             -10.70% |     -0.37% |
| deep/one/100     |           -17.81% |   -1.11% |             -10.03% |     +0.03% |
| deep/one/1000    |           -16.52% |   -0.65% |              -7.76% |     -0.34% |
| deep/one/10000   |           -15.04% |   -2.28% |              -7.99% |     -0.37% |
| deep/one/100000  |           -14.06% |   -1.34% |             -11.09% |     -0.21% |
| deep/root/10     |            +6.78% |   -2.54% |              -1.04% |     -0.67% |
| deep/root/100    |            -1.23% |   +1.21% |              +2.32% |     +1.74% |
| deep/root/1000   |            +3.54% |   +0.66% |              -1.08% |     -1.68% |
| deep/root/10000  |            -3.60% |   -1.89% |              -0.57% |     +0.29% |
| deep/root/100000 |            -6.82% |   -5.31% |              +1.27% |     -6.55% |
| deep/ten/10      |           -19.23% |   -1.23% |             -10.66% |     -0.17% |
| deep/ten/100     |           -20.66% |   -0.52% |             -12.35% |     +0.51% |
| deep/ten/1000    |           -20.83% |   -1.71% |             -11.95% |     -0.21% |
| deep/ten/10000   |           -16.77% |   -0.04% |             -11.14% |     +0.01% |
| deep/ten/100000  |           -14.86% |   +0.85% |             -11.28% |     -0.87% |
| flat/root/10     |           -25.93% |   -0.41% |              -4.80% |     -0.07% |
| flat/root/100    |            -1.96% |   +0.94% |              -0.29% |     -0.89% |
| flat/root/1000   |            +1.79% |   -0.49% |              -0.06% |     -0.54% |
| flat/root/10000  |            -0.75% |   +2.43% |              -0.62% |     -0.76% |
| flat/root/100000 |            -0.42% |   -1.55% |              +0.27% |     -0.33% |
| wide/one/10      |           -17.15% |   -1.15% |             -12.77% |     -1.68% |
| wide/one/100     |           -17.40% |   +0.06% |              -8.73% |     -0.99% |
| wide/one/1000    |           -15.95% |   -2.35% |              -7.45% |     -0.63% |
| wide/one/10000   |           -14.35% |   +0.69% |             -12.71% |     -1.48% |
| wide/one/100000  |           -13.48% |   -0.24% |              -6.43% |     -1.02% |
| wide/root/10     |           +32.92% |   -0.99% |              -1.15% |     -0.02% |
| wide/root/100    |           -18.35% |   +0.46% |              -4.58% |     -0.71% |
| wide/root/1000   |            -2.67% |   +0.33% |              -1.13% |     -1.41% |
| wide/root/10000  |            -0.38% |   -3.45% |              -0.07% |     +0.29% |
| wide/root/100000 |            -1.32% |   -4.06% |              -3.69% |     -4.09% |
| wide/ten/10      |           -17.87% |   -1.01% |             -13.11% |     -1.79% |
| wide/ten/100     |           -21.71% |   -0.19% |             -16.30% |     -0.85% |
| wide/ten/1000    |           -21.23% |   -1.16% |             -15.23% |     -1.45% |
| wide/ten/10000   |           -19.79% |   -0.19% |             -14.59% |     -1.24% |
| wide/ten/100000  |           -16.69% |   -0.10% |             -12.84% |     -1.17% |

## All 66 complete-command cases

| Case                              | Mac paired change | Mac copy | Linux paired change | Linux copy |
| :-------------------------------- | ----------------: | -------: | ------------------: | ---------: |
| archive/tar/deep/10/1/metadata    |            -1.02% |   -0.35% |              +0.83% |     +0.39% |
| archive/tar/deep/10/1/names       |            +0.46% |   +0.92% |              +0.00% |     -0.11% |
| archive/tar/deep/10/3/metadata    |            -1.29% |   -0.65% |              -0.12% |     +0.11% |
| archive/tar/deep/10/3/names       |            -0.30% |   -0.26% |              -0.72% |     -0.60% |
| archive/tar/deep/1000/1/metadata  |            +0.45% |   -0.42% |              -0.52% |     +0.12% |
| archive/tar/deep/1000/1/names     |            +0.58% |   +0.05% |              -0.48% |     +0.13% |
| archive/tar/deep/1000/3/metadata  |            +0.23% |   +0.44% |              +0.51% |     +3.02% |
| archive/tar/deep/1000/3/names     |            -1.57% |   -0.56% |              +1.46% |     +0.57% |
| archive/tar/deep/10000/1/metadata |            -1.08% |   +0.40% |              -0.11% |     +0.27% |
| archive/tar/deep/10000/1/names    |            -0.89% |   -0.07% |              -0.35% |     +0.22% |
| archive/tar/deep/10000/3/metadata |            +0.37% |   -1.06% |              +0.98% |     +1.23% |
| archive/tar/deep/10000/3/names    |            +0.18% |   +0.27% |              -2.63% |     -0.17% |
| archive/tar/deep/100000/1/names   |            -1.66% |   +0.05% |              +0.35% |     +0.54% |
| archive/tar/flat/10/1/names       |            -0.22% |   -0.36% |              -1.20% |     -0.63% |
| archive/tar/flat/10/3/names       |            +0.02% |   -2.36% |              -0.14% |     +0.87% |
| archive/tar/flat/10000/1/names    |            -0.71% |   -0.85% |              -0.15% |     +0.50% |
| archive/tar/flat/10000/3/names    |            -0.20% |   -0.23% |              +0.07% |     -0.56% |
| archive/tar/wide/10/1/metadata    |            +1.18% |   +1.58% |              +0.06% |     -1.31% |
| archive/tar/wide/10/1/names       |            +1.17% |   -0.07% |              -0.60% |     -1.00% |
| archive/tar/wide/10/3/metadata    |            +0.27% |   +0.67% |              -1.18% |     -1.02% |
| archive/tar/wide/10/3/names       |            +1.31% |   +0.29% |              +0.31% |     +1.83% |
| archive/tar/wide/1000/1/metadata  |            -0.98% |   +0.79% |              +0.18% |     +0.12% |
| archive/tar/wide/1000/1/names     |            -0.11% |   +0.55% |              -0.43% |     -0.13% |
| archive/tar/wide/1000/3/metadata  |            -0.03% |   -0.15% |              +2.15% |     -3.91% |
| archive/tar/wide/1000/3/names     |            -0.40% |   +0.40% |              -1.30% |     -0.01% |
| archive/tar/wide/10000/1/metadata |            +1.38% |   -0.23% |              -0.76% |     +0.42% |
| archive/tar/wide/10000/1/names    |            -1.89% |   -0.28% |              -0.64% |     +0.15% |
| archive/tar/wide/10000/3/metadata |            +0.95% |   +1.28% |              +1.23% |     +2.42% |
| archive/tar/wide/10000/3/names    |            -0.65% |   -0.13% |              -1.44% |     -0.01% |
| archive/tar/wide/100000/1/names   |            -2.51% |   -0.13% |              -0.93% |     +0.43% |
| archive/zip/deep/10/1/metadata    |            +1.73% |   +0.68% |              -0.37% |     -0.39% |
| archive/zip/deep/10/1/names       |            +0.15% |   +0.73% |              +0.50% |     +0.37% |
| archive/zip/deep/10/3/metadata    |            -0.20% |   +0.94% |              -0.24% |     +0.24% |
| archive/zip/deep/10/3/names       |            -1.48% |   -1.34% |              +0.79% |     -0.05% |
| archive/zip/deep/1000/1/metadata  |            -0.08% |   -0.25% |              +0.37% |     +0.56% |
| archive/zip/deep/1000/1/names     |            -1.38% |   -1.12% |              +0.49% |     -0.20% |
| archive/zip/deep/1000/3/metadata  |            -0.72% |   -0.27% |              +1.13% |     -0.14% |
| archive/zip/deep/1000/3/names     |            -1.19% |   +0.32% |              -1.06% |     +2.63% |
| archive/zip/deep/10000/1/metadata |            +0.06% |   -0.45% |              +0.07% |     -0.10% |
| archive/zip/deep/10000/1/names    |            -0.49% |   +0.21% |              +0.05% |     +0.09% |
| archive/zip/deep/10000/3/metadata |            -1.28% |   -0.73% |              +2.36% |     +1.55% |
| archive/zip/deep/10000/3/names    |            -1.21% |   +0.33% |              -0.29% |     +0.68% |
| archive/zip/flat/10/1/names       |            -0.24% |   -0.75% |              -0.20% |     -0.61% |
| archive/zip/flat/10/3/names       |            +0.37% |   -0.69% |              -1.16% |     +0.05% |
| archive/zip/flat/10000/1/names    |            +1.04% |   -0.85% |              +0.65% |     -0.35% |
| archive/zip/flat/10000/3/names    |            +0.35% |   -0.09% |              +0.06% |     -0.29% |
| archive/zip/wide/10/1/metadata    |            +0.75% |   +1.31% |              -1.14% |     -0.08% |
| archive/zip/wide/10/1/names       |            +0.75% |   +1.25% |              -0.30% |     -0.10% |
| archive/zip/wide/10/3/metadata    |            -2.19% |   -1.64% |              -0.31% |     +0.49% |
| archive/zip/wide/10/3/names       |            +1.21% |   +0.01% |              +0.36% |     +0.74% |
| archive/zip/wide/1000/1/metadata  |            -0.62% |   -0.32% |              +0.47% |     -0.25% |
| archive/zip/wide/1000/1/names     |            -0.60% |   -0.01% |              -0.24% |     -0.10% |
| archive/zip/wide/1000/3/metadata  |            -0.57% |   +1.35% |              +1.21% |     +0.02% |
| archive/zip/wide/1000/3/names     |            +0.11% |   +0.53% |              -1.90% |     -0.95% |
| archive/zip/wide/10000/1/metadata |            -0.78% |   +0.02% |              +0.14% |     +0.52% |
| archive/zip/wide/10000/1/names    |            -0.52% |   +0.03% |              +0.19% |     +0.22% |
| archive/zip/wide/10000/3/metadata |            +0.56% |   +0.77% |              +0.85% |     +2.32% |
| archive/zip/wide/10000/3/names    |            +0.04% |   +0.17% |              +0.41% |     +0.55% |
| native/broad/10/1/names           |            -2.07% |   -2.17% |              -0.08% |     +0.09% |
| native/broad/10/3/names           |            +0.07% |   -0.34% |              +0.35% |     -0.35% |
| native/broad/10000/1/names        |            +0.57% |   +0.23% |              +0.32% |     -0.36% |
| native/broad/10000/3/names        |            +0.20% |   -0.43% |              +0.51% |     -0.16% |
| native/deep/10/1/names            |            -0.36% |   -0.26% |              +0.84% |     -0.04% |
| native/deep/10/3/names            |            +0.11% |   +0.98% |              -2.90% |     +1.04% |
| native/deep/10000/1/names         |            -0.25% |   -0.16% |              +0.37% |     +0.45% |
| native/deep/10000/3/names         |            +0.57% |   +0.49% |              +0.54% |     -0.70% |

## Lossless evidence

The deterministic [evidence archive](archive-directory-allocation-20261105.json.gz) retains
2,029 original text records (1,015 Mac,
1,014 Linux). It is 4,156,037 compressed bytes and
129,604,105 uncompressed bytes, reconstructing 963,918,505
original record bytes. Identical content is stored once: 383 content blobs.
Every original record's complete text, byte count and SHA-256 survives round-trip reconstruction;
deduplication does not drop repeats, outcomes, failures, paths or per-host provenance.

Format `xff-lossless-text-evidence-v2` stores each host's record index with `path`, `bytes` and
`sha256`; recover the exact record text as `archive["contents"][record["sha256"]]`. Sources,
drivers, original preflights, complete raw matrices, held variants, diagnostics, qualification
logs and artifact identities are retained. Executables, generated fixture trees, raw perf binary
captures and full compile databases are excluded. The exact packaging/reconstruction code is also
retained as a text record.

Archive SHA-256: `3627cc9fea47963a48d0bc4be6fb35d45c30776fea1f4a6a562a5b656d5cec77`.
