# Owned transactional rg output, 2026-11-07

The selected variant passes owned rendered records directly to the matcher result,
removing three allocations per entry in the isolated 64-entry fixtures. Large-output
cohort times improve by 12.36% on Mac and 4.32% on Linux in the in-memory matrix.
Ordinary whole-command rg-line medians are effectively unchanged. This is a targeted
output improvement with retained slower cases, not a general search speedup.

## Mechanism and semantics

Previously, line rendering accumulated a complete transaction plus record-end offsets,
replayed it into another per-file string, and copied that string into the parallel
result. `RenderRgOutput` now returns an owned string and selection state. A worker
moves its reusable result allocation into the renderer and takes the transaction back.
Ordinary rg line output needs neither record-end indices nor replay. Native grep
retains its original local buffer, boundaries, frozen sink and bool/status result.
Reduced and multiline rg output keep the validated capture path.

Publication still waits for the complete read and binary checks. A late read failure
or binary marker discards the file's transaction. Heading/filename and sink state
are restored on errors. Existing result-capacity retention limits remain; no worker
count, batch size, CLI flag, VFS operation or archive barrier changes. The preceding
helpers were already inlined under production LTO, so removing surviving function
calls is not the claimed mechanism.

## Qualification protocol

Baseline is `c95140674254c371844f1fab27a64951a80dac55` (#1119). Subsequent base changes
through #1125 affect documentation rather than this implementation. Recorded source
hashes and unstripped executable hashes identify each measured participant. Both hosts
use explicit `--config=clang_release`; no sanitizer executable or duration contributes
to timing. Existing production benchmark fixtures are unchanged. Mac measurements are
unpinned on the local M5 Pro; Linux uses one pinned logical CPU per physical core.
No build, lint or sanitizer work runs concurrently with timing on the same host.

The memory matrix has 128 cases per host and variant: RE2/PCRE2, eight tasks,
10/100/1,000/10,000 entries, and 1/4 workers. Three counterbalanced sessions each
contain nine raw repetitions; retain the fastest seven and compute their mean.
Each case's reported change is the median of the three paired session changes;
the cohort statistic below is the median across those case changes. A cohort is
not weighted by elapsed time, bytes or invocation frequency. Positive is slower.

The native matrix has 124 cases per host and variant. It covers flat and many-directory
trees, listing/name/metadata/native RE2/rg-file/native-line/rg-line commands, the same
four entry counts and worker settings, plus 10/100/1,000-file output fixtures. Each
native output file has 32 4KiB lines; memory large-output files have one giant 128KiB
line. Those are distinct workloads. Files are warm APFS on Mac and tmpfs on Linux.

Native participants rotate across baseline, an exact same-binary baseline repeat,
and candidate. Each of three sessions has nine measured rounds, retaining seven,
after discarded warmups. A native launcher excludes Python process startup from
measured time; process launch and the complete xff invocation remain included.
Every invocation must match a fixture-derived complete output oracle. All oracles
pass. Raw wall time, CPU and RSS observations are retained, without a native RSS claim.

## Selected variant: memory matrix

| Task         | Mac median change | Linux median change |
| :----------- | ----------------: | ------------------: |
| traversal    |            +0.15% |              +0.02% |
| filter       |            +0.51% |              -1.04% |
| native-lines |            +0.64% |              +3.18% |
| rg-lines     |            -1.49% |              -0.26% |
| rg-files     |            +0.60% |              +0.37% |
| rg-counts    |            +0.39% |              +0.86% |
| rg-quiet     |            +0.99% |              +1.76% |
| large-output |           -12.36% |              -4.32% |

The Linux native-line memory cohort remains 3.18% slower. Retain the Mac PCRE2
native-lines/10,000/4 case at +6.63%, Mac RE2 quiet/10,000/1 at +5.49%, Linux PCRE2
count/100/4 at +8.53%, and Linux PCRE2 native-lines/1,000/1 at +4.57%. Allocation
counts do not explain every timing difference; there is no measured causal claim
that these are capacity, placement or unrelated-system effects.

## Selected variant: whole-command controls

| Task              | Mac candidate | Mac same-binary repeat | Linux candidate | Linux same-binary repeat |
| :---------------- | ------------: | ---------------------: | --------------: | -----------------------: |
| files-exact       |        +0.04% |                 -0.59% |          +0.76% |                   +0.32% |
| name-exact        |        +0.36% |                 -0.18% |          +1.51% |                   -0.22% |
| metadata          |        +0.78% |                 -0.87% |          -0.07% |                   +0.16% |
| native-filter-RE2 |        +0.44% |                 -0.47% |          +0.67% |                   -0.26% |
| rg-filter         |        +0.17% |                 -0.59% |          +0.48% |                   -0.11% |
| native-lines      |        +0.16% |                 -0.39% |          +0.64% |                   +0.27% |
| rg-lines          |        -0.23% |                 -0.31% |          +0.00% |                   -0.34% |

These cohorts include the separate output fixtures where applicable. Large native
rg-output cases improve by up to 11.35% on Mac and 50.56% on Linux; these are individual
case improvements, not cohort or general CLI claims. Preserve Linux flat10/rg-lines/4
at +4.50% (repeat -1.17%), many10,000/files/4 at +4.42% (repeat +0.45%), and Mac
flat100/files/1 at +2.06% (repeat -1.45%). The complete per-case and per-session
population, including every slower cell, is in the compressed evidence.

## Allocation traffic and image sizes

The diagnostic uses 64 entries, one inline production matcher and two recorded
repetitions. Fixture setup, warmup and assertions stay outside recording. Counts
are identical in both recorded repetitions. This is an allocation diagnostic under
ASan, not timing evidence or an RSS measurement.

| Host  | Padding bytes | Allocations before | Allocations after | Requested bytes before | Requested bytes after | Retained result capacity before/after |
| :---- | ------------: | -----------------: | ----------------: | ---------------------: | --------------------: | ------------------------------------: |
| Mac   |         1,024 |                704 |               512 |                349,696 |               217,088 |                                65,984 |
| Linux |         1,024 |                704 |               512 |                350,720 |               217,088 |                                66,496 |
| Mac   |       131,072 |                960 |               768 |             62,939,136 |            46,160,384 |                             8,389,056 |
| Linux |       131,072 |                960 |               768 |             62,942,720 |            46,162,944 |                             8,389,568 |

The diagnostic removes three allocations per entry. Retained capacities are unchanged
within each host. The two allocator implementations have different capacities; do not
attribute timing differences to increased result retention from these fixtures.

Unstripped lean CLI images grow from 4,193,792 to 4,196,608 bytes on Mac (+2,816), and
12,687,760 to 12,699,000 on Linux (+11,240). These are frozen experiment images, not
sizes of full release distributions.

## Other preserved variants

Transfer-only ownership moved the outer transaction but kept inner replay. Its memory
rg-line cohort changes were +0.47% Mac/+0.66% Linux; large-output +6.77%/-2.11%.
Mac RE2 large-output/10/1 was +23.30% and PCRE2 large-output/10,000/4 +21.85%.
It is unselected; fewer allocations alone did not qualify it.

The intermediate single-transaction variant also returned a transaction/index aggregate
for native grep replay. Memory large output improved 11.73% Mac/4.20% Linux, but native
lines cost 0.98%/3.50%, and Mac RE2 rg-files/10/4 cost 10.69%. Native rg-line medians
were -0.39% Mac/+0.43% Linux, with same-binary repeats -0.43%/-0.15%. It remains
unselected; the final specialization preserves native's original local publication.
All three source variants, raw populations, controls and available validation logs
remain separately named in the evidence. No unchanged candidate was retimed until favorable.

## Correctness and integration limits

Both hosts pass optimized `rg_test`, `parallel_match_test` and `evaluate_test`, plus
canonical Clang+ASan runs of those three targets and `expression_allocations_test`.
Both pass all 323 core and registered-extension targets with `clang_release` and
`xff_docs`; Linux requires the real FUSE test rather than silently omitting it.
The earlier lean-root 253-passed/10-skipped run is separate and is not described as
complete extension validation. Command arrays and complete logs are retained.

Regression cases in this change cover native callback boundaries/context separators,
callbacks replacing the sink, late read failures, headings and sink restoration,
late binary suppression, nonmatching scratch, and bounded reused result capacity.
Existing rg mode tests cover reduced/multiline output and parallel behavior. Final
integration on current main still requires the PR's own required CI, including
coverage, GCC, CI clang-tidy and sanitizers. Development results are not substituted
for those checks. The integration on `d8cf61cccd4b0efed5058bf6fa45228e47162b48`
(#1126) passes the three optimized semantic targets on Mac. The five C++ source/test
files remain byte-identical to the qualified experiment; this pre-publication check
is separate from the eventual pushed-head PR CI.

This completes the narrow O22c2h record-ownership experiment. O18 still needs global
byte admission/backpressure, ordered per-entry completion, cancellation/draining,
a head-credit reservation and an explicit oversized transactional-output contract.
This change does not bound a single file's output or overlap pipeline stages.

## Retained evidence

[`transactional-rg-records-20261107.json.gz`](transactional-rg-records-20261107.json.gz) is 5,079,513 bytes, SHA-256
`f32ec19cd3af284576a8f6af7e84fbedbf10dee48b044bfdc05909e0ffc7c466`. It contains 283 exact UTF-8 source/raw/log records with per-record byte counts and hashes.
Executables remain in the local/remote experiment directories; image identities and sizes
are included. The source of the native timing launcher and fixture generators is included.
