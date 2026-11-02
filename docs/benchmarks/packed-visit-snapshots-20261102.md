# Packed owned visit snapshots (2026-11-02)

## Decision

Retain one owned byte buffer with offsets for matcher-batch and deferred entry snapshots. The basename shares an exact suffix of the path and the root shares an exact prefix when possible; unrelated values are appended to that same allocation. Preserve opaque bytes, metadata observations, root identity and archive filesystem ownership. This reduces allocation traffic and retained text; whole-command elapsed gains are modest and are not uniform.

Keep the ordinary constructor. A separately measured no-inline constructor offers no consistent cross-platform advantage and is not adopted. No worker admission, batching, traversal, output, collection-arena policy, parser behavior or CLI option changes. This does not revive the rejected matcher input-buffer recycling API or introduce an arena/shared owner per batch.

## Lifetime and representation

Offsets are resolved against the current string on access, so short-string moves and vector relocation cannot leave self-referential views dangling. Copying produces an independent snapshot. Moving transfers its filesystem owner and resets the old text slices and observer. A borrowed Visit remains valid only while its snapshot stays in place. Metadata is copied; the traversal-only lazy loader is omitted exactly as in the former owned record.

Shard grouping borrows directly from these snapshots. It must not retain a view into a temporary materialized name. The existing complete/superfluous/dedup engine tests detected that error in the first prototype; the selected version passes them and the sanitizer checks.

Logical path/name/root lengths are unchanged even when storage overlaps. Existing collection budget accounting and matcher batch limits remain unchanged; deferred result sets retain their existing lifetime and size policy. This is fewer allocations, not a claim of a new global memory bound.

## Measurement contract

The exact measured parent is `5ca865a453acba2437909ec489c528b989bdf315` (the O08 directory-task slots implementation). LLVM 23.1.4 release builds use the normal `clang_release`/ThinLTO configuration. Source variants, executable hashes, commands, fixture descriptions, complete raw repetitions and reproduction scripts are retained below. Formatting and final test integration do not change the timed production mechanism.

The initial population has 120 native cases per host, three sessions, discarded per-variant warmups and nine interleaved rounds with the fastest seven retained. It contains 14,400 fresh invocations across both hosts, including warmups. Case order is forward, reverse and seeded; variant order rotates and reverses. Every invocation verifies independent path/size digests or complete collection/comparison records. Linux restricts affinity to the requested 1/3/10 CPUs on tmpfs. macOS uses warm APFS without affinity enforcement.

Fixtures include 10/1,000/10,000 flat files, broad and nested trees through 65,536 files, long collection names, and small/large two-root comparison controls. Added explicit-case listing/name controls are deliberately serial matcher controls: only content expressions and rg searches enter the current match pool. Actual rg file-selection searches exercise batching. Deferred shard fixtures contain 1,000/10,000 independent complete sets with long names and roots.

Two sessions of 36 in-memory RE2 workloads per variant retain nine repetitions/fastest seven with 1/4 worker allowances, without native affinity or an independent per-iteration oracle. These are separate engine controls, not fresh CLI startup timings. No build, sanitizer or lint runs concurrently with timing on the same host.

## Initial native results

Changes are medians of paired session ratios; negative is faster. Every cell remains in the archive, including slower controls.

| Fixture / task              | Workers | Linux change | macOS change |
| :-------------------------- | ------: | -----------: | -----------: |
| flat10 / files              |       1 |       -0.47% |       +0.32% |
| flat10 / files-exact        |      10 |       +2.33% |       -0.13% |
| flat10000 / rg-filter       |       3 |       -1.37% |       -0.10% |
| broad128x512 / rg-filter    |      10 |       -2.50% |       -0.09% |
| broad128x512 / files-exact  |       3 |       -4.50% |       -0.26% |
| nested8x8x64 / content      |      10 |       -2.94% |       +0.42% |
| shard1000 / shard-deferred  |       1 |       -1.76% |       -0.37% |
| shard10000 / shard-deferred |       1 |       -1.63% |       +0.29% |
| shard10000 / shard-deferred |       3 |       -1.77% |       -0.95% |
| broad128x512 / collect-long |       3 |       +1.93% |       -1.64% |
| compare-small / compare     |       3 |       +4.45% |       +0.34% |
| compare-large / compare     |       3 |       +9.06% |       -0.43% |

The initial Linux comparison controls include +4.45% and +9.06% medians; do not hide those samples. A five-session six-case confirmation includes a second label executing the identical baseline binary. That control itself varies, while several candidate costs remain. A further five-session eight-case constructor experiment uses byte-identical original Linux baseline/packed executables, and the comparison changes become -0.77%/+1.48%. Those costs are not stable across populations. The raw +4.09%/+3.84% intermediate confirmation and all duplicate-baseline results remain available; later runs do not replace earlier ones.

## Constructor controls

This supplementary population compares baseline, ordinary packed storage and forced out-of-line construction over five sessions/eight cases per host. It contains 2,400 fresh invocations, plus the earlier Linux duplicate-baseline confirmation has 900. Do not combine absolute times with the initial matrix. Rebuilding on macOS can change executable identity/size slightly; the manifests preserve every binary separately.

| Fixture / task              | Workers | Linux packed | Linux out of line | macOS packed | macOS out of line |
| :-------------------------- | ------: | -----------: | ----------------: | -----------: | ----------------: |
| compare-small / compare     |       3 |       -0.77% |            +0.08% |       +0.54% |            +0.18% |
| compare-large / compare     |       3 |       +1.48% |            +0.59% |       -0.62% |            -0.58% |
| flat10 / files              |       1 |       +1.31% |            +1.26% |       -0.18% |            +0.22% |
| flat10 / files-exact        |      10 |       +0.53% |            +0.82% |       +0.46% |            +0.44% |
| flat1000 / rg-filter        |       3 |       -0.18% |            +0.52% |       -0.48% |            -0.08% |
| flat10000 / rg-filter       |       3 |       -2.73% |            -2.34% |       -0.21% |            -0.25% |
| shard10000 / shard-deferred |       1 |       -1.77% |            -2.51% |       +0.20% |            +0.16% |
| shard10000 / shard-deferred |       3 |       -3.21% |            -3.91% |       -1.49% |            -1.02% |

## Final direct-construction confirmation

Scoped clang-tidy required direct emplace construction in the pending batch. This also removes the temporary snapshot move. Preserve that as a distinct final population: 32 representative native cases per host, three sessions, nine rounds/fastest seven, with the same output oracles. Both hosts also repeat the 36 in-memory controls. The final native population contains 3,840 fresh invocations including warmups. Existing full suites passed before this call-site refinement; the five affected release/ASan suites and Linux thread-safety suites were rerun afterward.

| Fixture / task              | Workers | Linux final change | macOS final change |
| :-------------------------- | ------: | -----------------: | -----------------: |
| flat10 / files              |       1 |             -1.38% |             +0.25% |
| flat10 / content            |      10 |             +1.92% |             +0.26% |
| flat10000 / rg-filter       |       3 |             -1.60% |             -0.48% |
| broad128x512 / content      |       3 |             -1.44% |             -0.62% |
| broad128x512 / rg-filter    |       3 |             -2.09% |             -0.32% |
| shard10000 / shard-deferred |       1 |             -1.38% |             -1.00% |
| shard10000 / shard-deferred |       3 |             -1.50% |             -0.07% |
| compare-small / compare     |       3 |             +4.23% |             -0.04% |
| compare-large / compare     |       3 |             -1.70% |             -0.04% |

## Counterbalanced-order confirmation

The qualification-only driver originally rotated the participants and reversed odd rounds. For two variants those operations cancel: every round within a case kept the same participant first. The original data remains separately identified and does not establish order-independent tiny gains. The corrected driver rotates without parity reversal and repeats the final 32 native cases, three sessions/nine rounds/fastest seven on both hosts. This adds 3,840 fresh oracle-checked invocations. The published benchmark runner already uses rotation without that reversal and is unaffected.

| Fixture / task              | Workers | Linux balanced change | macOS balanced change |
| :-------------------------- | ------: | --------------------: | --------------------: |
| flat10 / files              |       1 |                -0.64% |                +0.66% |
| flat10 / content            |      10 |                -1.81% |                -0.72% |
| flat10000 / rg-filter       |       3 |                -3.63% |                -0.58% |
| broad128x512 / content      |       3 |                -1.84% |                -0.64% |
| broad128x512 / rg-filter    |       3 |                -2.17% |                -0.65% |
| shard10000 / shard-deferred |       1 |                -2.09% |                -0.84% |
| shard10000 / shard-deferred |       3 |                -3.37% |                -0.75% |
| compare-small / compare     |       3 |                +0.10% |                -0.18% |
| compare-large / compare     |       3 |                +3.33% |                -0.08% |

The native helper also preserves user/system CPU time and child peak RSS per invocation. First-output latency and allocator-retained bytes are not measured separately in this qualification. No default, case-policy, worker-admission or output-order change is inferred from these results.

## Allocation traffic and footprint

ASan hooks count only coordinator allocations, with setup and assertions excluded. All snapshots stay alive through counting. These are requested bytes and allocation traffic, not peak RSS, allocator-resident memory, or sanitizer performance timing. Each case snapshots 1,000 entries, including one vector allocation. Short strings retain their allocation-free text path. Long independent name/root values exercise the fallback.

| Host / text length | Overlap  | Old allocations | Packed allocations | Old requested bytes | Packed requested bytes |
| :----------------- | :------- | --------------: | -----------------: | ------------------: | ---------------------: |
| macos / 1          | overlap  |               1 |                  1 |             232,000 |                224,000 |
| macos / 1          | fallback |               1 |                  1 |             232,000 |                224,000 |
| macos / 64         | overlap  |           3,001 |              1,001 |             512,000 |                360,000 |
| macos / 64         | fallback |           2,001 |              1,001 |             376,000 |                368,000 |
| macos / 1,024      | overlap  |           3,001 |              1,001 |           4,352,000 |              2,280,000 |
| macos / 1,024      | fallback |           2,001 |              1,001 |           2,296,000 |              2,288,000 |
| linux / 1          | overlap  |               1 |                  1 |             232,000 |                224,000 |
| linux / 1          | fallback |               1 |                  1 |             232,000 |                224,000 |
| linux / 64         | overlap  |           3,001 |              1,001 |             536,000 |                368,000 |
| linux / 64         | fallback |           2,001 |              1,001 |             392,000 |                368,000 |
| linux / 1,024      | overlap  |           3,001 |              1,001 |           4,376,000 |              2,288,000 |
| linux / 1,024      | fallback |           2,001 |              1,001 |           2,312,000 |              2,288,000 |

Executable sizes below are the original build outputs before release stripping/compression:

- linux: 12,295,424 -> 12,296,208 bytes (+784).
- macos: 4,051,984 -> 4,051,440 bytes (-544).

Final direct-construction build outputs are a separate population:

- linux: 12,295,424 -> 12,294,864 bytes (-560).
- macos: 4,052,000 -> 4,051,440 bytes (-560).

## Validation and limits

Both complete core/extension release suites pass 306/306. Linux uses the required-FUSE configuration. Focused ASan includes ownership/move tests, repeated batches, actual rg workers, deferred shard processing and the isolated allocation diagnostic. Linux TSan passes ownership, matcher and rg worker suites. Scoped clang-tidy covers the modified sources and dependent translation units; normal commit/push hooks and current-head CI remain required before merge.

Dedicated tests cover destroyed caller storage, shared/fallback bytes, counted non-terminated slices, embedded nulls, empty values, short/long copies and moves, vector relocation, metadata and root identity, and filesystem ownership. Existing archive/collection/safety/deferred tests remain in the full suites. No Unicode fixture is mixed into these ordinary byte/lifetime tests.

The main benefit is reduced allocation traffic and snapshot bytes, with modest elapsed gains in the qualified content/deferred workloads. Do not call every scan faster, infer new worker defaults, or reuse this evidence for a batch arena, input-recycling API, shared executor or network-storage policy. In-memory/session noise and small-process costs remain explicit.

## Lossless evidence

[packed-visit-snapshots-20261102.json.gz](packed-visit-snapshots-20261102.json.gz): 1,676,099 bytes; SHA-256 `c841d5d9d961838f776a2b88c663a6d44270b47b273b5db8c455a1943b6764ec`. Native fixture dictionaries are restored from `fixtures[report.fixtures_ref]`. Initial, duplicate-baseline, constructor, kernel and allocation populations retain their distinct provenance.
