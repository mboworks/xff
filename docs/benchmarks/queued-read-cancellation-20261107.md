# Queued read cancellation: bounded policy decisions

Neither tested O18a runtime policy qualifies for adoption. A deterministic test proves that
stopping a traversal can avoid five queued backend reads, but ordinary workloads expose costs.
Retain the implementations and their complete screens; this PR ships documentation and evidence
only. General O18 backpressure, fairness and ordered publication remain open.

The base is `d8cf61cccd4b0efed5058bf6fa45228e47162b48` (merged PR #1126). These are isolated optimized builds on the local
macOS arm64 and SSH Linux x86-64 hosts. All percentages are elapsed-time changes: negative is
faster. A same-image repeat measures drift using the exact baseline executable. Cohort medians
do not override individual slower cells, all of which remain in the downloadable evidence.

## Ownership and deterministic behavior

The existing `ReadPool` owns stable deque task slots. A consumer abandoning a pending request
marks it abandoned but does not recycle it; a producer completing after abandonment performs
the final release. Active VFS operations cannot be interrupted. Recycled slots must never be
accessed by an old queue generation or a late producer.

The predeclared test creates two actually active, condition-variable-blocked sibling reads,
one available first child and five queued siblings. It stops on that child's first file.
A parent-listing error payload uses an external Cord cleanup to release the blocked reads only
after frontier leases unwind. It verifies that release has not occurred before stopping; bounded
waits fail deadlocks rather than guessing scheduling with sleeps. Complete visits, errors and
coordinator callback identity must match serial traversal in directory-first and tree order.

The unchanged parent performs nine backend reads and fails the four-read expectation on both
hosts. Each measured policy performs exactly four: root, consumed child and two active siblings.
Continued pruning/recycled-request tests also preserve serial visits and errors. This is an
avoided-call result; no synthetic-delay elapsed speedup is claimed.

## Three retained implementations

1. **Dispatch acquire load.** Before executing its leaf operation, the dequeued producer loads
   the task state with acquire ordering. An abandoned request releases its producer-owned slot
   and skips the VFS operation. Late abandonment retains the existing completion protocol.
   This costs an additional load per dispatched task, with no new pool or allocation.
2. **Destructor-only draft, unmeasured.** Stop dispatch when pool shutdown starts under the
   existing mutex. One focused Mac test passed, but source audit found an ownership gap:
   parent payload cleanup can release workers before the pool destructor sets its stop flag.
   Those workers can claim queued requests in that interval. Retain this draft without a
   correctness or timing claim.
3. **Explicit stop boundary.** Set the pool's stop flag under its existing mutex synchronously
   when the coordinator commits a stop, including propagation from a nested archive walk.
   Workers already holding tasks finish; `Next` refuses new claims once stopped. Queued slots
   stay owned until workers join. The destructor calls the same idempotent stop helper.
   There is no per-task acquire load, new wrapper or allocation. Normal nonterminal pruning
   retains its earlier protocol; this policy does not skip every pruned request.

## Bounded production API screens

Each policy has 128 cases per host: broad/deep, lazy/eager metadata, files/name-txt, names/full
paths, 10/100/1,000/10,000 files and one/three requested workers. The unchanged production
`name_listing_benchmark` calls `RunFind` on the in-memory filesystem, with its complete-output
preflight. Existing traversal tests separately compare complete serial/parallel visit/error
sequences. It is not a synthetic queue-only throughput benchmark.

Each screen is **one session, three raw repetitions, fastest two retained**, with baseline,
candidate and byte-identical baseline repeat: 1,152 observations per host per policy. Processes
rotate participant order by CPU group; cases and repetitions interleave randomly inside each
process. Linux pins one or three distinct allowed physical cores; Mac worker counts are requested
but CPU affinity is unavailable. Raw units are checked as nanoseconds. These screens stopped
qualification; they are not the full three-session, nine-repetition/seven-retained protocol.

Dispatch acquire-load screen:

| Tree  | Workers | Mac candidate | Mac repeat | Linux candidate | Linux repeat |
| ----- | ------: | ------------: | ---------: | --------------: | -----------: |
| broad |       1 |        +2.78% |     +0.87% |          -0.07% |       -0.50% |
| broad |       3 |        +3.55% |     -0.10% |          +1.37% |       -0.35% |
| deep  |       1 |        +2.75% |     +0.97% |          +0.16% |       -0.83% |
| deep  |       3 |        +2.16% |     -0.19% |          +1.71% |       +0.42% |

Explicit-stop screen:

| Tree  | Workers | Mac candidate | Mac repeat | Linux candidate | Linux repeat |
| ----- | ------: | ------------: | ---------: | --------------: | -----------: |
| broad |       1 |        +6.16% |     +1.32% |          -1.99% |       -0.56% |
| broad |       3 |        +2.39% |     +2.68% |          +1.80% |       +1.08% |
| deep  |       1 |        +8.58% |     +1.03% |          +0.22% |       +0.71% |
| deep  |       3 |        +0.71% |     +1.13% |          +4.48% |       +0.98% |

For example, explicit stop's Mac broad/one-worker 100-file names/files case is +11.42% against
a +1.59% repeat; Linux deep/three-worker 10-file names/name-txt is +22.59% against +2.04%.
Code layout, allocation placement and scheduling have not been isolated as causes. The screen
does not justify inventing a file-count or timer threshold, nor repeating unchanged builds until
results improve. Both policies remain unselected.

## Explicit-stop native CLI cost controls

The frozen full CLI images use the same base and shipping release configuration. This separate
screen covers flat/many-directory 10/100/1,000/10,000-file trees, one/three workers and files,
metadata and content tasks: 48 cases per host. Each has a discarded participant warmup followed
by three measured rounds, fastest two retained, one session. Participants rotate every round.
The native launcher measures fork/exec/wait, excluding Python startup. Every invocation checks
exit status and all output paths or path/size records against an independently generated oracle.
There are 576 checked invocations per host including warmups (432 measured). Linux fixtures and
capture files are in RAM-backed `/dev/shm`; Mac uses its retained native temporary fixtures.

| Workers | Task     | Mac candidate | Mac repeat | Linux candidate | Linux repeat |
| ------: | -------- | ------------: | ---------: | --------------: | -----------: |
|       1 | files    |        +3.24% |     +0.31% |          +1.21% |       +0.56% |
|       1 | metadata |        +2.34% |     +0.36% |          -0.32% |       -0.21% |
|       1 | content  |        +2.41% |     -0.03% |          -1.85% |       +0.56% |
|       3 | files    |        +3.14% |     +0.75% |          -0.33% |       +0.87% |
|       3 | metadata |        +0.65% |     +0.68% |          +0.09% |       +1.61% |
|       3 | content  |        +2.85% |     +0.65% |          -0.77% |       +1.72% |

These are ordinary cost controls, not early-stop latency tests. The Mac costs reinforce the
decision to withhold runtime adoption; largely flat Linux cohorts do not erase those costs.
The Linux screen first failed fixture discovery before any timing because an assumed path was
wrong. Its failed manifest/log are retained alongside the corrected existing `/dev/shm` path.
No partial measurement population was combined with the corrected screen.

## Verification scope and limitations

- Parent proof: both hosts fail the expected nine-versus-four count, with raw logs retained.
- Dispatch policy: full 323-target optimized suites pass on both hosts (Mac `clang_release`,
  Linux `clang_opt`, normal extension/documentation selections). Focused Mac ASan and Linux
  ASan/MSan/TSan traversal tests pass. These full-suite results belong to this first policy only.
- Explicit stop: focused optimized traversal and Mac ASan, Linux ASan/MSan/TSan tests pass.
  This revision was screened out before another full 323-target integration or public runtime
  CI run. Do not transfer the dispatch policy's full-suite claim to it.
- Native/API source, configuration, binary lengths and SHA-256 identities are retained.
  The Mac listing images remain 3,610,616 bytes and full CLI images 8,275,000 bytes; equal size
  does not mean equal instructions or equal elapsed cost. Linux image identities are separate.
- No active-I/O preemption, global byte admission, fairness or retained-byte improvement is
  established. The draft's nested-stop path has a source audit, not an independent controlled
  nested-stop performance population. No runtime is published from any phase.

## Evidence and reproduction

[Download the lossless evidence capsule](queued-read-cancellation-20261107.json.gz): 147 individually hashed
records, 11,291,859 decoded bytes, 1,059,238 compressed bytes.
Its SHA-256 is `df49ed6f0ba47500afd0d5d5d5c8291d2f36f6a18b2eda430f4e79209db7f159`.

Decompress the JSON, then restore each `records` item using UTF-8 or base64 as indicated.
Verify its `bytes` and `sha256` before use. Exact base sources, both measured variants, the
unmeasured destructor draft, the runtime/test patch, unchanged production benchmark source,
native launcher source, driver commands, all raw observations, all-cell CSV, expected failures
and sanitizer/build logs are included. Executable images and redundant transport archives stay
private; their identities and reconstructible source/configuration are retained instead.

The API commands and environment are recorded in each population's `manifest.json`; native
commands use `native-screen.py --manifest ... --rounds 3 --retain 2 --sessions 1`. Those
experiment drivers are evidence, not new supported production tools. Their paths identify the
isolated original fixtures and binaries; regenerate equivalent fixtures and adjust paths to
reproduce. Any future policy must retain exact cancellation/recycling contracts, test ordinary
costs on both hosts and separately measure real early-stop latency before selection.
