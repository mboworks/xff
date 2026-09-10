# xff - Implementation Map and Change Sequence

> Companion to [design.md](design.md), which owns product decisions, and
> [test-plan.md](test-plan.md), which owns verification coverage. Current backlog
> and unresolved choices live in [TODO.md](../TODO.md); completed investigations
> move to [history.md](history.md).
>
> Status: implemented system map · 2026-09-10

This document describes the repository that exists now and the order in which a
change should cross its boundaries. It is not a feature roadmap.

## Build model

- Bazel 9 with bzlmod is the only supported build graph. Every package is private
  by default and exposes explicit cross-package targets.
- C++23 is required. `--config=clang` selects the hermetic Clang 22/libc++
  toolchain; GCC is a compatibility build, not the primary development toolchain.
- `//xff/cli:xff` is always the lean executable. `//xff/cli:xff_full` is always
  the full executable and is built with `--config=xff_full`.
- `//xff` is the configuration-sensitive convenience alias: lean normally and
  full under `--config=xff_full`.
- `--config=xff_docs` enables every composable extra. Generated `XFF.md`, HTML,
  NOTICE content, and their drift tests use that complete surface.
- `--config=clang_release` composes hermetic Clang with the production `-Oz`,
  ThinLTO, debug-information, and platform-appropriate linker settings. Artifact
  staging strips the executables and splits their symbols; it does not rebuild
  them under another compilation configuration.

## Repository boundaries

| Area                                | Responsibility                                                                                                   |
| ----------------------------------- | ---------------------------------------------------------------------------------------------------------------- |
| `xff/registry`                      | Expression descriptor source of truth: spelling, arity, binding, safety, style, cost, topic, and help prose.     |
| `xff/parser`                        | Pure command grammar: globals, roots, expression AST, validation, and style enforcement.                         |
| `xff/config`                        | System/user/explicit-file discovery, selector resolution, provenance, arming, and system policy.                 |
| `xff/vfs`                           | Thread-safe filesystem abstraction and local-filesystem implementation.                                          |
| `xff/filesystem/{ignore,repo}`      | Ignore evaluation, repository probing, and Git configuration discovery over the VFS.                             |
| `xff/matching/*`, `xff/content`     | Regex, MIME, language, similarity, fuzzy, and file-content matching.                                             |
| `xff/datetime`, `xff/values`        | Shared parsing and value semantics.                                                                              |
| `xff/engine`                        | Traversal, expression evaluation, actions, reductions, comparison, archive operations, and execution scheduling. |
| `xff/presentation/{fields,format}`  | Field vocabulary, templates, tabular values, and formatting primitives.                                          |
| `xff/presentation/{render,color}`   | Result renderers, path encoding, tree output, and terminal colour policy.                                        |
| `xff/cli`                           | Global-option registry, generated help/man/reference output, configuration wiring, diagnostics, and exit codes.  |
| `xff/examples`, `xff/conformance`   | Executable cookbook coverage and GNU/BSD find comparison.                                                        |
| `xff_extras_api`, `extra_modules/*` | Stable registration boundary and independently removable optional implementations.                               |
| `tools`, `.github/workflows`        | Generated-artifact checks, repository policy, CI, release staging, attestation, and publication.                 |

The important dependency direction is inward from CLI orchestration toward pure
registries, parsers, values, matching, and VFS interfaces. Optional modules cross
into the core only through `xff_extras_api`; the core does not depend on their
implementation packages.

## Composable extras

The lean executable keeps optional dependencies unlinked. Each extra is its own
Bazel module below `extra_modules/` and registers through `xff_extras_api`:

- archive formats through libarchive;
- standalone ASAR and SquashFS readers;
- Brotli archive compression;
- FUSE mounting;
- PCRE2 regular expressions;
- comprehensive MIME and language databases.

The command-line vocabulary remains visible in both executables. Requesting an
unlinked implementation produces an explicit error, never a silent fallback.
`--//xff:xff_all=True` is the single source of truth used by `xff_full` and the
documentation build. Adding an extra must declare its module in
`bazelmod/extras.MODULE.bazel` and extend the `xff_all` composition;
`tools/extras.py` then discovers its test wildcard from that module declaration.

## Change sequence

Move a feature through the narrowest applicable sequence; do not start at the CLI
and tunnel around lower layers.

1. Record a behavior decision in the relevant `docs/design-*.md` document or
   `TODO.md` when the spelling or semantics are not already settled.
2. Add or change the source-of-truth descriptor:
   `xff/registry/registry.cc` for expression vocabulary or
   `xff/cli/globals.cc` for whole-run options.
3. Extend parsing/value validation only when the existing declarative grammar is
   insufficient. Keep parsing independent of host I/O.
4. Add filesystem or optional-backend capability behind the VFS or extras API
   before wiring engine behavior to it.
5. Implement evaluation, traversal, action, reduction, or comparison behavior in
   `xff/engine`, preserving left-to-right expression semantics and explicit
   unsupported-operation errors.
6. Extend fields/format/render only when the feature creates a new result value or
   output contract.
7. Wire CLI configuration, diagnostics, and exit behavior. Update the generated
   help source in the same change; never patch `XFF.md` manually.
8. Add focused unit tests at each changed boundary and an integration or cookbook
   test for the user-visible path.
9. Regenerate checked-in outputs with their repository scripts and run the
   relevant pre-commit hooks before the full Bazel test graph.

For a bug fix, begin at the step that owns the defect and still cover the
user-visible regression. For documentation-only corrections, update the owning
source of truth and regenerate derived files when applicable.

## Validation ladder

Use the smallest test first for fast feedback, then widen in proportion to the
change:

1. package-level Bazel tests for the edited boundary;
2. generated-reference scripts when registry/help/license inputs change;
3. pre-commit over every changed file;
4. `bazel test //...` plus the wildcard targets printed by
   `tools/extras.py --wildcards` when validating the complete modular graph;
5. sanitizer, coverage, fuzz, minimal-core, and release-artifact jobs in CI.

Linux CI explicitly requires working FUSE integration where applicable. The
minimal job removes the extras tree and proves the lean core still builds. The
default Linux and macOS jobs test the production ThinLTO configuration and stage
and execute the same stripped artifacts that the release workflow publishes.

## Release boundary

The release workflow rebuilds both explicit executable targets with
`--config=clang_release`, stages stripped platform executables plus split debug
information, constructs the `.tar.zst` archives, generates `SHA256SUMS`, and
publishes attestations. Release preparation derives notes and documentation from
the repository sources; generated artifacts must pass their drift tests before a
tag is published.
