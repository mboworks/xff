# CI performance measurements

## Main after PR 847

Run [35152024018](https://github.com/mboworks/xff/actions/runs/35152024018),
commit `2db077917`, completed successfully. Times below come from job timestamps, Bazel logs,
and the uploaded cache/fuzz measurements, not estimates from cache-restore messages.

| Measurement                             | Main after PR 846 | Main after PR 847 |
| :-------------------------------------- | ----------------: | ----------------: |
| First job start through done completion |             562 s |             499 s |
| Coverage start delay                    |              51 s |               0 s |
| Fuzz start delay                        |              51 s |               2 s |
| Coverage job duration                   |             356 s |             330 s |
| Fuzz job duration                       |             467 s |             485 s |
| Parallel campaign phase, four workers   |         184.751 s |         184.661 s |
| Total fuzz executions                   |         7,393,740 |         7,909,468 |

The previous run is [35150612313](https://github.com/mboworks/xff/actions/runs/35150612313).
The campaign phase is about 69% shorter than the former 600-second sequential minimum.
These are uncontrolled runs with evolving fuzz inputs: the execution-count difference is not a
controlled throughput benchmark. Per-target rates vary; neither run shows a campaign failure.

### Cache retention

| Configuration | Retained uncompressed bytes | Uploaded compressed bytes | Evictions |
| :------------ | --------------------------: | ------------------------: | --------: |
| ASan          |               2,340,962,152 |               460,474,445 |         0 |
| TSan          |               1,346,498,087 |               303,415,664 |         0 |
| MSan          |               3,024,846,401 |               611,739,168 |         0 |
| Coverage      |               1,272,653,247 |               266,658,005 |         0 |
| Fuzz          |                 301,692,108 |                76,742,237 |         0 |

All ten build-cache measurements report zero evictions. Repository cache inventory during this
analysis totaled 2,687,676,819 bytes, about 26.9% of the decimal 10 GB budget. It includes other
caches and is a snapshot rather than peak storage.

ASan, TSan, and MSan each had 3,055 disk hits and no executed build actions. Normal Clang, GCC,
minimal, documentation, and fuzz builds also had no executed build actions. Coverage had 3,065
disk hits and two executed actions. Internal Bazel actions are excluded from those execution counts.

### Remaining preparation delay

The initial fuzz build took 265.402 seconds despite a 1.23-second action critical path and no
executed build actions. Ten cached launcher preparations then took about six seconds; actual
campaigns took 184.661 seconds. Coverage's Bazel invocation took 276.800 seconds with a
3.57-second action critical path. GCC's cached build took only 32.009 seconds.

This points to shared Clang preparation, not compilation or fuzz scheduling, as the next target.
Toolchain download/extraction is a candidate, but job logs alone do not identify the responsible
repository operation. Do not infer that the entire gap is network time or extraction time.

Coverage and the initial fuzz build retain JSON traces with repository-fetch, repository-function,
Starlark builtin, and fetch events. Their seven-day artifacts are named
`coverage-preparation-profile` and `fuzz-preparation-profile`. Only the initial fuzz build receives
its profile path, so later launcher preparation cannot overwrite the trace. Failed builds upload
whatever trace is available. These are diagnostic artifacts, not persistent module/compiler caches.

Use the traces to identify long repository operations and nested download/extraction work before
changing toolchain packaging, fetching, or caching. Keep compiler/module downloads out of the
compiled-output cache.

## Default cache budget

The shared uncompressed compiled-output budget is 1 GB. The cleanup ceiling for ordinary
compressed uploads remains 700 MB; instrumented jobs retain their explicit larger allowances.
ASan has a 3 GB uncompressed allowance and a 1 GB compressed ceiling.
Main run `35159334794` measured 729 MB before trimming for clang-tidy, and 674-692 MB for
Linux, macOS, and GCC builds. These fit the default without evicting outputs. The previous
clang-tidy PR run had no restorable cache at all; trimming and complete cache misses are distinct
problems. This main run created a 96 MB compressed clang-tidy cache. ASan reached 2.80 GB before
trimming, so its explicit 3 GB allowance also avoids eviction for this workload.
