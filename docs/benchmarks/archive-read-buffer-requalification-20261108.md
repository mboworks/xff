# Legacy archive buffer requalification after #1143

Fresh three-session measurements on current main retain the memory-TAR benefit of #1140.
This remains a scoped legacy API optimization with slower cases. It is not a general CLI
archive-search speedup, and the new results do not replace or relabel the original observations.

## Source and measurement identity

- Baseline: main `3e546dbef0993486217eb58f3f9276dd0003651c`, containing merged #1143 and #1139.
- Candidate: synchronized #1140 `1d2a6901c46719059e1c6eb584a0025bb344fab2`.
- The production difference is still only removal of value initialization from the 64 KiB
  `ReadPositionedEntry` array. The runtime, test, BUILD, original report and original capsule
  match the previously published PR bytes exactly.
- Both hosts use the original diagnostic source and driver, byte-identical native fixtures,
  `--config=clang_release`, and a verified `-flto=thin` link action. Each configured output is
  copied immediately after its build; later builds cannot change a measured image.
- Three independently ordered sessions retain nine observations and average the fastest seven
  per case/participant. Full content comparison and result destruction remain inside timing.
  Linux pins the complete benchmark process to one allowed CPU; macOS is unpinned.
- Each host retains 2,511 timing observations and 81 expected empty-gzip errors. The unchanged
  streaming path and an exact baseline-image copy remain controls. All observations, per-case
  means and per-session ratios were independently recomputed from raw JSON.
- Five focused archive test targets executed and passed against both variants on each host:
  buffer boundaries, archive reader, source buffer, filesystem and format fixtures.
  These checks do not substitute for this PR's final exact-head CI.
- Linux transport verified all 904 files (12,725,409 bytes), including binary hashes.
  Measured source files, harness, driver, rule and fixture identity match across hosts.

## Results and limits

Positive percentages are slower. Each cell is the median of three session ratios; the mode
summary is then the median across its cases. Controls are reported alongside results, never
subtracted to manufacture an adjusted win. These descriptive medians are not significance tests.

| Mode                     | macOS candidate | macOS copy | Linux candidate | Linux copy |
| :----------------------- | --------------: | ---------: | --------------: | ---------: |
| Memory TAR               |          -8.47% |     +0.20% |          -3.78% |     +0.18% |
| Native file-backed TAR   |          -0.31% |     +0.04% |          -1.17% |     +0.50% |
| Native single-file gzip  |          -2.88% |     -0.71% |          -0.03% |     -0.08% |
| Unchanged streaming path |          +0.12% |     +0.46% |          -0.07% |     +0.13% |

Memory-TAR improvements persist on both hosts and exceed their copy controls. All eight
memory-TAR case medians improve. Native TAR is narrower: the macOS mode median of -0.31%
does not reproduce the original -1.98% aggregate, while Linux is -1.17%. Linux gzip remains
effectively unchanged as a mode aggregate. No ordinary-command gain is established.

Keep the counterexamples visible: macOS gzip at 65,537 bytes is +4.99% (copy -0.83%), with
sessions -5.30%, +13.01%, +4.99%. At 65,535 bytes it is +2.50% (copy +7.85%). Linux gzip
at 65,535 bytes is +2.00% (copy +0.68%), at 65,537 bytes +0.33% (copy -0.09%), and at
1 MiB +0.91% (copy -0.39%). The original exact-64-KiB macOS slowdown does not recur here;
its -10.64% median has a -3.20% copy control and substantial session variability.

Unchanged-path controls remain variable: macOS 1 MiB streaming is -12.03% (copy -0.71%),
while Linux 1 KiB streaming is +8.15% (copy +8.34%). Linux 64-byte native TAR is +5.62%
(copy +6.61%). Do not treat every difference as a causal buffer effect or silently discard
noisy and slower cells. No cache, allocator or instruction-placement cause is established.

**Decision:** retain the single initialization removal for its reproduced memory-TAR gains,
with the gzip costs and narrower native-TAR result documented. Require green current-head CI
before merge. No size heuristic, buffer-size change or unrelated initialization removal is added.

## Every measured case

| Case / bytes           | macOS candidate | macOS copy | Linux candidate | Linux copy |
| :--------------------- | --------------: | ---------: | --------------: | ---------: |
| gzip-read/1            |          -4.75% |     -7.70% |          -6.18% |     -0.09% |
| gzip-read/64           |          -2.04% |     +1.55% |          -5.75% |     +0.63% |
| gzip-read/1024         |          -3.49% |     -0.50% |          -5.61% |     +0.18% |
| gzip-read/65535        |          +2.50% |     +7.85% |          +2.00% |     +0.68% |
| gzip-read/65536        |         -10.64% |     -3.20% |          -0.03% |     -0.08% |
| gzip-read/65537        |          +4.99% |     -0.83% |          +0.33% |     -0.09% |
| gzip-read/1048576      |          -2.88% |     -0.71% |          +0.91% |     -0.39% |
| legacy-read/0          |         -15.19% |     +0.15% |          -4.86% |     +0.83% |
| legacy-read/1          |         -14.02% |     +0.24% |          -5.53% |     +0.17% |
| legacy-read/64         |         -12.56% |     +0.33% |          -4.98% |     +0.49% |
| legacy-read/1024       |         -11.01% |     -1.07% |          -4.92% |     +0.05% |
| legacy-read/65535      |          -5.21% |     +0.16% |          -2.70% |     -1.13% |
| legacy-read/65536      |          -5.94% |     -0.32% |          -2.29% |     -0.52% |
| legacy-read/65537      |          -5.46% |     +0.72% |          -2.27% |     +0.18% |
| legacy-read/1048576    |          -0.16% |     +1.89% |          -0.69% |     +1.20% |
| native-read/0          |          -2.55% |     +0.16% |          -2.64% |     +0.97% |
| native-read/1          |          -3.31% |     -0.71% |          -2.64% |     +0.80% |
| native-read/64         |          -0.13% |     +1.01% |          +5.62% |     +6.61% |
| native-read/1024       |          -0.49% |     +0.29% |          -3.14% |     +0.36% |
| native-read/65535      |          -0.57% |     -0.12% |          -1.38% |     -0.12% |
| native-read/65536      |          +0.43% |     +1.02% |          -0.65% |     +0.63% |
| native-read/65537      |          -0.07% |     -1.13% |          -0.91% |     -0.09% |
| native-read/1048576    |          +0.73% |     -0.07% |          -0.96% |     +0.21% |
| stream-control/0       |          +1.23% |     +0.70% |          -4.01% |     -3.51% |
| stream-control/1       |          +1.34% |     +0.30% |          +0.29% |     +0.58% |
| stream-control/64      |          -2.42% |     -1.13% |          +0.87% |     +7.23% |
| stream-control/1024    |          +3.09% |     +0.61% |          +8.15% |     +8.34% |
| stream-control/65535   |          -3.81% |     -2.01% |          +0.28% |     +0.11% |
| stream-control/65536   |          +0.35% |     +3.29% |          -1.29% |     -0.38% |
| stream-control/65537   |          -0.11% |     +1.21% |          -1.89% |     -1.46% |
| stream-control/1048576 |         -12.03% |     -0.71% |          -0.41% |     +0.15% |

Empty gzip remains an expected pre-buffer API error, with no timing ratio. Memory and native
TAR fixtures include empty content. Native file-backed cases use warm ordinary filesystem
reads; they are not relabeled as memory-filesystem measurements.

## Setup, provenance and reproduction

The first macOS preparation stopped before timing because a guard searched raw Bazel JSON
for an equals sign that Bazel encoded as a Unicode escape. Decoding the JSON and checking
the actual link arguments corrected that guard; the successful build image/logs remain retained.
Linux preparation stopped twice before timing because its `/tmp` tmpfs user quota prevented
compiler temporary writes. Setting both client and action `TMPDIR` to campaign storage on
the main filesystem fixed preparation. No old experiments or user data were deleted.
All successful timing observations belong to the final complete runs; none were overwritten
or repeated for a favorable outcome. Neither host ran this campaign's builds during timing.

[The new lossless capsule](archive-read-buffer-requalification-20261108.json.gz) contains 1,842 individually hashed records
and 1,008,577 compressed bytes. SHA-256: `96fc538e897cfd6598d18a9f0a6adfeab1d272b28c330b2f4e4af2072e261e84`.
It retains every raw observation, complete summaries, both driver revisions, source/configuration
snapshots, build/link/test receipts, fixture bytes, transport inventory and preparation failures.
Executable copies remain private; their sizes and SHA-256 hashes are retained in the capsule.
The original 2026-11-07 report and capsule remain unchanged.

Rebuild the synchronized source with the retained private benchmark rule/source, substituting
only the recorded baseline or candidate runtime file. Use `requalify_archive.py` for build,
copy, ThinLTO verification, focused tests and three sessions; its Linux variant additionally
routes build temporaries. `analyze_archive.py` independently recomputes all raw means and
ratios. Neither private diagnostic source nor rule is added to production.
