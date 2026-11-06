# O30: Transfer owned OpenBytes storage into its read source

`ArchiveFileSystem::OpenBytes` receives its input string by value. The old streaming-first path
passed that owned string as an lvalue to `MemoryReadSource`, copying the entire archive before
parsing. The candidate moves the buffer into an immutable memory-source allocation shared with the
fallback parser. Native TAR parsing streams from that source; when it declines a native PHAR,
`OpenBytes` releases the stream wrapper and lets the PHAR parser inspect the same bytes. This keeps
failure classification and content-source lifetime intact while removing a full-size copy from the
ordinary and fallback paths.

## Measurement protocol

The baseline is commit `14520395c344903c3dfa247d4088c1f79cdfe0b7`; the candidate is the matching
uncommitted O30 worktree. Both use the same benchmark source and release build. Each case runs nine
repetitions. Inputs of at least 64 KiB run ten iterations per repetition; fixture construction and
the fresh input copy are paused outside timing, while `OpenBytes` and destruction of its result are
timed. Per invocation, the statistic is the mean of its fastest seven repetitions. The tables show
the median statistic for each participant across three Linux pairs and six complete macOS pairs;
Change is the ratio of those participant medians, not the median of individual paired ratios. The unchanged
`OpenSource` control creates its memory source before timing, isolating archive parsing from the
`OpenBytes` ownership step.

The retained raw artifact includes all 32 baseline/candidate JSON outputs, binary hashes, peak-RSS
records, and the initial macOS exploratory sessions. The initial macOS runs were inconsistent for
large inputs despite identical binary hashes; their cause was not identified. They are retained in
the raw evidence and excluded from the later six-pair macOS summary below. Primary Linux runs
are the three complete pairs numbered 1 through 3; primary Mac runs are complete pairs 8 through 13. Earlier Mac pairs 1 through 3 are complete exploratory populations, and pairs 4 through 7
are two-case diagnostics. All remain in the same artifact. The later complete runs
are internally consistent and the `OpenSource` control remained stable.

## Results

Elapsed microseconds; lower is faster. Member bytes specify payload length: TAR and PHAR headers
add to the complete archive size. Change is candidate versus baseline.

| Platform | Format | Member bytes |  Baseline | Candidate | Change |
| :------- | :----- | -----------: | --------: | --------: | -----: |
| Linux    | TAR    |        1 KiB |      7.92 |      7.04 | -11.2% |
| Linux    | TAR    |       64 KiB |     15.24 |     12.19 | -20.0% |
| Linux    | TAR    |        1 MiB |    132.10 |      4.76 | -96.4% |
| Linux    | TAR    |       16 MiB |  3,849.85 |     16.47 | -99.6% |
| Linux    | TAR    |       64 MiB | 17,536.62 |  1,064.34 | -93.9% |
| Linux    | PHAR   |        1 KiB |     10.08 |     10.19 |  +1.0% |
| Linux    | PHAR   |       64 KiB |     18.54 |     14.46 | -22.0% |
| Linux    | PHAR   |        1 MiB |    125.45 |      7.14 | -94.3% |
| Linux    | PHAR   |       16 MiB |  3,796.23 |     22.29 | -99.4% |
| Linux    | PHAR   |       64 MiB | 17,378.74 |  1,128.89 | -93.5% |
| macOS    | TAR    |        1 KiB |      4.59 |      4.52 |  -1.5% |
| macOS    | TAR    |       64 KiB |      6.99 |      5.65 | -19.1% |
| macOS    | TAR    |        1 MiB |     37.98 |      5.85 | -84.6% |
| macOS    | TAR    |       16 MiB |    351.14 |     11.49 | -96.7% |
| macOS    | TAR    |       64 MiB |  5,292.32 |    332.61 | -93.7% |
| macOS    | PHAR   |        1 KiB |      7.34 |      7.38 |  +0.6% |
| macOS    | PHAR   |       64 KiB |     12.60 |     10.92 | -13.4% |
| macOS    | PHAR   |        1 MiB |     33.48 |     11.15 | -66.7% |
| macOS    | PHAR   |       16 MiB |    351.71 |     16.68 | -95.3% |
| macOS    | PHAR   |       64 MiB |  5,208.31 |    335.90 | -93.6% |

At 64 MiB, the separate memory-source control stays effectively unchanged: 2.33 vs 2.37 us on
Linux and 7.52 vs 7.31 us on macOS. The improvement is specific to the owned `OpenBytes` lifecycle;
it is not a claim about host-path `Open`, ordinary `OpenSource`, or full XFF commands.

Peak RSS for the 64 MiB TAR case falls from 200,816 to 135,280 KiB on drogon Linux, saving exactly
64 MiB. On this Mac it falls from 206,585,856 to 139,100,160 bytes, saving 64.4 MiB. The RSS
commands ran the same release benchmark binaries as the timing runs.

## Correctness and implementation checks

The focused archive and read-source tests pass on both platforms: 27 tests on macOS and 27 on Linux.
The cases cover streamed TAR contents after filesystem destruction, native PHAR fallback contents
after source and filesystem destruction, nested in-memory source lifetime, empty input, malformed
and corrupt archives, and preserving status codes. The candidate resets the streaming wrapper before
fallback parsing while the shared immutable bytes remain alive.

The private candidate benchmark executable SHA-256 is
`58971a26b419adbafa0b7d9125385cd168110d36bd9bb591b1cad4a5d7587454` on macOS and
`c20ea23267780ca0395fe8755334efca6fef26853b3084ba4ecb04a0e45ce344` on Linux. Matching baseline
hashes are recorded in the compressed raw artifact. These are benchmark image identities, not
release CLI identities or size measurements. The developer confirms those targets were built from
the final frozen candidate sources before measurement; the current public integration remains a
separate qualification context. No private full-root or sanitizer qualification is claimed.
Local clang-tidy was not run; the eventual PR
must pass its changed-file CI lint and the complete required CI set after syncing onto current main.

The prototype is a strong cross-platform improvement for `OpenBytes`, especially at archive sizes
where a redundant copy is costly. It remains a local candidate until it is rebased, reviewed, and
passes current-main PR CI; the unexplained initial macOS timing spread remains visible in the raw
record and should be rechecked if CI shows a conflicting result.

The complete parsed benchmark JSON values and recorded RSS/image metadata are retained in
[openbytes-owned-input-20261106.json.gz](openbytes-owned-input-20261106.json.gz)
(142,990 bytes, SHA-256
`1f44eefa3caa1e27900f3b32fbcaae47c4bcd984575c9ebc7f7492a1d61d09bb`).
Independent inspection reproduces all 20 displayed timing/percentage rows from the declared
participant-median estimator and verifies all 32 retained run populations. This archive does not
claim byte-for-byte preservation of original JSON whitespace or include complete build/test logs.

## Public integration boundary checks

The later public PR adds direct root-module coverage of the shared memory-source factory's budget,
binary contents and independent byte/cursor lifetimes. Registered compression-extension tests also
exercise decoded TAR ownership after filesystem destruction and preserve decoder errors. These
new tests passed against the original public O30 runtime before its routing cleanup.

The private indexing pass has no fallible operations; public integration returns its constructed
filesystem directly. Legacy content selects its
existing in-memory or host owner first, then the PHAR or libarchive reader. This preserves behavior
and uses the same number of decisions while exposing meaningful ownership boundaries to tests.
These later integration changes are distinct from the private benchmark sources and images above.
Fresh pushed-head CI, including canonical coverage and affected-header lint, remains authoritative;
no timing or image-identity claim is extended to the later public integration.
