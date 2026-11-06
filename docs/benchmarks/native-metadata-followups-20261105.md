# Native metadata and task-cleanup follow-ups, 2026-11-05

Retain the current implementation after four independent prototypes fail to establish a useful
cross-platform improvement. Removing an apparent scan, allocation or mutex-protected cleanup is not
sufficient evidence of a faster complete command. Preserve their sources, every measured round,
independent output checks, copied-parent controls, initial failures and binary identities.

All prototypes use the same published parent, PR #1109 commit
`796e914dffd9b8c2a96b283fc72dc448267ec948`. None changes flags, dependencies, worker admission,
mutation authorization or public VFS interfaces. These are private experiments, not substitutes for
public pull-request CI. Qualification differs by prototype: all pass five focused release targets
and actual changed-unit canonical clang-tidy on both hosts, while only the single-condition form
completes the full matrix detailed below. Passing correctness checks does not establish a
performance improvement.

## Decision overview

Percentages are elapsed changes, so negative means faster. Each case has three balanced sessions,
nine measured rounds and the fastest seven retained, with a byte-identical copied parent. The table
gives medians across cases only as a compact overview; it is not a weighted workload estimate. Raw
evidence preserves per-session absolute times and every slower case.

| Prototype / population            | Host  | Cases | Median change | Lowest change | Highest change | Decision                                    |
| :-------------------------------- | :---- | ----: | ------------: | ------------: | -------------: | :------------------------------------------ |
| Early return, native              | macos |    80 |       -0.073% |       -1.781% |        +2.117% | Reject prototype                            |
| Early return, native              | linux |    80 |       -0.191% |       -2.699% |        +2.834% | Reject prototype                            |
| Early return, mounted archive     | macos |    16 |       +0.144% |       -1.203% |        +1.641% | Reject prototype                            |
| Early return, mounted archive     | linux |    16 |       -0.384% |       -1.194% |        +0.822% | Reject prototype                            |
| Direct observation vector, native | macos |    80 |       -0.234% |       -2.638% |        +2.606% | Reject prototype                            |
| Direct observation vector, native | linux |    80 |       -1.290% |       -5.424% |        +1.666% | Unchanged Linux instructions; noise control |
| Task path cleanup, in-memory      | macos |    80 |       -0.177% |       -4.382% |       +10.447% | Reject prototype                            |
| Task path cleanup, in-memory      | linux |    80 |       +0.025% |       -7.227% |        +2.817% | Reject prototype                            |
| Single condition, native          | macos |    80 |       -0.197% |       -2.660% |        +2.019% | Reject prototype                            |
| Single condition, native          | linux |    80 |       +0.061% |       -2.013% |        +5.682% | Reject prototype                            |
| Single condition, mounted archive | macos |    16 |       +0.142% |       -1.122% |        +1.195% | Reject prototype                            |
| Single condition, mounted archive | linux |    16 |       -0.751% |       -2.601% |        +0.641% | Reject prototype                            |

## O20l1: early metadata return with a second finish call site

The retained ReadObservedNow implementation counts missing observations before testing cheap serial,
metadata-demand, small-directory and mounted-owner conditions. This prototype returns through
FinishEntries before that count when those conditions apply. It preserves all existing chunk
thresholds, stat/follow fallback and directory-owner contracts.

The parent Linux assembly contains the actual count loop before the cheap checks; this is not a case
where the compiler already removes the unnecessary work. However, the new return duplicates a finish
path: the actual ReadNow function grows from 0x7e6 to 0x97f bytes, an increase of 409. The complete
Mac executable grows by 208 bytes. Complete-command improvements remain small and mixed, so retain
the current implementation. The separate single-condition prototype below tests the same work
avoidance without duplicating finalization; its results remain a separate population.

Each host has 80 native cases: regular, symlink-only and mixed broad/deep trees at 10, 1,000 and
100,000 entries, one/three workers, metadata CSV and summaries, plus eight regular name-filter
controls. These reuse the previously independent O20k path/lstat/count/byte oracles without changing
or relabeling that original population. Sixteen separately measured mounted TAR cases cover
wide/deep 1,000/10,000/100,000 members, summaries and large CSV output. Those execute the real
walker; a direct ReadDirectory-only microbenchmark would not exercise this candidate.

Both parent and candidate match complete original output bytes/order, exit and empty stderr before
timing, with complete output hashes checked on every invocation. Per host, 96 cases times three
sessions times three binary participants times nine rounds give 7,776 measured invocations, plus 864
declared warmups; any pause/resume rewarmups are recorded separately. Timing uses immutable release
images and the recorded native launcher, including startup, traversal and output.

Initial Linux archive preflight exhausted the /tmp user quota before any mounted timing. Its failure
and partial output survive. Only new mounted outputs moved to /dev/shm; source, executable and
fixture identities remain unchanged, and the complete independent preflight was rerun. A verified
redundant generated JSON transport was removed only after exact hash proof against its retained
compressed copy; all original observations and frozen publication evidence remain.

The first Mac mounted preflight ran under the agent sandbox and took about a minute for large CSV
through a pipe. All such preflight elapsed values are excluded from performance data. Actual
measurements run outside that sandbox through the native launcher and a regular output file. Do not
infer a product pipe bug from that isolated environment difference.

## O20j: direct native observation vector

Only the Apple bulk-directory implementation changes: retain segmented Entry accumulation but append
observations directly to a vector rather than materializing that final vector from segmented
storage. Its first allocation reserves the larger of 16 and the current entry count plus one.
Ownership, field completeness, follow fallback and the result's public contiguous representation
stay intact.

The 80 native cases and complete output oracles are the same selected population described above,
with 6,480 measured invocations and 720 declared warmups per host. No archive or followed-link
performance claim is made for this rejected storage prototype.

The Mac median change is -0.234%, with mixed -2.64% to +2.61% case values. Large serial symlink
summaries report RSS 52.58 to 52.03 MiB; metadata output reports 52.69 to 51.69 MiB. Mixed serial
summaries report 52.52 to 51.83 MiB. These small reported memory changes do not justify claiming a
large memory-saving rewrite; vector capacity slack and geometric growth copies remain real costs.
The Mac executable shrinks by 1,312 bytes, but the elapsed/RSS result does not justify the
prototype.

This change is Apple-only. Linux .text and all instruction bytes are identical. Its whole executable
SHA is not identical: the build ID, debug sections and ten source-location words differ. Each
changed word in .data.rel.ro is independently verified as a local_fs.cc diagnostic line shifted by
three. Those exact proofs are retained; Linux timing is an unchanged-instruction noise control, not
a claimed optimization. Both Linux executable sizes are unchanged.

The existing segmented representation remains. Its inactive MBO slots do not initialize unused
values, and FinishEntries already moves its owner; neither unused-slot zeroing nor an extra engine
copy is an established cost. A larger public representation change remains a separate design task.

## O08b: release a consumed directory path outside the queue mutex

Take acquires the sole consumer's Ready state and moves the completed result. The prototype then
moves the owned path into an explicitly lifetime-only local before taking the existing recycle
mutex. The path dies after unlock, matching the abandoned-payload cleanup pattern. It adds no
borrowed lifetime, capacity retention, worker policy, timer or wrapper.

The first version lacked the lifetime-only attribute and failed the real local unused-nontrivial-
variable clang-tidy rule on both hosts. Those failed logs and sources remain. The final version uses
maybe_unused explicitly and passes the five focused suites and its actual walk.cc lint.

First-stage measurement reuses the existing name_listing_benchmark without adding benchmark code: 80
files-only cases, broad/deep, complete/name-only listings, lazy/eager metadata, 10 through 100,000
files and one/three workers. The deep fixture has at most 248 leaf directories, not 100,000 separate
directories. Each of three balanced sessions measures parent, candidate and copied parent with nine
repetitions, fastest seven, random case interleaving, 0.02-second minimum measurement and
0.01-second warmup. Linux pins each process to one/three allowed CPUs; no Mac pinning is claimed.
The existing fixture independently checks complete-path versus name-only output inside each case.

The Mac median is -0.177% and Linux +0.025%, with no compelling gain. One Mac 1,000-file broad
three-worker complete-listing case changes +15.16%, +3.00%, +10.45% across sessions, versus copied-
parent +9.86%, +0.057%, -0.88%; absolute times are about 0.2 milliseconds. These short exploratory
measurements do not establish a real-filesystem regression estimate, and they certainly do not
justify adopting this prototype. The Mac executable size stays unchanged and Linux grows 424 bytes.
No native filesystem or complete winner qualification is claimed for this rejected first stage.

## O20l2: one finish call and short-circuit admission

The second scan trial retains a single FinishEntries call and places pending-worker, demand, size
and owner checks before the count. After the small-directory check is false, an empty observation
sidecar implies at least 512 missing entries, so the remaining count condition is required only for
a nonempty sidecar. Exact existing thresholds, stat/follow fallback, ownership and worker preference
remain unchanged; no wrapper, cache, flag or API is introduced.

Actual optimized Linux code confirms the change: the parent count loop precedes the first pending
check, while the candidate branches directly to the single finish path before reaching that loop.
ReadNow grows only from 0x7e6 to 0x7eb, five bytes rather than the first prototype's 409. The
complete Mac executable stays unchanged in size; Linux grows 48 bytes. Full native and mounted
matrices reuse the exact same 80 plus 16 independent output-oracle populations and three balanced
9/7/copy sessions per host. All original slower cases remain.

Native medians are -0.197% Mac and +0.061% Linux; mounted medians are +0.142% Mac and -0.751% Linux.
For context, the corresponding archive copied-parent medians are -0.375% and -0.522%. These controls
are retained rather than subtracted to manufacture a corrected win. Avoiding the scan in assembly
has not established a convincing practical gain, so reject this form too.

The original Linux mixed/deep 100k three-worker metadata case changes +5.682%, with copied parent
+2.654%; its three candidate sessions are all positive. A separate predeclared four-case repeat uses
the exact frozen images, that case and three serial/parallel controls. The same case then changes
+0.496%, with copied parent +3.492%. Both populations survive: the repeat does not replace the
original counterexample or prove that every cost is noise. The limited signal provides no reason to
adopt a new production form.

This prototype completed all 331 release targets on both hosts, including every extension and real
required Linux FUSE. Five focused ASan suites pass on both, with those same five MSan/TSan suites on
Linux. Initial actual changed coverage reaches 5/5 lines but only 10/12 branches (83.33%), failing
the unchanged policy. A meaningful end-to-end test at 129/511/512 entries verifies empty-sidecar
fallback, one missing-file error, exact reference visit/error order, Basic fields, coordinator-only
visitors and serial stats below the boundary versus real peer stat workers at it. Its actual
translation unit and hermetic formatting pass on both hosts. It passes release/ASan/MSan/TSan on
Linux and joins the final full release/ASan qualification on Mac. Changed coverage then reaches 5/5
lines and 11/12 branches (91.67%), without exclusions or threshold reductions. The initial failure
and recovery remain visible in the evidence.

Post-qualification rebuilt complete binaries exactly match the frozen measured images on both hosts.
Those correctness results do not change the performance decision. The production and test prototypes
remain reconstructible inside the evidence; neither is added to production by this report.

## CSV transport diagnostic

A separate outside-sandbox diagnostic uses one unchanged frozen parent, its byte-identical copy and
the declared 100k wide-TAR metadata CSV case. Three rotated rounds compare regular-file versus
Python pipe capture; all 12 executions per host match the original output length, SHA, exit and
empty stderr. Mac file results are about 0.246-0.267 seconds and parent pipe results 0.234-0.242;
one first-touch copied-parent pipe takes 0.605 seconds and remains recorded. Linux file results are
about 0.129-0.137 seconds and pipes 0.154-0.165. The previous minute-long sandbox preflight is not
reproduced outside that environment. This diagnostic does not justify a product buffering fix.

Existing RawRecordBuffer and EncodedRecordBuffer reuse bytes for each record, including custom CSV
rows. They do not coalesce multiple rows, and std::cout already buffers its underlying file/pipe
writes. Do not claim that existing row-buffer reuse is absent or that this diagnostic proves a new
batching policy would help. No source change is made for the transport observation.

## Evidence

[Lossless raw evidence](native-metadata-followups-20261105.json.gz) contains 2,753 records across
six experiment/diagnostic roots, deduplicated into 452 contents. The gzip is 7,201,006 bytes,
SHA-256 `3ddbd15c14b0e4563e341c768830a61de72ff92e6289ba9e28d8d812f158a51a`; every record retains its
original path, byte count and SHA, reconstructing 2,921,518,394 bytes. Every original record has
been independently reconstructed and checked. Producer scripts, source snapshots, output oracles,
all rounds, copied-parent drift, actual lint/test logs, initial failures, recovery checks and ELF
proofs are retained. Unrun qualification scripts remain plans; completed single-condition checks are
recorded separately from the rejected first stages.
