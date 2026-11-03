# Direct source result materialization (2026-11-03)

## Decision

Fill bounded ranges and supported materializations directly into final string storage. Keep the existing owned-read path for incapable decoders, adopting the first owned block when the result is empty. Later owned chunks append normally. Observe capability once per read, allowing later changes. No new public API, flag, dependency, worker, lock, cache, metadata probe or host-I/O route.

Validate every count before exposing initialized bytes. Preserve short reads, cursor position, errors, range reservations, offset overflow and the 64 MiB range guard. Default `Size` and sequential `OpenAt` retain their replay accounting. At exact materialization limits, including zero, a positive one-byte read distinguishes EOF from overflow; errors remain errors. Clamp the logical limit to the string maximum before arithmetic.

Supported reads reuse spare capacity, including SSO, rather than growing a string merely to discover EOF after a short read. This may add a small initial backend read for a larger input; it is included in the measurements. Final capacity can still grow geometrically. Source/backend and small controls can be slower; this is not a universal speedup.

## Protocol

Publication parent `e6a60c7ec0d9517e5e0bcf99bafe76b379dc32f6` adds only the full-rg integration-test timeout adjustment. Production and benchmark inputs remain byte-identical to the qualified parent; the raw evidence retains the comparison proof.

Fixed synchronized parent `c6a32909b17ca74eb6343f8dc938067c3bad8c69` includes #1065 dependencies and #1066/#1067/#1068 runtime work. Four variants isolate baseline, range-only, bytes-only and combined changes. Common tests and benchmarks are identical. Filename-aware formatting gates precede builds; raw artifacts retain compiler hashes, release flags, link actions, source hashes, binary hashes and sizes.

Per host: 100 native cases x three sessions over nested TAR-in-TAR/TAR-in-gzip-TAR/ZIP-in-TAR, flat archive controls and ordinary files, 10/1,000/10,000 members, 128/4,096-byte payloads, one/three requested workers and listing/final-member content. Every native stdout and exit code is checked against independent fixtures. Explicit `--archive=all --archive-depth=2` enables nested traversal. Native workers are requests, not a claim of parallel decoder work.

The separate memory matrix has 84 cases x three sessions x four variants: 48 archive cases plus 36 direct-source cases. Source cases cover incapable legacy, custom caller-owned and actual memory sources, range versus whole-result reads, and 0/10/4,096/65,536/1,048,576/16,777,216 bytes. Fixtures and content expectations are outside timed loops. Archive registration uses 0.02 s timing/0.01 s warmup; source registration uses 0.05 s/0.05 s. Every case retains nine repetitions, fastest seven. Batches amortize untimed CPU metadata startup without pooling different cases. Linux uses tmpfs and CPU affinity; macOS uses warm APFS without pinning.

Confirmation adds ten native cases x five sessions and twelve memory cases x three sessions with a byte-identical baseline participant. Three tiny source cases additionally use one million iterations per repetition, preserving nine repetitions/fastest seven. These fixed-iteration populations remain separate. No same-host timing overlaps builds, lint or instrumentation. Reference-repeat movement is shown, never subtracted.

## Native commands

Median paired-session elapsed-time changes; negative means faster.

| Fixture / action             | CPUs | Linux initial | Linux confirm | Linux repeat | macOS initial | macOS confirm | macOS repeat |
| :--------------------------- | ---: | ------------: | ------------: | -----------: | ------------: | ------------: | -----------: |
| ordinary / list              |    1 |        +1.17% |        +1.13% |       +0.20% |        +0.31% |        +0.73% |       -0.09% |
| tar-in-tar-10000-4096 / list |    1 |        +0.04% |        +0.88% |       -0.15% |        +0.09% |        +0.04% |       +0.35% |
| tar-10000-4096 / list        |    1 |        +0.48% |        +1.73% |       +0.25% |        +0.59% |        +0.32% |       -0.06% |
| tar-in-tgz-10000-4096 / last |    3 |        +0.02% |        +0.15% |       +0.02% |        +0.03% |        +0.17% |       +0.05% |
| tar-in-tar-10000-128 / list  |    1 |        -0.16% |        +2.04% |       +1.14% |        +0.96% |        -0.01% |       +0.15% |
| zip-in-tar-10000-4096 / last |    1 |        +0.95% |        +1.42% |       +0.22% |        -0.40% |        -0.01% |       +0.00% |
| zip-10-128 / last            |    3 |        -1.24% |        -0.02% |       +0.28% |        -0.31% |        +1.18% |       +0.19% |
| tar-in-tar-10-128 / list     |    1 |        +0.17% |        +1.00% |       +0.41% |        +1.76% |        +0.80% |       -0.62% |
| ordinary / last              |    3 |        -0.44% |        +3.68% |       +1.91% |        -0.63% |        +1.05% |       +0.11% |
| tar-in-tar-10000-4096 / last |    3 |        +0.36% |        +1.52% |       +0.02% |        +0.59% |        +0.84% |       +0.25% |

## Direct source kernels

All 36 source cases are retained. Each cell is the median paired-session fastest-seven elapsed-time change.

| Source / operation / bytes  | Linux range | Linux bytes | Linux combined | macOS range | macOS bytes | macOS combined |
| :-------------------------- | ----------: | ----------: | -------------: | ----------: | ----------: | -------------: |
| caller-owned/bytes/0        |      +0.08% |     -31.49% |        -31.27% |      +0.95% |      -0.39% |         +1.02% |
| caller-owned/bytes/10       |      +0.36% |     -53.71% |        -51.30% |      -0.71% |      -8.11% |         -7.37% |
| caller-owned/bytes/1048576  |      +0.47% |     -20.82% |        -19.80% |      -1.41% |     -22.00% |        -20.57% |
| caller-owned/bytes/16777216 |      -0.65% |     -16.46% |        -16.12% |      +0.62% |     -18.67% |        -20.19% |
| caller-owned/bytes/4096     |      -3.15% |     -23.42% |        -22.31% |      -0.32% |     -26.12% |        -26.20% |
| caller-owned/bytes/65536    |      -0.06% |     -35.47% |        -34.11% |      +0.69% |     -26.30% |        -26.41% |
| caller-owned/range/0        |      -0.18% |      +0.32% |         +0.75% |      +3.21% |      +4.46% |         -1.69% |
| caller-owned/range/10       |     -16.55% |      -1.65% |        -13.66% |      -5.75% |      +0.44% |         -2.59% |
| caller-owned/range/1048576  |     -27.74% |      -0.96% |        -27.59% |     -28.01% |     +11.84% |        -27.84% |
| caller-owned/range/16777216 |     -24.75% |      +0.66% |        -26.24% |     -29.20% |      -1.08% |        -29.21% |
| caller-owned/range/4096     |     -28.37% |      -0.48% |        -27.57% |     -28.33% |      +0.56% |        -27.64% |
| caller-owned/range/65536    |     -34.83% |      -0.12% |        -34.20% |     -47.00% |     -14.02% |        -47.11% |
| legacy/bytes/0              |      +0.59% |      +6.90% |         +4.50% |      +0.20% |     +10.21% |        +16.99% |
| legacy/bytes/10             |      -0.75% |     -18.44% |        -12.87% |      +0.98% |      +6.93% |        +10.57% |
| legacy/bytes/1048576        |      +3.68% |      +0.35% |         +2.85% |      +0.47% |      -5.21% |         -5.02% |
| legacy/bytes/16777216       |      +0.10% |      +0.91% |         -0.53% |      -1.64% |      -0.82% |         -2.05% |
| legacy/bytes/4096           |      -0.33% |     -30.83% |        -32.54% |      -2.47% |     -28.36% |        -29.38% |
| legacy/bytes/65536          |      +0.39% |     -32.39% |        -32.46% |      -0.28% |     -26.01% |        -26.19% |
| legacy/range/0              |      -0.12% |      +0.37% |         +0.01% |      -2.36% |      -0.63% |         -1.26% |
| legacy/range/10             |      +1.73% |      -0.23% |         +6.86% |      +0.93% |      -0.20% |         +2.63% |
| legacy/range/1048576        |      -0.87% |      -1.10% |         -0.01% |      +0.08% |      -0.06% |         -0.49% |
| legacy/range/16777216       |      -0.64% |      -0.61% |         -1.38% |      -0.26% |      +0.37% |         -0.67% |
| legacy/range/4096           |      +1.14% |      +0.47% |         -0.23% |      +0.64% |      -0.28% |         +0.08% |
| legacy/range/65536          |      -0.44% |      -0.05% |         -0.23% |     -16.82% |     -21.39% |        -13.43% |
| memory/bytes/0              |      +0.23% |     -34.74% |        -30.50% |      +3.24% |      -2.15% |         +3.27% |
| memory/bytes/10             |      +0.07% |     -52.60% |        -51.04% |      -0.87% |      -7.67% |         -4.46% |
| memory/bytes/1048576        |      -1.86% |     -18.83% |        -18.22% |      -9.22% |     -21.51% |        -20.36% |
| memory/bytes/16777216       |      -0.39% |     -15.95% |        -16.36% |      -0.05% |     -19.76% |        -21.36% |
| memory/bytes/4096           |      +0.64% |     -24.25% |        -21.55% |      +1.44% |     -26.50% |        -26.67% |
| memory/bytes/65536          |      +0.44% |     -30.04% |        -29.43% |      +0.24% |     -26.19% |        -26.36% |
| memory/range/0              |      -0.08% |      -0.24% |         +0.41% |      +0.32% |      +2.56% |         +3.27% |
| memory/range/10             |     -20.85% |      +0.95% |        -15.98% |     -10.73% |      +0.61% |         -9.82% |
| memory/range/1048576        |     -36.17% |      -0.16% |        -35.76% |     -51.48% |      -0.47% |        -51.64% |
| memory/range/16777216       |     -81.91% |      +0.22% |        -81.99% |     -29.26% |      -0.14% |        -29.48% |
| memory/range/4096           |     -30.78% |      -0.33% |        -29.76% |     -29.60% |      -1.40% |        -29.90% |
| memory/range/65536          |     -34.04% |      +0.28% |        -33.75% |     -83.11% |     -60.34% |        -83.08% |

## Archive and small controls

| Case                                  | Linux initial | macOS initial |
| :------------------------------------ | ------------: | ------------: |
| archive/legacy/list/10/128            |        -0.09% |        +1.61% |
| archive/legacy/list/10000/4096        |        -1.29% |        +0.30% |
| archive/legacy/last/10000/4096        |        -0.56% |        -0.98% |
| archive/caller-owned/list/10/128      |        -0.44% |        -1.36% |
| archive/caller-owned/list/10000/4096  |        -0.65% |        -0.65% |
| archive/caller-owned/last/10000/4096  |        +1.30% |        -1.92% |
| archive/memory/list/10/128            |        -0.39% |        +0.91% |
| archive/memory/list/10000/4096        |        +0.12% |        -0.25% |
| archive/memory/last/10000/4096        |        -0.76% |        +0.06% |
| archive/nested-memory/list/10/128     |        -0.57% |        -1.39% |
| archive/nested-memory/list/10000/4096 |        +0.49% |        -2.43% |
| archive/nested-memory/last/10000/4096 |        +0.50% |        +2.90% |

## Confirmation

| Case                                  | Phase               | Linux combined | Linux repeat | macOS combined | macOS repeat |
| :------------------------------------ | :------------------ | -------------: | -----------: | -------------: | -----------: |
| source/memory/bytes/10                | confirmation-memory |        -50.35% |       +2.62% |         -6.60% |       +1.31% |
| source/memory/range/16777216          | confirmation-memory |        -82.32% |       -0.54% |        -29.06% |       -0.00% |
| source/caller-owned/range/16777216    | confirmation-memory |        -27.25% |       -4.34% |        -28.46% |       -0.04% |
| source/caller-owned/bytes/4096        | confirmation-memory |        -21.25% |       -0.67% |        -26.49% |       +0.62% |
| source/legacy/range/4096              | confirmation-memory |         +1.27% |       -0.09% |         -0.73% |       -0.96% |
| source/caller-owned/bytes/10          | confirmation-memory |        -50.14% |       +0.28% |         -4.06% |       +2.32% |
| source/legacy/bytes/4096              | confirmation-memory |        -28.20% |       -0.05% |        -29.12% |       -0.31% |
| source/legacy/bytes/10                | confirmation-memory |        -12.32% |       -0.13% |         +9.66% |       +1.52% |
| source/memory/bytes/16777216          | confirmation-memory |        -16.54% |       -0.64% |        -20.78% |       +0.13% |
| archive/nested-memory/list/10000/4096 | confirmation-memory |         -1.88% |       -0.41% |         -0.56% |       -0.11% |
| archive/legacy/list/10/128            | confirmation-memory |         -2.78% |       -2.39% |         +0.36% |       -1.01% |
| archive/nested-memory/last/10000/4096 | confirmation-memory |         -3.61% |       -3.12% |         -0.59% |       -0.22% |
| source/legacy/bytes/10                | fixed-small         |        -12.17% |       +0.15% |         +9.27% |       -0.30% |
| source/caller-owned/bytes/10          | fixed-small         |        -51.00% |       -0.76% |         -9.17% |       +0.38% |
| source/memory/bytes/10                | fixed-small         |        -50.40% |       +1.77% |         -4.84% |       -0.09% |

Tiny fixed-iteration absolute times, median session fastest-seven means, in nanoseconds:

| Case                         | Linux baseline ns | Linux selected ns | macOS baseline ns | macOS selected ns |
| :--------------------------- | ----------------: | ----------------: | ----------------: | ----------------: |
| source/legacy/bytes/10       |              39.3 |              34.4 |              34.9 |              38.2 |
| source/caller-owned/bytes/10 |              39.2 |              19.2 |              35.1 |              32.7 |
| source/memory/bytes/10       |              37.8 |              18.7 |              33.8 |              32.7 |

## Allocation diagnostics

Warmed calling-thread ASan hooks count requests and cumulative requested bytes. Fixture construction/reporting are excluded. These are not release timings or peak/RSS savings; string capacity growth remains real.

| Family / case                                   | Host  | Baseline requests | Selected requests | Baseline requested bytes | Selected requested bytes |
| :---------------------------------------------- | :---- | ----------------: | ----------------: | -----------------------: | -----------------------: |
| source/memory/range/16777216/real_time          | linux |                 3 |                 2 |               33,554,496 |               16,777,264 |
| source/memory/range/16777216/real_time          | macos |                 3 |                 2 |               33,554,480 |               16,777,256 |
| source/caller-owned/range/16777216/real_time    | linux |               258 |                 2 |               33,558,584 |               16,777,272 |
| source/caller-owned/range/16777216/real_time    | macos |               258 |                 2 |               33,556,528 |               16,777,264 |
| source/legacy/bytes/4096/real_time              | linux |                 3 |                 2 |                    8,264 |                    4,152 |
| source/legacy/bytes/4096/real_time              | macos |                 3 |                 2 |                    8,248 |                    4,144 |
| source/memory/bytes/16777216/real_time          | linux |               266 |                10 |               50,278,416 |               33,505,280 |
| source/memory/bytes/16777216/real_time          | macos |               266 |                10 |               50,272,280 |               33,501,192 |
| archive/nested-memory/last/10000/4096/real_time | linux |            60,085 |            60,083 |                2,789,576 |                2,658,472 |
| archive/nested-memory/last/10000/4096/real_time | macos |            60,085 |            60,083 |                2,789,456 |                2,658,368 |

## Rejected generalized fallback

The first implementation copied even incapable owned reads through the span callback and used an SSO-sized initial read for them. Its Linux legacy 4 KiB materialization regressed about 24%. Preserve its complete 300 native rows and 1,008 kernel runs per host separately. The refined branch directly reads/appends owned chunks and takes ownership of the first block. Its three-variant pilot keeps 24 native rows and 126 kernel runs per host; final four-variant qualification is a new population. Do not pool the rejected or pilot samples with final results.

## Correctness and footprint

Nine committed new cases cover mixed capability/short reads, early EOF with reservation retention, positive exact/zero-limit probes, impossible counts from both APIs, late/probe errors, maximum arithmetic, and owned-only small/multiple-chunk results. Existing archive tests retain sparse PAX/gzip content, parent lifetimes, errors and budgets. The independent format probe now performs zero owned reads instead of one; only that expected counter changes.

Both hosts pass all 313 selected root-and-extension release suites and five focused ASan suites. Linux passes five MSan and five TSan suites with FUSE required for the complete release selection. Actual scoped clang-tidy passed all four changed translation units per host; the runtime refinement stays within the existing complexity limit. A final test-only assertion covers owned early EOF below the caller limit; its target is rerun under every validated configuration, with production and benchmark inputs unchanged. Seven-file source guards verify exact restoration before/after builds, validation and diagnostics. Normal hooks and this PR's current-head CI remain merge requirements.

- linux: unstripped full CLI 18,148,600 -> 18,159,248 bytes (+10,648).
- macos: unstripped full CLI 8,158,200 -> 8,158,392 bytes (+192).

O19b seeking/index/replay/admission remains open; no constant-time archive lookup or newly parallel decoder is claimed.

## Lossless evidence

[direct-source-materialization-20261103.json.gz](direct-source-materialization-20261103.json.gz): 5,695,819 bytes; SHA-256 `63160535a98a05869eeff1d2ba357a9f99a62b81ef424c72c7ab4ed92f9d41e3`.
