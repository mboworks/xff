# Bounded forward skipping of archive payloads

Discarded large payloads can advance an existing input cursor without reading every byte.
The archive reader requests optional skipping only for remaining spans of at least 64 KiB.
Memory cursors advance their existing offset. Read-only host cursors lazily inspect their owned
regular-file descriptor, bound advances by its observed size, and seek that same descriptor.
No path reopening or cursor allocation is added per skip. Sequential, decoded and member cursors
retain their ordinary read/discard fallback when they do not implement this capability.

`ReadStream::Skip` returns an optional count or a failure: absence means unsupported, a present
count must not exceed the request, and zero alone does not prove EOF. Read, ReadInto and Skip
share the cursor position. Cursor objects have unique ownership; transfer their unique_ptr rather
than copying or moving a cursor object. Immutable sources retain ordinary copy/move operations.

The libarchive callback preserves borrowed-buffer lifetimes. A failed skip or an oversized count
latches an error; its fallback read reports that error before touching the cursor again. This is
required by the pinned skip proxy, which does not treat a negative custom callback result as a
reliable termination mechanism. A common value-returning read-only syscall adapter preserves native
status conversion and is exercised by real metadata failures. No new writable handles, host
mutations, dependencies, threads, locks, decoder concurrency or CLI flags are introduced.

## Selected mechanism and controls

The first source-level experiment queried a source capability/size and reopened at each skipped
position. It demonstrates the potential benefit but adds cursor allocation/reopening and session
position state. The selected cursor-level design avoids those operations and leaves SourceSession
layout unchanged. Keep the first experiment's partial qualification separate: its memory pilots
completed on both hosts, but its native pilot completed only on Linux.

Four frozen participants compare the exact parent, API-only cursor changes without callback
admission, eager skipping and the selected 64 KiB threshold. The callback threshold avoids work
for small remainders; it is an internal optimization, not a new user-controlled safety limit.
Legacy and caller-owned-only sources are explicitly incapable controls. Actual memory and nested
member sources exercise production implementations rather than being relabeled legacy wrappers.

Final preparation is pinned to `38ac542f621abd9fb2610e0d01f8646f42428ab3`. Parent PR #1070 was
subsequently merged as `44e7e30102eb6c6ae122135189b6c62340c35bdb`; their complete Git trees are identical,
and all six candidate source files are unchanged after synchronization. Fresh current-head CI is
still required for readiness; tree equivalence does not substitute another PR's checks.

## Cross-platform measurements

The final four-participant matrix contains 37 in-memory cases and 40 native cases per host, each
in three sessions. Native cases cover small, boundary and large TARs, stored/deflated ZIP, gzip TAR,
nested TAR and ordinary files with one/three requested workers. Native Python fixtures independently
verify sizes and complete path/content output; stored/deflated ZIP fixtures have explicit local-header
sizes. Each participant discards a warmup; nine interleaved samples retain the fastest seven.

A separate confirmation compares the original parent, a byte-identical parent copy, the preceding
selected binary and the final selected binary. It repeats 12 memory cases in five sessions and
20 native cases in three sessions. The table shows median within-session duration changes from
that final confirmation. Negative means less elapsed time. Requested workers do not imply parallel
archive decoding. Linux uses tmpfs plus CPU affinity; macOS uses warm APFS without affinity.
No same-host timing overlaps builds, tests, lint or instrumentation.

| Native fixture          | Operation | Linux / 1 worker | Linux / 3 workers | macOS / 1 worker | macOS / 3 workers |
| :---------------------- | :-------- | ---------------: | ----------------: | ---------------: | ----------------: |
| TAR, 100 x 1 MiB        | list      |          -82.18% |           -81.00% |          -51.54% |           -51.95% |
| TAR, 100 x 1 MiB        | last      |          -86.93% |           -86.74% |          -63.80% |           -64.56% |
| Stored ZIP, 100 x 1 MiB | list      |          -80.12% |           -79.27% |          -49.66% |           -49.54% |
| Stored ZIP, 100 x 1 MiB | last      |          -84.79% |           -84.21% |          -61.42% |           -61.30% |
| TAR, 10 x 64 KiB        | list      |           -1.34% |            -1.34% |           +0.56% |            +0.17% |
| TAR, 10 x 64 KiB        | last      |           -0.92% |            -0.22% |           -0.14% |            +0.48% |
| Ordinary 1,000 files    | list      |           +0.79% |            +0.29% |           +0.28% |            +0.36% |
| Ordinary 1,000 files    | last      |           -0.49% |            -1.93% |           +0.38% |            +0.05% |

The final nested-TAR confirmation improves listing about 11% on Linux and 7% on macOS, and
last-member content about 11% and 6%, respectively. Gzip-TAR controls stay within roughly 0.3%.
Actual in-memory 100 x 1 MiB TAR listing improves about 97% Linux / 92% macOS; reading the final
member improves about 96% / 85%. Sequential/incapable inputs remain explicit controls.

These are paired medians, not the quotient of independently computed median absolute times.
The lossless data retains every raw sample, session and participant. The byte-identical native
reference copy moves by -10.61% to +1.63% on Linux and -1.33% to +0.59% on macOS; the large gains
are much greater than that movement. Final ordinary/boundary native controls cost at most 0.79%
on Linux and 0.56% on macOS in this confirmation.

Some tiny macOS memory kernels are unstable. An initial 4 KiB listing cell reports +59.72%, with
absolute values ranging roughly 7.5-15.9 us across participants/sessions; its final selected
confirmation is -3.80%, while an identical reference copy moves +4.75%. Preserve the initial
population rather than discarding it. Other final macOS controls retain costs up to 4.42%,
including caller-owned-only inputs that cannot skip. Do not claim a universal speedup or infer
that every small-kernel difference is caused by this mechanism.

The explicit ownership-declaration refinement produces identical runtime code sections for the
CLI and both kernels on each host. The subsequent status-adapter refinement changes CLI code,
and its final binary is used in native confirmation; both memory kernel code sections remain
identical. These are code-section identity checks, not claims that complete debug-bearing files
have identical hashes. Final full unstripped CLI footprint increases 6,712 bytes on Linux and
17,040 bytes on macOS relative to the parent (about 0.04% and 0.21%).

## Correctness, budgets and limitations

Eleven new regressions exercise unsupported/default cursors, zero/partial/maximum requests,
shared read positions, owner retention, native descriptors after source-path deletion, large
payload skipping, owned buffers, selected content, small/compressed inputs, sequential fallbacks,
latched failures, oversized results and physical truncation. Compile-time checks prevent accidental
cursor copying/moving. Existing archive format, member, XAR, sparse-input, resource and fuzz tests
remain active. Only successfully returned/initialized bytes reach readers.

Skip changes no shared memory/replay budget, decoder workspace or archive-owner lifetime.
The existing session buffer reservation is released on success and failure. Native cursor state
adds its access classification, position and observed size; it does not add a metadata syscall
to ordinary reads. Size inspection is lazy at the first eligible positive skip. Host input retains
the existing read-only source model; this is not an atomic snapshot facility for concurrent writers.

All 313 selected root/extension release suites pass on both hosts. Five focused ASan suites pass
per host, and five MSan/five TSan suites pass on Linux. Explicit checks of five actual modified
translation units include both logical and physical read_source.h aliases; an inherited missing
special-member declaration was corrected after that check found it. The final implementation
passes a focused check of its changed runtime unit. Full Linux coverage runs all 313 suites and
passes every repository threshold: the candidate has 61/61 changed lines and 28/28 branches covered.
No executable-line exclusions, threshold reduction or test-only production hook is used.

One existing streamed-ZIP limitation was independently reproduced on the unchanged parent:
a ZIP produced by libarchive's nonseekable memory writer can report a zero member size at the
header when the size is deferred to a data descriptor. The benchmark's exact size oracle caught
this; it was not weakened. Large in-memory payload kernels therefore use TAR. Retain the failed
setup, writer source and parent reproduction as evidence, and track the ZIP issue separately.

This completes bounded forward skipping only. Seekable ZIP central-directory indexing, repeated
member/replay reuse, retained-index accounting and decoder admission remain separate unqualified
work. The existing XAR seek callback is unchanged. No unbudgeted ZIP index or parallel decoder is
silently enabled by this optimization.

## Reproduction evidence

[Lossless experiment data](forward-cursor-skipping-20261103.json.gz) retains frozen sources for the
source-level and cursor-level experiments, original/eager/API-only/bounded controls, final source
refinements, compiler/configuration/link/binary identities, complete timing populations,
independent oracles, validation/coverage logs, scripts and parent synchronization proof. Earlier
failures and noisy populations remain labeled separately. Normal enforcing commit/push hooks and
fresh CI are additional merge-readiness requirements.

Archive: 5,017,324 bytes, containing 803 labeled evidence files. SHA-256:
`b66c5d4a2785e9d4659314293b1ffd5545459d6c379a9e70bc54691d2f309461`.
