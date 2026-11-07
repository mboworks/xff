# Reuse unchanged raw publication payloads

Select byte-exact reuse for non-interned JSON archives. Keep chart packing unchanged.
The complete two-report expand/compact cycle improves roughly 52% on macOS and 47%
on Linux when the retained raw observations are unchanged. Changed-input costs remain
visible: up to 0.99% Mac and 3.77% Linux; fresh-input controls stay within 1%.
These are fixture-cycle results, not total hosted publication-time measurements.

## Mechanism and decision

Publication expands existing reports, artifacts, normalization records and charts, then
compacts them again. Previously every expanded file was recompressed even when its
original archive remained alongside it. For non-interned JSON, decompress that retained
archive and compare exact bytes with the authoritative expanded file. On equality,
keep the original compressed bytes and remove only the expanded copy. Changed, missing,
or corrupt retained archives use the existing compression path. Numeric spellings,
whitespace, booleans, signed zero and original observation bytes remain significant.

The first experiment also reused packed charts. Unchanged charts improved 67-72%, but
modified charts regressed 27-33% because comparison decoded the old packed structure
before repacking the new one. Reject that broader implementation. The selected code
never adds that comparison to chart packing. This does not change the storage schema,
retention policy, chart values, renderer, source identity, or official measurement series.

## Paired measurements

The fixtures are the exact public Linux and macOS report, packed chart, and HTML payloads
from main run `42254867488`, attempt 1, revision `2487927cc6`. Original compressed inputs,
source URLs/hashes, source variants, scripts, host details and every timing sample are in
the [lossless evidence capsule](unchanged-site-payloads-20261107.json.gz).
The raw report files expand to 8,714,448 and 8,806,698 bytes. Their charts expand to
5,096,770 and 4,310,810 bytes.

Each full-cycle case processes both reports through the real `expand_benchmarks` and
`compact_benchmarks` functions. Three sessions alternate parent/candidate order, discard
one warm-up round, and retain the mean fastest seven of nine measured rounds per role.
The table shows median session times and each session's percentage change; negative
means less elapsed time. Setup and output verification are outside the timer. Every
iteration verifies exact raw bytes, chart values/types, removal of expanded files, and
the packed HTML entry point. Unchanged candidate reports also retain identical gzip bytes.

| Host  | Input state | Parent ms | Candidate ms | Session 1 | Session 2 | Session 3 |
| ----- | ----------- | --------: | -----------: | --------: | --------: | --------: |
| macos | unchanged   |    742.66 |       354.10 |   -52.39% |   -52.27% |   -52.10% |
| macos | changed     |    818.86 |       825.93 |    +0.45% |    +0.99% |    +0.86% |
| macos | new         |    684.37 |       684.27 |    +0.08% |    +0.07% |    -0.01% |
| linux | unchanged   |    497.43 |       263.08 |   -47.02% |   -47.11% |   -46.89% |
| linux | changed     |    556.75 |       575.20 |    +3.32% |    +1.91% |    +3.77% |
| linux | new         |    445.53 |       448.34 |    +0.84% |    +0.44% |    +0.19% |

The changed case appends raw JSON whitespace and changes chart metadata; both variants
must produce the changed values. The fresh case starts without retained gzip files.
Only the unchanged and changed cases include expansion in the timer, identically for
both roles; fresh inputs are prepared expanded. Compare roles within each row.
Mac uses native temporary storage and Python 3.13; Linux uses native home-directory
scratch and Python 3.14. Linux's first attempted full-cycle run hit the `/tmp` tmpfs user
quota before producing a case; its final three sessions all use the same home scratch.
No prior experiments or evidence were deleted. Timings ran without concurrent builds.
Absolute times across hosts are not machine-normalized comparisons.

## Correctness and practical limits

All five relevant Bazel targets pass on Mac: storage, coverage storage, complete-site
compaction, benchmark history, and PR previews. The corresponding 48 direct tests pass
on both hosts; final malformed-DEFLATE coverage also passes separately on Linux and in
Mac's hermetic storage target. Linux verification checked all 198 transferred source
hashes before testing; final source and test hashes were checked again after that case.

Committed tests cover exact gzip reuse, changed scalar types/signed zero, missing and
stale archives, invalid gzip headers, truncated streams, invalid DEFLATE, and existing
expand/compact idempotence and chart-disagreement checks. A damaged retained cache never
replaces authoritative expanded input. Filesystem permission or write failures still fail.
The comparison holds one additional decompressed file temporarily; peak RSS was not
measured. No entire-site cache or additional persistent checksum files are introduced.

A sampled publisher log had 80.8 seconds expanding inputs, 172.1 seconds before reporting
zero decorated pages, and another 444.3 seconds in the subsequent artwork/compaction
interval. That interval is not an isolated compactor profile. The staged site was about
728 MB (499 MB benchmarks, 202 MB coverage), already above the 200 MB review threshold.
The separate deployment repair does not remove these ingestion costs. This change
addresses repeated compression only; historical rendering, the publication backlog,
recovery of rejected source events and storage reduction remain open.
