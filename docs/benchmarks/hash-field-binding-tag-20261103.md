# Hash-field binding: direct tag refinement

The main-based coverage run for PR #1070 identified an obsolete hash parser whose address was
used to classify bound fields, although rendering no longer called it. The implementation now
uses the existing nonzero hash-binding tag and removes that parser. A segment is a field when
it has an ordinary renderer or a hash binding; literal and unknown-field behavior, content
counts, extraction, transforms, validation and dynamic defaults retain their contracts.

This refinement is measured against the exact pushed `d95860e8e4aa5ed57e442b8b7e91b8be88fee2d2`
implementation, separately from the [original qualification](hash-field-binding-20261103.md).
It adds no bytes to the segment layout, public API, dependencies, locks or content caches.

## Comparison

Each host runs 16 in-memory cases and 20 native cases in three independent sessions, with nine
interleaved repetitions and the fastest seven retained. Native cases discard warmups and check
complete output and exits against independent hashlib expectations. They cover 10, 1,000 and
10,000 files, long paths, ordinary listing, repeated fields and transformed hash fields with one
or three requested workers. Linux uses tmpfs and CPU affinity; macOS uses warm APFS. No release
timing overlaps builds, lint, tests or instrumentation on the same host. Negative changes mean
less elapsed time. These are paired changes from the already optimized implementation.

| Case                                                          |            Linux |            macOS |
| :------------------------------------------------------------ | ---------------: | ---------------: |
| Sixteen warm explicit hash fields                             |           -7.00% |           -7.98% |
| Sixteen warm default hash fields                              |           -3.28% |           -5.71% |
| Whole-engine repeated hash fields, 1,000 files / 1 worker     |           -3.23% |           -0.87% |
| Whole-engine transformed hash fields, 1,000 files / 3 workers |           -1.52% |           +0.15% |
| Ordinary name-field kernel                                    |           +0.04% |           +5.08% |
| Ordinary component-field kernel                               |           +0.83% |           +3.32% |
| Single warm explicit hash field                               |          +13.13% |           -1.87% |
| Native command change range, all controls retained            | -2.01% to +3.51% | -0.55% to +0.75% |
| Unstripped full CLI byte change                               |           -4,896 |             -208 |

Small kernels have mixed costs; do not describe every row as faster. The single-field Linux
cell and ordinary-field macOS cells remain in the lossless results with absolute times.
Compilation changes span -1.59% to +1.35% on Linux and -6.69% to +5.27% on macOS.
The original optimization's populations remain separate, rather than pooling timings across
this refinement or subtracting unrelated percentage changes.

## Verification and evidence

The added regression distinguishes hash tags from literals, escaped hash text and unknown fields,
and checks rendering, content counts and extraction classification. Existing hash extraction,
all-catalog algorithms, changing defaults, copied/moved templates, snapshot lifetimes and fuzz
cases remain active. All 313 release suites pass on each host. Nine focused ASan suites pass on each host, and nine
MSan and nine TSan suites pass on Linux. Three explicit changed-header consumer checks pass per
host. Focused Linux coverage of changed production field lines reaches 86/88 (97.73%) and
67/70 branches (95.71%); this scoped check does not replace the full CI coverage gate.
Complete release, sanitizer and explicit-header checks are recorded in the
companion lossless archive; current-head GitHub checks remain authoritative for merge readiness.

[Lossless sources, manifests, scripts, raw samples and validation logs](hash-field-binding-tag-20261103.json.gz)
retain compiler/configuration identities, old/tag binary sizes and hashes, all successful output
oracles and the original coverage failure. The finite-catalog overflow guard remains intact;
coverage is fixed by deleting dead code, without excluding executable lines or weakening policy.

Archive size: 585,438 bytes. SHA-256:
`558057de3b7c3f6cb8f84632721ad0297deae2e8bd050c7edd2099ba2ef9ebd6`.
