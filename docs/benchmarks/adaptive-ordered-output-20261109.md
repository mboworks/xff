# Adaptive ordered output and pipeline overlap

Select the adaptive output mechanism for PR #1152: the 1,000-file heavy-output rg
workload improves 28.88% on Linux and 33.16% on Mac, with roughly half the process
peak RSS. Small/unknown output stays batched. Ordinary results include costs and
unstable Linux populations; retain them explicitly rather than claiming a universal
speedup. Current-head CI remains the final merge gate.

The [lossless evidence capsule](adaptive-ordered-output-20261109.json.gz) contains
private variants, exact source/build/image identities, test records, all raw timing
samples, independent arithmetic verification, fixture content recipes and profiling
observations. SHA-keyed payloads deduplicate identical files without losing bytes.
Release executable images and raw extracted executable sections are omitted; their
hashes and original source records remain.

## Scope and ownership

The matcher retains its existing persistent workers. A completed batch selects
between batch publication and collecting one next batch while matching proceeds:
unknown/small output uses batch publication, while an average of at least 1 KiB
per visited entry enables overlap. Later small output turns it off again. This is
an internal measured policy, not a new command-line option or a claim that output
size predicts every CPU-heavy matcher.

Input admission accounts for actual owned text capacity, including inline storage
and the terminator. It admits at most 1 MiB per collecting batch, with one oversized
record alone and one transit record while checking its actual capacity. Entry-count
limits remain 256 for content output and 1,024 for decision-only work. Cleared
predecessor storage is recycled before workers wake. Output-free filters stay batched.

Output transactions reserve capacity before allocating: 64 inline bytes per slot,
a 4 KiB per-slot heap grant and 8 MiB of shared excess credit. Growth accounts for
both old and replacement allocations until the old allocation is released. Only
the current ordered head may exceed shared credit so arbitrary-size whole-file
binary/read-error validation can finish. Excess storage is returned after each
publication; only allocations within a slot grant are retained for reuse.

The coordinator borrows a stable whole-file view after producer completion and
releases it before advancing the ordered head. Native and rg rendering preserve
whole-file binary/late-error suppression, headings, contexts and callback order.
Archive-member and traversal-error barriers drain prior work. Cancellation wakes
byte-credit waiters and drains producers before filesystem/input ownership changes.

Matcher work and coordinator collection/publication share the requested `N` CPU
units. Workers borrow once per generation, release while parked for output credit,
and reacquire after unlocking the credit mutex. The coordinator releases its unit
before waiting for results. Directory I/O retains its existing independent limit;
this is not one process-wide `N`-operation budget. No per-file CPU lock, clock,
throwaway worker or generic executor is introduced.

The byte allowance covers retained output allocations, not source buffers, decoder
workspace, derived caches, renderer temporaries or process RSS. Broader JC/source/
temporary admission and active-I/O preemption remain open.

## Evidence and protocol

The runtime qualification head is `4245f78c00f483309b22f08d6ac00fdca54214c7`,
based on main `52950f937ad371e1450538faf7983024b324688d`. The baseline build
uses `efffe2529f26c1c7ca61763fa5fbb17efd573f6f`; only six backfill/workflow/docs
files differ between that baseline and the current base, with no runtime or
build-toolchain change. All images use `--config=clang_release`.

Native qualification covers 76 cells: listing, names, metadata, content filtering,
native lines, rg filenames and rg lines; flat trees from 10 through 10,000 files,
a many-directory 10,000-file tree and heavy output at 10/100/1,000 files. A separate
32-cell crossover matrix covers 128/512/1,024/2,048/8,192/32,768 bytes per file and
small-heavy-small/heavy-small-heavy transitions, with native/rg lines and one/three
workers. Each population has three sessions, nine rounds retaining the fastest
seven per case/participant/session, a discarded warmup, rotated participants,
reversed second-session cases and a fixed third-session shuffle. An identical
baseline is executed independently as a drift control.

Each child uses native fork/exec/wait timing and verifies complete output and exit
status. The native driver compares the complete line multiset with independently
constructed path/content/metadata oracles; committed tests separately prove ordering.
Linux pins distinct physical cores and uses `/dev/shm` fixtures; Mac uses APFS without
an affinity claim. Absolute host times are not compared.

The in-memory screen uses 13 cases, four workers and three grouped sessions with
nine repetitions retaining seven. It isolates VFS/engine work but includes no native
process startup. Its Linux diagnostic affinity uses the first four allowed logical
CPUs, unlike the physical-core native protocol. It is supporting evidence, not a
substitute for native results.

First-visible-output diagnostics use a parent monotonic clock from before `Popen`
to the first `read1` bytes. They include parent launch/pipe observation and are not
native throughput measurements. They verify complete ordered output hashes and
retain all samples and copied-baseline controls.

## Rejected and intermediate policies

Fixed overlap and per-entry synchronization repeatedly regressed ordinary Linux
output despite large-output/RSS gains. A 256-entry publication window still cost
9.28% native lines, 8.38% rg filenames and 13.07% rg lines on flat 10,000-file
searches; controls were -0.36%, -1.12% and -0.62%. Keep those counterexamples.

The atomic-wait variant spent 7.56% of sampled CPU in clock reads beneath libc++
polling; blocking conditions avoid that mechanism. A separate syscall diagnostic
increased futex calls from 324 to 7,550 with per-entry completion; traced elapsed
time is not used as performance evidence. Windowed publication drains early on
actual credit backpressure so it cannot deadlock behind a later blocked producer.

Returning live predecessor entries and clearing them after dispatch regressed flat
work. Recycling cleared entries before dispatch avoids that overlap. A 1 MiB shared
output policy regressed 32 KiB output; raising the output crossover alone did not
solve both native and rg costs. Interleaved isolated policy measurements select
8 MiB shared excess instead. Neither value is an eager reservation.

Collection-only CPU reservation missed publication running alongside every matcher;
the final shared allowance covers both phases. Prior results remain distinct from
that implementation. Its explicit lifetime/immobility declaration fix changed whole-file image hashes,
so Linux native measurements were repeated. Subsequent ELF inspection proves that
all allocated code/data sections and their addresses are identical between those
two builds; only the GNU build-ID note differs. The two complete populations are
retained separately, with that equivalence recorded.

The first fixed-policy Mac population's third session overlapped local clang-tidy;
retain it as contaminated, not final qualification. The final Mac preflight records
no compiler or lint process before timing. Earlier failed prototypes/builds and
private source snapshots remain in the evidence capsule.

## Correctness and remaining gates

Both hosts pass all 270 root full-feature release targets and the 73-target
standalone extension/API release invocation. Those are distinct invocations, not
a claim that every underlying source test is unique. The six focused targets are
CPU allowance, output credit, parallel matching, rg engine, run engine and conformance.
They pass ASan/TSan on both hosts and MSan on Linux. Local Linux MSan preserves
LLVM22 instrumentation and its instrumented standard library, using the existing
LLVM23 `ld.lld` only because Ubuntu26 lacks the older linker's `libxml2.so.2`.
CI uses its normal configured toolchain, with no override.

Tests exercise actual blocked CPU/byte-credit waiters, cancellation and restart,
ordered giant heads with full later credit, allocation growth/release, inline and
long-path records, recycled storage, small/heavy transitions, archive/error barriers
and owner teardown. The conformance GID tests query fixture ownership: a temporary
directory need not inherit the process group. The unchanged baseline reproduces
that distinction; production group matching is unchanged.

The production source is qualified by these local checks; current-head CI must
also pass before merging. Full source/decoder/temporary memory admission and
active-I/O preemption remain open under O18/JC. This change does not close those
gates or expose a public scheduling threshold.

## Native results

Values are the median of three session percentage changes; positive means slower.
RSS is the median of each session's mean process peak for the same seven fastest
children. It is not a sum of peaks or a general memory bound.

| Host  | Workload, three workers  | Elapsed change | Repeated baseline | Baseline RSS MiB | Candidate RSS MiB |
| ----- | ------------------------ | -------------: | ----------------: | ---------------: | ----------------: |
| linux | flat10000 / content      |         -1.19% |            -0.56% |             7.08 |              6.94 |
| linux | flat10000 / native-lines |         -0.90% |            -1.82% |             6.70 |              6.69 |
| linux | flat10000 / rg-filter    |         -0.66% |            -1.61% |             6.64 |              6.69 |
| linux | flat10000 / rg-lines     |         +8.70% |            +3.63% |             6.67 |              6.73 |
| linux | many10000 / content      |         +0.16% |            -3.84% |             6.61 |              6.49 |
| linux | many10000 / native-lines |         -3.80% |            -2.17% |             5.97 |              6.08 |
| linux | many10000 / rg-filter    |         +3.78% |            +0.02% |             6.25 |              6.28 |
| linux | many10000 / rg-lines     |         +4.82% |            +0.28% |             6.24 |              6.30 |
| linux | output1000 / rg-lines    |        -28.88% |            +0.08% |            38.80 |             18.52 |
| macos | flat10000 / content      |         +2.15% |            +0.22% |             8.64 |              6.82 |
| macos | flat10000 / native-lines |         +0.80% |            -2.25% |            14.85 |             13.78 |
| macos | flat10000 / rg-filter    |         +0.02% |            +0.22% |            15.49 |             14.56 |
| macos | flat10000 / rg-lines     |         +0.59% |            +0.05% |            15.85 |             14.51 |
| macos | many10000 / content      |         +0.73% |            -0.07% |             8.35 |              6.38 |
| macos | many10000 / native-lines |         +0.43% |            -0.14% |            13.93 |             12.95 |
| macos | many10000 / rg-filter    |         +0.48% |            -0.36% |            14.81 |             13.30 |
| macos | many10000 / rg-lines     |         +0.46% |            +0.55% |            15.42 |             14.38 |
| macos | output1000 / rg-lines    |        -33.16% |            -0.82% |            79.30 |             38.12 |

`rg-filter` is filename output (`-l`). The complete 76-cell matrices include listing,
names, metadata and one-worker controls, not just the rows shown here. The Mac
ordinary three-worker line/filename cells are within about 1%; metadata/content
controls also include small costs. Linux many-directory filename output costs
3.78%, with a repeated-baseline change of +0.02%. Keep this tradeoff visible.

## Linux unchanged-path confirmation

The published-image population includes a 26.07% many-directory name-listing cost
and an 8.70% flat rg-line cost. An interleaved seven-case confirmation with both
preceding implementations still shows +13.85% for the name case, but not the rg-line
cost. These are real observations, not discarded outliers.

A subsequent confirmation uses separate executable copies, including an independently
copied baseline. The current and preceding CPU implementations have identical
allocated code/data and addresses, except for the build-ID note. That cohort changes
the apparent name cost's sign and keeps the large-output gain. It demonstrates that
those large differences cannot be assigned to the declaration edit. It does not
prove that every small ordinary-path cost is zero.

| Three-worker case       | Published population | Repeat control | Independent-image confirmation | Independent baseline copy |
| ----------------------- | -------------------: | -------------: | -----------------------------: | ------------------------: |
| flat10000 / files-exact |               +5.02% |         -1.13% |                         +2.10% |                    -1.65% |
| flat10000 / name-exact  |               +6.08% |         -0.31% |                         +1.76% |                    -1.42% |
| flat10000 / rg-lines    |               +8.70% |         +3.63% |                         +0.29% |                    +0.02% |
| many10000 / files-exact |              +10.19% |         +1.03% |                         +1.43% |                    -0.15% |
| many10000 / name-exact  |              +26.07% |         +3.89% |                         -8.60% |                    +1.67% |
| many10000 / rg-lines    |               +4.82% |         +0.28% |                         -1.36% |                    -0.29% |
| output1000 / rg-lines   |              -28.88% |         +0.08% |                        -29.90% |                    -2.70% |

Separate `perf stat` diagnostics send output to `/dev/null` and are not the native
capture/timing protocol. Median retired user instructions change about +0.02% for
flat names and +0.69% for many-directory names; candidate cycle counts fall in that
diagnostic. This rules out a 26% increase in retired instruction work in those
fixtures, without proving a universal elapsed-time result or a specific host cause.
No live profiler/compiler was found in the subsequent Linux process check; idle
Bazel servers are not compilation activity.

Most earlier baseline/control labels execute the same image independently; they
are temporal controls, not independent file-backed copies. Only the explicit
independent-image confirmation makes the latter claim. Retain this distinction in
future qualification and inspect allocated sections before repeating a complete
population for a build-ID-only change.

## Crossover and changing output

| Host  | Three-worker case                | Elapsed change | Repeated baseline |
| ----- | -------------------------------- | -------------: | ----------------: |
| linux | payload32768 / native-lines      |         -5.67% |            -0.45% |
| linux | payload32768 / rg-lines          |         -0.22% |            -1.64% |
| linux | small-heavy-small / native-lines |         +4.41% |            +1.00% |
| linux | small-heavy-small / rg-lines     |         +3.47% |            +1.44% |
| linux | heavy-small-heavy / native-lines |         +1.05% |            +2.09% |
| linux | heavy-small-heavy / rg-lines     |         +0.52% |            -0.73% |
| macos | payload32768 / native-lines      |         -5.38% |            +0.95% |
| macos | payload32768 / rg-lines          |         -0.20% |            +0.07% |
| macos | small-heavy-small / native-lines |         -0.11% |            +6.99% |
| macos | small-heavy-small / rg-lines     |         +1.23% |            +0.53% |
| macos | heavy-small-heavy / native-lines |         +1.45% |            +3.94% |
| macos | heavy-small-heavy / rg-lines     |         +1.07% |            +2.65% |

All 32 crossover cells, including one worker and intermediate payload sizes, remain
in the capsule. The mixed small-heavy-small Linux cells retain 3-4% costs; the
change is selected for its substantial large-output and retained-storage benefits,
with these transitions and ordinary costs recorded for review and further tuning.

## Supporting memory-backend and first-output diagnostics

The memory-backend large-output case improves 7.75% on Linux and 16.52% on Mac;
repeated-baseline changes are -0.18% and -2.72%. Four-worker 10,000-file filters
improve 7.19%/13.94%. Small cases are mixed: Mac 1,000/10,000-file rg filename
output costs 2.22%/2.65%, with controls +2.72%/+4.01%; Linux 1,000-file rg lines
cost 2.44%, with control -2.27%. Preserve all 13 cells rather than extrapolating
from the strongest result.

| Host  | First-visible-output fixture | Baseline ms | Candidate ms | Repeated baseline ms |
| ----- | ---------------------------- | ----------: | -----------: | -------------------: |
| linux | flat10000                    |       2.846 |        2.990 |                2.800 |
| linux | many10000                    |       2.186 |        2.315 |                2.140 |
| linux | output100                    |       2.936 |        2.459 |                2.868 |
| linux | output1000                   |       5.967 |        3.098 |                5.778 |
| macos | flat10000                    |      16.483 |       16.436 |               16.610 |
| macos | many10000                    |      12.202 |       12.380 |               12.368 |
| macos | output100                    |      10.793 |        9.820 |               10.651 |
| macos | output1000                   |      15.019 |       10.492 |               15.005 |

These pipe-observation values include parent overhead. They support earlier visible
large-output progress; they do not establish native throughput or a small-input
startup improvement.

## Footprint and reproducibility

The lean release CLI grows from 12,729,496 to 12,823,944 bytes on Linux (+94,448,
0.74%) and from 4,196,976 to 4,222,336 bytes on Mac (+25,360, 0.60%). These are
copied local release images; no full-feature distribution size is inferred.

The capsule's path index references SHA-256-keyed UTF-8 or base64 blobs. Decode a
blob to recover its original file bytes; every reference and payload is verified
by a round trip. Source snapshots identify the base and exact modified files.
The fixture recipe maps 30,866 relative file paths to ten unique content blobs;
create parents, decode each content blob and write the exact bytes, then update
manifest roots/launcher/image paths for a new machine. Native and memory drivers,
launcher's source, all original commands/logs, test XML, failed prototypes and
profiling observations remain available. Executables are reproducible from the
recorded source and toolchain instead of embedded in the repository.
