<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# O19f2: archive descendant skipping, 2026-11-05

**Decision:** retain the implementation and evidence without adopting this loop.
It greatly improves a deep directory's direct-child listing but repeatedly makes
wide listings slower. A good asymptotic result does not erase that counterexample.

The candidate changes only the immutable ordered-map listing loop. After emitting
a direct child, it jumps over that child's descendants using a lower bound. It
preserves explicitly stored absolute paths, punctuation, repeated separators and
entry ordering. It introduces no second index, persistent count, lock or worker
policy. Three additional behavioral regressions pass against the original parent
before the production change. An independent path model checks 160 sets and
25,054 directory keys.

There are two separate parent populations; they must not be pooled:

- **Compiled target-demand parent:** the qualified C overlay on #1110. Linux has
  complete 82-command and 88-isolated-case trials, three sessions with nine
  repetitions retaining the fastest seven. Mac has all 164 command preflights
  but only an intentionally stopped 11/82-command screen. The Mac screen was
  stopped between invocations, with the timing guard clear, to investigate the
  independently identified formatter bottleneck. It is not a complete Mac trial.
- **Formatter parent:** C plus O26's empty-fragment guard, with the original
  archive implementation. Both hosts complete all 82 command and 88 isolated
  screens, using three repetitions retaining two and a copied parent. The exact
  same descendant-skipping implementation is used. Neither host has a full
  three-session trial for this new parent.

Each population keeps its commands, complete output checks, frozen source and
image hashes, and copied-parent controls. The ordinary release images have no
sanitizer or profiling instrumentation. Linux timing uses CPU affinity; Mac
requests one/three workers without claiming CPU pinning.

Selected counterexamples below are changes in elapsed time, so positive is slower.
The evidence retains every cell, not just these examples.

| Parent              | Host  | Isolated case              | Parent us | Candidate us |   Change |
| :------------------ | :---- | :------------------------- | --------: | -----------: | -------: |
| Target demand, full | Linux | Wide/root 100,000 listing  |     2,038 |        2,683 | +30.874% |
| Formatter, screen   | Linux | Wide/root 100,000 listing  |     1,972 |        2,607 | +32.233% |
| Formatter, screen   | Mac   | Wide/root 100,000 listing  |     1,865 |        2,419 | +29.724% |
| Formatter, screen   | Mac   | Wide/root 100,000 metadata |     2,213 |        3,181 | +43.744% |

Deep/root 100,000 isolated listings improve by about 99.99%, because the loop no
longer visits nearly every descendant. Selected large whole-command listings and
summaries improve around 8-12%. This does not establish a general archive-walk
improvement: the wide cases are common enough to matter.

Both parents have focused correctness qualification on both hosts: six release
suites, three archive memory-sanitizer suites (ASan on Mac, MSan on Linux), and
actual lint of the two changed translation units. These are not claims that the
current complete 332-target release suite or full Mac performance trials ran for
this rejected candidate. The formatter parent's new checks and measurements are
not substitutes for the deliberately partial earlier Mac population.

The follow-up should examine independent member-key allocation and index layout
costs. Repeating unchanged full populations until a preferred result appears is
not a useful next step. A combined loop/layout candidate needs a new hypothesis,
complete correctness tests and independent measurements.

## Retained evidence

[Lossless cross-platform evidence](archive-lookup-trials-20261105.json.gz) retains
each source/prototype, command, complete output check, result and original
qualification diagnostic as a named record with its byte length and SHA-256.
The six host/parent populations keep distinct prefixes and metadata; their
measurements are never pooled. Every decoded record is verified before packing.
Ordinary release images remain on their original hosts with retained identities;
they are not embedded as executable downloads in this text-evidence capsule.
