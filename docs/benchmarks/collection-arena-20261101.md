# Collection text arena block policies (2026-11-01)

## Decision

Prefer fixed 1 MiB blocks for the collection text arena. The measured allocation reduction is substantial, while elapsed gains are workload-dependent and usually modest in complete commands. There is no measured memory-pressure or tiny-command cost that justifies keeping the 64 KiB cap or adding a special small-block phase. The production collection text arena adopts the measured fixed 1 MiB policy; allocation remains lazy.

## Contract

Baseline: merged main `7b72a5f0a52e838dfe0b199ea0386c8e6b3dcd39` (#1039). Four source variants change only the collection arena initial/maximum block sizes; growth remains two-to-one. Build release CLI and existing collection benchmark with `--config=clang_release` (LLVM 23, ThinLTO). All four variants pass `//xff/engine:collect_test` on both hosts. Raw evidence preserves patches, build flags and binary identities.

Local macOS is an M5 Pro using warm APFS and worker allowances without affinity. Linux is the drogon Ryzen 9950X KVM guest using tmpfs and enforced CPU affinity. Do not compare absolute platform times as equivalent hardware or filesystems.

In-memory benchmark: 10/100/1,000/10,000/100,000 entries, root lengths 1/128, one/four threads; fixture construction is outside timing. Three sessions shuffle per-case variant order, with nine repetitions and the mean of the fastest seven real-time samples. A four-thread benchmark constructs four independent collections concurrently; it is not the single-coordinator production collection path.

Native commands: 0/1/10/100/1,000/10,000/100,000 files, short/long paths, one/three requested workers, one/four named collections, plus no-collection controls. Each case has three sessions and a discarded warmup per variant/session, then nine interleaved samples retaining the fastest seven. Every invocation validates JSON count/size output against the fixture independently. Four named collections share one arena and their summary combines all four populations. There are 38 native cases per session and 9,120 successful native invocations across both initial matrices, including warmups, plus 600 Linux confirmation invocations.

Percentages below are medians of paired session changes; negative means faster. Median absolute elapsed times and RSS are summarized independently, so percentages need not equal ratios of independently summarized times. RSS is the median of all 27 native peak measurements per variant/case.

## Actual arena allocation accounting

A separate untimed diagnostic build reads the arena block count, used bytes and reserved bytes after the same collection construction. Its timing is excluded from performance results. Both platforms report the same counts and capacities. Empty collections allocate zero blocks for every policy.

| Policy  | 10 short: blocks | 10 short: reserved | 100k short: blocks | 100k long: blocks | 100k long: reserved |
| ------- | ---------------: | -----------------: | -----------------: | ----------------: | ------------------: |
| main    |                1 |              4 KiB |                 54 |               442 |          27.434 MiB |
| grow4k  |                1 |              4 KiB |                 11 |                35 |          27.996 MiB |
| grow64k |                1 |             64 KiB |                  7 |                31 |          27.938 MiB |
| fixed1m |                1 |           1024 KiB |                  4 |                28 |          28.000 MiB |

The long-root 100k fixture stores 28,677,780 text bytes. Fixed 1 MiB reduces 442 block allocations to 28 (93.7% fewer), while reserved bytes rise from 28,766,208 to 29,360,128: approximately 580 KiB extra capacity. Tiny short collections reserve 1 MiB rather than 4 KiB but only touch their actual text and block header; RSS is measured separately below.

## In-memory collection timing

| Entries / root length / threads | Linux grow4k | Linux grow64k | Linux fixed1m | Mac grow4k | Mac grow64k | Mac fixed1m |
| ------------------------------- | -----------: | ------------: | ------------: | ---------: | ----------: | ----------: |
| 10 / 1 / 1                      |       -2.50% |        -7.82% |        -6.95% |     +2.99% |      -4.82% |      -4.01% |
| 10 / 128 / 1                    |       +0.03% |        -1.44% |        +2.04% |     -0.84% |      -6.32% |      -6.03% |
| 1,000 / 128 / 1                 |       +0.37% |        -0.98% |       -66.45% |     +0.09% |      -0.73% |      -2.04% |
| 10,000 / 128 / 1                |       +2.00% |        +2.92% |        +2.91% |     -4.26% |      -4.15% |      -3.32% |
| 100,000 / 1 / 1                 |       -0.38% |        -1.16% |        -0.81% |     +2.75% |      +1.20% |      +0.38% |
| 100,000 / 128 / 1               |       -2.82% |        -0.50% |        -0.97% |     -7.23% |      -6.02% |      -5.35% |
| 100,000 / 1 / 4                 |       -0.23% |        -0.30% |        -2.66% |     +1.61% |      +1.28% |      -2.00% |
| 100,000 / 128 / 4               |       +0.42% |        +0.68% |        -0.56% |     -1.62% |      -3.71% |      -3.87% |

The Linux 1,000-entry/128-root/one-thread kernel is a particularly strong size-specific result: roughly 56 us becomes 19 us, with all three sessions near -66.45%. This is repeated collection construction in one process, not a threefold whole-command speedup. The allocator-level cause has not been isolated. Preserve this result without generalizing it across sizes or hosts.

## Full-command timing and resident memory

| Host  | Path / files / jobs / collections | Main ms | Fixed1m ms | Change | Main MiB | Fixed1m MiB |
| ----- | --------------------------------- | ------: | ---------: | -----: | -------: | ----------: |
| macos | short / 10 / 1 / 1                |   3.919 |      3.942 | +0.62% |     3.78 |        3.83 |
| macos | short / 100,000 / 1 / 1           | 233.962 |    233.966 | -0.33% |    37.20 |       37.30 |
| macos | short / 100,000 / 3 / 1           | 106.660 |    105.906 | -0.51% |    39.44 |       39.67 |
| macos | long / 100,000 / 1 / 1            | 377.832 |    378.944 | -0.02% |    93.06 |       92.94 |
| macos | long / 100,000 / 3 / 1            | 143.226 |    143.287 | +0.04% |    99.14 |       99.98 |
| macos | short / 100,000 / 1 / 4           | 288.283 |    286.113 | -0.42% |   135.78 |      135.69 |
| macos | long / 100,000 / 1 / 4            | 455.199 |    455.140 | -0.39% |   358.20 |      357.34 |
| macos | long / 100,000 / 3 / 4            | 191.087 |    188.329 | -1.02% |   364.56 |      364.25 |
| linux | short / 10 / 1 / 1                |   0.674 |      0.662 | -1.00% |     5.46 |        5.43 |
| linux | short / 100,000 / 1 / 1           | 106.137 |    106.100 | -0.10% |    37.39 |       37.21 |
| linux | short / 100,000 / 3 / 1           |  42.907 |     43.246 | +0.73% |    38.52 |       38.57 |
| linux | long / 100,000 / 1 / 1            | 133.698 |    133.031 | -0.50% |    93.29 |       92.61 |
| linux | long / 100,000 / 3 / 1            |  58.476 |     58.027 | -0.77% |    98.10 |       97.43 |
| linux | short / 100,000 / 1 / 4           | 155.946 |    150.803 | -3.30% |   131.03 |      130.87 |
| linux | long / 100,000 / 1 / 4            | 224.080 |    219.163 | -2.24% |   353.60 |      352.28 |
| linux | long / 100,000 / 3 / 4            | 133.506 |    129.313 | -3.46% |   358.18 |      357.22 |

## Counterexamples and confirmation

Preserve every sample rather than declaring all small differences improvements. The Linux initial matrix has a +4.58% paired median at 10,000 long-path files/three workers/one collection. A separate three-session confirmation changes that case to -2.99%, with individual sessions -8.44%, -2.99%, +8.94%. This is not a stable regression or a proven gain. The initial +3.41% 100-file and +2.95% one-file observations also do not repeat with the same sign.

The Linux long-path 100k/three-worker/four-collection gain repeats: -3.46% initially and -3.65% in confirmation. The unchanged no-collection control shifts -0.66% in confirmation, illustrating the noise floor. Do not attribute fractional-percent changes confidently to allocator policy.

No growth variant dominates all timing cases. The simpler fixed policy sharply reduces allocation calls and has useful large-text Mac kernel results with small native tradeoffs. The Mac initial matrix includes +1.68% for ten long-path files/one worker (all three sessions positive) and up to +2.22% for a 10k-file case; report these rather than claiming the policy is free. These tests do not prove behavior under constrained address space, sustained service lifetimes, many simultaneous processes, cold physical storage or other allocators. This report concerns collection text only; it does not claim an improvement to all file listings.

## Evidence

Lossless raw results and exact drivers: [collection-arena-20261101.json.gz](collection-arena-20261101.json.gz), 385,618 bytes, SHA-256 `78831091cee795fb741e949266cbebed1eafc8b3e3ec74996b30fd2e4b099cd9`. Separate Linux confirmation and accounting populations remain distinct from the timing matrices.
