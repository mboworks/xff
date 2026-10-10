# Native Linux performance profiling

Profiling is separate from accepted timing collection. Finish all builds before
measuring, and do not run profiling or compilation alongside a benchmark batch.
Use the same retained executable and prepared fixture roots as the uninstrumented
comparison. Preserve commands, CPU allocation, output hashes and profile artifacts.
See [the native correctness audit](benchmark-correctness-audit.md) for the required
complete grid and Linux/macOS acceptance controls.

## Google Benchmark hardware counters

The opt-in `linux_perf` configuration enables the pinned Google Benchmark's
libpfm integration. Bazel builds libpfm from source; installing a system
`libpfm4-dev` package is not required. This is a native Linux configuration, not a
portable addition to the release or default build. Ordinary macOS builds are
unchanged; use native sampling/Instruments there instead.

```sh
bazel run //xff/engine:collect_benchmark --config=clang_release --config=linux_perf -- \
  --benchmark_filter='^Collect/100/1/threads:1$' \
  --benchmark_min_time=0.01s --benchmark_perf_counters=cycles,instructions
```

Check that the filter actually ran a benchmark and that its output contains
nonzero `cycles` and `instructions` counters. A successful build, a zero-match
filter, or timing columns alone do not establish working counters. Use
`--benchmark_list_tests=true` to find valid names for another benchmark target.
The short duration above is a capability probe, not performance evidence.

Google Benchmark 1.9.5 pins libpfm 4.11.0.bcr.1. That BCR source names the retired
`netcologne.dl.sourceforge.net` mirror. The downloader configuration
redirects exactly that archive to `downloads.sourceforge.net`, preserving the
pinned version, BCR patches and integrity check. No LLVM/toolchain dependency
changes are involved. It does not rewrite other archives or disable checksum
validation. If network access is unavailable, an independently verified copy of
`libpfm-4.11.0.tar.gz` can be supplied through Bazel's `--distdir=PATH`; do not
substitute an unverified system library.

Unconfigured `bazel query` traverses all `select` arms, including the optional
libpfm dependency, even when hardware counters are disabled. Queries therefore
use the same narrowly scoped download repair so compile-source indexing can
complete. This does not enable counters or change default/release builds,
compiler/linker selection or macOS runtime behavior. Source indexing still
fails closed on any unsuccessful query.

## Kernel and external perf capability

The `perf` executable is a separate host profiling tool, not a libpfm dependency.
Install it using the host distribution's supported kernel-tools package if it
is absent. Check actual capability as the user who will collect the profiles:

```sh
command -v perf
sysctl kernel.perf_event_paranoid
perf stat -e cycles:u,instructions:u -- /bin/true
```

Inspect the exit status and event output. A sysctl value alone does not prove
hardware counters work: kernel support, available PMUs, capabilities and
container/security policy can also restrict access. Do not automatically change
the host's security policy or install persistent sysctl overrides. Arrange any
necessary permission change with the host administrator. Recheck the actual
events to be used; system-wide and kernel-inclusive monitoring can require more
permission than profiling one process's user-space work.

## Fresh-process profiles

Build or select the retained release executable before collecting profiles.
Choose the allowed physical-core IDs recorded by the audit's affinity probe;
do not assume that CPU IDs `0,1,2` are available on another host. For example,
replace `CPU_IDS`, `BINARY` and `ROOT` below with those recorded values:

```sh
perf stat -r 50 \
  -e task-clock,context-switches,cpu-migrations,page-faults,cycles:u,instructions:u -- \
  taskset -c CPU_IDS BINARY ROOT --jobs=3 -type f
perf record --call-graph dwarf -o /tmp/xff-walk-perf.data -- \
  taskset -c CPU_IDS BINARY ROOT --jobs=3 -type f
perf report --stdio -i /tmp/xff-walk-perf.data
```

Repeat with workers `1`, `3` and `10` on exactly the same roots. The `stat -r`
form launches a fresh command each time; preserve its variability rather than
treating it as the audit's fastest-seven-of-nine timing estimator. Keep output
handling identical between controls and verify correctness separately. A single
very short `record` can have too few samples to attribute cost: aggregate fresh
invocations in a controlled harness with correctness checks when necessary.
Retain absolute times and the raw counters alongside the stacks; fewer created
threads or fewer context switches alone do not establish higher throughput.

## Native capability validation

On 2026-10-10, a forced fetch with empty download and repository-content caches,
system/home RC files disabled and no `--distdir` fallback succeeded through this
configuration. The fetched archive retained the pinned SHA-256:
`5da5f8872bde14b3634c9688d980f68bda28b510268723cc12973eedbab9fecc`.
The `Collect/100/1/threads:1` capability probe actually ran 6,539 iterations and
reported nonzero cycles and instructions. External `perf stat` likewise recorded
both events for `true`, with `kernel.perf_event_paranoid=0`. The five repository
tooling tests pass, including the opt-in and exact-rewrite regressions.

These checks establish this Linux host's capability, not performance acceptance
or permission on another machine. No sysctl, persistent host setting, default
build configuration or macOS linker/toolchain dependency was changed.
