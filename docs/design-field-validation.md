<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Field validation

Static field mistakes are usage errors before traversal or actions. The same check applies to
`--explain` after system, user, and explicit configuration files have been composed. It checks
active listing templates, selected summary keys (including collected entries), columns, attached
grep templates, and expression arguments identified by descriptor metadata. `-exec` arguments
are templates only with `--exec-fields`; ordinary predicate arguments and `--define` values
remain literal. Superseded listing templates and cleared summary requests do not participate.

Unknown names such as `{nmae}`, empty namespaces such as `{env.}`, overflowing numeric capture
indices, and malformed placeholders fail. Valid fields with absent runtime values remain empty:
an unset `{env.NAME}`, an undefined `{def.NAME}`, or a capture absent for an entry is not a typo.
An unused command capture still has its separate validation rule.

Use `{{` and `}}` for literal braces in templates. Printf interprets only `%{field}` as a field
escape; ordinary braces and escaped percent signs stay literal. Quoted field qualifiers can
contain `}`, and both renderers use the same placeholder parser to locate the closing brace.

Native qualifiers belong to their field renderer: size and block fields accept `h`, hash fields
use the shared algorithm/encoding vocabulary, and time fields accept presets or arbitrary custom
time patterns, including literal text. Generic path-component qualifiers and `s`/`m` transforms
work across fields. Rewrite validation checks RE2 patterns, replacement backreferences, the `g`
and `i` flags, command delimiters, and `join` reducer syntax without reading any file contents.

Compilation retains diagnostics on the compiled template. Low-level rendering remains usable by
isolated parser/render tests, but command preflight must inspect the diagnostics before evaluating
any expression. Both comparison roots pass this single preflight before either walk begins.
