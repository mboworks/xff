# Reuse immutable default catalogs (2026-11-02)

## Decision

Retain one generated default vocabulary per append-only database-registration epoch and reuse the default filtering catalog. Keep default initialization lazy. Repeated `Configure` calls without user files/edits can restore the same immutable default rather than reparsing generated JSON, rebuilding maps and retaining another complete snapshot. A later registration invalidates that default by registry size; registration only appends through the existing API. Old views and catalog handles remain valid.

Custom files and type edits still build and validate a fresh configuration before publication. User files are not cached by pathname. Restoring defaults after a custom configuration selects the immutable default again. No public API, flag, per-file branch, new lock or ownership wrapper is added. Existing per-entry global lookup locks remain; this is not a new lock-free lookup representation.

The actual opportunity is repeated library use and retained default memory. Empty configurations were already lazy for ordinary CLI startup. This change does not eliminate the first JSON parse or claim that each fresh CLI starts faster. Generated flat/offset tables, per-run MIME observers and broader catalog binding remain separate unqualified follow-ups. Pointer-rich static tables could add binary size/macOS fixups, as the existing startup audit explains.

## Measurement contracts

Measured parent `ed9763f4f8f893c3fd9d7d727c175b0433fb9ac9` (#1053). Final synchronization preserves that source tree. Clean `clang_release`/ThinLTO baseline and `immutable_defaults` binaries are copied before measurement. All runtime patches, support sources, binary hashes, flags, raw samples and scripts are retained. Copied benchmark binaries load only stable system libraries. No build, lint or sanitizer overlaps timing on the same host.

Fresh-command controls cover 40 native cases per platform in three sessions, a discarded warmup per participant/case/session, nine rotating rounds and fastest-seven means. Independent output oracles validate every invocation. Ten/1,000/10,000-file MIME filters/fields, custom type additions and ordinary listing/metadata/comparison use one/three/ten requested workers. Linux uses tmpfs and CPU affinity; macOS uses warm APFS without affinity.

The three separate repeated-library kernels link the real language/MIME database modules. Each process warms the selected mode once before each repetition, then times exactly five resets/lookups. Nine repetitions and fastest-seven means are measured in three sessions. Fixed iteration counts bound the baseline retained snapshots; adaptive calibration would retain ever more complete JSON vocabularies. Sub-microsecond candidate values include loop/timer costs and are not a fresh-startup or per-file throughput estimate.

The memory probe runs each case/variant in three fresh children, with the same nine repetitions/five resets and one setup reset per repetition (54 resets per child). Peak RSS is reported separately from kernel timing and allocation counts. Five native controls receive five-session confirmation with a byte-identical baseline participant; those results do not replace the initial samples.

## Repeated library construction

Values are microseconds per timed reset+lookup; the first construction is outside the timed loop.

| Mode     | Linux baseline us | Linux reused us | macOS baseline us | macOS reused us |
| :------- | ----------------: | --------------: | ----------------: | --------------: |
| language |         2,018.578 |           0.080 |         2,826.899 |           0.238 |
| mime     |         3,684.825 |           0.104 |         4,764.123 |           0.308 |
| catalog  |         3,179.203 |           0.142 |         4,833.805 |           0.527 |

Repeated default construction drops from milliseconds to sub-microsecond handle/reset work. The language test independently verifies that repeated defaults call the immutable provider once, custom edits still construct their own configuration, and a later registration refreshes the default. These numbers do not describe the first construction in a new process.

## Peak RSS in the repeated-use probe

MiB, median of three children per case and variant; includes executable/runtime and retained data, not just cache-owned bytes.

| Mode     | Linux baseline MiB | Linux reused MiB | macOS baseline MiB | macOS reused MiB |
| :------- | -----------------: | ---------------: | -----------------: | ---------------: |
| language |              97.91 |             5.99 |              90.19 |             4.92 |
| mime     |              95.35 |             6.42 |              82.16 |             5.45 |
| catalog  |              98.61 |             6.41 |              89.98 |             5.81 |

The baseline intentionally retains published snapshots to keep existing views valid. Reusing the default avoids that repeated growth while retaining customized and older-registration snapshots. This does not introduce a total memory cap or garbage-collect snapshots still covered by the lifetime contract.

## Fresh CLI controls and confirmation

Median paired session changes; negative means faster. Preserve initial costs and session reversals rather than projecting warm-kernel gains onto native commands.

| Fixture / task        | Workers | Initial Linux | Confirm Linux | Baseline repeat Linux | Initial macOS | Confirm macOS | Baseline repeat macOS |
| :-------------------- | ------: | ------------: | ------------: | --------------------: | ------------: | ------------: | --------------------: |
| flat10000 / metadata  |      10 |        +9.88% |        +1.18% |                +0.04% |        -0.19% |        +1.81% |                +0.84% |
| flat10000 / files     |       1 |        +3.02% |        +0.09% |                -0.29% |        -0.42% |        +0.98% |                +0.27% |
| flat10 / mime-filter  |      10 |        +2.34% |        -0.46% |                -0.72% |        +0.10% |        +1.42% |                +0.24% |
| flat10 / metadata     |       3 |        -0.98% |        +0.14% |                -0.13% |        +1.65% |        +0.69% |                +0.01% |
| flat1000 / mime-field |       1 |        -1.29% |        -0.93% |                -0.84% |        -1.22% |        +1.94% |                -0.07% |

The initial Linux 10,000-file/ten-worker metadata control is +9.88% with session values +16.47/+0.30/+9.88%; confirmation is +1.18% with the repeated baseline +0.04%. Metadata-only listing does not request these catalogs, so the cause must not be attributed to default reuse. The confirmed macOS MIME-field control remains about +1.94% and metadata about +1.81%; retain those modest costs. The warm-memory benefit does not imply every CLI is faster.

## Correctness and footprint

Separate test processes verify reused default-view identity, provider-call counts, custom type edits/default restoration, new registrations, preferred MIME overrides, and old view/catalog validity. Existing core and bundled-database suites retain shared extension candidates, aliases, precedence, metadata and error handling. No new test uses host file I/O.

- linux: unstripped CLI output 12,334,504 -> 12,337,640 bytes (+3,136).
- macos: unstripped CLI output 4,069,744 -> 4,069,696 bytes (-48).

Complete core/extension release suites pass 308/308 per host, with required Linux FUSE mounts. Seven focused ASan suites pass on each host, seven Linux TSan suites pass, and all five changed translation units pass local scoped clang-tidy. Normal enforcing hooks and current pushed-head CI remain required before merge.

## Lossless evidence

[immutable-default-catalogs-20261102.json.gz](immutable-default-catalogs-20261102.json.gz): 714,698 bytes; SHA-256 `74bdd51eed4d7225a56fab4b192b33a771cc732adfebc784cd43c3e97d77faea`. Recover native fixtures through `fixtures[report.fixtures_ref]`. Initial, confirmation, warm-loop and RSS populations retain separate provenance.
