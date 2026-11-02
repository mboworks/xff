# Field lookup allocation controls (2026-11-02)

## Decision: retain the existing runtime

Neither implementation qualifies for adoption. Keep both experimental source variants and their complete observations; publish this report without changing production callbacks or containers. They eliminate temporary lookup-key allocations and improve focused kernels, but fail ordinary whole-command controls, particularly Linux multi-field output. Allocation reduction alone is not a verified command-level improvement.

O05 follows the reconciled performance inventory and its instruction to try existing values/maps before interning. The compiled segment already owns a key, while its callback receives a view. The baseline turns that view back into a temporary owning string for ordered-map lookup. Two fully implemented alternatives were tested:

- `owned_key_reference`: callbacks borrow the compiled string by const reference, allowing ordinary map lookup directly. This changes every field callback ABI, including unrelated builtin fields.
- `transparent`: preserve the callback interface and use `std::less<>` in definition/capture maps and their owners/observers. Look up the view directly. String ownership and lexical ordering stay unchanged; no interner, wrapper or lock is introduced.

Keep future prepared binding or genuinely heterogeneous callers in the backlog (O13/O15). They need new measured evidence; neither candidate is a reason to introduce a second catalog or per-entry ownership layer. This rejection is scoped to the tested variants, not a proof that heterogeneous lookup is intrinsically slow.

## Provenance and protocol

Reference `f7fea209333dc2925fe43b9eb2e6e4da3b6ee22b` is the first published #1044 field-rendering head. It includes merged collection/glob improvements but predates the later merged fuzzy implementation. The experiments do not establish adoption on newer main: their negative acceptance decision is preserved, and any future revival needs fresh qualification. Runtime sources, common harness additions, patches, actual binary identities and launch helper source are retained below.

Both hosts use release LLVM 23 + ThinLTO. macOS is the local M5 Pro with warm APFS, requested 1/3/10 workers and no affinity. Linux drogon is the Ryzen 9950X KVM guest with tmpfs fixtures and child CPU affinity 1/3/10. Do not compare absolute times across these contracts. Builds, correctness tests and allocation diagnostics finish before timing.

Initial qualification compares baseline with owned-key callbacks. Confirmation compares baseline, owned-key callbacks and transparent maps in one matrix. Each native case has three sessions, a discarded warmup per variant/session, nine interleaved measured rounds and fastest seven retained. Exact output is independently checked after every invocation. Counts are 10/1,000/10,000/100,000 at one/three workers, plus 100,000 at ten workers. Short and long definition keys are paired with filename and six-component output controls. All use explicit UTC, `--exact`, `--sort=dir` and no config/pager. Across both phases/hosts, 10,800 fresh invocations include discarded warmups.

In-memory compiled-template kernels separately measure short/long definition and capture keys, plus filename/multi-component controls, over 10/1,000/100,000 short/long paths. Three shuffled sessions, nine repetitions and fastest seven. Fixture maps and templates are prepared before timing. A new copied/moved-key test and the existing field/fuzz, evaluator, expression-contract and whole-engine targets pass on both hosts (seven targeted suites). Full production CI is not claimed for a runtime change that was rejected before publication.

Changes below are medians of within-session elapsed-time ratios; negative means faster. Raw session means are preserved, rather than percentages alone.

## Confirmation: 100,000-file command changes

| Host  | Case             | Workers | Baseline ms | Owned-key change | Transparent change |
| ----- | ---------------- | ------: | ----------: | ---------------: | -----------------: |
| macos | definition-short |       1 |     225.373 |           -0.86% |             -0.47% |
| macos | definition-long  |       1 |     227.481 |           -1.44% |             -0.79% |
| macos | name-control     |       1 |     224.958 |           -0.11% |             +0.03% |
| macos | multi-control    |       1 |     243.267 |           -0.06% |             +0.17% |
| macos | definition-short |       3 |     103.269 |           +2.19% |             +0.83% |
| macos | definition-long  |       3 |     104.085 |           -1.26% |             -1.11% |
| macos | name-control     |       3 |     106.598 |           -3.30% |             -2.43% |
| macos | multi-control    |       3 |     103.473 |           +0.84% |             +0.46% |
| macos | definition-short |      10 |      78.884 |           +0.26% |             -0.32% |
| macos | definition-long  |      10 |      77.842 |           -2.75% |             -3.90% |
| macos | name-control     |      10 |      74.574 |           +2.37% |             +2.90% |
| macos | multi-control    |      10 |      77.644 |           -1.53% |             -0.18% |
| linux | definition-short |       1 |     100.448 |           +0.40% |             +0.92% |
| linux | definition-long  |       1 |     101.078 |           -0.14% |             +0.70% |
| linux | name-control     |       1 |     100.943 |           +0.23% |             +1.06% |
| linux | multi-control    |       1 |     109.450 |           +1.80% |             +1.44% |
| linux | definition-short |       3 |      37.282 |           -0.61% |             -0.99% |
| linux | definition-long  |       3 |      36.415 |           +0.03% |             +0.60% |
| linux | name-control     |       3 |      36.308 |           +0.61% |             +2.17% |
| linux | multi-control    |       3 |      41.188 |           +2.29% |             +4.27% |
| linux | definition-short |      10 |      20.861 |           +0.72% |             +1.67% |
| linux | definition-long  |      10 |      20.786 |           -2.31% |             +1.04% |
| linux | name-control     |      10 |      21.391 |           -4.28% |             +1.62% |
| linux | multi-control    |      10 |      29.028 |           +6.40% |             +8.18% |

The first two-way matrix already showed Linux multi-field regressions of 1.63% / 2.31% / 4.82% at one/three/ten workers for owned-key callbacks. Confirmation retains the concern instead of selecting favorable samples: the ten-worker multi-field control is +6.40% with owned keys and +8.18% with transparent maps. Long-key native output does not show a consistent Linux benefit. macOS has some improved long-key cases, alongside slower controls.

These controls do not isolate the cause of an unrelated output regression. ABI, template instantiation, layout and scheduling differ between variants; do not label one mechanism as proven. The acceptance result is that the tested combined commands fail to provide the required consistent benefit. No automatic benchmark gate or threshold was weakened.

## Confirmation: 1,000 short-path rendering kernels

| Host  | Kernel                         | Baseline us | Owned-key change | Transparent change |
| ----- | ------------------------------ | ----------: | ---------------: | -----------------: |
| macos | fields/name/1000/8             |      241.34 |           -1.38% |             +1.21% |
| macos | fields/components/1000/8       |      379.98 |           +3.29% |             +0.25% |
| macos | fields/definition-short/1000/8 |      249.09 |           -1.79% |             -2.00% |
| macos | fields/definition-long/1000/8  |      261.28 |           -4.29% |             -6.33% |
| macos | fields/capture-short/1000/8    |      245.27 |           -1.53% |             -1.93% |
| macos | fields/capture-long/1000/8     |      261.38 |           -5.90% |             -5.74% |
| linux | fields/name/1000/8             |       44.06 |           +1.88% |             -1.89% |
| linux | fields/components/1000/8       |      113.76 |           +0.05% |             -3.55% |
| linux | fields/definition-short/1000/8 |       47.47 |           -2.03% |             -3.18% |
| linux | fields/definition-long/1000/8  |       52.95 |           -8.10% |             -9.05% |
| linux | fields/capture-short/1000/8    |       47.30 |           -0.46% |             -2.77% |
| linux | fields/capture-long/1000/8     |       52.93 |           -8.38% |             -8.06% |

## Allocation-only diagnostics

An isolated ASan allocator hook counts 1,000 renders of an 80-byte definition key and capture key; the final `value|value` output fits inline storage. Setup, assertions and test-runner allocations are outside the count. Baseline makes 2,000 temporary-key allocations and frees; both alternatives make zero. Requested-byte totals differ with standard-library allocation rounding and are preserved in XML. The baseline intentionally fails the new zero-allocation assertion during this control; both candidates pass. It is not a production correctness failure. No sanitizer time is used as performance evidence.

## Footprint and limits

| Host  | Baseline bytes | Owned-key bytes | Transparent bytes |
| ----- | -------------: | --------------: | ----------------: |
| macos |      4,068,752 |       4,070,528 |         4,067,856 |
| linux |     12,287,256 |      12,288,432 |        12,281,936 |

Sizes are build-output executables, before release stripping/compression. Focused lookup gains do not justify an unconditional callback/container change when ordinary output regresses. The runtime prototypes remain local experiments; the report records why they were not promoted into a PR. Future work can revisit prepared bindings with separate cold/hot, missing-value, map-lifetime and native output controls.

## Lossless raw evidence

- [field-lookup-controls-20261102-initial-macos.json.gz](field-lookup-controls-20261102-initial-macos.json.gz): 153,250 bytes; SHA-256 `cba3535e2ab2c25e5c3f7e208f3a0f20ef85e31a0eb78c64f315d3345dbed87f`.
- [field-lookup-controls-20261102-initial-linux.json.gz](field-lookup-controls-20261102-initial-linux.json.gz): 151,632 bytes; SHA-256 `b8424d0fcd4b831d356fd702add9c7f2af4466fb9b823b15c043dc96adfcbe64`.
- [field-lookup-controls-20261102-confirmation-macos.json.gz](field-lookup-controls-20261102-confirmation-macos.json.gz): 228,997 bytes; SHA-256 `fe0bd17d3475409d79dd96cd30a5b082668aaf25babec60bb2ed7eccb47a27c1`.
- [field-lookup-controls-20261102-confirmation-linux.json.gz](field-lookup-controls-20261102-confirmation-linux.json.gz): 227,415 bytes; SHA-256 `a3187b93b84382258d4c19c403e4812dbea4845736fbcd71448341c4f4cdfb9b`.
- [field-lookup-controls-20261102-provenance.json.gz](field-lookup-controls-20261102-provenance.json.gz): 218,051 bytes; SHA-256 `8b13103d088d23903bada27ddf1b7688f0fb090e556472552b36b39de3995c2f`.
