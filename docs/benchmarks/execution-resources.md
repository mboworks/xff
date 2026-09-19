# Execution resource measurements

These development-build measurements illustrate the resource guide's method. They are
not release-performance promises or cold-cache results. All 36 invocations completed
successfully without stderr output.

## Reproduce the workloads

The [fixture runner](../../tools/benchmark_resources.py) creates matching broad/deep trees,
measures every workload in a fresh child process, writes the full invocation records after
each run, and removes the disposable fixture tree even when a command fails. It stops on the
first failed invocation and preserves that invocation in the report. The output report path
is overwritten; its parent directory must already exist.

```sh
python3 tools/benchmark_resources.py ./bazel-bin/xff/cli/xff \
  --output /tmp/xff-resources.json --build-label='REVISION; fastbuild clang'
```

Defaults reproduce the fixture sizes, workload order, job count, and repetition count below.
Use `--files=100 --depth=10 --repetitions=1` for a smoke run. `--directory=PATH` places the
owned temporary tree under a chosen filesystem, allowing the same workload on a network
mount; it does not alter existing files there. `--jobs=N` permits separate concurrency runs.
Record mount/backend and cache conditions when interpreting those results. These controls
do not turn the measurements into logical VFS bytes or storage-traffic measurements.

The [original raw records](execution-resources.json) include commands, per-run timings,
platform, output byte counts, statuses, and binary identity for the table below. Temporary
fixture paths in those records describe the measured runs; those directories were removed.

## Method

- macOS 26.6, arm64; local temporary filesystem; `--jobs=1`, `--no-config`, `--no-pager`.
- Hermetic Clang, Bazel `fastbuild`; source base `79716e9f9` with the locally verified B14
  hash-summary context fix. This is not an optimized release binary.
- Binary SHA-256: `00d20c98027aea7884d18a93f319b862531283928cc2504582d72d8ef9f3eba2`.
- Each root has 10,000 ordinary files of 1,024 bytes (`abc` plus newline, repeated 256 times).
  Left and right roots contain identical entries and contents.
- The broad shape places all files directly under each root. The deep shape has 100 nested
  directories named `d`, with 100 files at each level. Files have zero-padded numeric `.txt` names.
- Files were just written. No caches were flushed; later invocations reuse operating-system
  state. Workloads ran in the table order, three repetitions per shape, with a fresh process
  for each invocation. This is not randomized experimental ordering.
- `tools/measure_resources.py` drains stdout through a pipe. The table reports medians across
  the three repetitions. Its time to first byte includes output buffering, and its RSS is the
  child high-water measurement rather than a process-tree sum.

The listing uses `ROOT -type f`. Other workloads add `--summary=ext`, `--template={hash}`,
`--template='{hash} {hash}'`, or `--template='{hash} {lines}'` respectively. Comparison uses
`--compare=summary LEFT RIGHT -type f`, so it visits two roots rather than one.

| Shape | Workload   | Elapsed seconds | First stdout seconds | Peak RSS MiB |
| :---- | :--------- | --------------: | -------------------: | -----------: |
| broad | listing    |           0.122 |                0.096 |        18.81 |
| broad | summary    |           0.116 |                0.115 |        19.02 |
| broad | hash       |           0.721 |                0.112 |        19.06 |
| broad | hash_twice |           1.240 |                0.109 |        19.14 |
| broad | hash_lines |           0.956 |                0.117 |        19.09 |
| broad | compare    |           0.554 |                0.553 |        31.00 |
| deep  | listing    |           0.122 |                0.034 |        17.88 |
| deep  | summary    |           0.125 |                0.124 |        18.11 |
| deep  | hash       |           0.896 |                0.052 |        18.45 |
| deep  | hash_twice |           1.567 |                0.050 |        18.47 |
| deep  | hash_lines |           1.203 |                0.055 |        18.34 |
| deep  | compare    |           0.855 |                0.854 |        33.38 |

## What these results establish

The second hash field and the line-count field add measurable work on these fixtures.
Summary output arrives near completion, while an ordinary listing can emit earlier.
These observations do not establish the number of content reads, prove deduplication or
its absence, or predict network-filesystem performance. The comparison rows also process
twice the ordinary listing's input population, so their elapsed times are not per-entry
speed comparisons.

## Logical read accounting

The [isolated read-accounting workloads](../../xff/engine/read_accounting_test.cc) measure
logical reads directly, independently of the elapsed-time experiment above:

```sh
bazel test //xff/engine:read_accounting_test
```

Each root has 256 synthetic regular files, each containing 1,024 `x` bytes. The broad shape
places them directly in the root; the deep shape distributes them over 16 nested directories.
The workload uses one directory worker and safe mode, does not load INI files, and runs only
fixed read-only expressions. All content comes from memory; mutation requests fail the test.

With engine source base `cb146bdac`, both shapes produced these per-invocation measurements:

| Workload                   | Roots | Successful whole-file reads | Logical bytes |
| :------------------------- | ----: | --------------------------: | ------------: |
| Listing                    |     1 |                           0 |             0 |
| Extension summary          |     1 |                           0 |             0 |
| Hash field                 |     1 |                         256 |        262144 |
| Two hash fields            |     1 |                         512 |        524288 |
| Hash and line-count fields |     1 |                         512 |        524288 |
| Identical-root comparison  |     2 |                         512 |        524288 |

This establishes that those repeated consumers currently request file content separately in
these workloads. Comparison reads both files of each pair; its two reads represent two input
files, unlike the repeated-field cases. No read attempt failed in these measurements.

The test XML at `bazel-testlogs/xff/engine/read_accounting_test/test.xml` records fixture
parameters, attempted and successful reads, and logical bytes for each shape. Read counts are
observations, not performance assertions, so reducing duplicate reads will not break a test
that requires the current inefficiency. Traversal errors, missing output, or mutation attempts
do fail the measurement.

Counters observe successful `ReadContent` results. The fixture inherits the whole-file
`ReadContentRange` fallback. These counts do not measure unique file bytes, storage traffic,
or an optimized range backend; they also exclude archive decompression and host filesystem
metadata I/O. General runtime accounting, storage/network measurements, optimized-build
results, concurrency scaling, and an execution-cost explain view remain outstanding S13 work.

## Worker-count observations

The [raw worker-count records](execution-concurrency.json) contain 72 additional invocations
of one development binary, with the same 10,000-file/100-level fixture definitions above.
Each worker count used a fresh disposable fixture. The entire one-worker series ran before
eight workers; caches were not flushed, run order was not randomized, and other machine load
was not controlled. These observations do not isolate a causal worker-count speedup.

Source: `f24a2b0f5` (resource inspection plus static field validation), hermetic Clang fastbuild;
binary SHA-256 `f48715a651b3198ab575c5d6bcb1a21a708012ac39b2406543c3cd302cc347ac`. All invocations exited successfully
without stderr. The platform is recorded per invocation. Each cell summarizes three runs;
elapsed range is included to expose variability. First output and RSS are medians.

| Shape | Workload   | Jobs | Elapsed median (s) | Elapsed range (s) | First stdout (s) | Peak RSS (MiB) |
| :---- | :--------- | ---: | -----------------: | :---------------- | ---------------: | -------------: |
| broad | listing    |    1 |              0.266 | 0.196–1.593       |            0.216 |          19.53 |
| broad | listing    |    8 |              0.156 | 0.143–0.250       |            0.130 |          19.27 |
| broad | summary    |    1 |              0.234 | 0.222–0.276       |            0.234 |          19.45 |
| broad | summary    |    8 |              0.158 | 0.157–0.234       |            0.156 |          19.42 |
| broad | hash       |    1 |              1.529 | 1.330–2.466       |            0.219 |          19.91 |
| broad | hash       |    8 |              1.111 | 1.058–1.824       |            0.162 |          19.69 |
| broad | hash_twice |    1 |              2.735 | 2.166–4.129       |            0.193 |          19.88 |
| broad | hash_twice |    8 |              1.873 | 1.833–2.358       |            0.164 |          20.22 |
| broad | hash_lines |    1 |              1.899 | 1.565–2.900       |            0.206 |          19.80 |
| broad | hash_lines |    8 |              1.366 | 1.362–1.520       |            0.167 |          19.83 |
| broad | compare    |    1 |              1.308 | 1.046–2.700       |            1.305 |          30.48 |
| broad | compare    |    8 |              0.889 | 0.858–0.897       |            0.888 |          30.95 |
| deep  | listing    |    1 |              0.206 | 0.175–0.208       |            0.053 |          19.81 |
| deep  | listing    |    8 |              0.158 | 0.129–0.158       |            0.042 |          19.41 |
| deep  | summary    |    1 |              0.192 | 0.163–0.226       |            0.190 |          19.67 |
| deep  | summary    |    8 |              0.159 | 0.130–0.165       |            0.158 |          19.56 |
| deep  | hash       |    1 |              1.446 | 1.288–1.588       |            0.080 |          20.95 |
| deep  | hash       |    8 |              1.092 | 0.917–1.280       |            0.067 |          20.09 |
| deep  | hash_twice |    1 |              2.555 | 2.554–2.640       |            0.075 |          21.41 |
| deep  | hash_twice |    8 |              1.907 | 1.513–2.087       |            0.069 |          20.42 |
| deep  | hash_lines |    1 |              2.142 | 2.134–2.204       |            0.091 |          21.19 |
| deep  | hash_lines |    8 |              1.476 | 1.261–1.720       |            0.070 |          19.91 |
| deep  | compare    |    1 |              1.721 | 1.567–1.726       |            1.718 |          34.77 |
| deep  | compare    |    8 |              0.953 | 0.918–1.271       |            0.951 |          33.06 |

Eight workers change directory scheduling, not the coordinated expression evaluator.
The broad fixture has only one listing per root, and the deep fixture is a single directory
chain rather than many independent subtrees. Neither is a general parallel-scaling benchmark.
Content consumers still perform their own work, and comparison summaries still appear after
the inventories and comparisons complete. A branching-tree workload and controlled optimized
builds are needed before tuning worker defaults; network/storage measurements remain separate.

## Runtime observations on real fixtures

The read-only engine harness was run over 10,000 regular files of 1,024 bytes each,
with a broad tree and a 100-level deep tree, three repetitions each at one and eight
workers. These 72 instrumented invocations accompanied separate shipping-executable
measurements. Raw reports include both executable fingerprints:
[one worker](runtime-reads-jobs1.json) and [eight workers](runtime-reads-jobs8.json).
The build label is a development clang fastbuild on macOS arm64, not a release-performance claim.

Every repetition and both tree shapes produced the following logical counts. Comparison
uses two roots; the other workloads use one. All observed reads succeeded.

| Workload   | Whole-file calls | Whole-file bytes | Range calls | Range bytes |
| :--------- | ---------------: | ---------------: | ----------: | ----------: |
| listing    |                0 |                0 |           0 |           0 |
| summary    |                0 |                0 |           0 |           0 |
| hash       |            10000 |         10240000 |           0 |           0 |
| hash_twice |            20000 |         20480000 |           0 |           0 |
| hash_lines |            20000 |         20480000 |           0 |           0 |
| compare    |                0 |                0 |       20000 |    20480000 |

Whole-file and range reads are distinct observer entrypoints. In particular, comparison
uses ranges through the real backend; the earlier synthetic fixture's fallback implementation
reported those as whole-file reads. Both count bytes at their documented observation boundary.
The range observer does not count the backend's internal implementation reads a second time.

The files were just written and reused without controlled cache flushing. These observations
measure logical content returned to the engine, excluding metadata and physical/network I/O.
Timing and read-count measurements are separate invocations, labelled accordingly in the JSON;
they must not be treated as one instrumented execution or used to infer instrumentation overhead.
Network-storage measurements and archive/member backend instrumentation remain outstanding.
