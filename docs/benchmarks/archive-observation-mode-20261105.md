<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# O28: compile-time archive observation mode, 2026-11-05

**Decision:** retain the prototype without selecting it as a standalone runtime
change. Actual generated code contains a removable dynamic mode branch, but
removing that branch did not establish a consistent cross-platform benefit.

The independent parent is the qualified compiled target-demand change plus the
empty CCTZ fragment guard. It uses the original ordered-map archive loop; it does
not include descendant skipping, member-key views, insertion changes, B-trees,
capacity hints or PGO. The only production change makes the private
`ReadDirectory` helper a boolean template and replaces its two mode checks with
`if constexpr`. The public API, node layout, allocation policy and traversal
semantics stay the same.

Disassembly of each actual ordinary ThinLTO parent proves the runtime flag
survives optimization. Mac passes 0/1 in `w4`, retains it in `w21`, and branches
before reservation and entry emission. Linux retains the argument on the stack
and compares it before those operations. An initial disassembler command combined
symbol demangling with a mangled symbol selector and returned a warning without
a function body. That diagnostic is retained; only corrected commands selecting
the raw symbol and requiring an actual disassembly are accepted as evidence.

Both hosts pass six focused release suites, three archive memory-sanitizer suites
(ASan Mac/MSan Linux), and actual lint of all nine affected header consumers.
Existing committed metadata tests compare every metadata field and ordered entry,
normalization, nested/custom/URI paths, errors and ownership. Both complete the
82-command and 88-isolated-case screens, including full output preflights and a
byte-identical copied parent. These are screens with three repetitions retaining
two, not full three-session trials or a full 332-target release qualification.

Negative changes mean lower elapsed time. Copied-parent drift remains separate;
it is not subtracted to manufacture a candidate result.

| Host  | Isolated case              | Candidate | Copied parent |
| :---- | :------------------------- | --------: | ------------: |
| Mac   | Wide/root 100,000 listing  |   -6.460% |       -0.749% |
| Linux | Wide/root 100,000 listing  |   +5.704% |       +4.262% |
| Linux | Wide/root 100,000 metadata |   -4.367% |       +1.935% |

Whole-command family medians are mostly flat, while several apparent isolated
gains or regressions track substantial copied-parent drift. The full per-case
results, including slower cells, remain in the evidence.

| Release CLI | Parent bytes | Candidate bytes | Change bytes |
| :---------- | -----------: | --------------: | -----------: |
| Linux       |   18,437,472 |      18,450,096 |      +12,624 |
| Mac         |    8,274,840 |       8,274,808 |          -32 |

The next independent candidate removes avoidable normalized member-key ownership
while retaining ownership at delayed-read boundaries. Any later combination with
descendant skipping must be qualified itself; these percentages cannot be added.

## Retained evidence

[Lossless cross-platform evidence](archive-lookup-trials-20261105.json.gz) retains
each source/prototype, command, complete output check, result and original
qualification diagnostic as a named record with its byte length and SHA-256.
The six host/parent populations keep distinct prefixes and metadata; their
measurements are never pooled. Every decoded record is verified before packing.
Ordinary release images remain on their original hosts with retained identities;
they are not embedded as executable downloads in this text-evidence capsule.
