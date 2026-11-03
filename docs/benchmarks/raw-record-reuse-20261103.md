# Reuse raw listing record storage

The coordinator reuses one private record buffer for implicit raw plain and NUL listings.
The owning, stateless `Renderer::Record` remains available and other formats keep their existing
rendering. `resize_and_overwrite` copies the path and writes its terminator directly, without
zero-initializing a new destination or appending through several string operations.

Each synchronous consumer must finish consuming or copying the bytes before returning. The view
expires at the next mutation; callbacks must not re-enter the buffer, and paths must not alias its
storage. Score-sorted output still copies each record into its own string. Workers never share the
buffer. Capacity above 1 MiB is released after consumption; this bounds retained buffer capacity,
not total process memory or queued output. No flag, dependency, worker, lock, filesystem bypass,
exception mechanism or escaping rule changes.

## Cross-platform qualification

The frozen baseline is main `79f8b371480551c70654e1e83712ebdb6539ca0a`. Each host runs 68 direct
formatting cases, 32 whole-engine in-memory cases and 38 native cases in three sessions. Native
fixtures contain 10, 1,000 or 10,000 files; relative paths exercise short-string storage and absolute
paths exercise heap storage. One/three requested workers, raw/NUL records, escaped paths, CSV,
JSONL, templates, forced color, explicit `-print` and empty output remain distinct. Explicit
`-print` is an unchanged action control; the optimized implicit listing must not be measured by
silently adding that action. Native oracles independently construct and check complete stdout,
including path prefixes and terminators, and require zero exits.

Every participant discards a warmup and retains the fastest seven of nine interleaved samples.
Case order changes across sessions. Linux uses tmpfs and affinity to one/three allowed CPUs; macOS
uses warm APFS without affinity. No release timing overlaps builds, tests, lint or instrumentation
on its host. In-memory engine runs include command preflight, traversal, sink calls and teardown;
parsing and fixture construction remain outside those iterations.

A separate native confirmation includes the parent, a byte-identical parent executable copy and
the selected implementation. The table shows median within-session elapsed changes, not a ratio
of independently aggregated absolute durations. Negative means less elapsed time.

|  Files | Workers | Root spelling | Record |  Linux |  macOS |
| -----: | ------: | :------------ | :----- | -----: | -----: |
| 10,000 |       1 | absolute      | raw    | -1.33% | -2.38% |
| 10,000 |       3 | absolute      | raw    | -2.33% | -1.70% |
| 10,000 |       1 | absolute      | nul    | -1.74% | -1.48% |
| 10,000 |       3 | absolute      | nul    | -1.14% | -1.48% |
| 10,000 |       1 | relative      | raw    | -1.34% | -0.99% |
| 10,000 |       3 | relative      | raw    | -1.54% | -0.42% |
| 10,000 |       1 | relative      | nul    | -1.03% | +0.10% |
| 10,000 |       3 | relative      | nul    | +0.48% | -0.37% |

linux: all native confirmation changes range -2.33% to +0.48%; the identical baseline copy ranges -1.61% to +0.89%.

macos: all native confirmation changes range -2.38% to +0.50%; the identical baseline copy ranges -1.27% to +1.11%.

The whole-command effect is smaller than the formatting kernel effect and sometimes comparable
to identical-copy movement. Do not turn the kernel result into a claim that every command improves.
Unchanged formats and tiny startup controls retain their measured costs in the lossless artifact.

## Record kernel and allocation costs

The direct kernel compares owning construction and reusable synchronous consumption within the
same executable. Each reported path length includes three control bytes; both implementations
must return exactly those bytes plus the requested terminator. This is raw output, not escaping.
The table includes absolute nanoseconds so small percentage costs remain interpretable.

| Path bytes | Terminator | Linux owning ns | Linux reuse ns | Linux change | macOS owning ns | macOS reuse ns | macOS change |
| ---------: | :--------- | --------------: | -------------: | -----------: | --------------: | -------------: | -----------: |
|          3 | newline    |            5.42 |           4.83 |      -10.89% |            8.01 |           3.48 |      -56.52% |
|          3 | NUL        |            3.72 |           4.83 |      +29.89% |            5.40 |           3.48 |      -35.49% |
|         11 | newline    |            6.62 |           4.66 |      -29.65% |            8.02 |           3.83 |      -52.24% |
|         11 | NUL        |            5.28 |           4.66 |      -11.87% |            5.57 |           3.77 |      -32.30% |
|         25 | newline    |            8.49 |           1.97 |      -76.83% |           24.08 |           4.18 |      -82.65% |
|         25 | NUL        |            6.51 |           1.95 |      -70.03% |           20.53 |           3.84 |      -81.32% |
|        163 | newline    |            8.70 |           2.61 |      -70.01% |           23.77 |           5.17 |      -78.26% |
|        163 | NUL        |            6.55 |           2.61 |      -60.14% |           20.31 |           5.15 |      -74.63% |
|       4099 | newline    |           33.93 |          15.15 |      -55.36% |           82.79 |          55.68 |      -32.74% |
|       4099 | NUL        |           31.58 |          15.10 |      -52.17% |           74.43 |          55.77 |      -25.08% |

The first reserve/assign/push prototype is retained separately. Its short Linux NUL costs are
larger; the selected single-fill implementation reduces them. Very short Linux NUL records still
cost more in the isolated kernel. This is an accepted small absolute cost, not a universal win.
Ordinary short and heap-sized paths improve on both hosts. Keep all original populations and
controls rather than pooling a favorable subset.

Separate ASan allocator-hook diagnostics count allocations, never release performance. After one
warmup, 1,000 heap-sized owning records allocate 1,000 times; reusable records allocate zero times.
Short-string records allocate zero in both implementations. Counts measure allocator requests,
not RSS, sanitizer overhead or a whole-command allocation total. Tests independently cover empty,
short, growing, control-byte and large-to-small paths, both terminators, queued owning copies and
release after an oversized record. Existing format, Unicode, ranking and output suites remain.

Full unstripped CLI footprint: linux 18,162,504 -> 18,166,976 bytes (+4,472).

Full unstripped CLI footprint: macos 8,175,624 -> 8,176,264 bytes (+640).

All 314 root/extension release suites pass per host. Four affected ASan suites pass per host;
four affected MSan and four TSan suites pass on Linux. All three actual changed translation units
pass scoped clang-tidy with an explicit header filter matching the new buffer header. The tests
and diagnostic driver restore and check the exact frozen production sources.

## Remaining scope and evidence

This qualifies raw implicit record reuse only. Structured cell vectors, escaped/JSON scratch
storage, table-window streaming and byte-bounded pipeline publication remain separate work.

[Lossless evidence](raw-record-reuse-20261103.json.gz) preserves frozen first/refined sources,
compiler and link identities, raw timing populations, independent oracles, identical-baseline
confirmation, allocation diagnostics and validation logs. The initial Linux test command omitted
the FUSE-required configuration on a mount-capable host; the corrected complete command and its
results are retained alongside that setup failure. No production change or policy weakening hides it.

Archive: 3,515,078 bytes, containing 380 labeled evidence files. SHA-256:
`b42c8701fd9b5aed8c10f061b4a072e1003ba0b86fe8c434acf38f3a802dc2ee`.
