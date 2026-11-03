# Caller-owned memory and archive-member streams (2026-11-03)

## Decision

Expose the existing optional `ReadInto` capability from memory-backed streams and archive-member streams. Caller-owned storage reuses the archive session buffer introduced by O19a; legacy cursors keep their owned-string fallback. Memory cursors copy only initialized source bytes and advance once. Archive members retain the 64 KiB read cap, errors, EOF, sparse holes, cursor position, parent ownership and shared budget. Owned `Read` remains available.

This removes cumulative allocation churn rather than archive indexing or decoder work. Native gains are modest; nested in-memory workloads benefit more on macOS. Slower ordinary/small controls remain in the tables and raw data. The change does not promise a general archive speedup. Tiny memory sources can request a larger buffer even when the allocation count is unchanged.

No new flags, external dependencies, threads, locks, exceptions, format parsers, seeking/indexing or host-I/O routes. O19b index/seek/replay/admission and O19d direct final-buffer materialization remain separate.

## Protocol

Fixed parent `492c1ef635837571d26c5c45b78f7c6ef1776798` includes the #1065 dependency updates, #1059, O19a and O22a/c1. Four variants isolate baseline, memory-only, member-only and combined runtime changes. Common tests and benchmark support are identical. Every variant is filename-formatted before building. Captured release configuration, compiler hash, link actions, binary hash and sizes accompany the results.

Per host: 100 native cases x three sessions, each with discarded participant warmup and nine interleaved rounds, fastest seven retained. Independent Python fixtures cover TAR-in-TAR, TAR-in-gzip-TAR and ZIP-in-TAR, flat TAR/gzip/ZIP controls and ordinary files. Both 1/3 requested workers, 10/1,000/10,000 members and 128/4,096-byte payloads are represented. Complete stdout and zero exits are checked. Nested traversal explicitly uses `--archive=all --archive-depth=2`; a missing-depth setup attempt was rejected before accepting any row.

The 48 in-memory cases x three sessions x four variants separately exercise explicitly incapable legacy cursors, custom caller-owned cursors, actual MemoryReadSource and actual nested MemberReadSource. Fixture setup is outside each timed iteration. Batching amortizes untimed CPU metadata startup; each case retains its own warmup and nine repetitions, fastest seven retained. Linux uses tmpfs/native fixtures and CPU affinity; macOS uses warm APFS without CPU pinning. Requested workers do not imply parallel archive decoding.

Independent confirmation uses ten native cases x five sessions and nine memory cases x three sessions, with a byte-identical baseline repeat. A supplemental two-small-case check fixes each repetition at 100,000 iterations on the same binaries; it is separate from the time-calibrated populations. Reference-repeat movement is shown directly, never subtracted. No timing overlaps compilation, lint or instrumentation on its host.

## Native commands

Median paired session elapsed-time change; negative means faster.

| Fixture / action             | CPUs | Linux initial | Linux confirm | Linux repeat | macOS initial | macOS confirm | macOS repeat |
| :--------------------------- | ---: | ------------: | ------------: | -----------: | ------------: | ------------: | -----------: |
| ordinary / list              |    1 |        -0.64% |        +2.19% |       +0.67% |        +0.54% |        +3.35% |       +0.14% |
| tar-in-tar-10000-4096 / list |    1 |        -1.18% |        -0.60% |       +0.70% |        -1.07% |        -2.03% |       -0.05% |
| tar-10000-4096 / list        |    1 |        +0.19% |        +1.81% |       +0.56% |        -0.65% |        +1.06% |       -0.11% |
| tar-in-tgz-10000-4096 / last |    3 |        -0.30% |        -0.33% |       -0.00% |        -0.77% |        -0.57% |       +0.13% |
| tar-in-tar-10000-128 / list  |    1 |        -0.66% |        +1.02% |       +0.73% |        +0.22% |        +0.23% |       +0.18% |
| zip-in-tar-10000-4096 / last |    1 |        -0.68% |        +0.52% |       +0.31% |        +0.00% |        +0.07% |       -0.08% |
| zip-10-128 / last            |    3 |        +1.46% |        +1.51% |       +0.22% |        +1.93% |        +2.54% |       +1.21% |
| tar-in-tar-10-128 / list     |    1 |        -2.61% |        +0.90% |       +0.69% |        -1.89% |        +4.01% |       +2.83% |
| ordinary / last              |    3 |        -0.24% |        +3.85% |       -0.59% |        +0.51% |        -0.07% |       +2.79% |
| tar-in-tar-10000-4096 / last |    3 |        -1.93% |        -1.22% |       -0.63% |        -2.27% |        -2.51% |       -0.67% |

## In-memory streams

Median of paired session fastest-seven changes. Preserve all four variants, including regressions.

| Source / action / members / bytes | Linux memory | Linux member | Linux combined | macOS memory | macOS member | macOS combined |
| :-------------------------------- | -----------: | -----------: | -------------: | -----------: | -----------: | -------------: |
| legacy/list/10/128                |       +1.38% |       +8.45% |         +3.08% |       -0.53% |       -1.25% |         +0.40% |
| legacy/list/10000/4096            |       -1.37% |       +6.49% |         +1.19% |       -0.31% |       -0.17% |         +0.01% |
| legacy/last/10000/4096            |       -2.68% |       +9.59% |         +0.44% |       -2.56% |       -0.32% |         +0.57% |
| caller-owned/list/10/128          |       +2.81% |       +8.86% |         +3.68% |       +0.90% |       +1.68% |         -2.20% |
| caller-owned/list/10000/4096      |       +0.89% |       +6.95% |         +2.40% |       -0.52% |       -0.06% |         +0.41% |
| caller-owned/last/10000/4096      |       -1.08% |       +6.28% |         +0.91% |       +0.29% |       +1.53% |         +2.01% |
| memory/list/10/128                |       +1.11% |       +8.23% |         +3.12% |       +2.58% |       -0.08% |         +1.56% |
| memory/list/10000/4096            |       -1.57% |       +8.41% |         +1.11% |       -0.55% |       -0.41% |         -0.04% |
| memory/last/10000/4096            |       -0.39% |       +7.68% |         +0.85% |       -1.41% |       -0.44% |         -0.86% |
| nested-memory/list/10/128         |       +1.23% |       -1.43% |         -5.18% |      +17.64% |       -0.72% |         -6.06% |
| nested-memory/list/10000/4096     |       +2.69% |       +3.74% |         -3.33% |       -8.25% |      -14.58% |        -14.81% |
| nested-memory/last/10000/4096     |       +0.99% |       +1.70% |         -3.60% |      -39.76% |      -38.92% |        -42.75% |

## Confirmation and small-case stability

Original time-calibrated confirmation and the fixed-iteration check remain separate. Large reference-repeat movement indicates measurement instability and limits attribution of the candidate movement.

| Case                          | Phase               | Linux combined | Linux repeat | macOS combined | macOS repeat |
| :---------------------------- | :------------------ | -------------: | -----------: | -------------: | -----------: |
| nested-memory/last/10/128     | confirmation-memory |         -5.70% |       +1.97% |        -11.77% |       +3.69% |
| caller-owned/list/10000/4096  | confirmation-memory |         +2.09% |       -0.92% |         -1.93% |       +0.07% |
| nested-memory/list/10000/128  | confirmation-memory |         +1.65% |       -1.31% |         -7.11% |       -2.17% |
| nested-memory/last/10000/4096 | confirmation-memory |         -3.06% |       +0.36% |        -28.74% |      +10.30% |
| memory/list/10/128            | confirmation-memory |         +4.12% |       +0.72% |        +30.41% |      +19.10% |
| nested-memory/list/10000/4096 | confirmation-memory |         -1.78% |       +0.18% |        -15.28% |       +1.09% |
| memory/last/10000/4096        | confirmation-memory |         +4.02% |       +1.52% |         -0.30% |       -0.45% |
| memory/list/10000/4096        | confirmation-memory |         +1.29% |       +0.31% |         +0.12% |       +0.02% |
| legacy/list/10000/4096        | confirmation-memory |         +3.60% |       -0.81% |         -1.27% |       +0.26% |
| memory/list/10/128            | fixed-small         |         +3.70% |       +0.19% |         +0.11% |       -0.23% |
| legacy/list/10/128            | fixed-small         |         +3.62% |       +0.57% |         +0.26% |       +0.13% |

## Rejected owned-read rewrite

A separate three-variant pilot preserved the original owned member-read implementation and added the caller-owned method independently. It did not consistently improve the controls: Linux large kernel controls became slower than the selected draft, while small macOS controls also moved. Keep one checked decoder path instead of duplicating it. Both hosts retain all 24 native pilot rows and 72 memory pilot runs. The baseline and selected binaries were byte-identical to their original builds; changed control timings therefore also limit causal interpretation of small percentage changes.

## Allocation diagnostics

Warmed calling-thread ASan hooks count allocation requests and cumulative requested bytes only. They exclude fixture construction and reporting; they are not release timings, peak memory or RSS savings. Small sources reserve the same existing read budget but may allocate a full 64 KiB buffer instead of a short owned string.

| Case                          | Host  | Baseline requests | Combined requests | Baseline requested bytes | Combined requested bytes |
| :---------------------------- | :---- | ----------------: | ----------------: | -----------------------: | -----------------------: |
| memory/list/10/128            | linux |                90 |                90 |                  218,544 |                  272,800 |
| memory/list/10/128            | macos |                90 |                90 |                  218,504 |                  272,768 |
| memory/list/10000/4096        | linux |            60,742 |            60,039 |               50,601,072 |                4,574,320 |
| memory/list/10000/4096        | macos |            60,742 |            60,039 |               50,595,408 |                4,574,288 |
| nested-memory/list/10000/4096 | linux |            61,503 |            60,097 |               97,124,976 |                5,013,600 |
| nested-memory/list/10000/4096 | macos |            61,503 |            60,097 |               97,113,608 |                5,013,504 |
| nested-memory/last/10000/4096 | linux |            61,491 |            60,085 |               94,900,952 |                2,789,576 |
| nested-memory/last/10000/4096 | macos |            61,491 |            60,085 |               94,889,560 |                2,789,456 |

## Correctness and footprint

Memory tests cover default unsupported cursors, empty/binary sources, zero spans, independent offsets, EOF, untouched tails, mixed owned/borrowed position and lifetime. Member tests cover 64 KiB caps, positive short reads, late failures, retained parents, four nested compression combinations, shared-budget release and sparse PAX/gzip holes. Sparse readers may legally return a positive partial block: the test consumes each returned count and checks untouched tails.

Both hosts pass the 313 selected root-and-extension release suites and five focused ASan suites. Linux additionally passes five MSan and five TSan suites, with FUSE required in the full release selection. Five changed translation units pass real scoped clang-tidy on each host. Source guards verify restoration before/after validation and diagnostics. Current pushed-head CI, including coverage, remains required for readiness.

- linux: unstripped full CLI 18,146,176 -> 18,148,600 bytes (+2,424).
- macos: unstripped full CLI 8,157,560 -> 8,158,200 bytes (+640).

## Lossless evidence

[caller-owned-streams-20261103.json.gz](caller-owned-streams-20261103.json.gz): 3,186,597 bytes; SHA-256 `b23aadac072637ca3882b6fb25b1df2d5ee1960d4eae95d295743d38fb6b4d5c`.
