# Bounded encoded path-record reuse

Implicit escaped/colored plain, CSV, TSV and JSON-lines path listings now reuse one
coordinator-owned string through synchronous consumption. The stateless renderer and the
separately qualified raw/NUL buffer retain their existing roles. No output format, worker policy
or filesystem observation changes.

The candidate is accepted for its measured native machine-readable output gains and lower
allocation churn. Tiny direct encodings can regress: allocation removal is not itself a timing
win. Those cases, unrelated controls and every raw round remain in the evidence.

## Native whole-process results

Changes are median paired session ratios minus one; negative means faster. Three sessions each
use nine interleaved rounds, retaining the fastest seven, after one discarded warmup per
participant/case. Case order is shuffled and participant order rotates. An identical baseline-copy
participant records ordinary variation.

| Fixture / files / workers / output    | macOS elapsed change | Linux elapsed change |
| :------------------------------------ | -------------------: | -------------------: |
| ordinary / 10,000 / 1 / jsonl         |               -6.72% |               -6.63% |
| ordinary / 10,000 / 3 / jsonl         |               -6.84% |               -7.82% |
| ordinary / 10,000 / 3 / tsv           |               -6.99% |               -1.00% |
| ordinary / 10,000 / 3 / csv           |               -3.85% |               -8.54% |
| long / 1,000 / 1 / jsonl              |               -4.58% |               -4.23% |
| punctuation / 1,000 / 1 / escaped     |               -1.50% |               +0.90% |
| ordinary / 1,000 / 1 / csv / relative |               +0.83% |               -0.58% |
| ordinary / 10,000 / 3 / raw           |               +0.33% |               +1.14% |
| ordinary / 10,000 / 3 / nul           |               -0.08% |               -0.02% |
| ordinary / 1,000 / 3 / csv-columns    |               -0.49% |               +0.04% |

| Host / selected JSON-lines case  | Baseline median ms | Reuse median ms | Paired change |
| :------------------------------- | -----------------: | --------------: | ------------: |
| macos / 10,000 files / 3 workers |             14.961 |          13.937 |        -6.84% |
| linux / 10,000 files / 3 workers |              5.700 |           5.247 |        -7.82% |

The matrix has 94 cases, three participants and 8,460 checked invocations per host including
warmups: 16,920 across both hosts. Fixtures cover 10/1,000/10,000 ordinary files, long names,
punctuation/control-containing names, absolute/relative roots, requested workers 1/3, raw/NUL,
C-escaped, colored, CSV, TSV and JSON-lines output. Explicit print, empty output, templates and
explicit CSV columns are controls. Every invocation checks its exit and complete stdout against
independently constructed path, terminator, CSV, TSV, JSON and ANSI bytes.

Linux uses shared-memory fixtures and recorded CPU affinity; macOS uses local fixtures without
an affinity claim. `LS_COLORS=*.txt=33` and removal of `LSCOLORS` make the ANSI oracle independent.
Median identical-copy changes range from -1.43% to +0.68% on macOS and -5.39% to +4.23% on Linux.
These are per-case local paired measurements, not cross-machine normalized times.

## In-memory records and whole-engine controls

There are 218 selected cases: 112 encoded-record cases, 40 existing raw-record controls,
56 production-executor output cases and 10 existing name-filter controls. Two participants and
three sessions yield 1,308 case/participant/session records per host, each with nine raw timed
repetitions. Fixtures and independent byte validation run outside the measured loop.

The primitive matrix uses eight path-prefix lengths from zero to 4,096 bytes, seven format/
encoding/color profiles, and reusable versus stateless consumption. Every prefix receives the
same five-byte comma/quote/backslash/newline/tab suffix. The baseline-only adapter calls the
existing owning-string renderer; the candidate uses its new buffer. Stateless consumption remains
an explicit control. Raw/plain and NUL primitive cases do not imply a change to their shipping
fast path. Whole-engine cases use the production executor at 0/10/1,000/10,000 files and 1/3 workers.

| Direct profile / prefix bytes | macOS paired elapsed change | Linux paired elapsed change |
| :---------------------------- | --------------------------: | --------------------------: |
| escaped / 0                   |                      +2.43% |                     +16.81% |
| escaped / 23                  |                     -41.56% |                     -26.00% |
| colored / 0                   |                      +1.21% |                     +15.46% |
| colored / 23                  |                     -39.31% |                     -19.42% |
| csv / 0                       |                      +3.78% |                     +19.20% |
| csv / 23                      |                     -20.63% |                     -23.69% |
| tsv / 0                       |                      +7.09% |                     +21.15% |
| tsv / 23                      |                     -39.37% |                      -4.50% |
| jsonl / 0                     |                     +20.58% |                     +12.44% |
| jsonl / 23                    |                     -14.13% |                     -24.21% |

Direct output timing uses real-time nanoseconds; whole-engine timing uses real-time microseconds.
Reported ratios are medians of session ratios after retaining each participant's fastest seven
repetitions. The archive retains all absolute values, time units and stateless/raw controls.

Short strings that already fit inline can be slower with mutable-buffer dispatch and capacity
checks. Longer records avoid repeated allocations; the native machine-readable workloads show
the practical gain. We retain the simpler common buffer rather than adding an unqualified
per-record tiny-string policy. Existing callers can still choose the stateless renderer.

## Allocation diagnostic and retention

A separate ASan diagnostic counts warmed coordinator-thread allocations for 1,000 records in
each of 56 profile/length combinations, comparing stateless and reusable output. All 56 reused
cases on each host allocate zero bytes and zero objects. Inline stateless cases can also allocate
nothing; longer stateless records allocate repeatedly. No instrumented elapsed times support the
performance decision.

The buffer releases capacity above 1 MiB immediately after synchronous consumption. It is owned
by the coordinator, not shared between workers, and adds no lock or global cache. Queued/ranked
consumers retain their existing owning copies before the borrowed view expires. Consumers must
finish synchronously and must not re-enter the same buffer. The stateless API remains unchanged.

JSON-lines appends directly into the reusable string. Existing escaping helpers remain shared;
valid UTF-8 is preserved and malformed bytes keep the existing lossless base64 object. The rare
base64 fallback still has a temporary allocation. Column/table/summary cell and row reuse and
pipeline backpressure remain separate open work under O22c2/O18.

## Correctness and qualification

Both hosts pass all 314 enabled release targets and seven affected ASan targets. Linux also
passes those seven MSan and TSan targets and requires FUSE in its full release suite. Real canonical
compile-database checks cover six changed translation units and the new buffer header.

Committed behavior tests exercise all eight formats, raw/escaped selectors, colored/ordinary
paths, control/punctuation bytes, short/long paths, literal independent output expectations,
capacity reuse, oversized-tail release, independent owners and queued copies. UTF-8 preservation
and malformed/base64 fallback tests remain in the dedicated Unicode test source. Existing engine
and CLI tests cover actual formatting, explicit actions, ranking and deferred output lifetimes.

Source guards restore only owned files after experiments. Both hosts remain free of concurrent
builds, tests, lint, allocation probes and heavy fixture generation during their release timings.

## Footprint and reproduction

| Host  | Baseline full CLI bytes | Reuse full CLI bytes |  Change |
| :---- | ----------------------: | -------------------: | ------: |
| macos |               8,186,040 |            8,205,544 | +19,504 |
| linux |              18,227,152 |           18,270,232 | +43,080 |

Timings use frozen main `35b810bb806aa169c075c01a449a408cd3758248`, LLVM 23 release/ThinLTO
builds and recorded compiler, source, executable and link-action identities. Publication is based
on #1078, whose changes add ZIP benchmarking/documentation without changing these runtime sources.
The complete release and affected sanitizer checks are repeated with that parent present.

Run the added manual cases with:

```sh
bazel run //xff/presentation/render:output_benchmark --config=clang_release --config=xff_full -- --benchmark_filter='^output/encoded-record/'
bazel run //xff/engine:expression_run_benchmark --config=clang_release --config=xff_full -- --benchmark_filter='^engine/encoded-output/'
```

[The compressed evidence](encoded-record-reuse-20261103.json.gz) retains every measured round,
frozen variants, original/stateless controls, qualification and lint logs, allocation diagnostics,
dry runs and reconstruction scripts. Generated fixtures, binaries/objects and canonical compile-
database payloads are excluded.

Archive: 4,471,777 bytes, 848 labeled files. SHA-256:
`999ccd68c3d8924cfbbd103167c8f799ca5847f6494589ddf967d0c14cabacb4`.
