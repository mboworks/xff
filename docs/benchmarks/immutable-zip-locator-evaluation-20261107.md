# Immutable-memory ZIP locators, 2026-11-07

Defer this runtime policy. A complete prototype removes repeated prefix scans and
improves opening/reading later members by roughly 47% on both hosts, but has
repeatable first-read regressions: 27% on Mac for one large deflate member, and
11% on Linux for two large stored-member cases. The report preserves the working
implementation, every measurement population, controls, failing correctness proof,
and rejected variation. This change publishes research and tracking only.

## Implementation retained for follow-up

The prototype builds on `d8cf61cccd4b0efed5058bf6fa45228e47162b48`. A source capability
states that bytes are immutable across independent opens and that positions and
size are available cheaply. Memory sources satisfy it. Host, unknown, decoded and
member sources default to false: having `OpenAt` alone does not establish stable
identity when opening the same pathname again.

The required listing records unfiltered, unencrypted ZIP header offsets in a
scalar array. Index construction transfers those offsets to the existing synthetic
inode IDs. It adds no owned path index and does not enlarge `Member` or `Node`.
Both arrays' complete capacities, including overlapping growth storage, are charged
before allocation against the shared read budget. Retained-index admission leaves
one 64 KiB reader workspace available; admission failure drops the optional index
and keeps sequential reads. Returned member sources own their name, parent and
offset value, and can outlive the filesystem. Every open owns an independent cursor
and libarchive session; no new lock or shared cursor is introduced.

Metadata retains the last duplicate declaration while content retains the first
normalized name, preserving the existing behavior. Reopening validates capability,
source bounds, header identity and regular-file type. Original source error statuses
propagate. These contracts, rather than member-count or payload-size heuristics,
control eligibility.

A valid ZIP need not include data-descriptor signatures. An independently generated
230-byte fixture reproduces generic format bidding rejecting a cached position
that starts at preceding CRC bytes. Python `zipfile` independently validates both
exact payloads. The corrected prototype selects libarchive's streaming ZIP reader
and disables outer filters at known unfiltered ZIP positions. The failing proof,
fixture generator, fixture bytes, earlier source and corrected source are all kept.
The audit uses the pinned libarchive 3.8.1.bcr.3 implementation.

The existing streamed-ZIP metadata-size defect remains open: local headers may defer
sizes to descriptors, and unchanged parent listings can report zero for real
payloads. This prototype does not fix it. Benchmark counters explicitly retain
those size discrepancies; no exact-size or native-host ZIP speedup is claimed.

## Current production filesystem measurements

Baseline and candidate use identical research harness/BUILD inputs and actual
`ArchiveFileSystem::OpenSource` over immutable memory. Both are built with
`clang_release` and `xff_full`, including ThinLTO, without sanitizers. Source,
configuration and frozen executable hashes are retained. The Mac is Apple M5 Pro
arm64; Linux is a 10-CPU allocation on Ryzen 9 9950X x86_64 (`drogon`). Linux pins
the API benchmark to one allowed CPU; Mac is unpinned. Builds, lint and sanitizer
checks do not overlap our timing on the same host.

Each host completes 90 cases, three sessions, nine raw repetitions per case and
participant: 7,290 observations. Participants are baseline, candidate and an exact
repeat of the baseline executable. Their order rotates by session; each benchmark
process interleaves its cases/repetitions randomly. Retain the fastest seven,
average within case/session, then take the median paired change across sessions.
Positive percentages are slower. Every case and counterexample remains available.

The fixture matrix covers stored/deflate ZIPs at 10/1,000/10,000 members with
128-byte payloads, plus 4 KiB, 64 KiB and 1 MiB payload cohorts and additional
40-member stored 1 MiB controls. The raw data gives the exact 90-case list.
A preflight checks complete names, paths, types, source and read-only status.
Every selected content read, including timed iterations, checks exact payloads.
First/middle/last selection is derived independently from fixture generation.

`first`, `last` and `cycle` retain the filesystem and member source outside timing;
`cycle` reads first/middle/last. `open-*` includes constructing the filesystem,
reading the selected content, and destruction in each iteration. `names-only`
constructs the filesystem and lists its complete directory. The following are
medians across each task's cases, not a promise about every cell:

| Task       | Mac candidate | Mac baseline repeat | Linux candidate | Linux baseline repeat |
| :--------- | ------------: | ------------------: | --------------: | --------------------: |
| names-only |        +1.11% |              +0.36% |          -0.06% |                -0.07% |
| first      |       -17.38% |              +2.26% |         -23.77% |                -0.06% |
| last       |       -98.58% |              -0.55% |         -98.88% |                -0.31% |
| cycle      |       -97.71% |              -0.09% |         -98.63% |                -0.16% |
| open-first |        -0.10% |              -0.35% |          -1.22% |                -0.20% |
| open-last  |       -47.03% |              -0.19% |         -47.73% |                -0.04% |
| open-cycle |       -57.19% |              -0.11% |         -57.82% |                -0.23% |

The material exceptions drive the decision:

| Case                                             | Candidate | Same-image repeat |
| :----------------------------------------------- | --------: | ----------------: |
| Mac deflate, retained first, 10 members x 1 MiB  |   +27.17% |            +0.50% |
| Linux stored, retained first, 10 members x 1 MiB |   +10.95% |            -1.12% |
| Linux stored, retained first, 40 members x 1 MiB |   +11.45% |            -0.42% |
| Mac stored, retained first, 40 members x 1 MiB   |   +12.39% |            +3.53% |

Mac's tiny 10-member listing is additionally noisy: stored candidate changes are
-14.08%, +44.65%, +76.56% across sessions, while exact-image repeat changes are
+13.31%, +7.61%, +69.21%. Deflate tiny-listing medians are +22.15% candidate and
+23.80% repeat. This variability weakens conclusions for those cells; it does not
explain away the stable-control large-deflate first-read regression.

## Native-file controls and binary cost

Both hosts also complete 48 full-CLI cases across ordinary trees, TGZ, stored ZIP,
and deflate ZIP; workers 1/3; listing, first content and last content. The native
fork/exec/wait launcher excludes Python startup and includes the whole xff command.
Fixtures are independently generated and ZIP contents fully read back. Every run
checks exit status and byte-exact complete stdout. Three sessions of nine rotating
participant rounds plus discarded warmups yield 4,320 oracle-checked invocations
per host. The estimator again retains fastest seven and reports median paired
session changes. Linux pins one or three distinct available physical cores; Mac
is unpinned with the requested worker count.

| Native cohort | Mac candidate | Mac repeat | Linux candidate | Linux repeat |
| :------------ | ------------: | ---------: | --------------: | -----------: |
| Ordinary tree |        +0.85% |     +0.23% |          +0.16% |       +0.18% |
| TGZ           |        +1.05% |     +0.26% |          +0.70% |       +0.40% |
| Stored ZIP    |        +0.68% |     +0.71% |          +0.83% |       -0.03% |
| Deflate ZIP   |        +0.76% |     +0.49% |          +0.40% |       -0.01% |

Native sources keep sequential fallback. These controls show no material native
improvement. Every slower cell is retained, including Mac stored-10/1/list +3.31%
(control -0.27%) and Linux TGZ/3/list +2.30% (control +0.71%).

| Frozen unstripped full CLI | Baseline bytes | Candidate bytes |       Increase |
| :------------------------- | -------------: | --------------: | -------------: |
| Mac arm64                  |      8,275,000 |       8,311,864 | 36,864 (0.45%) |
| Linux x86_64               |     18,443,224 |      18,506,464 | 63,240 (0.34%) |

These are experiment images, not release-download sizes. The selected-format API
pulls additional code into the final link; the increase is measured rather than
assumed free. Both hosts' runtime/research-source hashes match their recorded
snapshots. Native images and benchmark images have separate identities.

## Rejected variation and diagnostics

A Linux-only variant uses the existing generic reader at source offset zero and
the selected ZIP reader only at interior positions. It changes a semantic boundary,
not a count/size/time threshold. All four affected optimized targets pass. Its
90-case screen uses three repetitions per participant, fastest-two means, and
exact baseline-image repeat controls. Large first stored reads remain slower:
+13.57%/+15.17% (controls +0.60%/+1.17%). It also loses the original small-first
benefit. Do not advance this variant to the full population or substitute it for
the original. Its complete source, image hash, logs and 810 observations remain.

A same-binary source-capability toggle checks input traffic for the large first
stored member. Both paths perform exactly 17 reads and transfer 1,048,805 bytes.
Extra input traffic is therefore not the cause. Separate Linux `perf` diagnostics
sample CRC and libarchive's stored-entry descriptor scan. All 216 instructions in
`zip_read_data_none` match after relative-address/error-string-symbol normalization;
its extent is 850 bytes, with different placement in each binary. Placement or
allocation/layout effects remain hypotheses. Sampling includes fixture setup and
is not used as qualification timing. Both full sampling recordings, annotations,
disassembly and normalization proof are retained.

Earlier feasibility probes exclude real retained-index admission and lifecycle
construction. A known late offset removes a prefix scan, but that alone does not
establish a product improvement. Preserve the original small probe, both 36-case
feasibility populations, and both 90-case bounded pre-correction screens. The first
Mac feasibility population overlapped prototype compilation and is explicitly
nonauthoritative. None replaces the corrected frozen-image population above.

## Verification and remaining work

Before the last test-only extensions, both hosts pass all 324 core and registered
extension release targets; Linux requires real FUSE mounting. Six affected ASan
targets pass on each host, six pass Linux MSan, and the locator concurrency target
passes Linux TSan. The final test-only additions cover small forced Zip64, stored
and deflate single-byte reads with independent prefix-probe cursors, and exact
input traffic. The revised target passes optimized and ASan on Mac, and optimized,
ASan, MSan and TSan on Linux. Other tests cover duplicate ownership, deferred and
concurrent cursors, first/middle/last content, unknown/TAR fallback, budget admission,
release/move/growth/overflow, optional descriptor signatures, wrong identity/type,
out-of-range locators and propagated source failures.

A six-target exploratory coverage run using CI's explicit extension filter covers
139/143 changed instrumented production lines and 66/76 changed branches. The
preceding invocation omitted that filter and produced empty coverage despite its
six passing tests; that setup result is retained separately. This subset is not a
full PR coverage result. Normal prototype hooks pass; local clang-tidy is skipped
as instructed. No claim of final public runtime-head CI is made.

Before adoption, eliminate the repeatable first-read regressions while preserving
later-read gains, budget admission and all source/ownership/error contracts.
Investigate allocation/copy paths and decoder/code-placement effects using isolated
controls, then repeat both hosts' lifecycle and first-read qualification after a
real implementation change. Metadata-size recovery and stable native-source identity
remain separate work. A future runtime proposal needs current-base integration,
normal changed-source CI lint and full CI. No arbitrary threshold or favorable
rerun is used to make this trial pass.

## Evidence

The companion lossless JSON/Gzip archive contains 230 individually hashed records,
including full populations, exact drivers, shared harness, baseline and all prototype
source phases, test sources, frozen image/configuration identities, complete validation
logs, fixture/oracle proofs, rejected variant and profiling data. Each record carries
its original byte length, SHA-256 and either UTF-8 or Base64 encoding. Transport
archives that duplicate these records carry identity receipts; the 304 MiB repository
transfer is retained locally rather than embedded in the publication. The base commit
and relevant source snapshots permit reconstruction without shipping repository
history or executables. All per-case results also appear as a CSV inside the archive.

[`immutable-zip-locator-evaluation-20261107.json.gz`](immutable-zip-locator-evaluation-20261107.json.gz): 7,135,345 bytes; SHA-256
`9f1ee67a571c1b2ae5d53e50d0bf904cbfcee0d1da11aca062a8511e1e938344`.
