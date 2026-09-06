<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# xff - eXtended File Find

`xff` is a `find(1)`-compatible file finder with modern extensions. It walks each starting path and acts on the entries matching an expression, exactly like `find`, then adds the conveniences you always wished `find` had: content and language search, structured output, per-run summaries and histograms, safe deletes, native hashing, and a shared `{field}` vocabulary that threads through `-printf`, `-exec`, and every renderer.

Anything `find` does, `xff` does the same way. Everything else is opt-in.

This `README.md` is a short overview. The complete, always-current reference lives in [XFF.md](./XFF.md) (generated from the binary; see Documentation).

---

## Core Highlights & Architectural Advantages

- **`find`-Compatible Core:** The standard primaries (`-name`, `-type`, `-size`, `-mtime`, `-regex`, `-exec`, `-prune`, ...), operators, and exit codes behave exactly as in GNU/BSD `find`. Invoked as `find`, it is strict `find` and nothing more.
- **Content & Metadata Matching:** `-grep` / `-content` search inside files, `-lang 'C*'` and `-mime 'image/*'` match by inferred language or media type, `-text` / `-binary` / `-eofnl` classify content, and native `-hash` primitives emit optimized checksum manifests.
- **Overrideable MIME Vocabulary:** The lean binary carries common media types; the removable
  `mime-db` extra expands that to thousands of registered types. Repeatable
  `--mime-vocabulary=FILE` JSON overlays can replace extension mappings and attach descriptions,
  sources, charsets, aliases, and compressibility, with explicit conflict policy and matching
  `{mime-*}` fields for output and aggregation.
- **Overrideable Language Vocabulary:** Common languages remain built in; the removable,
  Brotli-compressed GitHub Linguist extra expands classification to hundreds of canonical records.
  Repeatable `--lang-db=FILE` layers override suffixes, exact filenames, aliases, colours,
  groups, and provenance with an explicit ambiguity policy; `{lang-*}` fields expose the selected
  metadata without running a heavyweight content classifier, and the same colours theme regular
  filenames consistently in the plain and `-ls` listings.
- **One Composable Expression Language:** Path, content, ownership, permissions, age, allocated size, language, MIME type, hashes, and content equality are ordinary tests joined with `find`'s `!`, `-a`, `-o`, and parentheses. Search, reporting, and actions therefore share one walk instead of being stitched together with `xargs` and temporary files.
- **Fuzzy File Finding:** `-fuzzy` and `-fuzzypath` provide scored fzf-query, plain-subsequence, Levenshtein/edit, and character-shingle models, while `--sort=score` ranks the results. The score is also available as `{fuzzy}` for custom tables and templates.
- **Structured Layout Engines:** Stream matches natively as plain text, NUL-delimited, `JSONL`, `CSV` / `TSV`, an aligned console table, a visual tree, or a standard Markdown table, all calculated from one single filesystem walk.
- **Summaries & Histograms:** `--summary=ext` folds matches into counts and totals; `--histogram='ext:sum(lines)'` draws terminal bar charts using Unicode block characters. No external `awk | sort` pipeline overhead required.
- **Native Comparison & Deduplication:** `--compare[=status|diff] LEFT RIGHT` inventories both trees and reports selected result kinds or a unified patch while honoring each side's `.gitignore`; the one-sided `-diff TARGET` action compares every match from a normal expression walk with its templated counterpart. `-cmp` tests byte equality, `-hasheq` verifies a digest, and `--summary=hash` groups duplicate content.
- **Near-Duplicate Content Matching:** `-similar` compares every matched text file with a reference using exact word-shingle Jaccard similarity. Its shingle width and quality threshold are explicit, so source forks, copied documentation, and lightly edited configuration files can be found without reducing the question to byte equality.
- **Unified `{field}` Vocabulary:** The same named fields (`{relpath}`, `{size}`, `{lang}`, `{hash}`, `{capture.NAME}`, ...) drive `-printf`, `-exec`, `--format`, and `--summary`, complete with powerful `s///` regex rewrite and `m//` extraction qualifiers.
- **Commands as Data Sources:** `-capture` / `-capturedir` bind a command's output to `{capture.NAME}`. Later formatters and reductions can transform or aggregate it, enabling workflows such as tree-wide `git blame` totals without an `awk | sort` tail.
- **Safe by Default:** `-delete` implicitly forces `-depth` and strictly honors `--dry-run` / `--safe`. Configuration tiers loaded via an `--xffrc` file are sandboxed: they cannot execute dangerous directives (`-exec`, `-execdir`, `-ok`, `-capture`, or `-delete`) unless explicitly armed via a trusted CLI flag (`--allow-exec`).
- **Multi-Threaded Traversal:** `-j N` runs the filesystem walk across a native worker pool and also controls concurrent `-exec` jobs, scaling both discovery and actions across available CPU cores; `--sort` restores deterministic ordering when requested.
- **Virtual Archive Filesystems:** `--archive` walks archives and compressed files as directory trees, including nested containers, Electron ASAR bundles, SquashFS images, Snap packages, and AppImages. The normal name, metadata, content, hash, and reduction vocabulary works on members; removable format extras stay independently linkable rather than pulling every archive dependency into the core. Opt-in controls can mount or extract members for commands and safely rewrite formats that support deletion.
- **Archive Creation from an Expression:** `--pack=FILE` writes the matched set directly to a new archive, with deterministic member order, atomic publication, compression controls, and no `find | tar` filename boundary to get wrong. The removable Brotli extra reads raw streams and writes self-identifying RFC 9841 `.tar.br` / `.tbr` by default, with an explicit raw compatibility mode.
- **Developer-Aware Traversal:** Layered `.gitignore`, `.ignore`, and `.xffignore` handling; explicit include/exclude rules; hidden-file policy; and pruning for Git, Mercurial, Subversion, Jujutsu, Bazaar, Darcs, and CVS are independent, configurable controls.
- **Shard-Aware Validation:** `--shards` recognizes numbered datasets such as `data-00000-of-00010`
  and collapses each set to a useful first, wildcard, or count representation;
  `-shard-status complete|incomplete|superfluous` selects healthy sets, missing-index sets, duplicate
  copies, and declared-total outliers for inspection or action.

---

## Tool Feature Comparison Matrix

The matrix compares native, built-in capabilities. A `△` means the tool covers a narrower form of
the feature; a `-` means the workflow normally needs another utility or a shell pipeline. The point
is not that every specialist is interchangeable, but that `xff` composes these operations in one
expression and one traversal.

| Feature / Capability                    | `find` | `fd` | `rg` | `fzf` | `tree` | `du` | `diff` | hash tools | archive tools | `xff`                                    |
| :-------------------------------------- | :----: | :--: | :--: | :---: | :----: | :--: | :----: | :--------: | :-----------: | :--------------------------------------- |
| **Filesystem expression language**      |   ✓    |  △   |  -   |   -   |   -    |  -   |   -    |     -      |       -       | **✓ GNU/BSD `find` vocabulary**          |
| **Multi-threaded filesystem traversal** |   -    |  ✓   |  ✓   |   -   |   -    |  -   |   -    |     -      |       -       | **✓ Native worker pool (`-j`)**          |
| **Ignore-file and VCS awareness**       |   -    |  ✓   |  ✓   |   -   |   -    |  -   |   -    |     -      |       -       | **✓ Layered and configurable**           |
| **Path glob and regex filtering**       |   ✓    |  ✓   |  ✓   |   △   |   △    |  -   |   -    |     -      |       △       | **✓ Multiple selectable grammars**       |
| **Ranked fuzzy path matching**          |   -    |  -   |  -   |   ✓   |   -    |  -   |   -    |     -      |       -       | **✓ `-fuzzy`, `--sort=score`**           |
| **Regex content search with context**   |   -    |  -   |  ✓   |   -   |   -    |  -   |   -    |     -      |       -       | **✓ `-grep`, `-rxc`, `--context`**       |
| **Language and MIME filtering**         |   -    |  -   |  △   |   -   |   -    |  -   |   -    |     -      |       -       | **✓ `-lang`, `-mime`**                   |
| **Overrideable MIME metadata**          |   -    |  -   |  -   |   -   |   -    |  -   |   -    |     -      |       -       | **✓ JSON layers + `mime-db` extra**      |
| **Overrideable language metadata**      |   -    |  -   |  -   |   -   |   -    |  -   |   -    |     -      |       -       | **✓ JSON layers + Linguist extra**       |
| **Text, binary, and EOL tests**         |   -    |  -   |  △   |   -   |   -    |  -   |   -    |     -      |       -       | **✓ `-text`, `-binary`, `-eof*`**        |
| **Structured JSON/CSV/table output**    |   -    |  -   |  ✓   |   -   |   △    |  -   |   -    |     -      |       △       | **✓ Eight output formats**               |
| **Tree rendering**                      |   -    |  -   |  -   |   -   |   ✓    |  -   |   -    |     -      |       △       | **✓ `--format=tree`**                    |
| **Field templates and rewrites**        |  GNU   |  -   |  △   |   △   |   -    |  -   |   -    |     -      |       △       | **✓ Shared `{field}` vocabulary**        |
| **Grouped size/count summaries**        |   -    |  -   |  -   |   -   |   -    |  △   |   -    |     -      |       △       | **✓ `--summary`**                        |
| **Native histograms and statistics**    |   -    |  -   |  -   |   -   |   -    |  -   |   -    |     -      |       -       | **✓ `--histogram`**                      |
| **Cryptographic hashing**               |   -    |  -   |  -   |   -   |   -    |  -   |   -    |     ✓      |       △       | **✓ `-hash`, `{hash}`, `-hasheq`**       |
| **Single-pass hash verification tally** |   -    |  -   |  -   |   -   |   -    |  -   |   -    |     △      |       -       | **✓ `--summary=hash-verification`**      |
| **Duplicate-content grouping**          |   -    |  -   |  -   |   -   |   -    |  -   |   -    |     -      |       -       | **✓ `--summary=hash`**                   |
| **Reference-file near-duplicate match** |   -    |  -   |  -   |   -   |   -    |  -   |   -    |     -      |       -       | **✓ Exact word-shingle Jaccard**         |
| **Per-file content comparison/diff**    |   -    |  -   |  -   |   -   |   -    |  -   |   ✓    |     △      |       -       | **✓ `-cmp`, `-diff`**                    |
| **Gitignore-aware tree comparison**     |   -    |  -   |  -   |   -   |   -    |  -   |   △    |     △      |       -       | **✓ status selection or unified patch**  |
| **Virtual archive traversal**           |   -    |  -   |  △   |   -   |   -    |  -   |   -    |     -      |       △       | **✓ Members use the full expression**    |
| **Nested archive content search**       |   -    |  -   |  △   |   -   |   -    |  -   |   -    |     -      |       △       | **✓ Depth-controlled transparent reads** |
| **SquashFS/Snap/AppImage search**       |   -    |  -   |  -   |   -   |   -    |  -   |   -    |     -      |       △       | **✓ Indexed virtual filesystem extra**   |
| **Archive creation from matches**       |   -    |  -   |  -   |   -   |   -    |  -   |   -    |     -      |       ✓       | **✓ `--pack` sink**                      |
| **Standards-framed Brotli archives**    |   -    |  -   |  -   |   -   |   -    |  -   |   -    |     -      |       △       | **✓ RFC 9841 default; raw optional**     |
| **Safe delete preview**                 |   △    |  △   |  -   |   -   |   -    |  -   |   -    |     -      |       -       | **✓ `--dry-run`, `--safe`**              |
| **Parallel/batched per-match exec**     |   △    |  ✓   |  -   |   △   |   -    |  -   |   -    |     -      |       -       | **✓ `-exec ... +`, `-j`**                |
| **Capture command output as a field**   |   -    |  -   |  -   |   -   |   -    |  -   |   -    |     -      |       -       | **✓ `-capture`, `{capture.NAME}`**       |
| **Sharded-dataset validation**          |   -    |  -   |  -   |   -   |   -    |  -   |   -    |     -      |       -       | **✓ collapse + status matching**         |

`find`'s "Field templates and rewrites" entry is marked **GNU** because it is GNU find's `-printf`, a
GNU extension; POSIX and BSD/macOS `find` have no format primary (only `-print` / `-exec`).

---

## Flavor & Behavioral Shift Matrix

`xff` runs one unified grammar under three operational flavors. The flavor is selected automatically by the program binary name and can be explicitly overridden or layered using the `--config` flag (where the last specified style wins).

The table below illustrates how traditional shell workflows shift into optimized `xff` unified expressions.

| Target Intent / Use Case          | Legacy Command / Pipeline                  | The `xff` Unified Expression                                    | Architectural Advantage / Behavioral Shift                                                                                                  |
| :-------------------------------- | :----------------------------------------- | :-------------------------------------------------------------- | :------------------------------------------------------------------------------------------------------------------------------------------ |
| **Strict Compliance**             | `find . -type f -name "*.cpp"`             | `find . -type f -name "*.cpp"` <br>_or_ `xff --config=find ...` | **Strict POSIX compatibility mode:** Turns off all modern extensions; modern flags become immediate usage errors.\*                         |
| **Modern Structural Search**      | `fd -e cc`                                 | `xff -regex '.*\.cc$'` <br>_or_ `xff --config=xff ...`          | **Evolved mode (Default):** Expands `find`'s grammar with modern extensions, enabling sorted output and human sizes (`--human=si`).         |
| **Clean Developer Grep**          | `fd -H -E ".git" \| xargs rg "TODO"`       | `xff --config=rg -grep "TODO"`                                  | **Opinionated Developer Mode:** Implicitly respects nested `.gitignore` files, skips hidden files, and uses smart-case matching logic.      |
| **High-Performance Verification** | `find . -type f -exec sha256sum {} \;`     | `xff -type f -hash:sha256`                                      | **Zero-Fork Speed:** Eliminates system process-spawning overhead. Reads files directly into the native read loop buffer to hash inline.     |
| **Missing Newline Code Linting**  | _Complex multi-line awk scripts or loops._ | `xff -text ! -eofnl -print`                                     | **Native Classification:** Instantly flags text files violating POSIX trailing newline rules without streaming lines to the shell.          |
| **Cross-OS Time Constraints**     | `find . -mmin -60`                         | `xff -mtime "-3 weeks 3 hours"`                                 | **Advanced Parsing:** Uses human-readable compound duration strings interpreted cleanly via explicit IANA `--timezone` modifiers.           |
| **Compressed Asset Auditing**     | `tar -ztf src.tar.gz \| grep "cfg"`        | `xff --archive -path "*src.tar.gz*cfg*"`                        | **Virtual File-tree Mapping:** Treats archives as virtual read-only directories, matching inner structures without manual disk extraction.  |
| **Isolated Variable Outputting**  | `find . -printf "%p,%s\n"`                 | `xff --format=csv --columns=path,size`                          | **Structured Sanitization:** Formats data cleanly into formal arrays with safe, native C-escape column handling (`--path-encoding=escape`). |

\* **Strict `find` mode is still xff's engine, not a wrapper around the OS `find`.** It keeps
find's vocabulary and turns the xff extensions into usage errors, but the implementation is one
fast, mostly platform-independent binary. The clearest divergence is regex: `-regex` / `-iregex`
default to **RE2** (linear-time, no catastrophic backtracking) and behave identically on Linux
and macOS - where GNU find instead defaults to its Emacs dialect and BSD/macOS find to BRE.
`-regextype` selects xff's uniform grammar set (RE2, EXACT, FNMATCH, GLOB, SHGLOB, plus PCRE2 in
a full build), never GNU's dialect names. GLOB and SHGLOB are locale-independent, component-aware,
and reject malformed bracket expressions instead of silently changing their meaning. SHGLOB adds
nested alternatives and bounded integer or ASCII-letter sequences such as `{01..12}` and `{a..z}`.
Otherwise strict mode is find's documented behavior, made uniform across platforms.

---

## Quick Start

`xff` builds with Bazel and runs on macOS and Linux.

```bash
# Build and run the stock binary.
bazel run //xff -- . -type f -name '*.md'

# Or build it once and put it on your PATH.
bazel build //xff
cp bazel-bin/xff/cli/xff /usr/local/bin/xff
```

---

## Examples

```bash
# Ten largest files (-printf builds any columnar line; the shell sorts).
xff . -type f -printf '%s\t%p\n' | sort -rn | head

# Disk use per file type (a --long global like --summary may sit at the end).
xff . -type f --summary=ext

# Delete stale temp files, safely (prints what -delete WOULD remove).
xff . -type f -name '*.tmp' -mtime +7 -delete --dry-run

# Search code content, filtered by language (path:lineno:text for every TODO).
xff src -lang 'C*' -grep 'TODO'

# Checksum manifest for a tree (like sha256sum: `DIGEST  PATH` per file).
xff . -type f -hash:sha256

# Recently changed files as machine rows (one JSON object per file, for jq).
xff . -type f -mtime -1 --format=jsonl
```

See the [XFF.md](./XFF.md) cookbook for more worked examples, including native per-author `git blame` line counts computed with no shell pipes.

---

## Documentation

The vocabulary and options are defined once inside the C++ binary (the engine registry acts as the single source of truth), ensuring that every documentation surface is automatically generated and cannot drift:

- **`XFF.md`**: The full comprehensive reference in Markdown. Run `xff-md-update.sh` to regenerate it; the `//xff/cli:xff_markdown_test` target fails CI if it drifts from the binary.
- **`xff --help`**: Renders the main utility usage page. Use `xff --help=TOPIC` to review specific sub-topics (`fields`, `printf`, `time`, `size`, `grammars`, `stats`, etc.), or `xff --help=full` to dump all help sections.
- **Formatted full reference**: Append `:markdown` (or `:md`), `:html`, or `:roff` to `--help=full` (or its `long` alias). HTML is a standalone, self-contained HTML5 document with no scripts or external assets; `--man` remains the conventional alias for the roff form.
- **Paired release docs**: The shared `//xff/cli:xff_reference_gen` action always generates Markdown and HTML together, and release Pages publish that exact pair so the formats cannot silently diverge.

---

## Building & Dependencies

The default build provides a lean, dependency-light core. Heavier processing capabilities (such as the advanced `PCRE2` regex grammar or recursive archive diving) are decoupled as composable build-time extras that are disabled by default to keep the core binary small. The extended target links them all:

```bash
# The full binary, with every extra (PCRE2, archive diving, Brotli, etc.).
bazel build --config=xff_full //xff/cli:xff_full
```

The `//xff` target alias follows your active workspace configuration automatically: it resolves to the lean binary by default, and switches to the full binary under `--config=xff_full`. The underlying targets remain explicit and configuration-stable: `//xff/cli:xff` is always lean, and `//xff/cli:xff_full` is always full.

Published binaries use `--config=release`, which selects the hermetic Clang toolchain, optimizes for
size, and enables ThinLTO. Each platform release is one `xff-PLATFORM-ARCH.tar.gz` archive containing
the stripped `xff` and `xff_full` executables plus their matching symbol files under `debug/`. The
ordinary Linux and macOS CI jobs test this same configuration and execute the
staged, stripped binaries before a release can use it.

> **Compile-Time Enforcement:** The CLI options for extras (e.g., `--regextype=PCRE2` or `--archive`) are always exposed on the interface. Attempting to invoke an extra feature in a lean build that did not compile it will yield an immediate, explicit error rather than a silent failure or fallback.

- **Requirements:** Bazel 9.1.1 or newer, accompanied by a modern C++23 toolchain (`clang-22` or
  newer). A fully hermetic LLVM toolchain is available out-of-the-box via `--config=clang`.

---

## Contributing

See [CONTRIBUTING.md](./CONTRIBUTING.md), the LLM agent and contributor guidelines in [AGENTS.md](./AGENTS.md), and the coding styles in [STYLE_CPP.md](./STYLE_CPP.md) and [STYLE_SH.md](./STYLE_SH.md). In-depth design notes live under `docs/`; current work is tracked in [TODO.md](./TODO.md), while completed investigations and decisions move to the [development history](./docs/history.md).

---

## License

Apache License 2.0. See `LICENSE` and `NOTICE` for details.
