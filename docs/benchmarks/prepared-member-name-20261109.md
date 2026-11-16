# Prepared streamed archive member names

Prepare the requested member's normalized name once for each streamed open. Keep the stored
header name normalization inside the header loop. The requested view borrows the source object's
owned string for the duration of that open; it introduces no allocation, lock, cache, or lifetime
change.

A small value-returning adapter contains libarchive's nullable pathname API and serves both
streaming lookup paths. An absent pathname does not match even an empty request. An empty,
present pathname may match an empty normalized request. The first matching header still wins;
a matching directory or symlink remains an error rather than falling through to a later regular
duplicate. Normalization still strips leading `./` components and trailing slashes (retaining a single `/`), without
resolving interior `..` components or folding case.

The candidate reduces normal streamed-read elapsed time modestly on both hosts. Keep the
Linux missing-member microbenchmark cost (up to 3.45%) as an explicit tradeoff; this is not a
universal speedup. It adds no capability or command-line setting.

## Results

Negative changes mean faster. The copy is an independently stored, byte-identical main image;
it is a diagnostic participant, not a divisor or correction factor. Every native case requests
one worker. These are retained means, not confidence bounds or a weighted deployment workload.

| Host  | Task          | Tree  | Members/files |   Main ms | Candidate |   Copy |
| ----- | ------------- | ----- | ------------: | --------: | --------: | -----: |
| macos | archive-lines | broad |           100 |    12.706 |    -2.27% | +0.60% |
| macos | archive-lines | broad |         1,000 |   125.983 |    +1.42% | +1.79% |
| macos | archive-lines | broad |        10,000 | 10283.505 |    -2.35% | -3.89% |
| macos | archive-lines | deep  |           100 |     9.471 |    +0.62% | -0.66% |
| macos | archive-lines | deep  |         1,000 |   144.903 |    -1.21% | -1.62% |
| macos | archive-lines | deep  |        10,000 | 13273.324 |    -2.09% | -2.44% |
| macos | files-exact   | broad |           100 |     4.940 |    -2.59% | -2.40% |
| macos | files-exact   | broad |       100,000 |    25.316 |    -0.49% | +0.49% |
| macos | files-exact   | deep  |           100 |     5.895 |    -0.74% | +1.34% |
| macos | files-exact   | deep  |       100,000 |    30.402 |    -0.58% | +1.26% |
| linux | archive-lines | broad |           100 |     2.022 |    -0.67% | -1.03% |
| linux | archive-lines | broad |         1,000 |    87.058 |    +0.23% | +2.00% |
| linux | archive-lines | broad |        10,000 |  8277.677 |    -0.64% | +0.44% |
| linux | archive-lines | deep  |           100 |     2.105 |    -0.32% | -0.77% |
| linux | archive-lines | deep  |         1,000 |   122.435 |    -1.48% | -0.01% |
| linux | archive-lines | deep  |        10,000 | 12554.980 |    -1.53% | -0.13% |
| linux | files-exact   | broad |           100 |     0.841 |    -0.34% | -3.45% |
| linux | files-exact   | broad |       100,000 |    35.723 |    -0.07% | -0.40% |
| linux | files-exact   | deep  |           100 |     0.917 |    +0.37% | -3.12% |
| linux | files-exact   | deep  |       100,000 |    43.738 |    -0.04% | -0.53% |

Descriptive geometric ratios across the original kernel cases, with each case weighted
equally (this does not describe an expected command mix):

| Host  | Kernel | Cases | Candidate |   Copy |
| ----- | ------ | ----: | --------: | -----: |
| macos | last   |    24 |    -3.32% | -2.55% |
| macos | list   |    24 |    -2.29% | +1.55% |
| linux | last   |    24 |    -4.36% | -0.25% |
| linux | list   |    24 |    -4.48% | -1.11% |

First/missing controls use a fresh archive cursor in every operation, and distinguish
reusing a member wrapper from constructing a new wrapper. Each row groups four cases:
two archive sizes and two requested-name spellings.

| Host  | Control               | Candidate |   Copy |
| ----- | --------------------- | --------: | -----: |
| macos | first/reopen          |    -0.39% | -0.19% |
| macos | first/fresh-wrapper   |    -1.09% | -0.03% |
| macos | missing/reopen        |    -2.70% | -0.80% |
| macos | missing/fresh-wrapper |    -2.49% | -0.95% |
| linux | first/reopen          |    -0.06% | -0.32% |
| linux | first/fresh-wrapper   |    +0.08% | -0.69% |
| linux | missing/reopen        |    +2.60% | -0.89% |
| linux | missing/fresh-wrapper |    +2.54% | -0.90% |

### Slower observations and limits

The original Mac legacy last-member cases at 10/128 and 1,000/128 reported +6.74% and
+5.63%. A fixed confirmation selected every candidate above +5% or copy outside +/-5%
(16 cases), before examining its results. The same two cases then measured -11.60%
and +0.38%. The original nested-memory listing at 10/4,096 reported +17.09% with a
+21.39% copy control; its repeat measured -11.92%. The repeat retains an unrelated
legacy listing cost of +2.35%. Neither cohort replaces the other, and the large copy
movements do not establish a particular hardware, placement, or image-backing cause.

Linux missing-member controls retain descriptive group costs of +2.60% and +2.54%,
with a worst individual point of +3.45%. This error-path tradeoff accompanies faster
normal last-member kernels and modest native archive improvements. The largest native
point regression is Mac broad/1,000 members at +1.42% (copy +1.79%). Ordinary native
listings remain near parity. Small gains and moving controls limit causal confidence;
retain the simpler invariant preparation without promising larger end-to-end gains.

### Shipping image sizes

These are actual configured executables before stripping or compression, not tagged
release assets. The manual benchmark overlay is not linked into the CLI.

| Host  | Main bytes | Candidate bytes | Change |
| ----- | ---------: | --------------: | -----: |
| macos |  8,322,520 |       8,322,520 |     +0 |
| linux | 18,578,064 |      18,578,184 |   +120 |

## Data

Download the [Mac data](prepared-member-name-20261109-macos.json.gz) and
[Linux data](prepared-member-name-20261109-linux.json.gz). They retain all original
iteration documents, native samples, source/build identities, helper overlays, scoped
verification logs, rejected/contended attempts, and estimator checks. JSON documents are
semantically complete, with original byte lengths and SHA-256 hashes; exact raw bytes
and frozen images remain in the retained evidence directories.

| Dataset | Compressed bytes | SHA-256                                                          |
| ------- | ---------------: | ---------------------------------------------------------------- |
| macos   |           847872 | 91fa836dc223c0640b50e1551cd21def14e7556bb80ccd2625d163079d3ab674 |
| linux   |          1477789 | 08f7c48fb5c63cb212f41dd71a47a0104b8a6e926961749e165ebf5a43655113 |

## Source and build identities

The shipping baseline is main `91437fef41f86c7f048dd542e77f21103d4e1268`; the runtime candidate
is `b961a4bf977691f8e76e31b7dcf582cede6596f7`. Both were built from the same owned workspace on
each host, using the actual `clang_release` and `xff_full` recipes with ThinLTO. Each configured
executable was queried and frozen immediately. Identical-copy controls preserve the `xff_full`
invocation name, use ordinary independent copies, and appear once per rotated round.

The candidate's initial signed development commit was
`c041358fdb6cbf42849f1d88cee2c67af44bf23e`. Its complete Git tree and the rebased runtime tree are
identical (`fac59eeb91a4cd9236c43fbb6dc0cd102bf653d0`); the rebase changed ancestry after the prior
PR was squash-merged. This equivalence supports development evidence only, never checks from a
different PR context.

Mac main's full file hash differs from the previous scanner report's image because cached
Mach-O debugger source-directory strings contain different sandbox paths. All eleven file-backed
sections and symbol values match after normalizing only those debugger directory prefixes.
The current experiment uses its own baseline consistently; it makes no whole-file identity claim
against that older Mac image. The Linux baseline matches the previous shipping image byte for byte.

First/missing-member controls add an identical benchmark-only translation-unit overlay to both
runtime revisions. Every runtime source and build rule remains the corresponding signed revision's
content. These helpers are separate from the shipping CLI images; their source, patch, build
commands, and hashes are retained explicitly.

## Measurement contract

All timings use unsanitized optimized binaries. The native CLI corpus resides on case-sensitive
HFS+ RAM storage on Mac and verified tmpfs on Linux. Each measured CLI invocation runs as a fresh
process. The memory fixtures are resident; none of these results claims cold operating-system
cache performance. Linux uses an explicit single-CPU affinity for the native controls. On Mac,
`--jobs=1` requests one worker; no CPU pinning is claimed.

Native cases cover broad and deep TAR trees with 100, 1,000, and 10,000 members, plus ordinary
listings of 100 and 100,000 files as unrelated controls. Each image receives one discarded
warmup and nine measured rounds per case; retain the fastest seven for its point estimate.
Every invocation has an independent path/content oracle. Native timestamps exclude Python's
process-launch overhead. All slower observations and output-validation counts remain in the data.

The unchanged archive-source kernel covers 48 cases: four source implementations, listing or
last-member reading, three member counts, and two payload sizes. First/missing controls add 16
cases spanning ten or ten thousand headers, normalized or prefixed request spellings, and reused
or freshly constructed member wrappers. Both kernel groups rotate main, candidate, and an
identical-copy control across three blocks. Each case uses nine repetitions per block, retains
seven per block, and averages those three retained means. Fixture construction is outside timing;
read contents or expected missing errors are checked within every measured operation.

An earlier Linux kernel cohort overlapped a newly started independent benchmark. Its raw results
remain preserved as contended development evidence. The full fixed matrix was repeated after that
campaign completed, with before/after checks for competing measurement/compiler work. The first
native attempt stopped with zero timings when another task started an MSan compilation. A fresh
native cohort waits for sixty seconds clear before starting, with additional checks between
rotated rounds. No result is divided by the copy-control ratio or silently replaced because it is
slower. These checks do not claim an otherwise physically isolated machine.

## Correctness and validation

Seven committed member-source tests cover normalized requests, first duplicate selection,
directory/symlink rejection, missing/dot/case behavior, source ownership and reopening, invalid
source errors, and the actual C boundary's absent and present pathnames. The shared adapter's
short-circuit guard is exercised on all four branches in instrumented library code; it is not an
uninstrumented inline test substitute.

At the signed development runtime source, the full configured release suite passes 345 targets
on each host, allowing Bazel reuse only for unchanged declared inputs. Eight affected suites pass
freshly under ASan and TSan on both hosts and MSan on Linux. Actual affected translation-unit
clang-tidy passes on both hosts. The additional benchmark translation unit receives its own check.
These are scoped development results; final pushed-head checks belong to the resulting PR and
must complete in its own context.

## Retained work

This changes invariant name preparation only. It does not add a TAR index, avoid archive replay,
or change decoder admission, safety policy, metadata observation, concurrency, or output ordering.
The larger O19b opportunity remains: reuse validated plain-TAR payload locators independently,
while preserving format checks and read budgets. The broader O19g borrowed-key experiments and
all remaining performance candidates retain their separate TODO entries.
