# Delayed, bounded listing output buffer (2026-11-09)

## Outcome

Batch sustained, non-interactive listing output through a lazily allocated 4 KiB stream
buffer. Initial records use the existing stream, so output below the 16 KiB admission
threshold allocates no additional byte buffer. This reduces repeated ostream work without
retaining the complete output or adding another worker.

The independent 81-round listing repeat improves the large, three-CPU-budget cases about
0.9% on Linux and 4.4-4.5% on macOS. Those macOS cases finish about 4-5% ahead of GNU find;
Linux still takes about 9% longer. Small workloads remain mixed. Neither a universal speedup
nor parity with find across all sizes is established.

The exact measured candidate is `268bb2ef0f8b6887ac3924c94c9fb44d10f172a9`, compared with
`374c2588f876d054c82e017b3d3ad0b4b993dc41`, the prepared-predicate-guard implementation
merged in #1155. Later integration keeps those source identities and binary hashes unchanged
in the evidence. Its runtime sources and Bazel inputs match the measured candidate; the only
C++ difference is the parent PR's two const test locals.

The [qualification data](bounded-stdout-buffer-20261109.json.gz) retain all 1,004 cells,
including controls, initial adverse results, every independent repeat, build identities and
archive digests. The two hosts completed 66,192 correctness-checked elapsed/warmup participant
invocations. Another 144 Linux counter invocations are kept separate.

## Stream behavior

`BufferedListingOutput` wraps the existing stdout stream buffer. Its inline byte array is
uninitialized until written; the object itself is allocated only when an output record
crosses the remaining 16 KiB direct-output budget. Admission happens at record boundaries,
so this is not a guarantee that exactly the first 16 KiB use the ordinary stream.

Small writes are combined in the 4 KiB buffer. A write at least as large as that capacity
flushes pending bytes and goes directly to the original buffer. The destructor flushes and
restores the original buffer, preserving the stream's error state across `rdbuf()`, which
otherwise clears it. Explicit flushes also flush the underlying buffer; stderr's existing
tie to stdout therefore retains ordering before diagnostics.

TTYs, quiet output, terminal-taking expressions, pagers, failed streams, exception-enabled
streams and `unitbuf` streams keep the direct path. Stdin is consumed before installing the
wrapper. Terminal-taking expressions include process execution and interactive confirmation.
The additional byte storage is bounded at 4 KiB; this is not a whole-process memory bound.

## Production build and measurement identities

Both hosts built full and minimal production CLIs with `--config=clang_release`; the full
CLI additionally uses `--config=xff_full`. Each build is followed immediately by the matching
configured executable query and a frozen copy. Another configuration is built only after
that copy. Clean source/tree IDs, SHA-256, byte sizes, configuration hashes and actual ThinLTO
link actions are retained. Bazel servers were stopped before accepted elapsed measurements.

The full CLI covers listing, native-case filtering, metadata/date output, content regex and
TAR filtering. The minimal CLI covers the four native workloads. Native broad/deep trees contain
100, 10,000 and 100,000 files; TAR fixtures contain 100, 1,000 and 10,000 files. CPU budgets
are 1, 3 and 10. A budget does not imply that this
particular expression actually starts that many matcher threads.

Every cell rotates candidate, frozen baseline and a traditional-tool reference through a
warmup and measured rounds. Separate identical-baseline cells expose within-session
variation. Every participant invocation checks the independently generated output multiset,
duplicate counts, exit status and diagnostics. Final fixture, executable, reference-tool and
native-launcher integrity checks passed. Raw evidence retains warmups and excluded rounds.

### Primary large listing comparison

The following equally weights broad/deep 100,000-file listings with a three-CPU budget in
the independent 81-round repeat. Negative elapsed changes mean less time. Candidate/find
above one means XFF takes longer.

| Platform | CLI     | Elapsed change | Identical control change | Candidate/find |
| -------- | ------- | -------------: | -----------------------: | -------------: |
| Linux    | full    |         -0.85% |                   +0.16% |        1.08992 |
| Linux    | minimal |         -0.88% |                   -0.17% |        1.09534 |
| macOS    | full    |         -4.38% |                   +0.04% |        0.96307 |
| macOS    | minimal |         -4.50% |                   -0.37% |        0.94839 |

These are descriptive point estimates for these hosts and fixtures. Identical-baseline
controls are shown, not divided out after measurement. Interleaving reduces shared load
effects; it does not establish a machine-independent speedup.

### Estimators and independent populations

Chart-style points divide independently selected fastest-round means. Paired intervals use
the geometric candidate/baseline ratio across all measured round pairs, with 1,000 bootstrap
samples and seed 20261107. Those intervals describe a different estimator and are not
confidence intervals for chart-style points. Neither establishes confidence across machines.

| Population                            | Rounds / retained    | Linux cells / calls | macOS cells / calls |
| ------------------------------------- | -------------------- | ------------------: | ------------------: |
| Initial listing screen, both CLIs     | 27 / 21              |          72 / 6,048 |          72 / 6,048 |
| Independent listing repeat, both CLIs | 81 / 63              |          40 / 9,840 |          40 / 9,840 |
| Five full / four minimal workloads    | 9 / 7                |         324 / 9,720 |         324 / 9,720 |
| Targeted adverse-workload repeat      | 27 / 21              |          68 / 5,712 |          40 / 3,360 |
| Final native-case confirmation        | 81 / 63              |          16 / 3,936 |           8 / 1,968 |
| Total elapsed populations             | separate populations |        520 / 35,256 |        484 / 30,936 |

Calls include warmups, candidate and identical-baseline cells, and all three participants.
Initial screens and repeats remain separate populations; no averaging across repeats is
used to erase an adverse result.

## Adverse results and confirmations

Linux's initial listing screen has no candidate paired interval wholly slower than parity.
The broader nine-round screen has five: full deep listing at 10,000 files / one CPU, full
deep native-case filtering at 100,000 / one CPU, minimal broad content at 10,000 / three CPUs,
minimal broad native-case filtering at 100 / ten CPUs, and minimal deep native-case filtering
at 10,000 / three CPUs.

The fresh 27-round workload repeat includes those cases and their opposite shapes. None of
the full CLI's intervals remains wholly slower. Two minimal native-case results do:
broad 100 / ten CPUs has chart ratio 1.01118 and paired interval [1.00474, 1.03308]; deep
10,000 / three CPUs has chart ratio 1.03108 and interval [1.00351, 1.05818]. A final 81-round
confirmation covers both shapes of both cases for both CLIs. None has a wholly slower
interval. Its full/minimal chart aggregates are 0.99440/1.00150, with controls 0.99735/0.99698.
The minimal aggregate and controls still show variation around parity; the earlier slower
populations remain evidence, not failed tests to discard.

macOS's initial full listing screen has one adverse small case: deep 100 / three CPUs,
chart ratio 1.00959 and paired interval [1.00175, 1.03360]. The independent 81-round listing
repeat has no wholly slower interval in either CLI. The broader screen then identifies full
TAR filtering at 1,000 files / one CPU in both shapes: broad ratio 1.04258, interval
[1.00394, 1.10610], and deep ratio 1.02412, interval [1.00949, 1.04462]. Minimal metadata
output at broad 100 / ten CPUs has ratio 1.01522 and interval [1.00306, 1.02783].

Those three macOS broader-screen signals do not reproduce in their 27-round workload
repeat. A different full native-case cell, deep 100 / three CPUs, has ratio 1.00350 and
interval [1.00022, 1.02647]. The final 81-round confirmation covers both shapes for both
CLIs; none has a wholly slower interval. Full/minimal chart aggregates are 0.99560/0.99584,
with controls 0.99989/0.99515. These observations do not prove an absence of regressions
elsewhere or a general small-case improvement.

## Instruction and latency diagnostics

Linux's separate `perf` diagnostic checks 144 invocations over four cases for both CLIs.
At 100,000 files, candidate/baseline userspace instruction ratios are 0.94750/0.94688 for
full/minimal broad listings and 0.95581/0.95530 for deep listings: about 4.4-5.3% fewer
instructions. The 100-file broad case instead executes about 0.18%/0.29% more instructions.
Counter samples are not pooled with accepted elapsed samples; no new macOS instruction-count
claim is made for this change.

An audit of Linux's initial elapsed population finds large three-budget first-stdout ratios
0.99829/1.00152 for full/minimal, with controls 0.98564/0.98497. There is no established
first-output latency improvement. The raw process CPU and RSS observations remain available;
zero CPU values at small sizes are preserved rather than replaced with invented ratios.

## Correctness, lint and storage

Fourteen existing CLI suites actually executed on each host with `clang_opt`, `xff_full`
and cached test results disabled: color, CSV, exact output, exec, exit status, pager unit and
shell tests, parity, rg full/input/terminal/basic, sort and style. Logs and result XML are
retained. These correctness suites are separate from the production ThinLTO binaries used
for timing and per-invocation output-oracle checks.

Changed-translation-unit clang-tidy passed on both hosts, in addition to normal policy hooks.
On macOS the scoped analysis database initially mixed SDK and hermetic libc++ paths. The
retained fix uses physical external paths, the explicit SDK sysroot and `-nostdinc++` with
the lint wrapper's hermetic libc++ includes. It changes the analysis database only; original
compile actions, failed attempts and the final successful analysis remain. Baseline/candidate
compiler bytes match and both build logs identify LLVM 23.1.5.

Linux fixtures are on verified tmpfs with recorded CPU affinity. Each macOS phase uses a
new owned 2 GiB case-sensitive HFS+ RAM image without affinity. All five images were released
after integrity checks, matching the live image, device and UUID to ownership receipts.
These measurements exercise memory-backed traversal, not SSD throughput. The retained tools,
host/resource observations and reference-library paths describe the environment; the archives
are not complete operating-system images.

## Retained evidence and earlier variant

Both final archives are on `drogon.boerger.online` under `/home/marcus/Documents/`:

- `xff-lazy-listing-buffer-linux-20261109.tar.gz`: 27,473,416 bytes, SHA-256
  `1cf694372a3f0e78e3e228a6477a435f07e4a8a2cb13d210d2555c721c035066`.
- `xff-lazy-listing-buffer-macos-20261109.tar.gz`: 16,253,106 bytes, SHA-256
  `8714ff015878ead60cb0c53d6e6702da4a4c9c0fa0d9a4a670cd37790d0b2069`.

The Linux/macOS manifests verify 2,013/1,644 entries against archive contents. The macOS
destination digest was independently checked after copying. Archives retain measured source,
frozen production binaries, link actions, commands, contracts, raw rounds, output proofs,
fixture manifests, storage journals, correctness/lint logs and collection/analysis helpers.
The complete baseline sources are also retained in the prepared-guard evidence archives
identified by the qualification data and the preceding predicate-guard report.

The earlier immediate, stack-owned buffer at `feca20ad0da8dc13ab5dc23be864bc36a53e1d40`
was held after mixed Linux elapsed/latency results. Its 6,048 output-checked calls and 144
counter calls remain in `xff-buffered-listing-linux-initial-20261109.tar.gz`, SHA-256
`158ebf0a23d9a020de6538dd6d8a65ff5f565d6ff5abbfed3ea52dcdd12203e4`.
That experiment is not relabeled as the final candidate or pooled into its results.

This implementation does not change the measurement contract or publish local experiments
as viewer history. Continuous extended-production-v1 history and filling missing merged-PR
viewer entries remain separate work.
