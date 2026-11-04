<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Native names-only listing paths

Keep the names-only policy. Broad 100,000-file absolute-root listings improve about
9-14% on both hosts and retain about 9-10 MiB less peak memory. Selective names,
on-demand metadata and structured formats also improve. Short relative roots are
mostly neutral on macOS and cost about 1-2% on Linux at 100,000 files. That cost is
retained rather than normalized away. This is not a universal speedup.

## Ownership and admission

LocalFs keeps ReadDir's complete-path API. A supporting backend can additionally
return owned direct names and unchanged type/source hints without complete paths.
The safety wrapper and rg stdin adapter forward that read capability; stdin remains
a regular virtual file and cannot be listed as a directory.

For known leaf entries under Never/OnDemand metadata demand, the walker reuses one
path buffer per active directory during synchronous callbacks. Each recursion level
owns its buffer. Lazy metadata loading uses the current resolved path and updates
the same stable metadata reference, caching both success and failure. Deferred and
worker consumers retain their existing owned snapshots. Directories, unknown types
and followed symlinks still resolve owned full paths before traversal/read-ahead.

Unsupported backends, eager metadata and recursive archive traversal retain complete
listings. Archives named as root operands keep their original handling; listing a
native directory under root-only archive policy permits the optimization. Native
name ordering is complete-path ordering because every child shares the exact parent
prefix. Other backends keep their complete-path ordering and separators.

There are no new public flags, locks, dependencies or host I/O routes. This does not
shrink Stated's per-entry Metadata storage or implement bounded comparison streaming.
The benchmark uses `--exact`; filesystem-native case matching still forces eager
metadata and is a separate optimization opportunity.

## Native protocol and results

The baseline is commit `0dc8bef65f5f7ce4878b349f69019c7c15e16fd2`. Each host has
76 cases, three sessions, nine measured rounds per participant, and one discarded
warmup per case/session. Keep the mean fastest seven rounds, then the median of
session candidate/baseline ratios. Rotate baseline, candidate and identical baseline
copy every round; randomize case order per session. Every exit and complete stdout
is checked. This is 6,840 native invocations per host including warmups.

Five sizes cover 10, 100, 1,000, 10,000 and 100,000 zero-byte files, with 1/3 workers.
Broad means one directory. Keys named deep use two nested levels, `i%8/i%31`; these
are diagnostic fixtures, not a claim to reproduce the published benchmark topology.
Every tenth file has a .txt suffix; selective cases match those names. Relative-root
controls run from the fixture and pass `.`. Extra cases cover CSV/JSONL/NUL/Markdown,
eager extension summaries and short-circuited on-demand size checks. All 222,220
fixture paths and zero sizes were verified after measurement on each host.

Linux uses one/three allowed logical CPUs. macOS pinning is not claimed. These are
warm native filesystem runs; startup, parse, preparation, traversal, output and
teardown are included. Fixture construction is excluded. Ratios below one improve;
copy ratios expose drift and are not used to adjust results.

| Native case                         | Mac ratio | Mac copy | Linux ratio | Linux copy |
| :---------------------------------- | --------: | -------: | ----------: | ---------: |
| `broad/10000/1/files`               |    0.9514 |   1.0016 |      0.9280 |     0.9919 |
| `broad/10000/3/name-txt`            |    0.9473 |   0.9942 |      0.9358 |     0.9933 |
| `broad/100000/1/files`              |    0.8571 |   1.0060 |      0.9013 |     0.9999 |
| `broad/100000/3/files`              |    0.8561 |   0.9967 |      0.8990 |     0.9960 |
| `broad/100000/1/name-txt`           |    0.8804 |   0.9985 |      0.9052 |     1.0030 |
| `broad/100000/3/name-txt`           |    0.8830 |   1.0027 |      0.9045 |     0.9943 |
| `deep/100000/1/files`               |    0.9392 |   1.0047 |      0.9744 |     0.9978 |
| `deep/100000/3/files`               |    0.9880 |   1.0006 |      0.9295 |     1.0085 |
| `format/broad/10000/3/jsonl`        |    0.9595 |   0.9958 |      0.9351 |     0.9973 |
| `on-demand/broad/10000/3/name-size` |    0.9503 |   0.9981 |      0.9450 |     0.9923 |
| `eager/broad/10000/3/csv`           |    1.0009 |   0.9993 |      1.0134 |     1.0049 |
| `relative/broad/100000/1/files`     |    0.9880 |   0.9936 |      1.0190 |     0.9993 |
| `relative/broad/100000/1/name-txt`  |    0.9939 |   0.9989 |      1.0180 |     1.0006 |
| `relative/broad/100000/3/files`     |    0.9938 |   0.9956 |      1.0115 |     1.0005 |
| `relative/broad/100000/3/name-txt`  |    0.9918 |   1.0013 |      1.0155 |     1.0038 |

All 76 cells, including small slower controls, are retained in the evidence. The
largest Mac native cost is about 0.6%; Linux's relative 100k-file controls repeat at
about 1-2%. A four-participant, twelve-case pilot separately tests choosing the
comparator once per listing. It does not consistently remove those costs and worsens
some cells. That source and every sample remain retained; the original comparator
policy is selected. No causal claim about the comparison branch is made.

## In-memory control

`//xff/engine:name_listing_benchmark` compares both listing policies within the same
candidate runtime, without host filesystem access. Fixtures and parsing are outside
timing; each iteration includes engine preparation, traversal, output accounting and
worker lifecycle. Full output parity is checked before measurement. Names-only and
complete-path modes construct the same children from owned fixture names. A long
root prefix makes the full-path allocation being tested explicit.

There are three sessions, nine randomized repetitions and fastest-seven retention
per CPU request. Linux affinity matches the native protocol. This isolates policy
cost; it is distinct from the native old-binary/new-binary comparison.

| 100k-file memory case     | Mac ratio | Linux ratio |
| :------------------------ | --------: | ----------: |
| `broad/files/100000/1`    |    0.8158 |      0.8076 |
| `broad/files/100000/3`    |    0.8185 |      0.7579 |
| `broad/name-txt/100000/1` |    0.8207 |      0.8057 |
| `broad/name-txt/100000/3` |    0.8209 |      0.7819 |
| `deep/files/100000/1`     |    0.8585 |      0.8445 |
| `deep/files/100000/3`     |    0.9441 |      0.8940 |
| `deep/name-txt/100000/1`  |    0.8802 |      0.8642 |
| `deep/name-txt/100000/3`  |    0.9289 |      0.9150 |

Reproduce the manual diagnostic with:

```sh
bazel run //xff/engine:name_listing_benchmark --config=clang_release --config=xff_full -- \
  --benchmark_repetitions=9 --benchmark_min_time=0.01s \
  --benchmark_enable_random_interleaving=true --benchmark_out=names.json \
  --benchmark_out_format=json
```

## Memory, footprint and qualification

RSS is the median of native child peak RSS across all measured rounds, independent
of fastest-seven time selection. In broad 100k-file listing, the Mac drops from
about 46.4 MiB to 37.1 MiB and Linux from about 41.7 MiB to 31.3 MiB. It remains
linear directory-listing storage, with full per-entry Metadata still present.

The measured CLI is 8,246,712 bytes on macOS (+18,912) and 18,357,984 on Linux
(+39,816). Rebuilding the unchanged Mac runtime produces 8,246,728 bytes: exact
runtime code/data and resolved symbols match; only sandbox IDs in debug/object
symbol paths, alignment and dependent UUID/signature/linkedit metadata differ.
Both original signatures verify. No binary was modified. Linux's rebuilt CLI is
byte-identical to its timed binary. Sources and binary hashes are retained.

Both complete 315-target release suites pass. Ten applicable sanitizer targets pass
under ASan on both hosts and MSan/TSan on Linux. The additional missing-entry
regression passes under every applicable sanitizer. Ten changed translation units
and their headers pass scoped clang-tidy; the benchmark is included in the canonical
compile database. Normal publication hooks and the exact PR head's CI determine
merge readiness; these are local development results.

Committed cases cover names/types/errors from LocalFs; capability fallback; all sort
orders, symlink modes, eager/lazy metadata, 1/3 workers, depth/prune behavior, root-only
archive callbacks, the controlled VFS, cached lazy failures and resolved error paths,
and rg stdin/capability delegation. Existing complete suites exercise queued/deferred
ownership, archives, output formats and mutation controls.

Both working trees were synchronized to main after PR #1089 merged. Its merge tree
is identical to the public baseline tree; all qualified source bytes remain unchanged.
Ancestry receipts are included in the evidence.

The initial disabled-admission screens, synthetic symlink fixture correction,
benchmark setup compile/lint findings and the missing manual compile-database entry
are retained. Those failures were fixed rather than excluded from tooling. Compact
metadata, bulk stat, native-case admission and streaming/pipeline work remain open.

The [lossless evidence archive](native-name-paths-20261104.json.gz) retains source
variants, drivers, raw native/in-memory observations, full output hashes, fixture
integrity, rejected sorting samples and qualification records. Timed binaries remain
frozen in the local evidence directories and are identified by SHA-256.
