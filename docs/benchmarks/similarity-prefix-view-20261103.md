# Borrow the similarity binary-sniff prefix

`-similar` must inspect the first 8,000 bytes of each freshly read reference for a NUL. The old
`std::string::substr` built an owning temporary solely for this scan. A `std::string_view` prefix
scans those same bytes without copying or allocating the temporary. Reference reads, error checks,
target-template evaluation, sniff bounds, exact equality and word-shingle scores remain conditional
and unchanged. This adds no cache, flag, dependency, worker, lock or filesystem access.

## Measurement design

The experiment is based on PR #1072 head `7f2b67e10b0010530bec20b2c661c70eb781c897`.
The common benchmark fixture now supports 8,000/65,536-byte content with accurate metadata, in
addition to its original short inputs. These fixture changes are present in both participants.

Each host runs 48 production whole-engine in-memory cases and 70 native cases, in three sessions.
Native cases include 10/1,000/10,000 short files and 10/1,000 larger files, one/three requested
workers, equal/disjoint references, binary reference rejection, missing references and unchanged
listing. Independent oracles construct complete expected stdout and require zero exits. A third
native participant is a byte-identical baseline copy. Every native case discards a warmup, rotates
participant order over nine samples and retains the fastest seven. Case order changes each session.

Whole-engine cases include command preflight, traversal, worker setup, content reads, scoring,
output and teardown; parsing/fixture construction are excluded. They retain the fastest seven of
nine per session. Registered engine cases use a 0.01-second minimum; the driver's 0.02-second
command-line default does not override that registration, as the raw run names record. Linux uses
an in-memory VFS plus tmpfs native fixtures and explicit CPU affinity. macOS uses the same virtual
fixtures and warm APFS, without affinity. No same-host release timing overlaps builds, tests,
lint or instrumentation.

The tables report median within-session elapsed changes. Negative means less elapsed time. They
are paired changes, not ratios of independently aggregated absolute measurements.

| Workload                               | Linux / 1 worker | Linux / 3 workers | macOS / 1 worker | macOS / 3 workers |
| :------------------------------------- | ---------------: | ----------------: | ---------------: | ----------------: |
| In-memory, 1,000 files x 8,000 bytes   |          -12.38% |           -11.46% |          -12.08% |           -12.51% |
| In-memory, 1,000 files x 65,536 bytes  |           -4.06% |            -4.57% |           -3.08% |            -2.52% |
| In-memory, 10,000 files x 8,000 bytes  |          -12.08% |           -11.13% |          -11.45% |           -11.46% |
| In-memory, 10,000 files x 65,536 bytes |           -4.73% |            -4.56% |           -2.79% |            -3.09% |
| Native, 1,000 files x 8,000 bytes      |           -2.90% |            -1.52% |           -0.50% |            -1.60% |
| Native, 1,000 files x 65,536 bytes     |           +0.30% |            -1.56% |           -1.63% |            -1.16% |

linux selected native changes: -3.04% to +3.72%.

linux identical baseline-copy movement: -1.37% to +3.68%.

macos selected native changes: -2.63% to +0.61%.

macos identical baseline-copy movement: -1.50% to +0.77%.

The cost of copying the prefix is clear in the larger virtual workloads; real filesystem and
command overhead reduce its share of native elapsed time. Some tiny/control cells remain slower,
and the native reference copy also moves. Preserve those costs and absolute samples rather than
claiming universal command speedups. Short references may use short-string storage and need no
heap allocation in either implementation; removing their copy is distinct from allocation removal.

## Inherited header diagnostics and final-source proof

Scoped clang-tidy found existing redundant `typename` in the word-classification concept and
incomplete special-member declarations on the abstract expression state/plan bases. The final
change removes that redundant keyword and explicitly defaults constructors, copy/move operations
and assignments while retaining the virtual destructors. Copy/move availability is preserved;
no state field, object layout or ownership transfer is added. No check is disabled or suppressed.

Both baseline and selected CLI/engine benchmark binaries are rebuilt after these declarations.
Their text and selected constant-data sections are byte-identical to their respective measured
versions on each host: ELF `.text`/`.rodata`/`.data.rel.ro`, and Mach-O `__TEXT,__text`,
`__TEXT,__const`, `__TEXT,__cstring`. The archive retains section hashes and both source sets.
This permits reuse of those release timings; it does not substitute another head's CI checks.
Final full tests and explicit changed-header lint run against the final source tree.

## Correctness, allocation and remaining scope

A committed CLI regression places a NUL at offsets 0, 7, 7,999 and 8,000 in the reference while
the candidate stays ordinary text. It verifies rejection inside the exact sniff window and
acceptance beyond it at a zero-percent threshold. Existing missing-reference, binary candidate,
normalization, threshold, width and executor-contract cases remain active.

Separate ASan allocator-hook diagnostics exercise the actual one-worker production engine with
fixture setup excluded. They report allocator requests for the calling thread only; their times
are not release performance or RSS evidence. Full data distinguishes short-string references,
heap-sized prefixes and unchanged name-only control cases.

Full unstripped CLI: linux 18,166,880 -> 18,165,496 bytes (-1,384).

Full unstripped CLI: macos 8,176,264 -> 8,176,264 bytes (+0).

For 1,000 heap-sized reference checks, actual engine diagnostics remove exactly 1,000 allocations
on each host: 8,008,000 requested bytes on macOS and 8,016,000 on Linux. Both large reference sizes
show that same bounded-prefix difference. Short-reference and name-only allocation counts are
unchanged. These are allocator-request totals for the measured calling thread, not peak memory.

All 314 root/extension release suites pass on both final trees. Five affected ASan suites pass per
host; five affected MSan/five TSan suites pass on Linux. Both actual modified C++ translation units
pass scoped clang-tidy with the corrected headers explicitly included. Final source guards also
pass after the diagnostic driver restores the selected runtime and ordinary benchmark source.

This is only removal of an unnecessary reference-prefix temporary. It does not qualify reference
representation reuse, immutable-source identity, candidate indexing or near-duplicate grouping.
Those O21b items remain open.

[Lossless evidence](similarity-prefix-view-20261103.json.gz) retains exact source variants, all raw
release samples, independent oracles, identical-copy controls, compiler/link/binary receipts,
header code-identity proof, allocator diagnostics and validation logs. Initial lint diagnostics
remain separate from the final correction rather than being dropped from the evidence.

Archive: 1,799,935 bytes, containing 270 labeled evidence files. SHA-256:
`f5a1585ae7fd9c82b005e8059d937691de9675316089853f29d1cc4909339d91`.
