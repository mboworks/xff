<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# O24: CLI stream synchronization audit, 2026-11-05

**Decision:** add no runtime `sync_with_stdio(false)` call for the current hermetic
Clang 23.1.4 libc++ release configuration. The exact implementation only reads and
writes its previous-state byte. It neither switches stream buffers nor changes
the I/O strategy, so this call cannot provide the hypothesized listing speedup.
This conclusion is specific to these two toolchain libraries, not libstdc++ or a
future libc++ release.

The ordinary CLI already writes complete listing records with `cout.write`,
flushes at run completion, and flushes before replacing a pager descriptor. Its
entry adapter does not add another C++ stream buffering layer. No runtime code,
new flag, environment setting or benchmark timing was added for this audit.

The audit extracts `ios.cpp.o` from each actual toolchain's `libc++.a`, records
both hashes and the exact object/symbol command, and disassembles
`std::ios_base::sync_with_stdio(bool)`:

| Host         | Instructions                 | Effect                                                          |
| :----------- | :--------------------------- | :-------------------------------------------------------------- |
| arm64 Mac    | `adrp; ldrb; strb; mov; ret` | Read the stored byte, write the argument, return the prior byte |
| x86_64 Linux | `movzbl; movb; retq`         | Read the stored byte, write the argument, return the prior byte |

The initial Linux symbol filter also selected a similarly named static variable;
its output is retained as a diagnostic. The corrected command selects the exact
function. The evidence includes library and object hashes, complete disassembly,
commands and the decision. There is no measured speedup claim or implied test
qualification for a runtime change that was not made.

Revisit only if the standard-library implementation or output ownership changes.
A generic stream-disable call would otherwise add code without addressing a
current bottleneck.

## Retained evidence

[The machine-readable audit](cli-stream-sync-audit-20261105.json) includes both
exact library/object hashes, selected symbols, complete disassembly, extraction
commands, the reviewed decision and initial Linux diagnostic failure. The
original per-host records retain their pre-review fields; the combined record
explicitly marks interpretation as reviewed. No timing samples or speedup claim
are manufactured for this structural rejection.
