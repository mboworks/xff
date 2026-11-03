# Reusable archive input buffers (2026-11-02)

## Decision

Retain a lazily allocated, session-owned `std::array<char, 65536>` for sources advertising caller-owned reads. Its allocation is uninitialized; only the returned byte count is exposed to libarchive. A borrowed view reaches the C callback and expires at the next refill. The existing owned-string path remains for decoders without `ReadInto`. Capability changes release the inactive storage so both buffer forms are not retained under one reservation. The session and its existing 64 KiB reservation retain the buffer until the reader is destroyed.

Reject counts beyond the supplied span before exposing bytes; preserve errors, short reads, EOF, format probes, seeking, member identity and shared budgets. There is no new cache, thread, lock, exception path, dependency, format parser, flag or file-I/O bypass. ZIP member indexing/seeking and decoder concurrency remain separate O19 work.

A separately measured reusable-string prototype avoids allocations too. The selected owned array removes its nested `resize_and_overwrite` callback and error mutation, with comparable or slightly better pilot timings. The original matrix, duplicate-baseline controls and three-way pilot remain available in the raw artifact.

## Measurement contracts

Parent `1a210e61da249e27314dc5fc9a779cba50eb8d72` includes #1059 and the isolated CI repair already merged in #1058. The selected runtime changes only `archive_reader.cc`; tests and the manual benchmark are common to every variant. Both use extension-enabled `//xff/cli:xff_full`, `clang_release`/ThinLTO and recorded compiler/link/source/executable identities. These are paired changes on a fixed dependency set; later dependency updates require fresh qualification.

Per host: 76 native cases x three sessions over TAR, gzip TAR and ZIP with 10/1,000/10,000 members and 128/4,096-byte payloads, listing versus a literal-content test on the final member, one/three requested workers, and ordinary-file controls. Independent Python-generated fixtures and full stdout/zero exits are checked. Workers are requests, not a claim of newly parallel archive decoding. Linux uses tmpfs and CPU affinity; macOS uses warm APFS.

Separate in-memory timing has 24 cases x three sessions x two variants, with legacy and caller-owned sources. Each case has nine rounds/repetitions, fastest seven retained and discarded warmup. Confirmation uses eight native cases x five sessions and seven memory cases x three sessions, adding an identical-baseline participant. Timings never overlap builds, lint, sanitizers or allocation probes on their host. A filename-aware formatting gate checks every variant before building. Earlier include ordering produced different binaries, so the complete matrix and confirmation were rerun on the canonical source rather than reusing those timings.

## Native commands

Median paired session change; negative means less elapsed time.

| Fixture / action      | CPUs |  Linux |  macOS |
| :-------------------- | ---: | -----: | -----: |
| tar-10-128 / list     |    1 | -1.85% | +0.37% |
| tar-10000-128 / list  |    1 | -1.69% | -1.76% |
| tar-10000-4096 / list |    1 | -2.50% | -4.99% |
| tar-10000-4096 / last |    3 | -2.61% | -6.94% |
| zip-10000-4096 / last |    1 | +0.41% | -0.19% |
| tgz-10000-4096 / last |    3 | +0.04% | +0.13% |
| ordinary / list       |    1 | -0.07% | -1.09% |
| ordinary / last       |    3 | -1.03% | -0.48% |

## Confirmation and controls

Candidate and byte-identical repeat each compare directly with baseline; repeat movement is not subtracted. Preserve slower small/ordinary controls as well as heavier TAR gains. ZIP/gzip work may remain decoder/header dominated.

| Fixture / action      | CPUs | Confirm Linux | Repeat Linux | Confirm macOS | Repeat macOS |
| :-------------------- | ---: | ------------: | -----------: | ------------: | -----------: |
| ordinary / last       |    3 |        +2.43% |       +0.56% |        +1.18% |       +1.12% |
| tar-10000-4096 / list |    1 |        -2.26% |       +0.05% |        -3.55% |       -0.12% |
| zip-10000-4096 / last |    1 |        +0.89% |       +0.18% |        +0.18% |       +0.10% |
| ordinary / list       |    1 |        +0.27% |       +0.11% |        +1.87% |       -0.93% |
| tgz-10000-4096 / last |    3 |        -0.04% |       -0.09% |        -0.03% |       -0.22% |
| tar-10-128 / list     |    1 |        -0.08% |       +0.70% |        +0.02% |       +0.38% |
| tar-10000-128 / list  |    1 |        -0.74% |       +0.49% |        -0.40% |       +0.26% |
| tar-10000-4096 / last |    3 |        -1.96% |       -0.71% |        -5.51% |       -0.05% |

## In-memory callbacks

Values are microseconds: median session fastest-seven repetition means. Fixture creation is outside the timed iteration. Legacy sources retain their existing owned-read path; a different executable can still move their timings.

| Source / action / members / bytes | Linux baseline | Linux selected | Linux delta | macOS baseline | macOS selected | macOS delta |
| :-------------------------------- | -------------: | -------------: | ----------: | -------------: | -------------: | ----------: |
| caller-owned/list/10000/4096      |       4,286.44 |       3,997.75 |      -6.73% |       3,957.20 |       3,957.09 |      -0.00% |
| caller-owned/last/10000/4096      |       3,843.83 |       3,629.33 |      -5.58% |       3,768.58 |       3,770.62 |      +0.05% |
| caller-owned/list/10/128          |           7.89 |           8.11 |      +2.75% |           6.62 |           6.69 |      +0.99% |
| legacy/list/10000/4096            |       4,263.61 |       3,956.39 |      -7.21% |       3,965.36 |       3,991.04 |      +0.65% |
| legacy/last/10000/128             |       2,395.93 |       2,134.93 |     -10.89% |       2,980.26 |       2,980.54 |      +0.01% |

## Allocation diagnostics

Separate warmed calling-thread ASan hooks count requests/bytes only. These are neither release timings nor peak/RSS measurements; fixture setup and reporting are excluded. Session input-buffer allocations no longer scale with chunk count on the caller-owned path; member records still allocate.

| Case                         | Host  | Baseline requests | Selected requests | Baseline requested bytes | Selected requested bytes |
| :--------------------------- | :---- | ----------------: | ----------------: | -----------------------: | -----------------------: |
| caller-owned/list/10000/4096 | linux |            60,742 |            60,039 |               50,601,064 |                4,574,320 |
| caller-owned/list/10000/4096 | macos |            60,742 |            60,039 |               50,595,400 |                4,574,288 |
| caller-owned/last/10000/4096 | linux |            60,730 |            60,027 |               48,377,040 |                2,350,296 |
| caller-owned/last/10000/4096 | macos |            60,730 |            60,027 |               48,371,352 |                2,350,240 |
| legacy/list/10000/4096       | linux |            60,742 |            60,742 |               50,601,064 |               50,601,072 |
| legacy/list/10000/4096       | macos |            60,742 |            60,742 |               50,595,400 |               50,595,408 |

## Correctness and footprint

Nine new behavior cases verify stable storage, short reads, late failures, impossible counts, EOF, unchanged legacy fallback, compression, alternating read capabilities and budget sharing/release. All 324 extension-enabled release suites pass per host, with required Linux FUSE. Four focused ASan suites pass per host; four Linux TSan suites and five Linux MSan suites pass, including the similarity reference test with its corrected timeout. Exact selected-source checks precede final validation. Scoped lint and enforcing hooks remain required; current pushed-head CI determines readiness.

Observed maximum process RSS for the 10,000-member, 4,096-byte TAR listing across all native samples; this is separate from cumulative allocation requests.

| Host  | Baseline RSS MiB | Selected RSS MiB |
| :---- | ---------------: | ---------------: |
| linux |            13.04 |            13.06 |
| macos |            13.56 |            14.28 |

- linux: unstripped full CLI 18,118,000 -> 18,121,664 bytes (+3,664).
- macos: unstripped full CLI 8,140,216 -> 8,140,216 bytes (+0).

## Lossless evidence

[archive-input-buffers-20261102.json.gz](archive-input-buffers-20261102.json.gz): 5,196,448 bytes; SHA-256 `8ee9b6ccdf8656162ff129fa1440d5ec4826be240a9e800d46e6f5089fcedf93`.
