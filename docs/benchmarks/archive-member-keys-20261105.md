<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Archive member-key ownership and direct-child checks, 2026-11-05

**Decision:** adopt borrowed normalized keys for immediate lookups and the simpler
parent-length test inside the existing bounded directory range. Both platforms
show repeatable operation-level gains with complete correctness qualification.
Whole-command effects remain modest, and the listing tradeoffs below remain
visible; this is not a universal archive-command speedup claim.

## Change and ownership boundaries

Normalization strips leading `./` runs and trailing slashes by selecting a
contiguous subview. Immediate stat, access, link and cache lookups need no new
owning key. The index still owns its stored strings. Both escaping
`MemberReadSource` boundaries explicitly construct an owning string, preserving
later cursor creation after the caller path and filesystem are destroyed.
The public API, node layout, ordered map, worker policy, read-only protections,
error classifications and archive naming semantics are unchanged.

The listing interval already contains only the selected subtree. A member whose
parent has the directory key's length therefore has that exact parent. The
unbounded archive root uses an empty parent and empty key. A length check avoids
the redundant byte comparison without introducing another index, successor
search, stored count, template mode, B-tree, capacity hint, lock or PGO setting.

Four committed behavioral cases cover bounded caller views, normalized long
keys, links/cache/access/errors, delayed-source lifetime, and equal-length or
adjacent-prefix sibling exclusion in ordinary and observed listings. The exact
new tests pass the original runtime parent on both hosts before installation.
A separate ordered-index model checks 160 datasets, 25,054 directory keys and
59,256 bounded members, including absolute and repeated separators. That model
supports the range proof; it does not replace the C++ tests or actual archive
decoding.

## Keep all three variants separate

- **Initial borrowed-key view:** focused checks and all 14 lookup screens pass,
  but Linux flat listing controls regress around 48-56%. Reject this form.
- **Explicit empty-parent guard:** both complete 82-command, 14-lookup and
  88-listing trials pass all output oracles, as do both 333-target release suites.
  Linux changed coverage is 16/16 lines but 5/6 branches. The final nonempty
  parent-inequality outcome is impossible inside the bounded subtree. Preserve
  this failed qualification; do not invent an input or exempt the branch.
- **Bounded parent length:** independently measure the simpler final code with
  its own frozen source and images. Changed coverage is 15/15 lines and 2/2
  branches. The following full results belong only to this final variant.

The initial ordinary Linux image has an empty-parent `bcmp` call; an untimed
flat-1,000 diagnostic finds about 23.32%/38.96% parent/candidate self-time in
`__memcmp_evex_movbe`. Diagnostic invocations are excluded from performance
populations. They motivate the next hypothesis, not a causal throughput claim.

The final Mac screen has one short cached-read cell 0.968% slower (about 0.422ns)
and a flat-10,000 metadata cell 70.184% slower. Both remain retained. They were
not selectively repeated or relabeled as passes: the separately planned full
balanced trial below determines repeatability. Linux screen native controls
and every slower full-trial cell likewise remain present.

## Measurement contract

All three variants use the original qualified compiled target-demand plus empty
CCTZ-fragment parent, based on #1110 with those explicit overlays. No percentage
is added to a different variant's percentage. Each full population uses three
balanced sessions, nine repetitions retaining the fastest seven per case, and
an independently copied byte-identical parent. Fixture creation, index/cache
setup for hot-operation tests, calibration, warm-ups and complete-value
preflights are untimed. Complete command stdout/exit/stderr checks remain exact.
No sanitizer or sampling profiler is present in timed images.

Tables show the median of the session-paired elapsed-time changes. Negative is
faster. Copied-parent drift is separate and never subtracted from the candidate.
Linux uses explicit CPU affinity; native commands request one/three workers.
Mac requests one/three workers without claiming CPU pinning. The full wrapper
records selected locale/timezone environment values as observations; it changes
none and makes no retrospective environment-equality claim for older screens.

## Targeted member operations

All 14 operations improve in each of the three sessions on both hosts. This does
not mean every individual repetition is faster. Prefix length is the directory
component length, not an output byte count.

| Operation      | Prefix bytes |    Linux | Linux copy |      Mac | Mac copy |
| :------------- | -----------: | -------: | ---------: | -------: | -------: |
| stat           |            3 | -32.961% |    +0.065% | -14.019% |  +0.452% |
| stat           |           96 | -36.968% |    -0.597% | -43.807% |  +0.490% |
| access         |            3 | -33.516% |    -0.068% | -23.244% |  +1.163% |
| access         |           96 | -38.790% |    -0.577% | -51.105% |  -0.433% |
| readlink       |            3 | -35.133% |    -0.166% | -10.327% |  -0.161% |
| readlink       |           96 | -26.428% |    +0.049% | -30.521% |  -0.580% |
| readdir        |            3 | -16.750% |    -0.548% |  -8.832% |  -4.049% |
| readdir        |           96 | -13.766% |    -2.105% | -15.268% |  -1.710% |
| metadata       |            3 | -14.826% |    -2.619% |  -4.464% |  -1.185% |
| metadata       |           96 | -13.753% |    -2.657% | -13.051% |  -1.576% |
| cached-content |            3 | -26.618% |    -0.040% |  -8.398% |  +0.665% |
| cached-content |           96 | -29.593% |    -1.007% | -28.096% |  +0.905% |
| content-source |            3 | -25.703% |    -0.177% |  -7.309% |  +1.506% |
| content-source |           96 | -35.731% |    -2.515% | -36.740% |  +0.586% |

## Whole commands

All 82 command cases complete every output oracle in every session. Their
archive format/tree/file-count/worker matrix and native listing controls remain
unchanged. Parsing, index construction, output construction and startup still
contribute, so lookup gains do not translate directly to complete invocations.

| Group                         | Cases |   Linux | Linux copy |     Mac | Mac copy |
| :---------------------------- | ----: | ------: | ---------: | ------: | -------: |
| archive/metadata              |    24 | -0.272% |    +0.608% | -0.015% |  +0.238% |
| archive/metadata-members-only |     4 | -0.214% |    +0.620% | -0.665% |  -0.202% |
| archive/names                 |    34 | -0.697% |    +0.117% | +0.215% |  -0.159% |
| archive/summary-members-only  |    12 | -0.002% |    +0.269% | -0.431% |  +0.023% |
| native/names                  |     8 | +0.329% |    +0.164% | +0.431% |  +0.977% |

## Listing controls and counterexamples

Both hosts complete all 88 ordinary/observed, flat/wide/deep/chain listing cases.
The selected original counterexamples are below; all 88 cells and every raw
repetition remain in the capsule. Gains in a wide/flat case do not erase a
deep-root slowdown, and copied-parent variation is not a correction factor.

| Listing case               |    Linux | Linux copy |      Mac | Mac copy |
| :------------------------- | -------: | ---------: | -------: | -------: |
| flat/root 10 entries       | -80.578% |    +2.281% | -12.925% |  -1.581% |
| flat/root 10 metadata      | -76.739% |    +2.621% | -10.496% |  -1.387% |
| flat/root 1,000 entries    | -74.763% |    +2.854% |  -9.483% |  -2.158% |
| flat/root 1,000 metadata   | -70.086% |    -0.016% | -28.117% | -18.422% |
| flat/root 10,000 entries   | -71.546% |    +0.200% |  -9.057% |  -3.090% |
| flat/root 10,000 metadata  | -65.670% |    -1.646% |  -3.999% |  +1.633% |
| flat/root 100,000 entries  | -55.214% |    +0.166% |  -5.529% |  -0.742% |
| flat/root 100,000 metadata | -40.607% |    -0.415% |  -3.027% |  +0.645% |
| wide/root 100,000 entries  | -30.968% |    +6.582% |  +7.222% |  +1.540% |
| wide/root 100,000 metadata | -24.684% |    +3.836% |  +4.928% |  +6.233% |
| deep/root 100,000 entries  |  +2.204% |    -0.607% | +18.988% | +10.479% |
| deep/root 100,000 metadata |  +5.144% |    +2.289% | +19.220% |  +8.539% |

Linux deep/root 100,000 entries and metadata are slower in all three sessions:
the median changes are +2.204%/+5.144%, with copied-parent changes
-0.607%/+2.289%. Retain that specific tradeoff rather than calling all listings
faster. No claim is made that the old Mac screen's cause has been identified.

The Mac full deep/root 100,000 entries/metadata medians are +18.988%/+19.220%:
first/second sessions are slower, while the third is about 18% faster. Copied
parents also move substantially. The original flat-10,000 metadata screen's
+70.184% does not persist as a full-trial median (-3.999%), but remains retained.
These disagreements require more evidence, not deletion of the original cells.

A single predeclared independent corroboration uses the exact same qualified
images and all 12 ordinary/observed flat/wide/deep root cases at 10,000/100,000.
It repeats the balanced three-session, nine-retain-seven protocol without any
source change or correctness rerun. It remains a separate population; it is
not pooled with or substituted for the full 88 cases. All 12 Mac medians are
faster, and both large deep-root cases improve in each of its three sessions.
Linux large deep-root medians remain slower, making that tradeoff credible.

| Corroboration case         |    Linux | Linux copy |      Mac | Mac copy |
| :------------------------- | -------: | ---------: | -------: | -------: |
| wide/root 100,000 entries  | -33.366% |    +0.539% |  -7.127% |  +0.012% |
| wide/root 100,000 metadata | -27.089% |    +2.560% |  -6.507% |  -4.233% |
| deep/root 100,000 entries  |  +4.941% |    +0.175% | -14.051% |  -0.404% |
| deep/root 100,000 metadata |  +4.632% |    -4.498% | -11.768% |  -1.593% |

The corroborating Mac flat-10,000 metadata median is -4.586%; Linux is -68.162%.
No cause is assigned to the Mac population-to-population reversal. The choice
retains a repeatable member-lookup improvement and mostly modest whole-command
changes with an explicitly documented Linux large-deep-root listing cost. It
does not promise a faster result for every archive or allocation history.

## Qualification and size

Both platforms pass all 333 release targets and the relevant archive ASan cases;
Linux also passes the relevant MSan/TSan cases. Actual clang-tidy checks cover
both newly changed translation units; nine earlier affected units remain
byte-identical to their successful checks. Canonical Linux coverage passes all
15 changed lines and both changed branch outcomes without an exclusion or
threshold change. Its earlier successful run is reused only after exact source
and LCOV hash verification, not rerun on unchanged code for another result.

Actual current compile actions prove Clang 23.1.4, ordinary O2/ThinLTO,
exceptions disabled, and no PGO or sanitizer flags. Each final rebuilt CLI,
listing benchmark and member benchmark exactly matches its frozen measured
image. Local development evidence does not replace the eventual PR's required
current-base CI or post-merge checks.

| Release CLI | Parent bytes | Candidate bytes | Change bytes |
| :---------- | -----------: | --------------: | -----------: |
| linux       |   18,437,472 |      18,433,032 |       -4,440 |
| macos       |    8,274,840 |       8,274,840 |           +0 |

## Retained evidence

[The lossless evidence](archive-member-keys-20261105.json.gz) includes all three
variants and the independent corroboration on both hosts under eight prefixes.
Every named record has its
byte count and SHA-256 checked before deduplication. It retains source snapshots,
all commands, preflights, raw timings, copied-parent controls, complete analyses,
source/image/compiler proofs and initial qualification failures. Executable
images remain on their original hosts with retained identities, rather than
being embedded as runnable downloads. No failed stage is rewritten into a pass.

Index insertion/ownership experiments and forward-separator classification are
separate follow-ups. Their untested prototypes are not included in this runtime
change, and this decision is not delayed to add another unmeasured hypothesis.
