<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Help model design

Status: adopted 2026-08-05. Implemented in slices (see the #154 epic in
[`TODO.md`](../TODO.md)); this document is the decision record.

The HTML backend shipped after the original plain, Markdown, and roff slices. It emits a
self-contained HTML5 document directly from the model; Pages does not convert Markdown or run a
client-side renderer. Non-console renderers are formats of the full help document rather than
standalone operations: `--help=full:markdown`, `--help=full:html`, and `--help=full:roff`; `long`
aliases `full`, and `--man` remains the conventional roff shortcut.

## Problem

Help generation grew into two parallel systems. The expression `registry` and the
global-options table are a real content single source of truth, and the
`WriteReference` walk drives the roff / Markdown / plain backends structurally. But
plain `--help` and most `--help=TOPIC` bypass all of that: sixteen hand-assembled
`Render*` functions in `xff/cli/help.cc` plus `kHelpText` / `kHelpTextExpression`
in `xff/cli/main.cc` build strings directly, with hardcoded prose. The result is
duplicated rendering logic, prose that cannot wrap to width, cross-references that
are dead text, and content that drifts (the dead "see the Printf directives section
below" that #126 had to repair is exactly this failure mode).

## Model

Help is a **semantic document model** (an AST), and every output format is a
separate **renderer backend** that walks the one model. The model carries meaning,
never presentation.

### 1. Output independence

Backends: plain text, ANSI-colored text, Markdown, roff (man page), and HTML. The
same node renders natively per backend (a `Section` is a blank-line heading / a
bold ANSI line / `##` / `.SH` / `<h2>`). The model has no format-specific strings.

### 2. Width (text backends only)

Width is a text-backend concern; the model is width-agnostic and Markdown / HTML /
roff never hard-wrap (their consumer reflows: a viewer, the browser, mandoc's fill).
The plain and color backends take a width, resolved highest-precedence-first:

1. an explicit `--width=N` (or config value);
2. the detected terminal width when stdout is a TTY (`ioctl(TIOCGWINSZ)`, honoring
   `$COLUMNS`);
3. a fallback of 80 columns (piped / redirected / undetectable), so non-TTY output
   is deterministic.

Per node: `Prose` free-flows to the width; `Row` / `Entry` keep the term column and
wrap the description with a hanging indent; `Example` is verbatim and never wrapped.
Width and color live in a small render-options struct passed to the text backend
(so a test can feed `width=40` and assert the wrap).

### 3. Highlighting, cross-references, index

- **Highlighting** is typed inline runs, not markup baked into strings: a text
  field is a sequence of `Inline` runs styled `Text` / `Code` / `Emphasis` /
  `Strong` / `Ref`. Each backend maps a style to its surface (ANSI SGR, `` ` ``,
  `\fB`, `<code>`); plain drops the styling but keeps the text.
- **Cross-references** are semantic, never formatted links. A `Ref` run (and
  `SeeAlso`) carries a `RefTarget {kind, id[, section]}` where kind is `Topic`,
  `Flag`, `Primary`, `ManPage`, `Url`, or `Anchor`. Backends resolve it: HTML
  `<a href>`, Markdown `[..](#..)`, roff cross-ref, plain / color the text form
  (and color / TTY may emit OSC-8 hyperlinks).
- **Index**: every `Section` / `Subsection` / `Entry` carries a stable `anchor`
  (auto-slugged from its title / term, overridable). A `BuildIndex` walk collects
  `{anchor, title, summary, kind, depth}` into an index the backends render (plain
  `--help=list`, HTML / Markdown a table of contents).
- Because refs are semantic and the index knows every anchor, a **resolution pass
  validates every internal cross-reference at build time**: a ref to a topic / flag
  / primary that does not exist is a build error, not a shipped dead link. This
  needs the whole document as data walked twice, which is why the model beats the
  one-pass imperative `WriteReference`.

### 4. Authoring

The model is the interchange format; it is authored in C++, not an external markup.

- **Structured data** (flags, primaries, printf / time / size vocabularies) is
  built from the `registry` / `globals` SSOT into `Entry` / `Row` nodes. No markup.
- **Prose and topics** are declarative model data, diff-reviewable beside the code.
- **The only string markup is backticks** - single `` `code` `` for an inline
  `Code` run, and a triple-backtick fence for an `Example` (verbatim; an optional
  info string like ` ```sh ` sets `Example.lang`, which the structured backends use
  and the text backends ignore). We deliberately do NOT parse Markdown's `#`
  headings, `-` / `*` bullets, or `*emphasis*`: those sigils are exactly xff's flag,
  glob, and regex characters, so they would collide with the content. Headings,
  bullets, entries, rows, refs, and anchors are all typed nodes; a literal backtick
  is escaped (doubled) in the rare case it appears.

XHTML / Markdown as an authored input was rejected: the content is programmer
authored, terse, and lives with the code, so an XML layer adds a parser, verbose
authoring, and a round-trip for no gain. They remain output backends only. Revisit
only if help ever becomes externally authored by non-coders.

## Focused navigation and full references

Focused `--help=NAME` pages end with **See also** commands. Entry navigation combines the
registry's topic membership, explicit `see_also` topics, and forward/reverse `affects`
relationships. Duplicate targets are removed while preserving the authored order. A topic's
related pages are declared in `HelpTopics()`; aliases share the canonical topic's navigation.
The commands distinguish a topic (`--help=regex`), a global flag (`--help=--regextype`), and an
expression primary (`--help=-regex`). External manuals retain their `name(section)` notation.

Focused option and primary help expands at most one related topic: an explicit
`primary_expansion_topic`, or the sole distinct topic when none is designated. An explicit
selection must be an element of `see_also`; both constexpr registries enforce this with a
static assertion. A single-topic relationship needs no repeated selection. Topic membership
and `see_also` are deduplicated before choosing the automatic expansion. Related flags and
primaries are pointers, never expansion requests. For example, `--regextype` designates
`grammars` while retaining `regex,grammars` as its related topics. Its short value list precedes
the full grammar vocabulary. Topic help lists its related pages without expanding them.

`--help=full` and `--help=long` render the same complete document. Each detailed entry and each
topic appears once. The model retains related pointers only to targets present in the document;
no reference renderer can expand a target. Console long help omits these per-entry and per-topic
pointers. HTML and Markdown show clickable topic titles and flag names. Man pages show those
names as text references. Focused console help keeps copyable `--help=NAME` commands.

Stable topic and entry anchors (`topic-`, `flag-`, and `primary-` namespaces) keep internal
Markdown/HTML references independent of display headings, including automatically generated
Markdown heading anchors. The reference index resolves display labels from actual sections and
entries and omits relationships whose targets are absent from that complete document.

## Selecting the help renderer

`--help=TARGET` selects content; `--help-format=plain|markdown|html|roff` selects its
renderer. The format applies to usage, indexes, topics, individual flags and primaries, and
full references. `md` aliases `markdown`. `--help=TARGET:FORMAT` remains equivalent shorthand.
Matching explicit selections are accepted; conflicting selections are usage errors. `--man`
selects the full reference in roff, so an incompatible format is also an error. A format without
help output is rejected, including with `--version`. These are command-line controls.

Plain help respects width, color, and paging. Markup renderers preserve their document structure
and ignore terminal width and color. Markdown and HTML suppress automatic paging; an explicit
pager command or `--pager=always` still applies. Roff retains the terminal formatting and paging
behavior of `--man`. Legacy style/extra tables use verbatim blocks in formatted documents.

`NOTICE.md` is generated by the all-extras binary using
`--help=notice --help-format=markdown`; the same registered components supply terminal notices.
