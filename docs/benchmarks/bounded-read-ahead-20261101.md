# Bounded read-ahead without a shared executor

Retain a bounded sibling-read window on main's existing directory worker pool. This gives a
useful Linux throughput improvement and a substantial peak-memory reduction on both Linux
and macOS, without the earlier shared-executor integration's large macOS regressions.
The selected window permits four pending sibling listings per requested worker at each active
recursion level. It preserves the existing two-directory admission rule, worker allowance,
metadata policy, coordinator actions and traversal order. It adds no clock-based admission gate.

## Measured result

The reference is merged main `473f73452a6b47d7a3b522f2cc6128c4fafa86d8`, including the
clock correction, isolated executor diagnostics and temporary-key fixes. The new experiment
uses fresh binaries for this base; it does not reuse the earlier audit's baseline timings.

Native elapsed changes below are median paired-session percentages; negative is faster.
The broad fixture has 128 sibling directories with 512 files each (65,536 files). The nested
fixture has eight directories, each with eight subdirectories containing 64 files (4,096 files).

| Fixture / task              | Requested workers | Linux elapsed change | macOS elapsed change |
| :-------------------------- | ----------------: | -------------------: | -------------------: |
| Broad, list files           |                 3 |                -5.3% |                -0.1% |
| Broad, eager metadata       |                 3 |               -13.1% |                -1.1% |
| Broad, literal content      |                 3 |                -9.9% |                -0.3% |
| Broad, long-name collection |                 3 |                -1.3% |                -0.2% |
| Broad, list files           |                10 |               -10.4% |                +0.1% |
| Broad, eager metadata       |                10 |                -5.6% |                -0.6% |
| Nested, list files          |                 3 |                -0.2% |                -0.3% |
| Nested, eager metadata      |                 3 |                 0.0% |                +0.8% |

Peak RSS is the median of all 27 native samples per variant/case, including samples not
retained for the elapsed-time mean:

| Broad metadata    |  Main RSS | Selected RSS | Reduction |
| :---------------- | --------: | -----------: | --------: |
| Linux, 3 workers  | 20.34 MiB |     7.43 MiB |     63.5% |
| macOS, 3 workers  | 19.80 MiB |     7.69 MiB |     61.2% |
| Linux, 10 workers | 22.66 MiB |    12.20 MiB |     46.2% |
| macOS, 10 workers | 23.19 MiB |    16.19 MiB |     30.2% |

These are useful gains, not a universal speedup. Across the 53 final native cases, the
largest median paired elapsed increase was 4.8% on Linux's ten-file metadata scan with ten
requested workers and 1.8% on macOS's eight-directory/eight-file listing. The final in-memory
matrix had one inconsistent macOS ten-file rg-selection result: +20.4% and +0.3% across two
sessions. Its original samples are retained. A separate three-session, paired-per-case follow-up
measured +1.7% (sessions +1.7%, +1.6%, +2.6%) for that same case. Linux's corresponding median
was +0.7%; native-lines and small-filter follow-up controls stayed near baseline. This establishes
a small remaining cost rather than a zero-overhead claim, and does not erase the original outlier.
Small timing differences and unaffected comparison controls must not be assigned to a particular
allocation without stronger causal evidence.

The memory reduction primarily concerns eager metadata read-ahead. File-only lazy scans
and collections that intentionally retain all matched records have different memory floors.
For example, macOS's broad three-worker listing was 5.89 versus 6.03 MiB, and its collection
was 27.08 versus 27.25 MiB. Do not advertise a 61-64% reduction for every command or tree.

## Controlled alternatives

- Moving the existing aligned future-array allocation after worker/directory eligibility
  removed unnecessary allocations, but did not establish a convincing standalone native win.
- A two-listings-per-worker deque reduced memory further, but repeatedly slowed Linux's nested
  listing by roughly 10%. It is rejected despite its stronger large-metadata result.
- Four listings per worker removed that nested-tree penalty while retaining useful broad-tree
  throughput and memory gains. This is the selected implementation.
- A compact ring with the same four-listing window had mixed results: some broad listings improved,
  but other cases regressed and it added queue bookkeeping. It is not retained.

The wider deque and compact ring were separate controls. The evidence therefore distinguishes
window width from container implementation instead of attributing their combined effect to one.
The selected mechanism does not promote #992/#996/#998's shared command ownership or #1001's
cost model. Their previous regressions remain relevant; this smaller change is independently tested.

## Limits and correctness

The bound is per active recursion level and counts listing requests. It is not a global byte
budget: one listing can be arbitrarily large, nested frontiers can coexist, and caller-retained
results remain owned. Pending leaf jobs are joined at walker destruction. Cancellation of already
queued jobs, a global retained-byte budget and physical/network slow-storage qualification remain
separate follow-ups. No new allowance, public flag or unstable spelling is introduced.

The original pool still starts only with at least two eligible directories. Worker jobs perform
leaf reads, never nested waits. The coordinator retains visitation, actions, pruning, stop decisions
and error ordering. Single-worker and file-only directories do not allocate the request deque.

Regression tests cover all five sort orders, requested worker counts 1/2/3,
multiple queue windows, serial/parallel visit-order equality and early-stop read bounds. The early-stop
test fails on unchanged main: it reads all 64 siblings despite limits of eight/twelve for two/three
workers. It passes with the selected implementation. Existing traversal tests pass on both OSes,
and both traversal targets pass Linux ThreadSanitizer. Final-source `run_test` and `rg_test` also
pass on both platforms. A complete confirmation matrix remeasures the exact retained source after
lint fixes; its patch and binary hashes are stored separately from the earlier controls.

## Reproduction and evidence

Linux is the authorized drogon KVM guest reporting a Ryzen 9950X CPU and ten available vCPUs,
with tmpfs fixtures and affinity restricted to the requested 1/3/10 CPUs. macOS is the M5 Pro
with eighteen logical CPUs and warm APFS fixtures; worker allowances are not CPU pinning.
Compare paired results within each host, not absolute times between these different contracts.
Builds use `--config=clang_release`, LLVM 23.1.4 and ThinLTO. Native content uses `-content x` over two-byte files;
regex-backend comparisons remain separate. No build, lint or other benchmark
ran during timing on its measured host.

Each native matrix has 53 cases and three sessions: forward, reverse and seeded case order.
Every case/variant has a discarded warmup and nine interleaved runs, retaining the fastest seven.
The first matrix compares main, allocation-only and two-listing deque; the refinement compares
fresh main, two-listing deque, four-listing deque and four-listing compact ring. A final confirmation
compares fresh main with the exact retained source after lint fixes. Together these contain
28,620 successful native invocations across both OSes, each checked against an independent
path/metadata digest or complete collection/comparison output oracle.

Separate in-memory RE2 engine controls use 36 workloads, requested 1/4 engine workers, nine
repetitions retaining seven, and two variant-order sessions without CPU affinity. They do not include
native process startup or provide the same complete per-iteration output oracle as the native driver.
An initial Linux in-memory population, both refinement and final-confirmation populations, and
the supplementary six-case/three-session paired-per-case populations are retained separately.

[Raw evidence](bounded-read-ahead-20261101.json.gz) includes all thirteen report populations,
full source patches, fixture definitions, drivers and binary hashes. Identical fixture dictionaries
are losslessly deduplicated: restore a native report's `fixtures` from the archive's
`fixtures[report.fixtures_ref]`. Native launcher provenance references the already retained
[executor audit evidence](executor-copy-audit-20261101.json.gz) and its checksum.
