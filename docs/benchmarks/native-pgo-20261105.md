<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# O23: native profile-guided release compilation, 2026-11-05

This is a completed private compiler experiment on exact XFF commit
`d148059dc631f15d9dcf6ca70ea256c8bb0658ac` (#1110). It does not enable PGO in
XFF builds or publish a training profile for release use.

**Decision:** retain the measured Linux opportunity and the complete evidence, but
keep the portable release default unchanged. Linux has repeatable gains across
these workloads. macOS has a useful archive gain, mostly small or flat generic
gains, and a 14.33% larger executable. A portable, reproducible training producer
and a deliberate release-policy decision are still necessary before adoption.
The later compiled symlink-target-demand experiment is independent and is not
included in these results; profiles would need regeneration after source changes.

## What was built

Both hosts used the existing `--config=clang_release --config=xff_full` build,
including its O2 optimization and actual Bazel `thin_lto` feature. No source,
allocator, ISA, linker policy, or runtime default was changed. Actual C++ action
queries recorded both profile and LTO flags.

Training used Bazel `--fdo_instrument=PATH` and
`--copt=-fprofile-update=atomic`. After training, native `llvm-profdata` merged
the 179 profiles on each host; its recorded version is LLVM 23.1.4. The actual compiler binary, resolved from
the recorded production action resource directory, is also Clang 23.1.4 on both
hosts (LLVM revision `7a941dfb2877a0b8cf30323fefa85ad019e0b0f5`). The final
candidate used `--fdo_optimize=HOST.profdata`, with no generation flags. Profiles
and binaries are bound to the exact source, compiler, architecture, training
contract and recorded hashes. These are not interchangeable cross-platform
profiles, nor reusable promises for a later source tree.

Instrumented training binaries write raw profiles on process exit outside the
VFS. They are isolated diagnostic producers, never safe-mode release binaries.
Final parent and optimized images contain no profiling runtime symbols; the
instrumented image is the positive control. Dynamic dependencies are identical
between parent and optimized images on each host. The optimized product has no
new dynamic dependency or profiler-output path.

| Image                        | Linux bytes | macOS bytes |
| :--------------------------- | ----------: | ----------: |
| Parent                       |  18,435,008 |   8,274,424 |
| Training only                |  28,508,048 |  17,489,288 |
| Optimized candidate          |  17,493,792 |   9,460,296 |
| Candidate size change        |      -5.11% |     +14.33% |
| Merged host-specific profile |   2,771,256 |   2,867,040 |

## Training, held-out cases, and correctness

Each host has 179 declared training invocations and 179 held-out invocations.
Native tree training uses file counts 100 and 10,000; native held-out cases use
10, 1,000 and 100,000 as selected by the retained original matrix. Archive
training uses 1,000/10,000 members and held-out tests use 100,000. Canonical
benchmark scenarios train on 128/2,048 entries and hold out 64/4,096. Regex
subjects, file counts and comparison fixtures also have independent held-out
forms. Three startup controls deliberately repeat the same public command forms;
they do not claim disjoint executed code.

The fixtures include regular and symlink trees, mixed trees, broad/deep layouts,
1/3 workers, name/content-present/content-absent/fuzzy/safe/listing cases,
RE2/PCRE2 complex regexes, archives, all four comparison statuses and startup.
Both parent preflights and every training invocation passed their output oracles.

The full held-out experiment has three independently balanced sessions, nine
measured repetitions retaining the fastest seven per case, and a separate
byte-identical copied-parent executable. There are 14,499 measured invocations
plus 1,611 excluded warm-ups per host, before separately logged resume warm-ups.
An initial one-session fastest-two-of-three screen is also retained. The native
helper measures the executable, excluding Python startup. Linux uses the first
one or three allowed affinity CPUs; macOS requests one/three workers without
claiming CPU pinning. Canonical pauses are honored between invocations with
checked, excluded rewarming on resume.

Every invocation checks exit status, stderr and complete stdout length/hash.
Native and archive expectations remain the original independent expectations.
Unordered NUL records are sorted outside elapsed time only where the command's
ordering contract requires it. Regex expected payloads and comparison counts,
statuses and bytes are independently generated. Startup outputs use frozen
parent hashes. All held-out correctness checks passed on both hosts.

These are native filesystem CLI experiments, not a claim of equal storage across
hosts: macOS uses cached APFS fixtures; Linux also uses tmpfs fixtures. Do not
compare absolute platform times as though the CPU were the sole difference.

## Held-out timing

The value is candidate/parent elapsed time minus one, so negative is faster.
For each case, take the median percentage from its three sessions; then report
the median of those case medians in each group. Do not pool 537 session rows or
subtract the copied-parent control to manufacture a gain.

| Held-out group | Cases | Linux elapsed change | macOS elapsed change |
| :------------- | ----: | -------------------: | -------------------: |
| regular        |    32 |              -5.131% |              -0.966% |
| mixed          |    24 |              -5.767% |              -1.564% |
| canonical      |    64 |              -5.645% |              -0.615% |
| archive        |     8 |              -9.793% |              -7.956% |
| symlinks       |    24 |              -5.663% |              -1.090% |
| regex          |    16 |              -3.616% |              +0.012% |
| compare        |     8 |              -3.292% |              -0.172% |
| control        |     3 |              -2.476% |              +0.153% |

Linux's regular, mixed, canonical, archive and symlink groups have every case
faster in all three sessions. There is nevertheless a repeatable counterexample:
the 4,096-entry, three-worker status comparison is about 2.80% slower (0.133 ms).
The initial screen's roughly 14% symlink regression did not repeat; both screen
and final measurements remain retained rather than replacing inconvenient data.

On macOS all eight archive cases improve in every session, while ordinary
workloads have small, mixed gains. The worst canonical case, deep/64/three-worker
content, is about 3.65% slower (0.208 ms) in all three sessions. Startup is flat or
slightly slower. Copied-parent differences are included in the per-case data and
limit the interpretation of small gains. Neither host's result is claimed as a
universal workload guarantee.

## Release test qualification and retained failures

All 331 root, extras and API release test targets passed on both hosts (246 executed
and 85 satisfied from their valid Bazel test cache in each final run), including
Linux's required real-FUSE test. Production compilation remains profile-guided;
217 test-only source units use ordinary optimization. The test configuration uses
static production code generation and `--build_tests_only`, which avoids unrelated
default non-test outputs and does not filter out tests. The final production CLI
is selected with `config(//xff/cli:xff_full, target)` and has exactly the measured
candidate hash after testing on both hosts.

The narrower qualification context is explicit because these unsuccessful trials
also happened and remain retained:

1. Unqualified global profile use encountered a CFG mismatch when applying the
   production non-PIC profile to a PIC expression compilation.
2. A targeted static expression test passed; building all default outputs still
   requested the unmatched PIC output. Static code generation with
   `--build_tests_only` removed that irrelevant output.
3. Instrumentation named unrelated CLI and test-helper `main` functions alike.
   Actual production action queries and the complete test source catalog were
   used to scope profile use: only untrained test-only source paths receive
   `-fno-profile-instr-use`. Production retains PGO; no diagnostic policy is
   weakened and no warning is ignored.
4. After all tests passed, an unqualified `cquery` returned both target and cached
   exec configurations and an identity assertion failed. An identity-only
   recovery explicitly selected the target configuration, verified the exact
   executable hashes and did not rerun tests or alter their result.

Original logs, failed driver versions and the query recovery note remain
available. No claim is made that the original dynamic, all-default-output test
configuration passed. These are release tests, not sanitizer timings or a claim
that every sanitizer configuration was rerun with this profile.

## Costs and remaining adoption work

Observed cached-context build times were parent/instrumented/optimized 9.9/97.4/
151.6 seconds on macOS and 3.4/50.0/47.2 on Linux. These are not controlled cold
build comparisons. Final full qualification took about 562.6 seconds on macOS
and 517.7 on Linux and required many new compile/LTO actions. Profile inputs can
invalidate broad compiler-cache keys. Raw training profiles occupied about
356 MB on macOS and 344 MB on Linux before compression. Retain a compact evidence
archive rather than adding raw training files to the repository or Pages.

Before enabling an optional Linux profile producer or any release default:

- Implement a portable, committed training producer instead of depending on
  retained private fixtures and host paths.
- Bind profile metadata to source, actual compiler version, architecture,
  extension set, training contract and complete binary/profile hashes.
- Decide artifact provenance, retention, regeneration, supply-chain trust, cache
  scope and a clean source-only fallback when no valid profile is available.
- Extend held-out performance coverage for features not represented here and
  keep broad correctness/safety testing; training is not a safety proof.
- Resolve the deliberate static/test-only profile scope as part of supported
  build policy, without weakening warnings or silently changing test coverage.
- Rebuild and remeasure integration with future runtime winners. Do not add
  percentage gains from separately built candidates.

## Evidence layout

The experiment roots are `/private/tmp/xff-pgo-experiment` on macOS and
`/tmp/xff-pgo-experiment` on Linux; Linux large measurement output is under
`/dev/shm/xff-pgo-experiment`. Both have frozen parent, instrumented and candidate
images/manifests, raw and merged profiles, production action queries, full
training/held-out cases and contracts, oracle results, screen/full measurements,
per-case analyses, audit logs and test qualification records.

The authoritative full analysis is `full-HOST/case-median-analysis.json`;
`qualification-scoped-final-HOST.json` records final tests and exact image
identity. Earlier failed qualification records have distinct names. Image audits
include a profiler-runtime positive control and actual dynamic dependency lists.
The scripts and source/driver hashes are retained so failed probes and successful
results remain distinguishable.

## Downloadable evidence

The [lossless evidence archive](native-pgo-20261105.json.gz) contains both hosts'
measurement and qualification populations, scripts, commands, original failures,
queries, image audits and exact source/compiler/profile identities. It contains
537 named records, stored by content hash as 152 unique payloads. Its compressed
size is 8,847,607 bytes; the preserved named records reconstruct 146,092,887 bytes.
The SHA-256 of the compressed archive is
`eb39325c7f74ca3e8158b8a5e6c10cb4e7123f914dc732e5b5609342e7187ed8`.

The gzip file decompresses to JSON with format
`xff-lossless-experiment-group-encoded-evidence-v1`. The `contents` object maps
SHA-256 hashes to `{encoding, value}` payloads. Most payloads are UTF-8 text;
the two merged host profiles use base64 so exact binary evidence is retained.
Each `experiments[].hosts[].records[]` item gives the original relative path,
byte length and content hash. Decode the selected payload according to its
encoding and verify its length and SHA-256 before reconstructing a record.
All record hashes, lengths, paths and host ownership were verified before
publication, as were both complete 179-case populations and final image hashes.

Retained executable and raw-profile inventories provide original-host paths,
sizes and hashes rather than embedding executable images or 700 MB of raw
training output. The merged profiles are diagnostic evidence for this exact
experiment, not supported release inputs. No repository build reads this archive
or applies its profiles. A future supported profile producer must regenerate and
validate its own source- and toolchain-bound output.
