# Compiled output field stat demand

The combined template-and-column policy is not selected. It removes unnecessary metadata
requests and improves many lexical-output cases, but it repeatedly slows Linux TSV output
over many directories. The separate template-only policy is proposed after both-host development qualification against
a fresh main baseline. It leaves columns conservative on every platform and worker count.
This is a targeted improvement with measured costs, not a universal faster-path claim.

## Requirement ownership and semantics

The field registry owns a conservative stat requirement beside each bound renderer. Literal
segments, audited lexical path fields and supplied context values do not require stat metadata.
Unclassified bindings, size/time/identity fields, content fields, symlink targets and type fields
remain conservative. Compilation copies the requirement into the segment; command planning
queries the compiled requirements once. There is no per-entry field-name dispatch, new worker,
shared owner or synchronization primitive.

Expression demand, colors, reductions, collections, deferred evaluation, native-case handling,
root/directory/unknown-type/followed-link traversal and safety action requirements remain
independent. A mixed `{name}:{size}` template still needs metadata. Lexical output follows the
same enumeration semantics as ordinary metadata-free listing: an attribute it never reads does
not cause a new stat failure. Required metadata errors still stop that entry.

Fuzzy-score observation is a separate requirement. The original implementation coupled it to
full metadata. The first full test runs caught empty `{fuzzy}` output after the new admission
avoided eager stats. The corrected implementation preserves the original template/column score
collection independently of stat demand. Both failed suites and the initial, corrected and final
cross-batch regression sources stay in the evidence. The final test uses 1,000 entries, both one
and three workers, a successful score and a non-fuzzy OR result whose score must be empty.

## Protocol and populations

The combined policy uses base `d8cf61cccd4b0efed5058bf6fa45228e47162b48`. Its first bounded
screens retain three raw repetitions and the fastest two. Its initial full and corrected-image
full populations each have three sessions, nine raw repetitions and the fastest seven averaged
per case. Do not mix observations across these source/image identities. The correction was
required by a demonstrated correctness failure; it was not another attempt to select favorable
timings. Original images, controls and slower observations remain separate.

Each full host population has 80 memory cases: broad/deep trees, 10 through 100,000 files,
one/three workers, plain listing, lexical template, lexical TSV columns and required-size
template. The native CLI population has 64 cases: flat/many-directory layouts, 10 through
10,000 files, the same four output tasks and CPU allowances. Each native command verifies its
complete independently generated output. Each memory case verifies complete output before
timing production `RunFind`; this does not substitute for the engine/CLI regression suites.

Participants are unchanged parent, candidate and an exact same-image parent repeat. Native
participants rotate between rounds and discard warmups; the memory library randomly interleaves
cases/repetitions and participant order rotates across sessions and CPU groups. Linux pins one
or three distinct physical cores. macOS requests one/three workers without affinity enforcement. Linux native fixtures live in
`/dev/shm`; macOS uses warmed `/private/tmp` filesystem fixtures, not a claimed RAM disk.
Per case, divide candidate by parent within each session and take the median of those three
elapsed-time changes. A cohort median describes its included cases, not a weighted throughput
claim; inspect the per-case rows too. Negative elapsed-time change means faster.

## Combined-policy counterexample

The corrected cohort median for Linux three-worker lexical columns is about -1.00%, but the
many-directory 10,000-file TSV case is +24.36%. Its three session changes are -0.53%, +24.36%
and +35.24%; its same-image repeat median is -5.87%. Candidate elapsed times remain about
5.2-5.5 ms. The initial full image has the same case at +24.30% median, with session changes
+43.83%, +24.30% and +15.65%. This is an adoption blocker for the combined policy, despite large
flat-directory wins. No cause is claimed from these timings alone.

Other costs remain visible: corrected macOS serial memory cohort medians are +2.21% for plain
listing and +2.31% for required-size templates. Individual required-size cells reach +6.31% on
macOS and +5.67% on Linux. Native ordinary-control medians are generally within about 1%, but
that does not erase individual costs or establish a universal no-regression result.

## Separate template-only qualification

O31b starts from fresh main `1786340680499f50b08dee5b1ceffbc20fd7b533`, after the independent
rg transaction change. Only implicit templates use the compiled requirement for admission;
column output keeps eager metadata on every platform and worker count. There is no count,
timer, CPU-specific or operating-system gate. The full protocol, all controls and the earlier
regressing TSV case were declared before measuring this source. Its measurements cannot be
added to earlier gains or used to infer a benchmark change caused solely by the base update.

Both-host focused suites and all 323 targets pass; seven focused ASan targets pass on both
hosts, and seven focused MSan/TSan targets pass on Linux. The unchanged-parent counter test
produces exactly the expected
eight template failures: 1,001 stats instead of one for four output forms at one/three workers.
Columns still satisfy their unchanged 1,001-stat expectations. These development results cover the stated main baseline and variant; the actual synchronized
public head still needs its own required CI and integration review.

The shipping CLI images use `clang_release` and `xff_full`; actual macOS main compile and link
actions both contain `-flto=thin`. The memory API harness has its own target configuration and
is not claimed to be the shipping LTO binary. The temporary measurement source is restored
after each build; its exact source and command/image receipts are retained for reproduction.

## Template-only measured results

Each table cell is a cohort median of the per-case three-session changes; its adjacent repeat
column uses the exact unchanged parent image. All individual rows and session signs are in
`analysis-final/all-case-comparisons.json` and CSV in the lossless capsule.

### Native whole-command CLI

8 cases per output/CPU cohort. Negative change is faster.

| Output                | CPUs | Mac change | Mac repeat | Linux change | Linux repeat |
| --------------------- | ---: | ---------: | ---------: | -----------: | -----------: |
| Lexical template      |    1 |     -4.34% |     +0.64% |      -11.18% |       +0.44% |
| Lexical template      |    3 |     -1.06% |     +0.91% |       -2.43% |       +0.62% |
| Columns control       |    1 |     +0.91% |     +0.78% |       +0.75% |       +0.05% |
| Columns control       |    3 |     +0.74% |     +0.21% |       +0.64% |       -0.23% |
| Plain control         |    1 |     +0.29% |     -0.08% |       +1.09% |       +0.31% |
| Plain control         |    3 |     +1.12% |     +0.29% |       +1.22% |       +0.12% |
| Size template control |    1 |     +1.25% |     +0.50% |       +1.49% |       +0.36% |
| Size template control |    3 |     +0.68% |     +0.64% |       +0.89% |       +1.71% |

### Production RunFind in memory

10 cases per output/CPU cohort. Negative change is faster.

| Output                | CPUs | Mac change | Mac repeat | Linux change | Linux repeat |
| --------------------- | ---: | ---------: | ---------: | -----------: | -----------: |
| Lexical template      |    1 |    -10.55% |     -0.17% |      -17.41% |       -1.04% |
| Lexical template      |    3 |     -5.63% |     +6.95% |      -12.04% |       -0.06% |
| Columns control       |    1 |     +1.92% |     -0.18% |       -0.65% |       -0.30% |
| Columns control       |    3 |     +0.48% |     +3.25% |       +0.28% |       +0.34% |
| Plain control         |    1 |     +3.24% |     +0.31% |       -1.24% |       -0.44% |
| Plain control         |    3 |     +0.02% |     +3.10% |       -1.13% |       +0.19% |
| Size template control |    1 |     +4.04% |     -0.68% |       +1.14% |       -0.33% |
| Size template control |    3 |     +0.24% |     +5.60% |       +0.04% |       +0.04% |

The former Linux 10,000-file/many-directory/three-CPU TSV case is now +1.08% at its median,
versus -0.33% repeat. Session changes are +1.08%, -7.50% and +10.25%; do not describe it as
uniformly unchanged. Column controls overall remain close to their repeat cohorts.

Costs are explicit. macOS serial memory plain/size cohorts are +3.24%/+4.04%, with repeats
+0.31%/-0.68%. A deep 100,000-file size case is +4.88% with -1.41% repeat; some small/medium
memory controls reach about +6%. Linux native flat/ten-file size is +7.69% (+3.10% repeat),
and many-directory/10,000-file serial size is +5.60% (+1.27% repeat). The cause is not established;
these are not removed as noise. Mac parallel memory repeats vary about +3% to +7% at cohort
level, limiting precision of those comparisons. No unchanged-image rerun was selected to erase
costs or enlarge a favorable result.

The recommendation favors the lexical-template gain and removal of unnecessary required I/O
on both hosts, while retaining those bounded costs for review and further work. Native ordinary
control medians are about +0.3% to +1.5%; individual results need the qualifications above.
Do not multiply gains by earlier optimization percentages or apply them to different roots,
archive workloads, operating systems, CPU allocations or source versions.

### Frozen shipping-config image sizes

| Host         | Parent bytes | Template-only bytes | Change bytes |
| ------------ | -----------: | ------------------: | -----------: |
| macOS arm64  |      8277640 |             8277528 |         -112 |
| Linux x86_64 |     18454496 |            18451888 |        -2608 |

These are the exact experimental full CLI images, not downloaded release assets or compressed
packages. Image hashes and actual main compile/link action arguments are retained. The source
and configuration difference from the older combined-policy base is explicit; do not attribute
cross-base image differences to this single policy.

## Verification and limits

The corrected combined prototype passes all 323 targets on each host, seven focused ASan suites
on both hosts and seven focused MSan/TSan suites on Linux. Strengthening only the cross-batch
regression afterward passes its optimized and applicable sanitizer targets on both hosts.
These are development results on the stated prototype base, not final checks of a later public
head. Runtime adoption requires the actual synchronized public context to pass its own checks.
No local clang-tidy, peak RSS, hardware-counter, cold-cache or archive-specific speedup claim is
made. No experimental population is uploaded as an official merge/release benchmark series.

Preparation failures are retained: an initially deprecated benchmark barrier, an unchanged failed
replacement attempt, the original fuzzy-score coupling failure, a missing workflow-input setup
failure and the corrected TSV-header expectation. The source completeness repair restores two
workflow action files without changing runtime inputs. A later cross-host check covers all
1,302 tracked files per role: the combined parent is identical, and candidate differences are
limited to two working prose files; runtime, tests and build inputs agree.

## Remaining opportunities

The combined column policy needs a causal pipeline investigation before adoption; do not add
an unexplained file-count, worker-count or platform threshold to conceal the counterexample.
A separate score-demand capability could eventually restrict fuzzy ranking work to consumers
that actually read scores. This trial deliberately preserves the old broader score-observation
contract for every template/column and does not claim that further optimization. Stat-bearing
field output also remains eager; conditional rendered-field metadata admission needs its own
error-ordering and traversal analysis rather than being silently included here.

## Download and reproduce

[Lossless evidence capsule](compiled-field-stat-demand-20261107.json.gz) retains
629 individually hashed records, 73,562,227 decoded record bytes and 6,954,149 compressed bytes.
SHA-256: `b52cc8687dd213cda89b958b31ca912c4846d8f9d00ae32714227d7afdbbd8be`.

Restore each `records` entry according to `encoding`, then verify `bytes` and `sha256`.
The roots are `combined/` and `template_only/`. They retain the original/corrected source and
image identities, plans, all raw timing runs, all-cell analysis, failed and successful tests,
source agreement, build logs/actions and private runner programs. Some stage snapshots retain
identical records; copies are not independent observations. Executables and large transport
archives remain private; exact image hashes and reproducible source/configuration inputs are
included instead.

`combined/analyze-final.py` recomputes the corrected-image case/cohort report.
`template_only/analyze-final.py` computes the separate template-only report. Adjust only their
input-root paths after restoring; neither rebuilds or times a binary. Native fixtures have an
explicit relative-path/byte-length specification and a constructor that requires an empty root.
Those tasks read paths and sizes, not file contents. The native launcher source is included;
its paths and output manifests must be adjusted for the new machine. Preserve the declared CPU,
participant, warmup and retention protocol when reproducing timing, and retain every result.
