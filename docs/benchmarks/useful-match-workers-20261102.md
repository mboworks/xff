# Useful matcher worker participation (2026-11-02)

## Decision

Keep the existing pool creation/admission policy, persistent threads and four-entry dynamic work chunks. Once a pool has grown, activate at most one worker per available chunk for a later batch. Retain every worker for subsequent growth. Only selected participants decrement completion; eligibility stays fixed for the entire generation. Shutdown overrides eligibility and joins every retained thread.

This removes unnecessary participation, not a thread per file: the pool was already persistent. Abseil still manages condition evaluation and wakeups. No new clock, queue wrapper, shared owner, flag, stat, or exception boundary is introduced. Ordered result slots and the bounded output reuse from O10 remain unchanged.

Reject the `growth_limit` control, which applies the pool creation quota (`ceil(entries / 16)`) to every later batch. It is faster for cheap in-memory tails but throttles useful read concurrency and regresses the controlled delayed-read cases by about 95-294%. A grown pool may use twelve workers for sixty-four entries; only a sixteen-entry tail has just four chunks.

## Source and measurement contracts

Measured parent: `277e98795a27d24a0c07a64a45b50afb33c7435a` (#1050). Variants are baseline, `useful_chunks` (selected), and `growth_limit` (rejected). Clean `clang_release`/ThinLTO executables are built before timing. Complete binary/source identities and raw samples are retained below. No build, lint or sanitizer overlaps a timing session on the same host.

Each platform has 49 native cases in three sessions. Each case discards a warmup per variant, rotates participant order across nine rounds, and averages the fastest seven. Linux uses tmpfs with 1/3/10-CPU affinity; macOS uses warm APFS without affinity. Native output is validated on every invocation. Cases include flat 10/272/288/320/1,000/10,000-file trees, deliberately expensive tails, output modes, metadata/listing and small/large comparisons. Heavy fixtures have 256 short files followed by 16/32/64 files of 128 KiB under directory sorting.

The 18-cell in-memory population runs three sessions and nine repetitions/fastest seven. Eight warmed-pool tail cells grow to 256 entries outside timing and then repeat 16/32/64/256-entry batches with three/ten requested workers. Eight RE2 and two comparison controls remain separate. Linux pins tail workers to three/ten CPUs and existing controls to one/four.

The delayed-read population has four cells and three sessions with the same sampling. Both VFS read entry points delay 200 microseconds; this is a controlled concurrency counterexample, not a claim about real disk latency. An earlier fixture delayed only `ContentSource`, which this predicate did not call. Those invalid-delay runs are retained and excluded from policy conclusions.

## Warmed-pool kernel results

Median paired session changes; negative means faster. These isolate repeated tail overhead and do not imply equivalent complete-command gains.

| Tail entries | Workers | Linux selected | macOS selected | Linux rejected quota | macOS rejected quota |
| -----------: | ------: | -------------: | -------------: | -------------------: | -------------------: |
|           16 |       3 |         -1.03% |         -0.54% |              -37.83% |              -46.56% |
|           16 |      10 |        -31.50% |        -50.86% |              -62.49% |              -75.67% |
|           32 |       3 |         +0.55% |         -0.37% |              -13.97% |              -12.15% |
|           32 |      10 |         -9.25% |        -13.79% |              -45.70% |              -63.08% |
|           64 |       3 |         -0.19% |         +1.70% |               +2.43% |               +1.10% |
|           64 |      10 |         -0.28% |         -1.15% |              -30.53% |              -44.11% |
|          256 |       3 |         +0.76% |         +0.06% |               +0.90% |               +1.12% |
|          256 |      10 |         +0.95% |         +0.47% |               +0.06% |               +0.35% |

## Controlled read latency

| Tail entries | Workers | Linux selected | macOS selected | Linux rejected quota | macOS rejected quota |
| -----------: | ------: | -------------: | -------------: | -------------------: | -------------------: |
|           16 |       3 |         -0.01% |         +0.22% |              +95.54% |              +99.26% |
|           16 |      10 |         -1.15% |         +0.10% |             +269.97% |             +294.06% |
|           64 |       3 |         -0.01% |         -0.05% |               -0.00% |               +0.10% |
|           64 |      10 |         +0.05% |         +0.36% |              +94.70% |              +97.81% |

## Complete native commands and negative controls

Initial native results are small and include slower samples. A five-session six-case confirmation adds a third participant pointing to the exact baseline executable, rather than inventing a source-level no-op. Preserve both populations: the confirmation does not replace initial samples.

| Fixture / task          | Workers | Initial Linux | Confirm Linux | Baseline repeat Linux | Initial macOS | Confirm macOS | Baseline repeat macOS |
| :---------------------- | ------: | ------------: | ------------: | --------------------: | ------------: | ------------: | --------------------: |
| flat10000 / files       |      10 |        +5.10% |        +0.77% |                -2.23% |        -0.71% |        -0.18% |                +0.07% |
| flat272 / rg-filter     |      10 |        -3.09% |        +1.07% |                +0.14% |        +0.69% |        -0.10% |                +0.79% |
| heavy320 / rg-filter    |      10 |        +0.15% |        +1.82% |                -0.04% |        +0.44% |        -0.54% |                +0.13% |
| heavy288 / rg-filter    |      10 |        -2.02% |        +4.13% |                +1.60% |        -0.34% |        +0.28% |                +0.56% |
| compare-large / compare |       3 |        +1.88% |        +2.15% |                +2.76% |        -0.48% |        +1.34% |                -0.13% |
| flat288 / rg-filter     |       3 |        +2.81% |        +3.33% |                +1.76% |        -0.11% |        +0.86% |                +0.39% |

The original Linux 10,000-file listing control is +5.10%, while its confirmation is +0.77% and the duplicate baseline is -2.23%. That path does not use this matcher pool, so these shifts cannot be attributed to fewer participants. The Linux heavy-288 filter confirmation is +4.13% (duplicate baseline +1.60%), reversing the initial -2.02%; flat-288/three-worker filtering confirms +3.33% (duplicate baseline +1.76%). Keep these unresolved small whole-command costs visible. This is a targeted overhead reduction, not a universal throughput win.

macOS complete commands mostly remain near baseline; the comparison-large/three-worker control confirms about +1.34%. It likewise does not exercise the changed matcher participation. No first-output, allocation-count, RSS or end-to-end scaling gain is inferred from the kernel percentage alone. Raw native wall/user/system time and peak child RSS remain available.

## Validation and footprint

- linux: unstripped CLI build output 12,292,520 -> 12,292,800 bytes (+280).
- macos: unstripped CLI build output 4,051,424 -> 4,051,424 bytes (+0).

The regression test grows four workers to twelve, preserves twelve useful workers for sixty-four entries, limits 16/32-entry batches to four/eight participants, passes an empty batch, regrows, runs short tails inline, and destroys the pool with inactive participants. A read barrier records real worker identities rather than assuming requested counts. Existing output, cancellation, deferred state, archive admission and safety coverage remain in the full suites.

Both complete core/extension release suites pass 306/306, with required kernel FUSE mounts on Linux. Focused macOS ASan and Linux TSan each pass the three matcher/rg/run suites. Six affected translation units pass local header-aware clang-tidy, including the manual tail benchmark. Validation logs are retained in the archive. Final current-head CI and normal enforcing hooks remain required before merge. O12 adapter work, JC role budgets, cancellation, slow/network storage qualification and stage overlap remain separate open items.

## Lossless evidence

[useful-match-workers-20261102.json.gz](useful-match-workers-20261102.json.gz): 976,431 bytes; SHA-256 `1f83ab3a032f31666c6d37587c5252003cef55bc470c72b986f6ac50e96a5d5f`. Recover fixture tables through `fixtures[report.fixtures_ref]`; source patches and scripts preserve rejected variants and the invalid-delay control.
