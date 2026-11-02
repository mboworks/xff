# Typed reusable directory jobs (2026-11-02)

## Decision

Retain typed reusable task records and per-task atomic completion on the existing directory worker pool. Replace per-job shared packaged tasks, futures and erased queue callbacks with move-only typed requests to stable slots. No new executor, worker policy, clock gate, CLI flag or dependency is added. The former futures already used persistent workers; they did not create a thread per file.

The initial typed implementation completed each result under the global queue mutex. That implementation is rejected despite lower allocation counts: Linux directory-heavy controls regressed about 11-14% with some worker/task combinations. The refined implementation publishes each result through a three-state atomic handoff and uses the queue mutex only for admission, queue operations and recycling. Standard atomic wait supplies blocking for the sole consumer.

Keep the original two-directory admission rule, 128-entry stat chunks, 512-entry eager-stat gate, source/archive restrictions, four-listings-per-worker frontier and requested worker counts. This does not reintroduce the rejected compact frontier ring, narrower window, remaining-work planner or timed admission policy. Serial/file-only scans allocate no queue storage.

## Ownership and synchronization

A request exclusively owns the consumer lease for one deque-stable task record. Moves disengage the old lease. Required storage/pool observers use references; optional task observers use OptionalRef. The producer alone reads inputs and fills its result. A release/acquire atomic publication hands that payload to its consumer. The second participant discards an abandoned result: either abandonment sees Ready, or completion sees Abandoned. Exactly one participant recycles the slot.

A late notification after recycling is harmless: atomic wait rechecks the current value, and a slot cannot be reused while its old consumer still waits. Slot addresses remain valid until all pool workers join. The queue drains already submitted jobs when walking stops, preserving existing speculative-read behavior; abandoned errors are not published as visited errors. Move discarded payloads into a local value so their allocation cleanup runs after the queue mutex unlocks.

Directory paths remain owned by their tasks. Stat slices borrow their listing only until all stat results are consumed. Jobs remain leaves: no worker submits work to its own pool or waits for a sibling. Pool destruction joins workers before the filesystem owner or walker state dies. No shared ownership is introduced for task records.

Slot metadata retains its high-water capacity until that walker ends, while strings/result payloads are released promptly. The frontier bound is per active recursion level and counts requests; it is not a global byte bound, and abandoned queued jobs still drain. Do not claim constant retained memory for arbitrary tree depth or huge listings.

## Measurement contract

Both phases use exact parent `d2ff56bc444be383de44b3e3b5dd349700abac87`, including O01-O07 runtime changes. After #1047 merged, the publication branch was synchronized onto main; the two base trees are identical. Preserve the original measurement identity rather than relabel old data. Release binaries use LLVM 23.1.4 + ThinLTO; all sources, source/executable hashes, build flags, raw samples and scripts are in the archive below.

Initial variants are baseline, lazily allocated old queue, and typed slots with global-lock completion. Refined variants are fresh baseline, the global-lock candidate and atomic completion. The lazy-only control isolates constructor allocation from repeated task handoff. Do not pool absolute times between phases.

The 80 native cases cover ten files; flat 1,000/10,000-file directories; 8/1,024 one-file directories; 8/128 directories of 512 files; an 8-by-8 nested tree of 64 files; long collection names; and small/large two-root comparison controls. Files contain `x` plus newline. Requested 1/3/10 workers cover listing, size-formatting and literal-content tasks, with the established largest-content and collection selections preserved.

Each case has three sessions, a discarded warmup per variant/session, nine interleaved repetitions and the fastest seven retained. Every native invocation validates an independent path/size digest or complete collection/comparison records. Both phases together contain 28,800 fresh invocations across the hosts, including warmups. Linux uses tmpfs with affinity restricted to requested CPUs; macOS uses warm APFS and worker allowances without affinity. Builds, sanitizers and lint finish before timing on each host.

Separate in-memory RE2 engine controls retain the established 36 workloads, 1/4 requested workers, nine repetitions/fastest seven and two sessions, without CPU affinity. Two Linux rg-files cells vary sharply between those sessions (+26.47/-3.25% and +20.41/-2.14%); retain them unchanged. Supplementary single-case processes interleave all variants across three sessions and eight cases. Linux supplements use explicit 1/4-CPU affinity, a distinct contract; macOS remains unpinned. Neither kernel population has the native driver's independent per-invocation output oracle.

## Refined native results

Changes are medians of paired session ratios; negative means faster. These are selected representative cases, with every case retained in the archive.

| Fixture / task          | Workers | Linux change | macOS change |
| :---------------------- | ------: | -----------: | -----------: |
| broad1024x1 / files     |       3 |      -48.27% |       -1.93% |
| broad1024x1 / files     |      10 |       -8.03% |       +0.29% |
| broad1024x1 / metadata  |       3 |      -23.30% |       -0.28% |
| broad1024x1 / metadata  |      10 |      -11.76% |       +0.17% |
| broad1024x1 / content   |      10 |       -5.69% |       +0.48% |
| broad128x512 / files    |       3 |       -4.09% |       +0.32% |
| broad128x512 / metadata |       3 |       -0.39% |       -0.64% |
| nested8x8x64 / files    |       3 |       -0.26% |       +0.11% |
| flat10000 / metadata    |       3 |       +1.12% |       -0.23% |
| flat10000 / metadata    |      10 |       +1.40% |       +0.48% |
| flat10 / files          |       1 |       -0.33% |       +0.52% |

The large Linux many-directory gains are not universal scan speedups. macOS is mostly near baseline and its syscall/storage cost differs. The global-lock Linux control again regresses 10.50% for many-directory ten-worker listing and 13.63% for its three-worker metadata case; atomic completion changes those to -8.03% and -23.30%. This establishes the synchronization refinement, rather than attributing all elapsed changes to allocation counts.

## Allocation traffic and footprint

ASan diagnostics never provide performance timings. Hooks accept only the initializing coordinator; worker callbacks are rejected before accessing global POD counters. This avoids the macOS lazy-TLS bootstrap recursion exposed by the first diagnostic that started actual worker threads. A committed worker-start regression covers it. Fixture setup, warmup and assertions occur outside measurement. Cross-thread frees are excluded, so allocation/free differences are not live-byte or leak accounting.

| Host  | 1,024-directory coordinator allocations: old | Atomic slots | Requested bytes: old | Atomic slots |
| :---- | -------------------------------------------: | -----------: | -------------------: | -----------: |
| macos |                                        4,145 |        1,056 |              617,136 |      332,656 |
| linux |                                        4,137 |        1,056 |              625,104 |      332,656 |

The lazy-only queue control does not remove per-job allocation traffic; both typed mechanisms reduce it about 75% in this diagnostic. Counts include unavoidable coordinator traversal work and exclude worker result allocations. This is not an all-process allocation claim or a peak-RSS reduction of the same percentage.

- macos: worst native paired median +1.16% at ('broad8x1', 10, 'files'); all small cases and slower samples remain.
- macos: build-output executable 4,070,368 -> 4,051,984 bytes (-18,384), before release stripping/compression.
- linux: worst native paired median +3.40% at ('broad8x1', 10, 'metadata'); all small cases and slower samples remain.
- linux: build-output executable 12,309,616 -> 12,295,424 bytes (-14,192), before release stripping/compression.

## Supplementary in-memory controls

The focused Linux rg-files medians are +2.79% (1,000/four workers) and +1.40% (10,000/four). Those remain small costs, not erased regressions. No claim is made that every kernel improves.

| Kernel control          | Linux change | macOS change |
| :---------------------- | -----------: | -----------: |
| RE2/filter/10/1         |       +1.03% |       -0.17% |
| RE2/native-lines/1000/1 |       -0.09% |       -2.22% |
| RE2/rg-files/10/4       |       +2.44% |       -0.91% |
| RE2/rg-files/1000/4     |       +2.79% |       +0.29% |
| RE2/rg-files/10000/4    |       +1.40% |       -0.06% |
| RE2/traversal/10/4      |       +0.65% |       +0.02% |
| RE2/traversal/1000/4    |       +1.79% |       +1.96% |
| RE2/traversal/10000/4   |       +1.05% |       +0.38% |

## Validation and limits

Both full core/extension release suites pass 305/305 for both implementations; Linux requires working FUSE integration. Focused macOS ASan, Linux TSan and the full allocation diagnostics pass. Scoped local clang-tidy passes all three modified translation units. Tests cover mixed stat/directory slot reuse above the actual 512-entry stat gate, partial chunk tails, repeated windows, moved/default requests, coordinator visit order, ordered read errors, early-stop drain bounds and abandoned-error suppression. Existing archive/owner, metadata-demand, symlink, prune/depth and readdir-race tests remain in the full suites.

These measurements qualify cached local storage and in-memory engine controls, not new network-storage concurrency defaults or archive parallel admission. There is no default worker-count change. Further cache-line layout, batching and shared-executor experiments need their own evidence; do not fold their unmeasured hypotheses into this result. Normal commit/push hooks and current-head CI are required before merge.

## Lossless evidence

[directory-task-slots-20261102.json.gz](directory-task-slots-20261102.json.gz): 1,418,765 bytes; SHA-256 `b3810d7d8e2553f11416c4307886a6959e1b4d910777c921a51f35ccdc1766af`. Restore a native report's fixtures from `fixtures[report.fixtures_ref]`. All initial, refined, targeted, rejected/lazy controls and source identities remain available.
