# Agent rules - xff

Conventions for AI agents and contributors working on this repository.
Companion to [`docs/design.md`](docs/design.md) (decisions) and
[`docs/implementation-plan.md`](docs/implementation-plan.md) (build & sequencing).
The full **C++ coding style** is [`STYLE_CPP.md`](STYLE_CPP.md); project-level rules are
[`RULES.md`](RULES.md); the contribution flow is [`CONTRIBUTING.md`](CONTRIBUTING.md). The
GoogleTest section below is the quick reference; `STYLE_CPP.md` is canonical.

Build & test: `bazel test //...` · sanitizers: `bazel test //... --config=clang --config=asan`
(also `--config=tsan`, and `--config=msan` on Linux).
Toolchain: clang-22 minimum (hermetic LLVM under `--config=clang`).

## C++ API design

Xff-owned C++ interfaces use no raw object pointers. Pass required observers by reference,
ownership by value or smart pointer, and optional observers as `mbo::types::OptionalRef`; lookups
return an optional reference rather than a nullable pointer. Prefer pure, value-returning helpers:
return a value, `absl::StatusOr`, or a named result record instead of mutating caller-owned output
parameters. Mutation remains valid when it is the explicit purpose of a stateful object or action.
Exact raw-pointer spellings required by C, POSIX, or third-party ABIs stay inside narrow adapters
and must carry `XFF_ABI_POINTER: <reason>` on that declaration or the immediately preceding line;
do not broaden that exception to xff-owned interfaces or internal convenience pointers.
`STYLE_CPP.md` is canonical.

Outside required OS or third-party interfaces and explicitly designated adapters, file I/O APIs are
forbidden: this includes STL streams, `std::filesystem` file operations, C/POSIX stdio and file
descriptor calls, and path-based `mbo::file`/Abseil helpers. Route reads and writes through the xff
VFS or a value-returning `absl::Status`/`absl::StatusOr<T>` adapter. Prefer pure functions over
side-effecting output parameters. Narrow host-I/O adapters must carry `XFF_HOST_IO: <reason>` on
the declaration or immediately preceding line; the pre-commit policy enforces this boundary.

## Bazel package policy

Every `BUILD` / `BUILD.bazel` file declares
`package(default_visibility = ["//visibility:private"])`. Cross-package APIs opt in with a narrow
target-level `visibility`; never make a whole package public for convenience. Target and exec C++
configurations both keep Bazel's `layering_check` and `parse_headers` features enabled. The
`check-bazel-policy` pre-commit hook enforces all three invariants, including for new packages.

## Pull request descriptions

Every PR description has two layers in this order:

1. a short, human-readable explanation of the outcome and why it matters;
2. an `## AG;DR` heading, followed by the full implementation details, reasoning, validation,
   portability notes, and known limitations needed by reviewers or a future agent.

Update the PR description whenever a commit is added to the PR, so the detailed section describes
the complete current change rather than the state at PR creation. The short human-readable section
should remain stable and should rarely need updating: most additional commits refine implementation
or validation and therefore belong only below `## AG;DR`. Change the human section only when the
PR's user-visible outcome or motivation actually changes.

## Writing tests (GoogleTest)

1. **Always `TEST_F` with a fixture; never bare `TEST(...)`** - even when the
   harness is a one-line `struct FooTest : ::testing::Test {};`.
2. **The fixture is a `struct`** (not a `class`) inheriting from
   `::testing::Test` (or a friend such as `::testing::TestWithParam<T>`).
3. **Use `EXPECT_THAT` / `ASSERT_THAT`; convenience assertion macros are forbidden.** Never use either
   `EXPECT_` or `ASSERT_` with `EQ`, `NE`, `LT`, `LE`, `GT`, `GE`, `STREQ`, `STRNE`, `STRCASEEQ`,
   `STRCASENE`, `FLOAT_EQ`, `DOUBLE_EQ`, or `NEAR`, and do not use `EXPECT_TRUE`, `EXPECT_FALSE`,
   `ASSERT_TRUE`, or `ASSERT_FALSE`. Use `EXPECT_THAT` / `ASSERT_THAT` with `IsTrue` / `IsFalse`
   instead. This is enforced by pre-commit with no exceptions.
4. **Multi-line text: use `mbo::testing::EqualsText`.** For a
   multi-line string use `EXPECT_THAT(actual, EqualsText(golden))` (unified diff,
   line by line). Write the golden as a `DropIndent`-filtered indented raw string,
   not concatenated `"...\n"` literals (which `clang-format` shoves against the
   `EqualsText(` paren): `WithDropIndent(EqualsText(R"out(` ... `)out"))`.
   `WithDropIndent` de-indents the expected text only; `DropIndentAndSplit` yields
   the lines as a vector. Caveat: a raw-string golden cannot carry significant
   trailing whitespace (the trim-trailing-whitespace hook strips it) - use the
   literal form there. `STYLE_CPP.md` is canonical; use `EqualsText` for text comparisons.
5. **Typed and parameterized tests supply names from the types/values** (name
   generators for `TYPED_TEST_SUITE` / `INSTANTIATE_TEST_SUITE_P`), so the
   output never shows numbered tests (`Suite/0`, `Suite/1`).
6. **Test `absl::Status` / `absl::StatusOr<T>` with status matchers - never raw
   `.ok()`.** Raw `EXPECT_TRUE(s.ok())` / `EXPECT_FALSE(s.ok())` throws away the
   code and message on failure. Use `mbo::testing`
   (`@mboworks_mbo//mbo/testing:status_cc`):
   - `EXPECT_THAT(s, IsOk())`
   - `EXPECT_THAT(s, StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("…")))`
   - `EXPECT_THAT(so, IsOkAndHolds(Eq(42)))`
   - `ASSERT_OK_AND_ASSIGN(const auto value, MakeThing());` to unwrap a `StatusOr`.

   mbo's set is the MBO Works canonical superset: it works on both `Status` and
   `StatusOr`, and adds payload matchers (`StatusHasPayload`) plus the
   `EXPECT_OK` / `ASSERT_OK` / `ASSERT_OK_AND_ASSIGN` macros over abseil's
   `absl_testing`.

7. **Match optional-like values directly; never assert `.has_value()`.** Use
   `Optional(matcher)` for a present `std::optional` or `mbo::types::OptionalRef`, and
   `Eq(std::nullopt)` for absence. `StatusOr` uses `IsOkAndHolds(matcher)` as described above.

## Markdown

Keep GitHub-flavored Markdown tables **vertically aligned** (the `|` pipes line
up, every column padded to its widest cell, honoring the `:--` / `--:` / `:-:`
alignment markers). Do not hand-align them: run the formatter
[`tools/align_markdown_tables.py`](tools/align_markdown_tables.py) (`FILE...`, or
`--check` to only report). It is enforced by the `align-markdown-tables`
pre-commit hook, so a misaligned table fails CI; let the tool do the spacing.

## Self-documenting features (the registry is the single source of truth)

The expression vocabulary (`xff/registry`) and the global options
(`xff/cli/globals.cc`) are the single sources of truth from which `--help`,
`--help=TOPIC`, the `--help=list` index, the `--man` roff page, and the
`--help=full:markdown` reference are all generated. So **every feature add or change carries
its self-documentation in the same change** - this is part of "done", never a
follow-up:

- a new / changed **primary** -> its `registry::Descriptor.summary` (a non-empty
  one-line synopsis; `registry_test` enforces presence + shape);
- a new / changed **global flag** -> its `cli::GlobalFlag` entry in `globals.cc`
  (`globals_test` enforces it; set `alias` / `display` for short or alternate forms);
- a new / changed **cookbook recipe** (`cli/help.cc` `RenderCookbook`) -> a matching
  execution case in `//xff/examples:cookbook_test`; its guard case fails CI if a recipe ships
  without one, so the user-facing examples are run, not just rendered;
- a new **environment variable** the code reads -> a row in `cli/help_build.cc`'s `kVars` table
  (`--help=environment`), enforced by the `check-env-documented` pre-commit hook, which also fails on
  a documented name nothing reads;
- the hand-maintained `kHelpText` usage page in `cli/main.cc`;
- any prose docs the change affects (`docs/design-*.md`, `TODO.md`).

The generated `--help` / `--man` / `--help=full:markdown` then stay complete by construction.

When implementing a new feature, consider whether it belongs in the README's
**Core Highlights & Architectural Advantages** section and **Tool Feature Comparison Matrix**.
Those sections are curated rather than exhaustive: add only major or genuinely differentiating
features.

### Markup in help prose

Help text is authored in a small inline markup (`ParseInline`), and the backends render it per
target: the plain backend keeps the backticks, roff bolds the span, Markdown makes it a code span.
So **write concrete literals as `` `code` ``** in `details`, in `ValueDoc.meaning`, and in topic
bodies: option values (`` `auto` ``, `` `all` ``), flag names (`` `--no-pager` ``), primaries
(`` `-exec` ``), and literal tokens (`` `*.{cc,h}` ``). A reader should be able to tell a value from
a word without parsing the sentence.

Two limits, both about noise:

- **`summary` stays plain.** Summaries appear in dense list views (`--help`, `--help=list`), where
  literal backticks on every row cost more than they explain. The exception is a literal that is
  unreadable bare, such as punctuation (`` `!` ``).
- **Never mark a word used in its English sense**, even when a value shares its spelling: "all
  archive features", "treats the duplicate as an error", "a git working tree", and "`ls`'s own
  colours" (the tool, not the `ls` value) all stay plain. This is why the sweep is done by hand -
  mechanical backticking gets exactly these wrong.

## Fuzz-test isolation

- A fuzz harness that parses or evaluates arbitrary expressions must never execute a descriptor
  whose `registry::Descriptor::safety` is not `Safety::kNone`. Do not rely on a generated `--safe`
  argument alone: arbitrary input can contain a later override or place it inside another primary's
  argument run.
- Evaluate only against an in-memory or otherwise isolated filesystem whose mutation operations
  fail the test. Never point fuzz-generated paths at the host filesystem.
- Leave process-execution, file-output, archive-write, mount, extraction, and confirmation sinks
  disconnected unless that capability is the explicit subject of a separately isolated harness.

## CLI conventions

- **Flag scope by dash count.** `--flag` is a whole-run global (a config / output /
  traversal modifier); a single-dash `-flag` is an expression primary (a per-entry
  test or action). Grep / GNU single-dash _globals_ (`-h`, `-help`, `-version`,
  `-q`) are deliberate special-cased compatibility aliases of their `--` form, not
  new primaries.
- **`--` globals are position-independent.** Because every primary/operator is
  single-dash, a `--flag` is unambiguous anywhere, so the parser hoists it out of any
  primary/operator position - before the roots, among them, or in the expression
  (including the tail: `xff . -type f --summary=ext`). It is NOT hoisted out of a
  primary's argument run (an `-exec` command's args, a `-printf` format), where a
  `--flag` is a literal argument to that primary / the child command; a bare `--`
  ends option parsing and disables hoisting. Single-dash globals stay leading-only
  (they are ambiguous with primaries). The parser handles this in ExprParser's
  `SkipGlobals()` + the roots loop.
- **Flag-only; no subcommands.** xff is a single-purpose tool (like `fd` /
  `ripgrep`), so meta operations are flags (`--help`, `--man`, `--help=full:markdown`,
  `--explain`), never `git`-style subcommands; find and xff share one grammar,
  differing only in vocabulary. (Decided 2026-06-28.)
- **A boolean capability belongs to a FAMILY or to an existing flag's value set - not to a
  `--feature` namespace.** The `--feature=NAME` mechanism (#73) stays unbuilt: 22 boolean globals
  later, every one of them read better either as a named member of its family (the `--archive-*`
  set, which `--help=archive` then gathers) or as a value on a flag that already exists
  (`--case=smart`, not `--smart-case`). So when adding a boolean: name it into its family if it has
  one, fold it into a valued flag if that reads naturally, and only then consider a standalone
  boolean. The verdict is recorded in [`TODO.md`](TODO.md) under #73.
- **A feature whose SPELLING is not settled ships behind `--unstable=NAME`, not under a provisional
  flag name.** That is the one `--feature`-shaped mechanism xff adopts (design in
  [`TODO.md`](TODO.md) under #73): one repeatable, comma-separated list; an unknown name is a usage
  error; the gated flag's own help says it is unstable; graduation means DELETING the gate rather
  than keeping an alias. It gates spelling only - a destructive capability keeps its own explicit
  flag and is never armed by list.
