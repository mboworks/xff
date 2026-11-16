# Performance follow-up after the extended viewer

Finish PR #1153 and prepare the complete `extended-production-v2` history first.
Performance implementation and timing are deferred during that work. This ledger preserves
the completed candidate and the next investigations; it does not claim they have all qualified.

The new history covers 130 merged revisions, ending at PR #1130. It does not measure the
later changes below. Its fixed-reference and traditional-tool views retain their actual
Drogon/Linux provenance; they are not macOS measurements or current-main qualification.
The [dense history handoff](benchmark-dense-history-20261115.md#github-historical-continuation)
records the newest-first, resumable GitHub backfill follow-up and its ten-revision batch policy.
It is collection/publication work, separate from the performance candidate below.

## First separate performance PR: TAR pathname conversion

**Implemented; macOS qualified; Linux validation outstanding.** This is the Unicode/wide-character
hotspot discovered while examining the TAR comparison, not an unimplemented proposal.

- Source base: `42d87175e0e8261493f3bfcf1f9b82c5626edc9f` (main through #1159).
- Candidate commit: `0c5aaa91be`, local branch
  `performance/tar-pathname-conversion-20261115`.
- Preserved checkout on this Mac:
  `/Users/marcus/Documents/github/mboworks/xff-2/bazel-tar-pathname-conversion-20261115`.
- Change: the pinned libarchive TAR header reader checks an ASCII pathname's final byte
  directly instead of eagerly converting the entire pathname to wide characters solely
  to recognize a trailing slash. High-bit bytes and ESC retain the original conversion
  and fallback path. A later wide-pathname request still converts lazily.
- No Unicode support is disabled. UTF-8, invalid multibyte names, C/UTF-8 locales, full
  100-byte header names and regular-header/trailing-slash directory detection are covered.
- All seven affected macOS archive suites passed. Both executables are full production
  ThinLTO builds, with immediate configured-query/copy and retained executable hashes.
- Two independent RAM-backed populations contain 2,688 output-checked invocations. Deep
  100,000-member TAR listing repeats at about 10% faster with 1/3 requested workers. Small
  cases overlap observed control variation. One adverse repeated control is retained;
  this is not a universal 10% speedup or a demonstrated Linux improvement.
- Candidate qualification and all raw/build/ownership evidence are committed in that checkout
  as `docs/benchmarks/tar-ascii-pathname-20261115.md` and
  `docs/benchmarks/tar-ascii-pathname-20261115.json.gz`. Frozen binaries and logs remain in its
  ignored `bazel-pathname-investigation/` directory. The root handoff is
  `bazel-tar-pathname-conversion-status-20261115.json`.
- A portable Git bundle, the qualification report, compressed evidence and status handoff are
  also retained on drogon in `/home/marcus/Documents/xff-performance-follow-up-20261116/`.
  `tar-pathname-candidate.bundle` contains the candidate branch and requires the base commit
  above; SHA-256 is `2cea6e3865584a93f7d0f7970212e95908b38dbc458be6a02d63d3500a75b434`.
  This preserves the implementation for another session without publishing a performance PR.
- Next gate: isolated Linux baseline/candidate production builds, the seven affected archive
  suites, and paired tmpfs measurements with the same independent oracle and baseline control.
  Preserve failed populations and recheck fixture, tool, launcher and executable hashes.
  Build and test before timing; do not combine samples with viewer history.
- No PR has been opened and no candidate code has been pushed. Open its separate performance
  PR immediately after that remaining qualification succeeds. Keep its code out of #1153.

## Already landed after the historical endpoint

These changes are on main. Do not reopen them as new candidates or attribute the PR #1130
chart endpoint to their implementation. Their individual reports retain the measured source
identities, adverse cells and qualification limits.

- #1151 (`ed883ecc52`): avoid unused evaluation and shard setup during ordinary walks.
- #1152 (`a600f8e307`): adapt the ordered matching pipeline to observed output volume.
- #1154 (`ad3be8a12e`): reduce Linux raw filename output overhead.
- #1155 (`94e785f02f`): prepare immutable predicate guards once per expression.
- #1156 (`cc30ffd2f8`): lazily batch sustained listing output with a bounded buffer;
  [qualification](benchmarks/bounded-stdout-buffer-20261109.md).
- #1157 (`91437fef41`): use caller-owned buffers for streaming line scans;
  [qualification](benchmarks/direct-line-scan-spans-20261109.md).
- #1158 (`81f5d87315`): prepare streamed archive member names once;
  [qualification](benchmarks/prepared-member-name-20261109.md).
- #1159 (`42d87175e0`): retain first-member metadata for duplicate archive paths. This is a
  correctness fix in the candidate baseline, not an independently established speedup.

## Planned investigations, in order

1. **Reconcile current-main gaps.** Measure current main against traditional tools on the
   complete grid before selecting more implementation changes. In the retained PR #1130
   population, listing, native-case filtering and metadata remain slower in many cells;
   content matching and larger TAR listing already win in many cells. Historical ratios
   identify tasks to investigate, not the costs of today's implementation.
2. **Startup and ordinary listing.** Separate fresh-process startup, traversal, expression
   dispatch and output costs. Prioritize the 100/1,000-file deficits and 1/3-worker cases;
   preserve native case behavior and exact output semantics. No thread threshold or arena
   change is selected without current-main profiles and a paired qualification.
3. **Archive index and first output.** Reprofile current main after the pathname patch.
   Separate header decoding, member-name preparation, index insertion and traversal. The
   old Linux profile also contains string/tree comparison costs; streaming or lazy index
   alternatives must preserve duplicate-member metadata, directory and archive semantics.
4. **A genuine compound workload.** Qualify an equivalent task that benefits from combining
   operations in one process. The existing TAR reference is one GNU tar invocation; adding
   unnecessary reference processes would not establish a real advantage. Fix commands and
   an independent oracle before collecting history, then assign a new measurement contract.

Each selected optimization needs its own source and executable identities, both-host
correctness results, raw paired measurements, controls and limitations. Retain rejected
trials so later sessions do not repeat them. The existing
[performance analysis](performance-analysis.md) remains the detailed record of older trials.
