# Bounded matcher result reuse (2026-11-02)

## Decision

Retain bounded text-capacity reuse in the existing matcher result slots. Each worker writes only its exclusively claimed slot. Resize the result population without discarding every previous record, release oversized old output before starting new evaluation, clear reused text, reset selection status, and disengage content when a retained matched slot becomes unmatched. Keep generation barriers, input ownership, ordered consumption, worker counts and admission policy.

Reusable text capacities are limited to `min(64 KiB, 1 MiB / current entries)` per slot. This bounds text capacity carried from the consumed batch, not newly rendered output, status-message storage, vector metadata or total process memory. Current output can exceed the reuse limit and remains available until the next call or destruction. Shrinking/empty batches destroy their old tails. No output is truncated. The pool owns returned records; callers observe them until the next batch.

Reject the direct-write-only reset variant as a separate change: it saves no output allocations. Reject comparison-vector retention: range reads still create fresh owning strings, so retaining old result slots does not reuse their VFS input buffers and can prolong old small patch payloads. Genuine comparison payload reuse needs a separate budget-aware source design. Do not revive slower generic copying wrappers or bypass the VFS.

## Measurement and source contract

The exact measured parent is `8d96bc90fea5adb469b42e5e34a8be6a518f0f4b` (#1049). Merged main `901c8fea916124f4d0e195c5385f2a977b44093d` has the same tree; preserve the measured identity after synchronization. Clean LLVM 23.1.4 `clang_release`/ThinLTO binaries are built before timing. All source variants, binary hashes, flags, fixtures, raw samples and scripts are retained below. Test-only BUILD dependencies and final regression/wording fixes do not change the timed runtime mechanism.

Variants are baseline, `in_place_reset` (direct result-slot writes while clearing/recreating records), `bounded_match`, and `compare_vector` (only removes the comparison vector clear). Keep the latter two controls separate from the selected matcher change. Copied benchmark binaries link only stable system libraries; no mutable project shared library supplies a different variant at runtime. Unrelated timing changes of the comparison-only control remain unexplained and are not credited to matcher buffer reuse.

Each host has 61 native cases and three sessions, one discarded warmup per variant/case/session, nine rotating participant rounds and fastest-seven means. The combined population contains 14,640 fresh invocations including warmups. Rotation has no parity reversal. Every invocation validates an independent output oracle: sorted paths, sizes, complete collection/comparison records, or explicit `path:x` line-output digests. Linux uses tmpfs with 1/3/10-CPU affinity; macOS uses warm APFS without affinity. Fixtures span 10, 1,000, 10,000 and broad/deep 4,096 files, plus 65,536-file collection controls and small/large two-root comparisons.

In-memory kernels use 46 RE2 cells and 16 comparison cells, three sessions, nine Google Benchmark repetitions/fastest seven, with a separate process per case/participant and rotating participant order between cases. Linux pins 1/4 CPUs; macOS uses worker allowances without affinity. The eight-case targeted confirmation uses the same source binaries and a separate population. Builds, lint and sanitizers never overlap timing on a host. Kernel validation checks expected outputs once; it is not the native per-invocation oracle.

## Native selected results

Changes are medians of paired session ratios; negative means faster. All cells and slower samples remain in the archive.

| Fixture / task              | Workers | Linux change | macOS change |
| :-------------------------- | ------: | -----------: | -----------: |
| flat10 / rg-lines           |       1 |       +0.41% |       +0.54% |
| flat1000 / rg-filter        |       3 |       +0.19% |       -0.84% |
| flat10000 / rg-lines        |       1 |       -0.24% |       +0.32% |
| flat10000 / native-lines    |       3 |       -3.17% |       -0.36% |
| nested8x8x64 / native-lines |       3 |       -3.42% |       +0.38% |
| flat1000 / native-lines     |      10 |       +2.69% |       +0.24% |
| flat10000 / metadata        |      10 |       +1.06% |       +1.63% |
| compare-small / compare     |       3 |       -1.46% |       +0.01% |
| compare-large / compare     |       3 |       -1.35% |       -0.16% |

The native results are modest. The largest selected native medians are +2.69% in a Linux output control and +1.63% in a macOS metadata control; do not claim every scan is faster. Three-session medians do not eliminate scheduling or code-layout sensitivity.

## Kernel confirmation and negative controls

The original selected macOS ten-file large-output cell is +4.90%, with session values -0.49/+13.84/+4.90%; its focused confirmation is -1.42%. Preserve both. The original Linux small early-difference comparison control is +6.67% and varies sharply; the focused change also occurs in the unrelated comparison-only control. Do not explain unrelated benchmark shifts as saved matcher allocations.

| Kernel                     | Linux direct/reset | Linux bounded | Linux compare control | macOS direct/reset | macOS bounded | macOS compare control |
| :------------------------- | -----------------: | ------------: | --------------------: | -----------------: | ------------: | --------------------: |
| RE2/filter/1000/4          |             +0.40% |        -1.09% |                +2.70% |             +2.03% |        +1.66% |                +0.26% |
| RE2/large-output/10/1      |             -0.78% |        -0.39% |                -0.71% |             -0.56% |        -1.42% |                -0.56% |
| RE2/large-output/1000/4    |             -4.12% |        -3.24% |                -2.72% |             +1.67% |        -1.32% |                +0.62% |
| RE2/native-lines/1000/1    |             +1.16% |        -0.72% |                +0.88% |             +0.63% |        -0.63% |                +0.21% |
| RE2/rg-files/10/4          |             -1.22% |        -0.41% |                -1.38% |             +0.31% |        +0.37% |                -0.73% |
| RE2/rg-files/1000/1        |             -5.73% |        -8.39% |                -9.05% |             +0.26% |        -0.09% |                +0.80% |
| RE2/rg-files/10000/1       |             -5.03% |        -8.67% |                -8.83% |             +1.61% |        -2.95% |                -0.16% |
| large-early-different/10/4 |             +1.14% |       -12.07% |               -11.94% |             -1.58% |        -0.52% |                -0.49% |

The confirmed macOS 1,000-file/four-worker filter control remains +1.66%; native small/metadata costs and the full initial kernel population are retained. These costs accompany a measured allocation reduction, not a universal throughput improvement.

## Isolated allocation traffic

One coordinator processes a warmed 64-entry output batch twice. Each entry produces a 1,028-byte line. Inputs, fixture construction, warmup and assertions are outside recording. ASan hooks count only coordinator allocation traffic, not release elapsed time, net live memory or allocator-resident bytes. The second batch repeats the same counts. The unrelated packed-snapshot diagnostic remains a negative allocation control.

| Host / variant         | Allocation calls | Requested bytes |
| :--------------------- | ---------------: | --------------: |
| macos / baseline       |              320 |         264,704 |
| macos / in_place_reset |              320 |         264,704 |
| macos / bounded_match  |              256 |         198,656 |
| macos / compare_vector |              320 |         264,704 |
| linux / baseline       |              320 |         266,752 |
| linux / in_place_reset |              320 |         266,752 |
| linux / bounded_match  |              256 |         200,192 |
| linux / compare_vector |              320 |         266,752 |

Only bounded reuse changes those counts: 320 -> 256 calls (20% fewer) and about 25% fewer requested bytes. The reset-only and comparison-only controls remain at baseline allocation traffic.

## Footprint and validation

- macos: unstripped build output 4,051,440 -> 4,051,424 bytes (-16).
- linux: unstripped build output 12,294,864 -> 12,292,536 bytes (-2,328).

Both complete core/extension release suites pass 306/306; Linux requires its working FUSE integration. Focused ASan and Linux TSan cover real worker output and repeated states. Header-aware scoped clang-tidy covers five translation units; the final test-only arithmetic finding is corrected and its unit rechecked. Normal commit/push hooks and current pushed-head CI remain required before merge.

Tests exercise reuse below the limit, aggregate/per-record capacity limits, large output followed by a short inline tail, a retained matched slot becoming unmatched, an empty batch followed by growth, and false/error/true selection transitions with no stale output. Existing ordering, source ownership, archive admission, safety and deferred-evaluation tests remain in the full suites.

Native wall/user/system CPU and child peak RSS are retained per sample. No first-output-latency or total-live-memory improvement is claimed. Persistent worker participation and comparison source-buffer reuse remain separate candidates.

## Lossless evidence

[bounded-match-results-20261102.json.gz](bounded-match-results-20261102.json.gz): 1,606,108 bytes; SHA-256 `8a4b4bfbdb8ecff5904597325ab570cad2650d5b13007d177746b762b563ae8f`. Restore native fixtures from `fixtures[report.fixtures_ref]`. Initial, targeted, allocation and rejected-control populations keep their separate provenance.
