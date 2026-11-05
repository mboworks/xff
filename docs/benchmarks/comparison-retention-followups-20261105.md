<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Comparison summary and owner-transfer decisions

The owner-transfer experiments are rejected. Keep the existing retained-entry owner contract.
A separate candidate returns after comparison output when no ordinary file summaries remain;
it is selected as a small simplification after both-host qualification. Native CLI times remain
mostly near baseline; the measurements do not establish a universal end-to-end speedup.

## Owner-transfer candidates (O16a4a)

`RunTreeCompare` creates a temporary `TreeCompareEntry` from each matched visit. The existing
`ComparisonEntries::Add` accepts a const reference and copies its backend owner into retained storage.
We tested consuming the entry by value and moving that owner, then two reference overloads sharing
one compile-time implementation. Borrowed values still need independent retained ownership.
The overload variant transfers accepted owners and leaves rejected rvalue owners with the caller.
First-wins duplicates, stable paths, metadata and readable retained backends remain covered by tests.

Each host/form has 40 in-memory cases: 10/100/1,000/10,000/100,000 entries, short/long path prefixes,
and empty-owner temporary, owned temporary, owned borrowed and borrowed-duplicate inputs.
Fixtures and owners are prepared outside timing. Collection, allocation, sorting and destruction
are timed. An opaque compiler escape preserves the complete collection. This is a serial collection
mechanism benchmark, not a filesystem throughput or worker-scaling measurement.

Percentages below are candidate/parent elapsed time minus one; negative improves. Each cell first
uses the mean fastest seven of nine repetitions, then the median of three paired-session ratios.
The table takes the median over all ten size/path cells in each scenario. The identical parent copy
is separate and is never normalized away. Both improvements and costs remain in the raw archive.

| Form                | Scenario                  |  macOS | macOS copy |  Linux | Linux copy |
| :------------------ | :------------------------ | -----: | ---------: | -----: | ---------: |
| By value            | empty-temporary           | +4.08% |     -0.43% | +2.10% |     -0.03% |
| By value            | owned-temporary           | +3.32% |     -1.00% | -8.54% |     -0.12% |
| By value            | owned-borrowed            | +4.57% |     -0.20% | +1.77% |     +0.18% |
| By value            | owned-borrowed-duplicates | +5.16% |     +0.13% | +7.16% |     +0.17% |
| Reference overloads | empty-temporary           | +4.73% |     -1.53% | +5.81% |     +0.28% |
| Reference overloads | owned-temporary           | +3.89% |     -0.40% | -6.01% |     +0.21% |
| Reference overloads | owned-borrowed            | +4.11% |     -0.50% | +5.29% |     +0.13% |
| Reference overloads | owned-borrowed-duplicates | +4.60% |     -0.96% | +4.45% |     -0.24% |

Both forms improve the Linux owned-temporary mechanism while worsening other paths, including
macOS owned temporaries. Reference overloads do not remove that tradeoff. Optimized Linux symbols
show that the public wrappers were inlined, so an extra wrapper call is not an established cause.
No instruction-count or shared-pointer-count estimate substitutes for these measurements.

The 1,000-entry Mac cells have substantial allocator/identical-copy variation. Their individual
outliers remain recorded; they are not reliable estimates of a transfer-specific effect. The stable
100-entry borrowed-duplicate cells also show costs, including +14.63% Linux by-value and +5.76%
macOS overloads. The complete matrices support rejection without relying on the drifting cells.

The overload form additionally completes 40 real CLI cases on each host: native broad/deep
10/1,000/10,000-file trees, stored ZIPs with 10/1,000 members, and compressed TARs with 10/100 members;
1/3 requested workers; status and summary output. Every invocation checks the complete stdout and
exit status against the parent, including discarded warmups. Each archive includes equal, changed
and side-only entries. Status explicitly selects all categories. The summary control also explicitly
selects all, so its measured output includes selected status records alongside comparison counts.
These are exact recorded invocations, not a claim about the quiet summary-only shortcut.

Mac 1,000-member ZIP medians span -0.36% to +0.16%, with unchanged-copy results spanning -0.40%
to +0.11%. Linux candidate results of -2.76% to -2.32% accompany copy movement of -2.73% to -2.16%.
Those workloads do not establish an attributable transfer benefit. Both forms stay private; their
source snapshots, tests, qualification logs, binaries' identities, raw results and rejected findings
are retained. Full release/sanitizer qualification is reserved for selected production changes.
The prototype passes three focused release and ASan suites on both hosts plus actual changed-TU lint;
new owner-lifetime tests demonstrate the original extra retention and the tested transfer contracts.
No ASan timing is used. Comparison arena initial-block policy and completed-frontier ownership
remain separate open candidates.

## Empty ordinary comparison scopes (O16a5)

The selected implementation removes unused ordinary-summary work; it does not change collection
ownership or make the owner-transfer experiments acceptable. Small counts-only engine kernels
improve roughly 3% on both hosts with one worker. Larger cells include slower results and drift;
the full native matrix is mostly neutral. The Linux release executable shrinks by 1,392 bytes;
Mac executable size is unchanged. This is a limited cleanup, not a scalability or archive-reader fix.

| Entries | Workers |  macOS | macOS copy |  Linux | Linux copy |
| ------: | ------: | -----: | ---------: | -----: | ---------: |
|      10 |       1 | -3.23% |     -1.47% | -2.68% |     -0.74% |
|      10 |       3 | -2.51% |     +1.18% | -5.98% |    -12.10% |
|  10,000 |       1 | +2.41% |     +1.72% | -6.58% |     +1.38% |
|  10,000 |       3 | -0.29% |     +0.53% | -4.07% |     -5.27% |

These are representative counts-only cells, not pooled averages. The 10-entry three-worker Linux
unchanged copy improves more than the candidate, so that cell does not prove an attributable gain.
Linux 10k serial ordinary summaries also move roughly -6.4% to -6.7%, despite not taking the early
return. Their code-layout/measurement effects must not be attributed to skipped scope construction.
The Mac 10k serial counts-only cost remains explicit. Across the 48-case memory matrix, ordinary
controls include a +4.90% Linux 10k three-worker mixed-scope cell (copy +2.28%).

After exporting comparison headers and counts, return the unchanged `RunResult` when the remaining
ordinary-summary vector is empty. This avoids constructing empty scope tables, labels and joins.
Ordinary summaries retain the existing path. Status, patches, counts, headers, exit status, input
reads and error precedence keep their behavior. There is no new flag, worker, clock, I/O adapter,
backend owner, dependency or exception path.

The in-memory matrix has 48 cases per host: counts-only defaults and five ordinary-summary/scope
controls, 10/100/1,000/10,000 entries and 1/3 workers. Whole engine execution is timed, with controlled
memory input and parser binding prepared beforehand. Linux processes are pinned to the corresponding
number of allowed logical CPUs; Mac worker requests do not claim CPU affinity.

An initial diagnostic mistakenly attached explicit `--summary-scope` to counts-only summaries.
The existing validation correctly rejected those commands. The entire partial process is retained
as ineligible, including its ordinary controls; none of it enters the accepted comparison. The final
harness exercises the valid `--compare=summary` shortcut and requires every case's preflight to pass
before timing begins. No production validation was changed to accommodate a benchmark.

The 40 native cases cover counts-only console/CSV output, ordinary extension-summary controls and
status/patch controls. Thirty additional functional combinations compare full stdout, stderr and
exit status for output formats, explicit summary sinks, disabling summaries and headers, including
preserved usage errors. All preflights and measured invocations compare against the exact frozen
parent. Matrix cardinalities, source, driver and binary hashes are checked.

Native cohort medians below aggregate paired-session percentages across twelve cells per output
mode and four status/patch controls. They summarize the full matrix without claiming every cell
improves. The raw archive retains every case and repetition.

| Native output         |  macOS | macOS copy |  Linux | Linux copy |
| :-------------------- | -----: | ---------: | -----: | ---------: |
| Counts console        | +0.27% |     +0.09% | -0.30% |     -0.30% |
| Counts CSV            | -0.04% |     +0.01% | -0.01% |     +0.09% |
| Ordinary extension    | -0.35% |     -0.24% | -0.03% |     +0.23% |
| Status/patch controls | -0.20% |     -0.04% | -1.09% |     -0.83% |

The Mac native worst cell is +1.15% (10 entries, deep tree, three workers, counts console), with
copy +0.61%. Linux ordinary deep 1k three-worker extension output costs +2.62% (copy +0.27%).
No new archive mechanism is selected, and no native archive speedup is claimed for this early return.

A rebuilt Mac parent has the same executable/loadable bytes as the earlier qualified parent, but
its two `N_SO` sandbox directory strings and their dependent CodeDirectory page hash differ.
The strict whole-file check caught this. A standalone proof validates every other byte, both OS
signatures and every SHA-256 signing page before admitting the exact new frozen parent. Linux's
parent is whole-file identical. Final candidate qualification matches the exact measured binary on each host.

## Qualification

All 329 release targets pass on both hosts, including all extension modules and required real FUSE
coverage on Linux. The run, comparison totals, parallel comparison and CLI summary suites pass on
both hosts; the first three also pass under ASan on both, and MSan and TSan on Linux. Actual
`run.cc` and `compare_benchmark.cc` translation-unit lint passes on both. These lint runs used the
parent policy; they do not claim the later owned-header lint-policy fix was already applied.
Linux focused raw patch coverage covers all three added lines and both early-return branches:
3/3 lines and 2/2 branches (100%). Existing behavior tests and thirty full command-oracle checks
cover headers, formats, explicit summary drivers, disabled summaries and preserved usage errors.

Both final qualified CLI binaries are byte-identical to their corresponding measured candidates:

| Host  | Parent bytes | Candidate bytes | Change |
| :---- | -----------: | --------------: | -----: |
| macOS |    8,273,096 |       8,273,096 |      0 |
| Linux |   18,431,008 |      18,429,616 | -1,392 |

- macOS candidate SHA-256: `2cb934e0a7c9494529114e3b55e80546032ad1126fa4eaa7e4000e5d359cafed`.
- Linux candidate SHA-256: `15747fb102a6e12814b0fbf41572e261e2956913a832a1b94d872feb50639983`.
- Parent source commit: `d9ddecbc21d3dfbc394aebdafc127fb73d9279ba`.

These are the immutable experiment/qualification identities, not a substitute for fresh checks
on a subsequently synchronized public head. Public integration and CI remain separately required.

## Evidence and reproduction

The lossless [evidence archive](comparison-retention-followups-20261105.json.gz) contains
553 UTF-8 records from both hosts: source snapshots, exact drivers, immutable manifests,
raw repetitions, command oracles, qualification/lint logs, identity proofs and retained failures.
Frozen executables and generated fixture/archive trees are omitted; their hashes and generators
remain. The archive is 4,021,786 bytes compressed and 38,177,064 bytes
uncompressed. It uses deterministic gzip (`mtime=0`); each record also carries its original byte
count and SHA-256.

- Archive SHA-256: `3420cc23b81930b13abec2436d2e2676509d5ff64c6c01bee03ca5f3d0ab0e8f`.
- Decompressed SHA-256: `cb6f54917946550bf6d2c6acc53300a238ad4dfdc7c5aa0d4f607a89c22e4436`.

Extract and independently verify records without running any experiments:

```python
import gzip
import hashlib
import json
from pathlib import Path

source = Path("comparison-retention-followups-20261105.json.gz")
data = json.loads(gzip.decompress(source.read_bytes()))
for host in data["hosts"]:
    for record in host["records"]:
        raw = record["text"].encode("utf-8")
        assert len(raw) == record["bytes"]
        assert hashlib.sha256(raw).hexdigest() == record["sha256"]
        target = Path("evidence") / host["host"] / record["path"]
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(raw)
```

For reproduction, start separate clean worktrees at the recorded parent. Restore the appropriate
`baseline-final-sources-HOST.json` and `candidate-final-sources-HOST.json` file maps and use their
recorded build commands. The rejected experiments also include their private benchmark source,
BUILD registration, functional owner-lifetime tests and lint target snapshots. The selected change
extends the existing `compare_benchmark` target with a valid counts-only case. No private benchmark
framework is required for production use.

The `prepare-*`, `freeze-*`, `measure-memory.py`, `measure-native.py`, `analyze-*` and qualification
scripts record exact invocations and cardinality/identity checks. Their host/worktree/output paths
are experiment-specific: adapt those paths in a fresh evidence root rather than overwriting the
immutable original records. Match release configurations, Linux affinity, worker requests,
fixtures and retained repetition policy; run the full preflights before timing. Keep sanitizer
qualification outside timing, preserve the unchanged-parent copy, and prevent other heavy work
from sharing the timing hosts. A rebuilt executable with different bytes requires a new qualified
identity; the narrowly proven Mac debug/signature exception is not a general normalization rule.

The archive's eligibility notes distinguish the invalid counts-only-scope partial process and
failed owner-native selection preflight from the final complete matrices. Retained failed compile,
lint and identity attempts explain corrections; they are not successful qualification. Analysis
percentages are medians of paired-session ratios, while any independently reported absolute time
is a marginal median and need not reproduce that percentage by division.
