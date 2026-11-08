# Ordered-output credit requalification after #1143

The retained H2 prototype still improves the large-output case but repeatedly slows ordinary
Linux searches. Keep it unselected and preserve its results for future redesign.

## Measurement

The baseline is main `f221c2c16a878855d12734b476286b54b5af9123`, including #1140, #1141,
#1143 and #1144. The exact retained H2 patch applies cleanly to this main on both hosts;
its resulting patch has SHA-256 `8e8f0e58a78c307fc5c925fdf0d7a852220d601efdb9f06253ce61bd6d5fe02b`.
Production CLI baseline and candidate are built with `--config=clang_release` and copied
after their respective builds. An exact baseline-image copy is measured as a drift control.
Four focused candidate targets (`ordered_output`, `parallel_match`, `rg`, and `run`) pass
on each host. This is a private prototype, not a production runtime change.

The original 16-case H2 native workload runs three balanced sessions per host with nine
observations per image and case, retaining the fastest seven. Linux uses the original
`/dev/shm` in-memory fixtures and physical-core affinity; macOS uses the original native
fixtures under `/private/tmp` and is unpinned. The launcher verifies exit status, and the
original driver verifies complete output against an independent oracle on every invocation.
Each host retains 1,296 timed observations plus 144 warmups. All sample exit codes and
fastest-seven means were independently checked; binary hashes and Linux transfer hashes
were verified. The recorded fixture filesystem is part of the measurement conditions.

## Complete results

Negative percentages are faster. Each value is the median of three session ratios;
baseline-copy controls are shown separately. These descriptive values do not establish
a global process memory bound or a universal speedup.

| Case                    | macOS candidate | macOS copy | Linux candidate | Linux copy |
| :---------------------- | --------------: | ---------: | --------------: | ---------: |
| `flat1000/1/rg-filter`  |          +0.42% |     +2.47% |          +0.39% |     +0.15% |
| `flat1000/1/rg-lines`   |          -0.42% |     -0.08% |          +1.20% |     -0.10% |
| `flat1000/3/rg-filter`  |         -10.79% |     -0.53% |          -0.27% |     -2.06% |
| `flat1000/3/rg-lines`   |         -11.96% |     -0.42% |          -2.07% |     -0.08% |
| `flat10000/1/rg-filter` |          -0.27% |     -0.72% |          +2.56% |     -0.34% |
| `flat10000/1/rg-lines`  |          +0.54% |     -0.48% |          +1.67% |     +0.12% |
| `flat10000/3/rg-filter` |          -3.18% |     -0.03% |         +12.85% |     +1.49% |
| `flat10000/3/rg-lines`  |          -2.99% |     +0.19% |         +10.88% |     -1.16% |
| `many10000/1/rg-filter` |          -0.58% |     -0.21% |          +1.48% |     -0.06% |
| `many10000/1/rg-lines`  |          -0.37% |     +0.05% |          +2.98% |     +0.13% |
| `many10000/3/rg-filter` |          -0.66% |     -0.29% |          +5.89% |     -2.38% |
| `many10000/3/rg-lines`  |          -1.33% |     -0.06% |          +4.32% |     +0.81% |
| `output100/1/rg-lines`  |          +0.54% |     +0.04% |          -0.51% |     -0.28% |
| `output100/3/rg-lines`  |         -24.66% |     -1.59% |          -9.92% |     +0.40% |
| `output1000/1/rg-lines` |          -1.24% |     -1.84% |          +0.24% |     -0.48% |
| `output1000/3/rg-lines` |         -52.64% |     -3.26% |         -13.99% |     +1.32% |

Linux `flat10000/3/rg-filter` is +12.85% (copy +1.49%); its candidate session
changes are +9.75%, +14.44%, and +12.85%. `flat10000/3/rg-lines` is +10.88%
(copy -1.16%), with +10.88%, +10.83%, and +14.02% sessions. These repeated
ordinary-search costs outweigh the large-output benefit for adoption.

The 1,000-file large-output, three-worker case improves -52.64% on macOS (copy -3.26%)
and -13.99% on Linux (copy +1.32%). The test also captures per-process resource data;
it does not prove a global memory cap. Input/slot/source/renderer-temporary admission,
native output, and traversal overlap remain open as described in the original report.

## Evidence

[The new lossless capsule](ordered-output-credit-requalification-20261108.json.gz) contains 18 individually hashed
records (335,561 compressed bytes; SHA-256 `a7cd79be04501fef580f09b47a6b76ff7f5520ddae410b4f10497f43aba4d543`). It retains the full raw sample
populations, manifests, fixture digests, build/test logs, driver, both source patches,
analysis and Linux transfer manifest. Executable copies remain private; their identities
are recorded in the capsule. The original report and capsule remain unchanged.
