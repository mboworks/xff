# Prepared immutable predicate guards (2026-11-09)

## Outcome

Prepare immutable predicate guards once with each expression callback. Reached predicates
borrow those flags instead of repeatedly reading descriptor fields and inspecting a grep
template for birth-time demand. Metadata loading, content invalidation, dry-run execution
previews and deferred controls still happen in the original evaluation order.

Large Linux listings execute about 3% fewer userspace instructions. macOS process instruction
counts fall by about 0.7-1.0% for the large diagnostic cases. Elapsed improvements are smaller,
and the measurements do not establish a speedup for every workload. The Linux gap to GNU find
remains about 7-10% for the primary large three-worker cases.

The measured candidate is `374c2588f876d054c82e017b3d3ad0b4b993dc41`, compared with
`ed0d34be2b41dc48ad975403289199f99c41a66d`. That baseline integrates #1151 and #1154 with main
after #1152. These source identities remain fixed even when documentation is committed later.
The [qualification data](prepared-predicate-guards-20261109.json.gz) retain every cell,
identical-baseline control, initial slower result, build identity and archive digest.

## Current production binary qualification

Both hosts built the full CLI with `--config=clang_release --config=xff_full` and the minimal
CLI with `--config=clang_release`. Each build is immediately followed by a configured executable
query and a frozen copy, before another build can change output links. Clean source/tree IDs,
binary SHA-256, configuration hashes and actual ThinLTO link actions are retained. Build servers
were stopped before measurements.

Each CLI's listing screen covers broad/deep trees, 100/10,000/100,000 files and workers 1/3/10:
18 candidate cells plus 18 separate identical-baseline controls. Every cell rotates XFF,
baseline XFF and GNU find through one warmup and 27 measured rounds, retaining the fastest 21
per participant. All 6,048 participant invocations per host passed their independent output
oracle. Raw samples include warmups and rounds excluded by the estimator.

The following table equally weights broad/deep trees at 100,000 files and three workers.
Negative elapsed changes mean less time. Candidate/find above one means XFF takes longer.

| Platform | CLI     | Elapsed change | Identical control change | Candidate/find |
| -------- | ------- | -------------: | -----------------------: | -------------: |
| Linux    | full    |         -0.60% |                   +0.34% |         1.0955 |
| Linux    | minimal |         -0.80% |                   -0.08% |         1.0737 |
| macOS    | full    |         -0.78% |                   -0.06% |         1.0146 |
| macOS    | minimal |         -0.70% |                   -0.16% |         0.9948 |

These point estimates describe these hosts and fixtures. Equal weighting is descriptive;
within-cell paired intervals do not establish confidence across independent machines.

### Independent repeats, including adverse results

Linux repeated all six three-worker cases for both CLIs: 100/10,000/100,000 files and both
shapes, with a fresh 27-round population and identical controls. All 2,016 participant calls
passed. The large full/minimal ratios were 0.99338/0.99213, with controls 1.00803/1.01376.
The full CLI's initially adverse middle-size point estimate, 1.01631, became 1.00159 on repeat;
its repeat control was 1.01010. No Linux candidate interval in either screen or repeat was
wholly slower than parity. This does not prove an absence of regressions elsewhere.

On macOS, the initial full CLI's broad, 100-file, single-worker case was slower: ratio 1.02502,
paired interval [1.01994, 1.30712]. A separate repeat included both shapes and workers 1/3
at that size, for both CLIs, using 81 measured rounds and retaining 63. All 3,936 participant
calls passed. No repeat candidate interval remained wholly slower than parity. Its overall
full/minimal ratios were 0.99669/0.99814 and controls 0.99414/0.99711; those near-parity results
do not establish a small-case speedup. The initial slower cell remains in the data.

## Instruction diagnostics and memory

Linux's separate `perf` diagnostic uses nine repetitions per binary, case and CLI, across four
listing cases. All 144 calls passed their output oracle, with 100% counter scheduling coverage.
Large broad/single-worker listing uses 3.00% fewer userspace instructions with the full CLI
and 3.04% fewer with the minimal CLI. Deep/three-worker reductions are 2.88% and 2.91%.

macOS's separate `/usr/bin/time -l` diagnostic rotates candidate, baseline and identical-baseline
control through one warmup and nine measured rounds for the same four cases and both CLIs.
All 240 calls passed. Paired geometric process-instruction ratios at 100,000 files are:

| CLI     | Shape/workers | Candidate/baseline | 95% paired interval | Identical control |
| ------- | ------------- | -----------------: | ------------------- | ----------------: |
| full    | broad/1       |            0.99307 | [0.99122, 0.99521]  |           1.00145 |
| full    | deep/3        |            0.99176 | [0.98942, 0.99496]  |           0.99892 |
| minimal | broad/1       |            0.99051 | [0.98942, 0.99146]  |           0.99941 |
| minimal | deep/3        |            0.99149 | [0.98954, 0.99318]  |           0.99970 |

The intervals use all nine measured rounds, a paired geometric ratio, 1,000 bootstrap samples
and seed 20261107. They describe within-cell count variation. macOS process resource accounting
is not assumed equivalent to Linux's userspace event; absolute counts are not compared across
OSes. Neither counter population is combined with accepted elapsed timings.

For the measured `-type f -print0` expression, `--explain` reports 328 owned prepared-record bytes
for baseline and candidate, on both hosts and both CLIs. This accounting excludes the source
AST, regex backends, adapters, allocator headers and worker storage. No broader memory-reduction
claim follows from it.

## Earlier broad qualification and rejected variants

Before integration with #1152, the same const-reference policy implementation at
`c0b861add359058f54207c501a1cfcbfac7d5479` was compared with
`90f8c8c0579b1132ea4d0bc714da9f855a77e012`. These are earlier source identities, not aliases
for the current binaries. Full production qualification covers all five extended workloads:
listing, native-case filtering, metadata/date output, content regex and TAR filtering.
The minimal CLI covers the four native workloads because it lacks TAR support.

Each host completed 180 full-CLI cells and 144 minimal-CLI cells, including separate controls,
with nine measured rounds retaining seven. All 9,720 participant calls per host passed.
Linux's descriptive full/minimal ratios were 0.99283/0.98520, with controls 0.99734/0.99522.
Three full-CLI cells initially had wholly slower intervals. A fresh 20-case, 27-round population
for each CLI included every adverse case and the opposite shapes, plus representative small
workloads. All 6,720 calls passed; no repeat interval remained wholly slower than parity.

macOS's earlier full/minimal grid ratios were 0.99847/0.99789, with controls 0.99601/0.99930.
Six full and two minimal cells initially had wholly slower intervals. Repeats included all
eight and their opposite shapes: 24 full cells and eight minimal cells, 2,688 calls total.
None of those original slower intervals reproduced. A different minimal broad-content case
was 1.98% slower, while its identical-baseline control was 1.80% slower; the retained evidence
does not isolate that result to the change. No broad macOS elapsed improvement is claimed.

The initial by-value experiment at `a425603e4333d9c4fd90532de2622805a43d60dc` is also retained.
Its listing screens and repeats motivated borrowing the stored flags by const reference.
Its initial adverse macOS middle-size cells and later small-case adverse result remain in the
earlier archives and qualification data. Those populations are not pooled with final results.

## Correctness and storage

The current candidate executed and passed nine existing suites on each host under `clang_opt`,
with cached test results disabled: expression execution, expression program, metadata,
evaluation, run, walk, parallel matching, ripgrep mode and raw record buffering.
The new parameterized birth-time case checks both reached and short-circuited grep templates
across Tree/Production/Bound/Prepared/Eager/Program execution modes. A reached metadata failure
must remain unknown, and a skipped predicate must perform no metadata or content read.
Existing cases cover action invalidation, dry-run security, deferred controls and errors.

Linux fixtures use verified tmpfs, with recorded guest CPU affinity. macOS fixtures use owned
2 GiB case-sensitive HFS+ RAM images without CPU affinity. Both current images were released
after fixture, binary, tool and native-launcher integrity checks. Creation/release receipts
record the live device/image/UUID identity. The measurements describe memory-backed traversal,
not SSD throughput; host context and process-resource observations remain in raw evidence.

Every participant invocation checks an independently generated multiset oracle, duplicate
counts, exit status and diagnostics. Final integrity checks passed for every completed
population. No timing sample is accepted merely because its command succeeded.

## Retained evidence

Durable archives are on `drogon.boerger.online` under `/home/marcus/Documents/`:

- `xff-prepared-guards-linux-qualified-20261109.tar.gz`
- `xff-prepared-guards-macos-qualified-20261109.tar.gz`
- `xff-predicate-policy-linux-evidence-20261109.tar.gz`
- `xff-predicate-policy-macos-evidence-20261109.tar.gz`
- `xff-predicate-macos-native-instructions-20261109.tar.gz`

The qualification JSON records exact archive SHA-256 and sizes. Current archives include both
complete measured source trees, frozen production binaries, actual link actions, configured
queries, correctness logs, contracts, all raw rounds, output proofs, fixture manifests,
storage receipts and collection/analysis helpers. Their manifests were verified against the
archive contents before retention. The macOS copy was additionally checked on the destination.

This change does not alter the benchmark measurement contract or add these local experiments
to published viewer history. Continuous extended-production-v1 history remains separate work.
