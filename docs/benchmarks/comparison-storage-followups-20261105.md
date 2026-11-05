<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Comparison storage follow-up decisions

Keep the existing comparison arena and duplicate-index implementation. Three private candidates
were measured on both hosts. The smaller initial arena regresses stable Mac cases; the experimental
single-probe map trades small Linux costs for long-key gains; the key-only set saves storage but
does not establish a sufficiently reliable cross-platform runtime improvement. None is proposed
as a production runtime change. Complete source snapshots, rejected observations and qualification
results are retained so these experiments do not need to be rediscovered.

## Common protocol and boundaries

The immutable experiment base is `e7e75e220dc8a58fce1cde59ba6e7c13c9029c49`, with the separately
qualified empty-scope early return applied equally to parent and candidate. This is private
experiment provenance, not current public-head readiness. Parent, candidate and identical-parent
copy have frozen binary and source hashes. No implementation changes occur during a matrix.

Each collection matrix has 40 cases: 10/100/1,000/10,000/100,000 entries, two key/path lengths,
and empty-owner temporaries, owned temporaries, owned borrowed inputs and borrowed duplicates.
Inputs are prepared outside timing; collection allocations, ordering and destruction are timed.
An opaque escape and memory clobber retain the complete collection. Three balanced sessions each
use nine randomly ordered repetitions and retain the fastest seven. Percentages are the median
of paired session elapsed-time ratios minus one; negative improves. Independently medianed times
need not divide to the reported percentage. Identical-parent-copy drift is shown separately and
is never subtracted or used to erase an unfavorable result. Linux processes use allowed CPU
affinity; Mac worker requests do not claim hardware pinning. Builds and checks do not overlap
timing on the same host. Sanitizer elapsed times are not performance measurements.

The arena experiment varies full path prefixes. The single-probe and key-set experiments instead
put the long prefix in the actual relative key to exercise hashing. Both parent and candidate were
rebuilt and measured for that revised fixture. Do not combine those matrices as if their inputs
were identical, or attribute full-path effects to long relative-key hashing.

## Smaller initial arena (O16a4b)

The prototype starts with 64 KiB, doubles up to 1 MiB and preserves the existing arena ownership
and no-exception contract. The production control uses fixed 1 MiB blocks. It passes three focused
release suites on each host. It is rejected before full native/sanitizer qualification. No peak-RSS
benefit is inferred merely from the configured block size.

Mac long-path 10k cases cost +7.84%, +11.20%, +10.00% and +10.18%, with corresponding parent-copy
movement of +0.63%, +0.64%, +2.00% and +0.42%. Linux is mostly neutral or slightly faster. These
stable Mac costs suffice to reject the policy without relying on noisy 1k cases or hiding the
costs behind aggregate medians. Keep the existing fixed 1 MiB comparison arena.

## Experimental single-probe map

The pinned Abseil public experimental `lazy_emplace` API can combine duplicate detection and
insertion. The constructor is called exactly once; the index is not accessed before construction,
and the slot is not erased from its callback. Arena-owned keys, first-wins duplicates, retained
backend ownership and sorted output remain unchanged. No new dependency, timer or worker is added.

The mechanism generally improves long Linux keys by roughly 6-13%, while short ten-entry Linux
cells cost roughly 5-8.5%. Mac mechanism medians improve, but several 1k cases have substantial
allocator/copy variation. Three focused release suites pass on each host. The mixed small-case
tradeoff and experimental API are not adopted; native, full-suite and sanitizer qualification are
not claimed for this variant. A future stable one-probe design needs its own complete measurement.

## Key-only duplicate set

The map value is never read: lookups only ask whether a relative key exists, while ordering and
entry retrieval use separate retained records. The prototype replaces that map with the existing
stable Abseil `flat_hash_set<string_view>` and keeps the same contains/insert control flow. It
preserves arena-owned key lifetimes, first-wins duplicates, backend ownership and ordered lookup.
The Bazel dependency changes narrowly from the map target to the set target. No experimental API,
new lock, callback wrapper, worker policy, exception path or user-visible flag is introduced.

The scenario medians below include all ten size/key cells, including slower observations. They
summarize the mechanism and do not establish whole-command improvements.

| Scenario                  |  macOS | macOS copy |  Linux | Linux copy |
| :------------------------ | -----: | ---------: | -----: | ---------: |
| empty-temporary           | +0.59% |     +0.77% | -0.60% |     -0.09% |
| owned-temporary           | -0.96% |     -0.19% | -0.56% |     +0.14% |
| owned-borrowed            | -2.11% |     +0.60% | -0.51% |     -0.05% |
| owned-borrowed-duplicates | -0.70% |     +0.63% | -0.68% |     +0.09% |

The whole-engine matrix has 48 cases per host: counts-only plus five ordinary summary/scope
controls, 10/100/1k/10k entries and 1/3 requested workers. All preflights pass before timing. Linux
initial ten-entry counts-only with three workers costs +12.51% (copy -2.45%), and 1k costs +5.52%.
A separate predeclared twelve-case confirmation uses the same exact binaries and keeps all
initial observations: those two costs become +1.81% and -2.50%, respectively. It does not establish
that all unfavorable observations were noise. In particular, confirmation serial 10k ordinary
and category summaries cost +8.11%/+8.05%, with copy -1.34%/+2.86%. Their initial medians were
-1.22%/+0.29%. Keep both rounds separate rather than selecting the favorable one or presenting
a pooled median as a clean win. Mac serial 10k ordinary cells cost roughly 2-4.6%, alongside
roughly 3.5-6.3% copy movement. Attribution remains limited.

Forty native cases per host cover broad/deep 10/1k/10k files, stored ZIP 10/1k members, compressed
TAR 10/100 members, 1/3 workers, status and summary output. Both modes explicitly select all
comparison statuses, so these archive summary commands also emit status records. Every invocation
including warmups checks complete stdout, exit status and empty stderr against the parent. The
worst case is +2.12% Mac (copy +0.44%) and +2.02% Linux (copy -2.68%). These results are mostly
near baseline. The 1k-member ZIP remains roughly 2.47 seconds on Mac and 0.76 seconds on Linux;
changing the duplicate index does not fix per-member archive traversal.

An additional eight native cases per host use 100k-file broad/deep trees, 1/3 workers, all-status
output and separately quiet counts-only `--compare=summary`. All run three balanced sessions with
nine repetitions and seven retained. Mac elapsed differences span -0.75% to +0.72%; Linux spans
-1.04% to +0.02%. The large native elapsed result is essentially neutral rather than an
across-the-board speedup. Peak RSS is sampled separately; negative below reduces memory.

| Tree  | Workers | Output      | Mac RSS delta MiB | Mac copy MiB | Linux RSS delta MiB | Linux copy MiB |
| :---- | ------: | :---------- | ----------------: | -----------: | ------------------: | -------------: |
| broad |       1 | status      |             +1.61 |        +0.16 |               -1.94 |          +0.02 |
| broad |       1 | counts-only |             -0.31 |        +0.00 |               -2.06 |          -0.33 |
| broad |       3 | status      |             -2.95 |        -2.91 |               -2.11 |          -0.10 |
| broad |       3 | counts-only |             -3.56 |        +0.00 |               -2.02 |          -0.02 |
| deep  |       1 | status      |             -2.97 |        -0.38 |               -1.94 |          -0.03 |
| deep  |       1 | counts-only |             -3.09 |        -0.28 |               -1.93 |          -0.01 |
| deep  |       3 | status      |             -3.12 |        +0.94 |               -2.15 |          -0.30 |
| deep  |       3 | counts-only |             -3.59 |        -0.09 |               -2.32 |          -0.22 |

Linux saves about 1.93-2.32 MiB across these cases. Mac deep cases save 2.97-3.59 MiB, but
broad serial status increases by 1.61 MiB. The Mac broad three-worker status reduction is almost
entirely mirrored by the unchanged copy. Do not claim a universal memory reduction. The prototype
release executable shrinks 448 bytes on Mac and 12,904 bytes on Linux. Those resource savings
are useful evidence, but they do not justify silently accepting the unresolved summary costs.
Keep the set candidate private; revisit only with a specific way to remove or explain those costs.

## Key-set correctness qualification

All 329 release targets pass on both hosts, including all nine extension packages and required
real FUSE tests on Linux. Four focused suites (comparison entries, totals, parallel comparison and
engine run) pass ASan on both, plus MSan, TSan and focused coverage on Linux. Tests retain first-wins
duplicates, ordering, empty/non-suffix keys, independent text storage and readable retained owners.
The private benchmark target is an experiment harness, not a proposed production build addition.

Linux final rebuilt CLI is whole-file identical to the measured candidate. The strict Mac hash
check fails and is retained as a failed check: 36 bytes differ. An independent Mach-O proof identifies
four digit bytes in two `N_SO` sandbox directory strings and their one dependent 32-byte
CodeDirectory page hash. It independently validates every signing page and both strict OS
signatures; every other file byte, including every executable/loadable section, matches. This is
a narrowly proven runtime identity, not a claim of whole-file equality or permission to normalize
other differences. Sources remain byte-identical to the frozen candidate snapshots.

- Mac measured CLI: 8,272,568 bytes; SHA-256
  `ecef2fb3b14f5b6aefc9b582e4aaec634008f191ed6db172c0d6e99f5c386330`.
- Mac qualified rebuild: SHA-256
  `296fc948f29691cc7514f26368b63261ff037dd1a7e6e407988f333df6e6ca0f`.
- Linux measured/qualified CLI: 18,416,704 bytes; SHA-256
  `54066b16e12f006c8882d4e53e7f482f9831dcc6360e55bb11efd3d8de4b8551`.

Actual current-head affected-header lint and public CI are not claimed for these private
prototypes. The new lint policy independently exposed pre-existing header findings; its repair
belongs to the preceding public change. A future production candidate must integrate those fixes,
run its actual affected-TU checks and obtain its own current-head CI before publication/merge.

## Retained evidence and next candidates

The accompanying archive retains source snapshots, driver hashes, original and confirmation
results, all warmup/output oracles, build/test logs, raw LCOV, binary identities, failed dependency
preflight builds and exact Mac identity proofs. It excludes executables, generated fixture trees
and full compile databases. Restored private review worktrees match the archived source hashes;
their new locations are not represented as the original measured build locations.

Further archive-read and traversal hypotheses remain separate; see the source-prefix and worker
policy notes in the archive. `ParallelCompare` already retains reusable workers, so these results
do not justify inventing a throwaway-thread fix. The three-worker Mac 100k deep comparison cost
exists in parent and candidate; it needs an isolated traversal/stat-policy experiment rather than
being attributed to this set substitution.

The deterministic [lossless evidence archive](comparison-storage-followups-20261105.json.gz) contains
561 text records (262 Mac and 299 Linux), 8,585,361 compressed bytes
and 75,021,865 uncompressed bytes. Every record is checked against its original byte count and
SHA-256. Archive SHA-256: `11d9ecde18e25e1bd7b3900f0518e03e50b4bda2baa56629a608db2279e67498`.
