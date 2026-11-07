# Archive index insertion qualification, 2026-11-07

Keep the existing archive index insertion code. Delaying ancestor-key ownership was
not selected after its complete earlier trials; direct mapped-node construction also
does not justify adoption in a new screen on current main. The remaining hinted
ancestor-insertion idea is separate and unmeasured.

The [lossless evidence archive](archive-index-insertion-20261107.json.gz) retains the
new source patch, both participants' source and image identities, every screen
observation, the analysis drivers and build/test logs. It also embeds the original
Linux and Mac evidence archives for the earlier deferred-ownership experiment.
Their different parents and protocols remain separate; do not combine their numbers.
The combined archive is 4,525,832 bytes, SHA-256
`ef0f035c7369a11f8191f9b46407b6682cfb93ce1190e70743172fd0ed360527`.
Its embedded earlier archives retain their original compressed bytes and hashes.

## Direct node construction

Both participants start at main `9bf1aad510d843d1d6338fbdf23107a30d339730`
(#1125). The candidate changes only `ArchiveFileSystem::Index`:

- Construct a new member's mapped `Node` directly through `try_emplace`.
- On an existing key, retain the original overwrite and direct-child-count preservation.
  A failed `try_emplace` does not move from its value argument.
- Construct a proven-missing synthesized ancestor directly through `try_emplace`.

The original ancestor-key ownership, lookup count, `std::map`, inode order and source
ownership remain unchanged. This is a distinct mechanism from the rejected borrowed
ancestor lookup. It removes source-level default construction followed by assignment;
that fact alone does not prove that the optimizer emitted fewer costly instructions.

Both hosts build with `--config=clang_release --config=xff_full`. Frozen images are
recorded before measurement. Linux uses one allowed logical CPU; Mac runs unpinned.
No build, sanitizer or lint runs alongside a timing population on the same host.

The existing public construction benchmark checks all fixture paths, file types,
metadata and directory counts before timing. It measures complete `OpenSource`
construction and destruction, including archive parsing and member materialization.
Fixture generation and source ownership stay outside timing; this is not a private
index-only benchmark. Its 28 cases cover flat, short/long wide and deep parents, and
chains. Counts are 10, 100, 1,000, 10,000 and 100,000, with chains capped at 1,000.

The predeclared screen uses one session, three repetitions per case and participant,
and the mean of the fastest two. Participants run baseline, candidate and a second
copy of the exact baseline image, with randomized case interleaving inside each
invocation. The table gives the median of case percentage changes within each shape;
it is not aggregate throughput. Negative is faster. All 28 individual results and
every raw repetition are retained.

| Shape      | Mac candidate | Mac baseline repeat | Linux candidate | Linux baseline repeat |
| :--------- | ------------: | ------------------: | --------------: | --------------------: |
| Flat       |        +0.70% |              +0.99% |          +2.34% |                -1.64% |
| Wide short |        -0.59% |              -0.60% |          +2.10% |                -0.22% |
| Wide long  |        +0.73% |              -0.32% |          +0.70% |                +0.02% |
| Deep short |        -1.74% |              -2.69% |          +0.76% |                -0.45% |
| Deep long  |        +0.43% |              -0.27% |          +1.11% |                -0.76% |
| Chain      |        -5.56% |              -5.82% |          +0.60% |                -0.38% |

No Linux shape improves at this estimator. Mac's small apparent gains mostly track
its unchanged-image control. Its tiny cases also have large disagreement: wide-short
10 is +66.79% while the repeat baseline is -23.65%; deep-long 10 is +48.78% with a
+40.10% repeat. Retain those observations without assigning them an unmeasured cause.
At larger counts the candidate has no convincing independent gain on both hosts.

The screen fails its predeclared requirement for an attributable cross-platform
improvement. Stop here: no full three-session timing population, full-root test suite,
new sanitizer qualification or runtime PR is claimed for this candidate. Both hosts
passed the three focused optimized archive targets (`archive_fs_test`,
`archive_directory_metadata_test`, `archive_openbytes_ownership_test`), and all screen
fixture oracles passed. These results justify retaining the baseline, not a claim
that the candidate can never win on any machine.

## Earlier deferred ancestor ownership

The earlier contains-first candidate delayed constructing an owned ancestor key until
a transparent lookup proved it missing. It completed all 28 public construction,
82 native command and 88 hot-listing cases on each host, in three balanced sessions
with nine repetitions retaining seven and an unchanged-parent image control.
Its own final review found modest lifecycle gains without an attributable
cross-platform whole-command benefit beyond control variation. It remained unselected.

That experiment's exact parent, source variants, raw populations and narrower
validation claims are retained in its original embedded archives and plan. They are
not replaced by the new direct-construction screen. A duplicate borrowed-key prototype
was identified before any new build or measurement and was not rerun.

## Remaining work

A transparent `lower_bound` plus hinted insertion can avoid a second search for a
missing ancestor. It remains an independent candidate. Preserve duplicate replacement,
synthesized-directory state, child counts and inode order; returned sources and map
keys must retain their existing ownership. A future B-tree layout still needs its
own reference-invalidation and construction/RSS audit.

O30's owned archive-input transfer is already implemented by #1122. Its exact merged
commit `0e95e191ba7d7ee3769217e7fbf82a825ff9b90d` passed
[post-merge main Test run 42276661524](https://github.com/mboworks/xff/actions/runs/42276661524),
including both operating systems, sanitizers, coverage and changed-file lint. Its
stale create-a-PR TODO is closed; the initial noisy Mac observations remain in O30's
existing raw evidence.
