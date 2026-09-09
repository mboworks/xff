# xff - Implementation Plan

> Companion to [design.md](design.md). **design.md** is the source of truth for _what / why_ (decisions); this plan owns _how / in what order_ (build, modules, sequencing). Decisions are referenced here, not re-argued.
> Status: **Draft** · 2026-06-07 · Org: MBO Works · License: **Apache-2.0**
> Design review #1–#13 complete and incorporated (see design.md §Notes/Decisions); the module seams and phasing below reflect every resolved decision.

## Toolchain

- **Build:** Bazel with bzlmod (`MODULE.bazel`), pinned `.bazelversion`, shared `.bazelrc`.
- **Language:** C++23. For consistent C++23 + stdlib across macOS and Linux, prefer a **hermetic LLVM toolchain** (`toolchains_llvm`) over system compilers - reproducible, matches the fetch/pin ethos. (Fallback: system clang/gcc; decided in Phase 0.)
- **Tests:** GoogleTest + gmock (bzlmod `googletest`); `bazel test //...`.
- **Dependencies - all via bzlmod (Bazel Central Registry); availability verified 2026-06-07:**
  - `abseil-cpp` - utilities only (`absl::Status`/`StatusOr`, strings, containers, synchronization). **Not** `absl::flags` - CLI parsing is our own `parser` library.
  - `re2` - default regex engine.
  - `pcre2` - Phase 2 (lookaround/backrefs). On BCR - no `foreign_cc` needed.
  - `libarchive` (+ `zlib` / `zstd` / `bzip2` / `xz`, all on BCR) - Phase 3 archive backend. Acquisition is trivial via bzlmod, but it remains a sizable dependency + transitive compression libs - revisit at Phase 3 whether to take it whole or support a narrower archive set.
  - `rules_foreign_cc` - declared **fallback only** (also on BCR); not required by any current dependency.
- **CI:** GitHub Actions, matrix macOS + Linux, `bazel test //...` + buildifier lint.

## Repository layout (Bazel packages)

```
xff/
  MODULE.bazel  .bazelrc  .bazelversion  .github/workflows/
  docs/        design.md  implementation-plan.md  test-plan.md
  xff/
    registry/  # SoT: option/predicate/action descriptors + resolved Query/settings model
    parser/    # argv -> AST (left scanner + recursive-descent); parse-only
    config/    # config files + cascade -> settings (uses registry)
    vfs/       # source abstraction + local backend (archive/remote later)
    regex/     # engine abstraction (RE2/PCRE2 + literal prefilter)
    engine/    # AST + settings + VFS -> match stream + actions; parallel; cost-warning
    stats/     # aggregation over the match stream
    render/    # result/stat model -> output formats
    cli/       # the `xff` binary (thin wiring; owns exit codes)
```

## Module contracts (the seams)

- **`registry`** - the single source of truth. Descriptors carry name(s), region (global/expression), arity, dialect (GNU/BSD), cost-tier, purity, toggle-style. `--help`, completions, `--explain`, and the cost-warning are all _derived_ from it. **DAG root** - everything else depends on it.
- **`parser`** - `argv → AST` (globals, roots[], expression tree) or a diagnostic with source spans. No traversal / IO ⇒ unit-testable as a pure `string[] → AST`. (design.md §CLI grammar & parser.)
- **`config`** - config files → settings, merged by the cascade with provenance (`unset` ≠ explicit); CLI > config > defaults. Enforces the **trust model** (data-only tree configs; `--config`-armed exec blocks; ownership gate) - design.md §Security & safety.
- **`vfs`** - `Entry`/`Metadata` interface + directory iteration; `LocalFs` backend first. Archive/remote backends slot behind the same interface, tagging entries by source (real-fs/archive-member/remote) + read-only flag, with untrusted-input guards (decompression-bomb / Zip-Slip) - design.md §Virtual entries. Exposes platform metadata caps (btime, normalization/case).
- **`regex`** - `Matcher` abstraction; RE2 default (linear-time), PCRE2 (opt-in, **configurable limits**) for lookaround/backrefs; literal/Aho-Corasick prefilter; translates find's `-regex`/`-regextype` grammars onto RE2 - design.md §Regex engines.
- **`engine`** - evaluates the AST over the VFS stream: strict left→right + short-circuit (design.md §Evaluation); parallel directory read-ahead with coordinator-owned evaluation; emits the result model; runs the cost-warning from registry cost-tiers.
- **`render`** - result/stat model → formats; streaming (plain/JSONL/NUL/CSV) vs buffered/aligned (columns/markdown/tree/stats); display-width-correct alignment.
- **`cli`** - wires `registry → parser + config → engine → stats → render`; owns exit codes.

Dependency direction: `registry` ← {`parser`, `config`, `engine`}; `engine` ← {`vfs`, `regex`}; `render` ← result-model; `cli` ← all. No cycles.

## Phases

> Every phase carries the cross-cutting prime goals (design.md §Security & safety): security against untrusted input, safety with self-documenting refusals/warnings, find-fidelity on the drop-in surface.

### Phase 0 - Foundations

- Bazel/bzlmod skeleton; hermetic-toolchain decision; `.bazelrc`; CI matrix (macOS+Linux); **Apache-2.0 LICENSE + NOTICE/THIRD_PARTY**; buildifier.
- `registry` (descriptor types incl. **`safety` classification + cost-tier + toggle-style**; initial descriptors); `parser` (left scanner + recursive-descent + AST + diagnostics).
- Result model; minimal `cli` (`--help`/`--version`).
- Author `docs/test-plan.md`.
- **Exit:** `bazel test //...` green on both OSes; `parser` passes table-driven AST + error-diagnostic tests.

### Phase 1 - Drop-in find (the contract)

- `vfs` local backend; `engine` traversal (sequential → parallel) with **`--exact` FS-aware name matching** (#8) and **`--path-encoding`** non-UTF-8 output handling (#5).
- Full find expression: tests (`-name/-iname/-path/-type/-size/-mtime/-perm/-empty/-newer…`, **`-regex`/`-regextype` via RE2 grammar-translation** (#4), **birthtime `-Btime`/`-Bmin`/`-Bnewer`** (#8)), positional options (`-maxdepth/-mindepth/-depth/-xdev`), symlink modes `-H/-L/-P`, operators/precedence, actions (`-print/-print0/-printf`, `-exec \;`/`+`, `-execdir`, `-delete`, `-prune`, `-quit`, `-ok`/`-okdir`), default `-print`; GNU-canonical + BSD globals.
- **Exit-code model** (#9, find-default); **impossible-task-fail + `--skip-unsupported`** (#8); **safety**: `--safe`/`--dry-run` + destructive-primitive warnings (#2); **`-j`-controlled semicolon-form `-exec` concurrency** (synchronous at `-j 1`, direct potentially interleaved child output above one) (#7).
- Renderers: plain, NUL, JSONL.
- **Exit:** **find-compatibility conformance suite passes** (Linux GNU-find + macOS BSD-find).

### Phase 2 - Modern layer

- **Content matching** - composable `-contains`/`-grep` via `regex` (**PCRE2 opt-in + configurable limits** (#4), prefilter) + `-i`/`--ignore-case`; **binary-skip + `--all-text`** (#10), **`--encoding`/`-E` input decoding** (#10), **`--max-contentsize`** + negative-match cost (#6).
- **Ignore family** (gitignore stack, `.ignore`/`--ignore-files`, `.xffignore`, `-u`/`--no-ignore`, `--exclude/--include`); dotfiles/hidden.
- **`config` cascade + promotion-to-modern + trust model** (#2): data-only tree configs, `--config`-armed exec blocks, ownership gate.
- Renderers: aligned columns, markdown, CSV/TSV (display-width-correct); full `--path-encoding` mode set.
- `--explain`, cost-warning, shell completions.
- **Exit:** content/ignore/config-trust/render/explain tested incl. CJK-width golden + config-trust security tests; completions generated.

### Phase 3 - Differentiators

- `stats` (sizes/counts, per-type, histograms, top-N); **`--shards` collapsing + completeness flagging** (#11); duplicate detection (content hash); `vfs` **archive** backend - **virtual entries (read-only, `container!member`) + bomb/Zip-Slip guards** (#3) - via libarchive.
- **Exit:** stats/shards/dedup/archive tested incl. decompression-bomb + Zip-Slip cases.

### Phase 4 - Later / optional

- Freshness-aware index (**FSEvents/inotify** behind a platform interface, #8); `vfs` **remote/SFTP** virtual entries (#3); opt-in `--optimize`; named query profiles. (sed-like editing remains a non-goal.)

## Test strategy (detail in docs/test-plan.md)

Per the project rule (tests at every level; no one-shots):

- **parser:** table-driven `argv → AST` / error (pure, no fixtures).
- **find-compat:** golden conformance - real `find` vs `xff` over a fixture tree × expression matrix, both dialects (Phase-1 gate).
- **engine:** integration over fixture trees (predicates, actions, parallel determinism with `--sort`).
- **regex:** per-engine conformance; prefilter equivalence.
- **render:** golden output per format incl. Unicode-width alignment and non-UTF-8 path handling (JSON escaping).
- **config:** cascade precedence + provenance.
