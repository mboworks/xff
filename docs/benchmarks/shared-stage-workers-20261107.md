# Shared stage-worker qualification, 2026-11-07

Keep the existing directory, matcher and comparison pools. Six shared-executor
implementations were built and measured; none qualifies as a cross-platform
replacement. Small native comparisons sometimes improve, but the final candidate
still makes a 1,000-pair, four-worker Linux comparison 15.55% slower. Repeating the
same experiment on macOS gives +1.51%.

This completes O17's qualification, not O18's byte-bounded overlap work or the wider
performance goal. No production worker, queue, admission or CLI behavior changes in
the qualification PR. The rejected implementations remain reconstructible from
their source patches and raw measurements in the
[lossless evidence archive](shared-stage-workers-20261107.json.gz).

## Parent and implementations

All trials start from main `c95140674254c371844f1fab27a64951a80dac55`
(#1119). The baseline already retains each stage's threads. The question is whether
reusing threads across stages and the two comparison walks improves that design.
It is not a test of a thread-per-file baseline.

1. `direct-leaf-safe` uses one fixed-size shared executor, typed directory slots and
   promise-free per-slot dispatch. Owners drain before their task storage or
   filesystem can die. Matching and comparison retain ordered results.
2. `coarse-drain` retains the typed directory queue and dispatches a bounded number
   of drains instead of a generic job for each directory. Drains run leaf VFS
   operations and never wait for another executor job.
3. `demand` preserves independently useful stage allowances: readers and matchers
   can overlap, as can two comparison walks. The executor grows for outstanding
   jobs, retains its threads and resolves the requested worker count once.
4. `role-demand` limits growth by task class and initializes private matcher state
   on workers, matching the original dedicated pool's placement. Indexed state
   survives between batches even when a different OS thread runs the next batch.
5. `disjoint-phases` restores the original matcher pool. Explicit stage claims
   preserve concurrent directory-reader allowances and reuse those threads for
   comparisons after both walks finish. Per-job demand accounting is removed.
6. `stage-claims` also preserves independent nested mounted-walk allowances and
   separates growth from dispatch locking. Thread creation no longer holds the
   dispatch mutex. The exact final Linux and macOS source patches are identical.

No retained candidate adds a millisecond admission probe, lifetime file counter,
per-file promise, process-wide pool or nested worker wait. Effects and ordered
publication stay with their coordinators. The existing bounded directory frontier
is preserved; this does not establish a global retained-byte bound.

An earlier atomic-drain draft published an in-flight zero before its last owner
access. That lifetime race invalidates the draft. Its source and screening data are
retained separately and excluded from every adoption decision. Safe variants
complete their final owner access under the owner's completion mutex.

## Measurement protocol

Both hosts explicitly build performance executables with `--config=clang_release`
and ThinLTO. #1119 changes which configuration CI selects; it does not prevent a
manual benchmark from selecting the production configuration. No build, sanitizer
or lint runs alongside a timing population on the same host.

The in-memory selection matrix covers RE2 and PCRE2; flat traversal, native content
filtering, many-directory content filtering and rg file selection; 10, 100, 1,000 and
10,000 files; and 1, 3 and 10 workers. Many-directory fixtures have up to 256 sibling
directories. This gives 96 cases per session per host. Preflight checks validate
the selected output. Three sessions alternate executable order. Average the fastest
seven of nine repetitions for each participant and session, then report the median
of the three paired percentage changes.

The native matrix has 126 cases per session: flat and many-directory trees at the
same four sizes; 1, 3 and 10 workers; listing, name, metadata, content and rg
controls; and small and large two-tree comparisons. Three sessions each discard
one warmup per participant/case, rotate participant order across nine rounds, and
average the fastest seven. Every native invocation is checked against an independent
fixture-derived output oracle. An unchanged duplicate baseline is measured alongside
the candidate as a noise control. The lean native CLI uses RE2; PCRE2 is explicitly
linked in the in-memory matrix.

Linux uses drogon's AMD Ryzen 9 9950X, tmpfs fixtures, and one logical CPU per selected
physical core. macOS uses the local Apple M5 Pro, warm APFS and no affinity. The
native launcher measures fork/exec/wait after Python startup and records child CPU
time and RSS separately. Known-duration sleep controls check this measurement-only
launcher. Its clock calls never enter production XFF.

The large native comparison has only 32 pairs of 262,144-byte files, making it a
short-command/startup workload. A supplementary existing comparison benchmark
therefore covers four scenarios at 10/100/1,000/10,000 pairs and 1/4 workers. It uses
the same three-session, fastest-seven-of-nine estimator. The final prototype's
focused repeat keeps both 1/4-worker controls for the identified 1,000-pair regression.

The archive retains every raw repetition, binary identity recorded by the drivers,
source patches, exact invocations, build context, validation logs and analysis
summary. `rg-driver-v1.py` and `paired-driver-v2.py` distinguish fixture/driver
revisions. The final benchmark-helper cleanup preserves behavior; exact measured
fixtures remain in the patches. A cache-size diagnostic overlapped the first
`role-demand` Linux memory run. That population is labeled contaminated and excluded;
`memory-linux-clean` is the separate uncontaminated repeat. A launcher-field mismatch
stopped the first native attempt before valid results; it is not a timing population.

## Results and decision

Positive values mean longer elapsed time; negative values mean faster. These are
selected counterexamples and controls, not a claim that all measured cases behave
alike. The archive's `summary` contains the complete case-level estimates.

| Variant          | Workload                                   | Mac change | Linux change |
| :--------------- | :----------------------------------------- | ---------: | -----------: |
| direct-leaf-safe | Memory RE2, many directories, 10k, 3 jobs  |     -0.32% |      +27.07% |
| coarse-drain     | Memory RE2, many directories, 10k, 3 jobs  |     -0.19% |      +27.96% |
| demand           | Memory RE2, many directories, 10k, 3 jobs  |     -0.03% |       +5.57% |
| role-demand      | Memory RE2, many directories, 10k, 3 jobs  |     +0.01% |      +10.36% |
| disjoint-phases  | Memory RE2, many directories, 10k, 3 jobs  |     -0.21% |       -0.72% |
| demand           | Native rg, flat 100, 10 jobs               |     -2.02% |       +8.29% |
| role-demand      | Native RE2, many directories, 10k, 3 jobs  |     -0.50% |       +9.42% |
| disjoint-phases  | Native large comparison, 32 pairs, 10 jobs |     -4.90% |      -15.64% |
| stage-claims     | Memory small identical, 1k, 1 job          |     +1.86% |       -0.25% |
| stage-claims     | Memory small identical, 1k, 4 jobs         |     +1.51% |      +15.55% |

The fixed-size shared pool reduces the baseline's independently useful stage
allowances. Typed drains alone do not remove its Linux many-directory regression.
Preserving allowances and worker-local matcher initialization reduces some costs but
still leaves native counterexamples. The `demand` Linux flat-100 rg sessions are
+7.41%, +8.29% and +8.59%; its unchanged-baseline control is -1.13% at the median.
The `role-demand` Linux 10k RE2 sessions are +9.42%, +9.49% and +1.95%; its unchanged
baseline control is +0.09% at the median.

Restoring the dedicated matcher pool removes that particular memory regression.
For `disjoint-phases`, the native 32-pair comparison improves on both hosts; its
unchanged-baseline controls are -1.25% on Mac and +1.30% on Linux. However, the wider
Linux comparison matrix exposes +12.92% for 1,000 small identical pairs at four
workers: +12.92%, +10.72% and +20.37% across sessions. Other large cases can still
improve, including 10k early-different pairs at four workers (-4.17%).

Preserving nested allowances and separating the growth lock does not cure that
comparison cost. The final focused Linux sessions are +15.55%, +14.81% and +22.69%,
while the one-worker control is -0.25% at the median. Mac's four-worker sessions are
+2.63%, +0.98% and +1.51%. The generic executor introduces an additional queue and
dispatch/completion path compared with waking the existing persistent batch workers;
these measurements do not isolate one instruction or lock as the sole cause.

Reject all six implementations. There is no evidence here that replacing the
specialized pools with this generic dispatch surface is a net improvement. Preserve
the useful tests and source designs as research evidence, rather than shipping a
known Linux regression or adding workload-specific admission thresholds to hide it.
Any new reuse design must first beat these counterexamples while preserving
independent root/mount allowances, leaf execution, owner draining and ordered output.

## Correctness evidence and limits

The `disjoint-phases` trial passed all 335 core/extras test targets on each host,
including required real FUSE mounts on Linux, six focused TSan targets on Linux, and
six focused ASan targets on macOS. Those runs do not validate the later source as if
it were unchanged. The final `stage-claims` trial passed five focused optimized
targets on Linux and five focused ASan targets on Mac. It was rejected by timing
before another complete-suite run; it is not a production-ready change.

Two fixture assumptions surfaced during validation. A Mac temporary directory can
inherit a different group from the effective process group; the conformance fixture
must ask for its actual group. The relative-ignore-path test must place roots deep
enough that a working-directory-relative path cannot accidentally reach the same file
from a root. Both test-only corrections are in the archived trial patches; neither
is claimed as a worker optimization.

A black-box nested-mount overlap test requires two outer readers and three useful
inner readers to retain their independent allowances. Restoring a run-wide three
worker ceiling makes that test fail after its bounded five-second fallback; the
uncapped stage-owner implementation passes. This is a correctness constraint, not
a timing threshold in production. The final Linux and Mac source patch SHA-256 is
`1e79800c968945656c82e01cc72cd38bd246177a0bc31e8fb382b48f6593dbc5`.

These are local qualification populations, not official historical benchmark-series
replacements. The committed PR contains documentation and archived evidence only;
its CI does not adopt or exercise a new worker implementation. O18 and the remaining
O01-O22 items retain their own qualification requirements.
