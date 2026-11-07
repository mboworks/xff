# Ordered output credit: retain the experiment, keep production unchanged

No runtime variant is selected. Ordered coordinator publication and pre-allocation
heap credit substantially reduce retained large output, but every complete variant
still slows ordinary parallel searches on Linux. The large-output gains do not
override those counterexamples. O18's complete byte-bounded pipeline remains open.

The [evidence capsule](ordered-output-credit-20261107.json.gz) contains the exact
private implementations, drivers, source/build/image identities, test XML/logs,
raw observations, independently recomputed summaries, controls, failed intermediate
attempts and explicit decisions. Executable images and redundant transport archives
are omitted; their hashes and the corresponding source records remain. The baseline
is public commit `ae8c5af3eb11dc8571f11db9b307b0db948f78c6`.

## Mechanism and ownership

Existing `ParallelMatch::Match` completes a whole batch before the coordinator can
publish any result. If a producer must wait for output credit, a coordinator waiting
for the complete batch can deadlock. The prototype instead publishes each stable
result slot with release/acquire readiness; the coordinator consumes slots in input
order while other persistent workers continue. No per-batch threads, timer-based
admission or changed worker/chunk thresholds are introduced.

A move-only lease reserves a transaction's new heap capacity before allocation.
Growth keeps both old and new allocations charged until the old allocation is
freed. Only the current ordered head may exceed the normal 1 MiB heap-output
allowance: a giant, late-validating head must be able to finish despite full later
records. Its live old-plus-new growth capacity is intentionally an oversize
exception, not a hard whole-process bound. Fixed inline bytes and slot storage are
separate accounting categories.

One worker owns each transaction until release-publishing completion; only the
coordinator then publishes or clears it. The renderer validates the complete file
before lending one contiguous whole-file view. It preserves late read-error and
binary suppression, headings, contexts and record boundaries without a flattening
copy. Cancellation wakes blocked reservations, suppresses later publication and
drains all producers before reuse. Integration stays limited to printing rg output;
native output and nonprinting/reduction/quiet paths keep the existing bulk API.
Traversal, effects, capture, limits and serial archive ownership barriers remain.

## Preserved variants

- A: 4 KiB chunked output storage. Nine primitive cases pass across both platforms
  and all six release/sanitizer combinations. Not selected because chunk callbacks
  would change the existing whole-file publication boundary.
- B/C2: contiguous owned output and then ordered completion without byte admission.
  C2 passes 374 Mac / 375 Linux focused cases per configuration. It establishes the
  completion/ownership prerequisite, not an output bound.
- D2: integrate charged contiguous output into the actual rg renderer. Delayed head,
  oversized output, cancellation, read-error and binary cases pass all six configs.
- E: borrow reusable transaction slots and use 32 inline bytes. All six configs pass
  379 Mac / 380 Linux cases. The initial memory screen retains every gain and cost;
  it does not establish an end-to-end improvement.
- F1: restore compile-time string fast paths and restrict the production queue to
  printing rg. All six configs pass; both complete 124-case native matrices remain
  unselected because of repeated ordinary Linux output regressions.
- F2: test-only delayed-head integration strengthens F1. A real renderer blocks its
  head read while later completed output fills credit and another producer waits;
  releasing the head proves progress, exact publication and zero outstanding credit.
  The 74-case rg suite passes release/ASan/MSan/TSan on Linux. No timed runtime change.
- G2: increase inline capacity from 32 to 128 bytes, adding 96 fixed bytes per output
  slot while removing the heap reservation for ordinary path records. Boundary and
  pool-reuse tests pass; the remaining Linux costs still reject this variant.
- H1: an atomic pending/waiting/ready state requests notifications only for an actual
  waiter. All six configs pass, but complete Linux measurements still regress.
  Mac deliberately yields after 37 of 48 saved complete case rows so the ready PR
  can merge and its child can run normal hooks. Those partial results are retained
  separately and are not a complete Mac qualification.
- H2: restore G2 readiness and render directly into the producer-owned transaction,
  removing intermediate ownership moves and inline copies. Shared helpers operate
  on explicit transaction state; the existing string value-returning interface
  remains. All six configs pass, including reuse after a failed prefix. Both native
  focused matrices complete, but ordinary Linux output costs remain.

Initial private compile failures (nonassignable owned visits, initializer ordering
and an incomplete callback extraction) and G's now-inline fixture expectation are
retained beside their corrected manifests. They were not pushed or reported as
successful source states.

## Measurement protocol

All comparisons use actual `--config=clang_release` images with identical diagnostic
instrumentation on each side. The baseline and repeated-baseline images have the
same SHA-256. Linux pins one or three distinct physical cores; macOS has no affinity
claim. Native fixture I/O uses the same existing tree within each host: APFS on Mac,
RAM-backed `/dev/shm` on Linux. Cross-host absolute times are not compared.

F1 covers 124 cases: flat/many-directory trees of 10, 100, 1,000 and 10,000 files;
large-output populations of 10, 100 and 1,000 files; one/three workers; listing,
name, metadata, native content and rg filename/line tasks. Each complete population
uses three sessions, nine rounds per case/variant, the fastest seven per session,
one discarded warmup per case/variant/session, rotating participant order, reverse
case order in session two and a fixed shuffle in session three. It records 10,044
measured children plus 1,116 warmups per host.

G2/H2 and Linux H1 focus on the 16 cells implicated by F1: flat 1,000/10,000,
many-directory 10,000, and output-heavy 100/1,000, with one/three workers. Each has
1,296 measured children plus 144 warmups per complete host population. These focused
refinements are not substitutes for F1's unchanged-path coverage or a full shipping
qualification. Every invocation independently verifies complete output and exit
status. `native-data-verification.json` recomputes every retained native mean from
27,567 raw measured children, including the explicitly partial H1 Mac population.
H2's driver can acknowledge an optional pause only between completed cases, outside
native timing; neither completed H2 population used a pause.

The E memory screen has 48 cases, three repetitions retaining two and an identical
baseline control. It includes a first-output diagnostic clock equally on both images.
Its large first-output improvements belong to E only; no H2 first-output or startup
claim is inferred. Mac controls in that initial screen drift by up to 51%, and F1
contains individual unchanged-control costs; all raw values remain available.

## Ordinary 10,000-file output, three workers

Values are median session elapsed changes from baseline. Positive is slower.
Copy control is the separately executed identical baseline image. `rg-filter` in the
raw data means rg's filename-output `-l`, not a different search expression.

| Variant | Host  | Filename output | Copy control | Line output | Copy control |
| ------- | ----- | --------------: | -----------: | ----------: | -----------: |
| F1      | macos |          +2.83% |       +2.62% |      -3.26% |       -0.63% |
| F1      | linux |         +20.86% |       +0.46% |     +16.95% |       -0.12% |
| G2      | macos |          -3.92% |       -2.08% |      -3.09% |       +0.01% |
| G2      | linux |         +12.07% |       -0.92% |     +11.98% |       +1.16% |
| H1      | linux |         +14.89% |       -0.23% |     +14.35% |       +0.07% |
| H2      | macos |          -2.92% |       +0.31% |      -3.06% |       +0.28% |
| H2      | linux |         +10.24% |       -0.96% |     +10.04% |       -1.57% |

Every shown Linux ordinary-cell cost has positive cost in all three sessions,
except where the full raw population explicitly shows mixed results outside this
10,000-file flat subset. For H2, flat line costs are +6.09%, +12.95% and +10.04%;
filename costs are +10.24%, +17.26% and +6.77%. The corresponding copied controls
are materially smaller. Do not hide this with all-task medians or infer a cause
solely from elapsed time. Notification removal and ownership-copy removal were
separate experiments; neither qualifies the complete queue design.

## Output-heavy 1,000-file search, three workers

Each source file has 128 KiB and 32 selected lines. RSS is the median across sessions
of the same seven retained children, not the sum of process peaks. Lower RSS here
is a measured result for this workload, not a global memory guarantee.

| Variant | Host  | Elapsed change | Copy control | Baseline RSS MiB | Candidate RSS MiB |
| ------- | ----- | -------------: | -----------: | ---------------: | ----------------: |
| F1      | macos |        -53.66% |       +8.34% |            80.17 |             19.52 |
| F1      | linux |        -15.40% |       -0.80% |            38.71 |              8.32 |
| G2      | macos |        -41.31% |       -0.31% |            74.85 |             17.60 |
| G2      | linux |        -19.53% |       -1.02% |            38.74 |              8.30 |
| H1      | linux |        -10.86% |       +0.04% |            38.86 |              8.61 |
| H2      | macos |        -42.73% |       +0.57% |            72.53 |             17.71 |
| H2      | linux |        -16.70% |       -2.13% |            38.84 |              8.53 |

F1's Mac copied-control change is +8.34%, so its exceptionally large timing gain is
not treated as a universal or uncontaminated speedup. H2 still shows a useful,
repeatable large-output mechanism on both hosts, including Linux's roughly 39 MiB
to 9 MiB RSS reduction. That supports retaining the mechanism for a future design;
it does not justify shipping the ordinary-output regression.

## Correctness and unfinished scope

G2/H1 have 381 Mac / 382 Linux focused cases per configuration. H2 has 382 / 383:
12 primitive storage tests, seven pool tests, 76 rg tests, and 287/288 run tests.
Release/ASan pass on both platforms; MSan/TSan also pass on Linux. These cover
opaque bytes, capacity boundaries, old-plus-new charging, delayed heads with full
future credit, giant records, cancellation/draining/reset, whole-file callbacks,
late binary/errors, option restoration, reusable slots and short/empty batches.
Retrieved Linux test/evidence hashes are independently verified. These are private
prototype results, not actual public-head GCC/coverage/clang-tidy or full-suite CI.
No local clang-tidy was run.

This experiment bounds only saved rg output heap capacity plus its explicit
oversized-head allowance. It does not admit input/path/slot capacity, bound active
source/decoder memory or account for renderer JSON/colour/template temporaries
constructed before the output sink. Native output and actual traversal/matching
stage overlap are not implemented. The complete O18/JC contracts remain open.

Next work needs a concrete way to avoid the ordinary-record synchronization cost,
with independent qualification, and separate admission for those missing storage
classes. Keep per-file validation and stable coordinator order. Do not claim a hard
bound, adopt an arbitrary file-count/timer gate or retry a rejected policy without
a new measured mechanism.
