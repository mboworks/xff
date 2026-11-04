<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Construct only the selected comparison-summary output

Comparison summaries previously built four representations of each row: ordinary table cells,
compact console metrics, JSON text and delimited export metrics. Only one representation was consumed.
Resolve the output kind once and construct its required data, moving numeric cell strings into
their final row instead of copying them into representations that are discarded.

## Native results

Ratios are candidate elapsed time divided by its paired baseline; below 1 improves. Each case uses
the median of three session ratios. Each session retains the mean fastest seven of nine measured
invocations, after one discarded warmup per participant. Cohorts take medians across cases rather
than pooling hosts or treating different file counts as interchangeable measurements.

The primary cases use 1,000/10,000 relative paths, one/three worker requests, two/six scope columns,
and per-name grouping. Primary console cases use width 1,000 to isolate a wide table; the separate
110-column supplement covers the usual narrow presentation. All counts and sizes remain exact.

| Host  | Format   | Larger per-name cases | Ten-path cases | Extension-group control | Empty roots | Identical-binary control, larger cases |
| :---- | :------- | --------------------: | -------------: | ----------------------: | ----------: | -------------------------------------: |
| macos | plain    |                0.8570 |         0.9867 |                  1.0008 |      0.9964 |                                 0.9983 |
| macos | aligned  |                0.8598 |         0.9938 |                  0.9964 |      1.0001 |                                 1.0008 |
| macos | markdown |                0.8619 |         0.9996 |                  0.9957 |      1.0026 |                                 0.9991 |
| macos | csv      |                0.8449 |         0.9857 |                  0.9917 |      1.0078 |                                 1.0018 |
| macos | tsv      |                0.8458 |         0.9938 |                  0.9979 |      1.0046 |                                 0.9917 |
| macos | jsonl    |                0.9524 |         0.9986 |                  0.9957 |      1.0014 |                                 1.0000 |
| linux | plain    |                0.8002 |         0.9908 |                  0.9999 |      0.9965 |                                 1.0040 |
| linux | aligned  |                0.8089 |         0.9838 |                  1.0076 |      0.9954 |                                 0.9981 |
| linux | markdown |                0.8199 |         0.9929 |                  0.9893 |      1.0057 |                                 1.0015 |
| linux | csv      |                0.7472 |         0.9825 |                  1.0139 |      0.9982 |                                 1.0012 |
| linux | tsv      |                0.7546 |         0.9865 |                  1.0001 |      0.9961 |                                 1.0027 |
| linux | jsonl    |                0.9291 |         0.9975 |                  1.0033 |      1.0034 |                                 1.0026 |

Larger native cohorts improve about 5-16% on macOS and 7-25% on Linux. Small and fixed-group
controls are much closer to startup costs; retain every slower cell and the identical-binary
participant rather than claiming every command improves. No result is normalized to erase a cost.

| Host  | 110-column format | Selected ratio | Identical-binary control |
| :---- | :---------------- | -------------: | -----------------------: |
| macos | plain             |         0.8473 |                   0.9966 |
| macos | aligned           |         0.8512 |                   1.0009 |
| linux | plain             |         0.7993 |                   1.0017 |
| linux | aligned           |         0.7919 |                   1.0004 |

The ordinary-width console supplement improves about 15% on macOS and 20% on Linux. It uses the
same 1,000/10,000-path fixtures, one/three worker requests and six columns, with eight cases per host.

## Implementation and ownership

- Resolve `SummaryRowOutput` from the existing `render::Format` enum once per summary. There is no
  new flag, mode, lookup table or scan of flag spellings.
- Plain/aligned output constructs only the owning `ScopedTableRow` and reserves its metric slots.
- Markdown/other ordinary table output constructs only owning cells and reserves their known arity.
- JSON constructs only JSON text; CSV/TSV construct only the export map.
- Present, absent and zero-byte cells retain their distinct handling. Percentages still use each
  column's full population, and category counts pair paths once while sizes include both sides.
- Move the temporary numeric strings into their selected destination. Labels and deferred table
  contents remain owning; no view outlives its source.
- Reuse invariant population text per sink. Keep JSON-prefix formatting in a small pure helper,
  and retain the existing cognitive-complexity threshold without a diagnostic exemption.

The helper still returns the existing result record, whose unused default containers allocate no
backing storage. Exported metadata retains its existing synchronous source lifetimes. Sorting,
top/truncation, quoting, repeated request identities and all output schemas remain unchanged.
No new worker, lock, filesystem path, ownership wrapper, exception handler or output byte is added.

## In-memory mechanism matrix

There are 144 manual cases: six formats, two/six columns with per-name grouping and a two-column
extension-group control, zero/10/1,000/10,000 entries per side, and one/three workers. Both variants
run the same synthetic two-root VFS. Fixture construction and parsing occur outside measurement;
every measured iteration still includes production preparation, traversal, comparison, summaries
and teardown. The benchmark also checks expression-factory/reference agreement, which is not an
independent format oracle; the separate native models and committed tests provide that validation.

Three participants (baseline, selected and an exact executable copy) run three sessions and nine
rounds: 1,296 case/session/participant records and 11,664 raw rounds per host, 2,592 and 23,328
across both. Cases shuffle, participants rotate between batches, and Linux batches pin the requested
one/three logical CPUs. macOS records worker requests without claiming CPU affinity. A short
six-case screen is retained separately and is not substituted for the complete matrix.

| Host  | Format   | In-memory larger per-name cases |
| :---- | :------- | ------------------------------: |
| macos | plain    |                          0.6660 |
| macos | aligned  |                          0.6623 |
| macos | markdown |                          0.6656 |
| macos | csv      |                          0.6286 |
| macos | tsv      |                          0.6252 |
| macos | jsonl    |                          0.8382 |
| linux | plain    |                          0.7236 |
| linux | aligned  |                          0.7270 |
| linux | markdown |                          0.7344 |
| linux | csv      |                          0.6584 |
| linux | tsv      |                          0.6711 |
| linux | jsonl    |                          0.8460 |

## Native correctness and protocol

Each host has 96 primary cases: ten/1,000/10,000 paths, one/three workers, six formats and two/six
scope columns, plus fixed extension groups and empty roots. Each has three participants, three
sessions, nine measured rounds and a discarded correctness-checked warmup: 8,640 invocations per
host. The narrow supplement adds 720, giving 9,360 per host and 18,720 across both hosts.

Fixtures contain left-only/right-only/identical/different paths, zero-byte files, literal `total`
labels, and ASCII commas, quotes, tabs, newlines and backslashes. Independent Python models construct
complete CSV/TSV bytes and exact JSON objects, including denominator math, scope order, absent cells,
request identity and aggregate markers. Plain/aligned/Markdown retain exact frozen baseline goldens;
their machine-format siblings have independent oracles, and committed display tests cover rendering.
Every timed invocation checks complete stdout and exit status. Generated input trees are excluded
from the archive; their deterministic generator and per-file hashes/sizes remain in its model.

Never overlap a host's timing with builds, tests, lint, fuzz or heavy fixture generation. Global
timing guards defer local synchronization hooks. Linux uses `/dev/shm` native fixtures and recorded
affinity; macOS uses local APFS fixtures without claiming pinning. Instrumented tests establish
correctness, never release-performance numbers. Native wall time comes from the existing launcher,
excluding Python process-launch/oracle work; per-invocation CPU/RSS data and exact command lines remain.

## Qualification and provenance

The complete 314-target release suite passes on each host, including all extras and required working
Linux FUSE coverage. Ten affected targets pass ASan on both hosts and MSan/TSan on Linux. Both
changed translation units pass actual scoped clang-tidy on both hosts, using the canonical compile
database and associated-header scope. The normal full suite also replays the committed fuzz corpora.
No new parser or arbitrary-operation sink is introduced that would need a new fuzz capability.

Compiler versions/hashes, source snapshots, exact commands, result codes, release binary hashes,
link-action output and all original failed attempts are retained. Local pre-publication qualification
is separate from the fresh PR-context CI required for merge readiness.

The frozen build base is `4279697ccb448251677d31f1a658a5f67d573b9b`. The ancestry-only synchronization
to `cc3e48617cdc3f16853dd460099dfe7647abdf3c` includes merged row storage on main
`6e6b25ffc8696d6d28867a1ee6ea2e9beb00a77d` and preserves that complete base tree. Frozen build
identities and later qualification revisions are recorded separately; old samples are never
relabeled as a later build. A rebuilt Mac baseline differed only in an `N_SO` sandbox-directory
debug string and its derived ad-hoc signature; a parser verifies every other byte, including
runtime code/data and the UUID, is identical. Both current baseline-copy controls are byte-identical.

Initial prototype lint found missing arithmetic parentheses and excess nesting; pure format/JSON
helpers fix the changed source without raising thresholds. The exploratory broad header filter
also records style findings in unrelated unmodified headers. Actual enforcing checks cover both
changed translation units and their associated `run.h`; no header is changed by this optimization.
An initial native extension oracle expected an empty group; the documented grouping is `(none)`.
The oracle correction, original failure and reused validated cases are preserved separately.

## Footprint

| Host  | Baseline CLI bytes | Selected CLI bytes | Change |
| :---- | -----------------: | -----------------: | -----: |
| macos |          8,206,936 |          8,206,936 |     +0 |
| linux |         18,272,872 |         18,273,560 |   +688 |

Run the committed manual cases with:

```sh
bazel run //xff/engine:expression_run_benchmark --config=clang_release -- --benchmark_filter=^engine/comparison-summary/
```

Borrowed structured-cell contracts, fixed-arity numeric storage and streaming/backpressure remain
separate candidates. This change removes unused representation construction; it does not claim
to complete those broader designs.

## Evidence

`selected-summary-output-20261104.json.gz`: 9,978,720 bytes, 1,027 labeled files; SHA-256
`9ad6391c93bf11d4a1b4cdc8af98b7b9b5421d8fa87ec686e05893fab0ecfed2`.
