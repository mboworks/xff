# Archive ancestor insertion hints, 2026-11-07

Keep the production index unchanged. Reusing an ordered ancestor lookup for hinted
insertion improves the Linux constructor screen, but Mac results are mixed and include
a substantial unchanged-flat-path cost. This trial does not qualify portable adoption.

## Isolated mechanism

The candidate retains owned ancestor keys, default mapped-node construction and its
subsequent assignment. Member insertion, duplicate overwrite, direct-child counts,
synthesis flags and inode order stay unchanged. Only the missing-ancestor search
changes: `lower_bound` supplies the insertion position to `try_emplace`, avoiding a
second ordered lookup. This differs from the earlier rejected borrowed-key and direct
mapped-value-construction trials; neither change is included here.

A nonempty ancestor is a strict prefix of the already-stored member. Under the map's
`std::less<>` ordering, that member is greater than the ancestor, so `lower_bound`
cannot return `end()`. The lower bound plus the reverse comparator determines equality.
No other map mutation happens before the hinted insertion.

## Screen and provenance

The unchanged frozen baseline is main `9bf1aad5`, reused byte-for-byte from the preceding
O27 experiment. Candidate/baseline configurations, compiler version (Clang 23.1.4) and
recorded source hashes match except for the intended index implementation. Both use
`clang_release` and `xff_full`. The exact baseline image is repeated as a control.

Each host runs every one of the 28 public construction/destruction cases: flat,
wide-short/wide-long, deep-short/deep-long and chain archives, with 10 through 100,000
members where supported; the chain cases stop at 1,000. This includes real public
archive parsing/source/index/destruction work, not a hot private map lookup. Complete
fixture metadata/path/directory oracles run before timing. Every oracle passes.

This is one initial screen with three raw repetitions per case/participant; retain
the fastest two and average them. The table shows the median case change per shape,
not elapsed-time weighting. Linux is pinned to one allowed CPU; Mac is unpinned.
No host builds or sanitizer checks run concurrently with timing. Positive is slower.

| Shape      | Mac candidate | Mac same-image repeat | Linux candidate | Linux same-image repeat |
| :--------- | ------------: | --------------------: | --------------: | ----------------------: |
| flat       |        +3.46% |                -1.80% |          -4.35% |                  +0.38% |
| wide-short |       +10.53% |                -1.09% |          -3.20% |                  +0.59% |
| wide-long  |        +2.65% |                +0.14% |          -2.89% |                  +0.55% |
| deep-short |        +1.98% |                -4.83% |          -3.93% |                  +0.35% |
| deep-long  |        -3.71% |                -0.86% |          -2.89% |                  +0.48% |
| chain      |        -4.40% |                -0.74% |          -4.28% |                  -0.88% |

The Mac flat/100,000 case costs 13.97% while the identical baseline repeat changes
-0.79%. The modified ancestor branch does not execute for flat members; no causal
explanation is claimed from source inspection alone. Keep that counterexample and
the noisy tiny cells, including Mac deep-short/10 at +30.66% (repeat -21.61%).
Linux's consistently favorable shape medians do not erase the Mac costs.

## Correctness and decision boundary

Both hosts pass optimized `archive_fs_test`, `archive_directory_metadata_test` and
`archive_openbytes_ownership_test`. The additional in-memory regression checks
synthesized ancestors alongside lexically adjacent member keys and their contents;
existing tests cover explicit/synthesized replacement, duplicate/direct-child counts,
inodes, metadata and retained ownership. Test source, Mac XML and the Linux complete
build/test log are retained. This is focused prototype validation, not a claim of a
full release/sanitizer/current-head CI run.

The first Mac attempt failed before any timing because copied baseline files lacked
executable permissions. Only their mode was corrected, failed setup logs remain,
and the complete corrected screen is separate. No source or measured image bytes
changed as a recovery, and no successful screen was repeated until favorable.

The predeclared next stage required a promising cross-platform screen. That condition
is not met, so no full three-session nine-repeat/retain-seven population, native
82-case CLI/88-case listing controls, sanitizer expansion or runtime PR follows.
Retain the candidate rather than hiding the negative result or changing production
code on a Linux-only improvement.

## Evidence

[`archive-ancestor-hint-20261107.json.gz`](archive-ancestor-hint-20261107.json.gz) is 115,195 bytes, SHA-256
`8359b99d815469270bbdb90aef7ec8dd11f9156ef37d6b7c5a879672badfaad7`. Its 53 exact source/raw/log records include both complete populations,
copy controls, failed setup and per-record byte counts/hashes. Measured executables
remain in the local/remote task directories with image identities recorded.
