<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# 0.9.0

- Reduce collection growth and text-allocation overhead with segmented records and collection-owned arenas.

- Add a platform selector and measured-version slider to the benchmark overview landscape.

- Render benchmark landscapes with Three.js, plane-mounted titles, linked hover guides and a smaller offline bundle.

- Accelerate eligible PCRE2 patterns with JIT on ARM64 and x86-64, retaining bounded interpreter fallback.

- Report completed deletion counts and known logical file bytes when a local deletion fails.

- Let `-diff [TARGET]` default to Git creation patches against `/dev/null`, including binary files, empty files, executable modes and symlinks.
- Clarify and test that `-delete` removes only empty directories and makes `-prune` ineffective.

- Add percentage tick units and a logarithmic speedup-factor view to benchmark landscapes.

- Show interactive comparison landscapes above benchmark tables and collapse absolute measurements.

- Preserve site favicons when publishing benchmark results.

- Avoid clearing unused read-buffer capacity for each content search.
- Optimize release builds for execution speed with `-O2` and ThinLTO.

- Compile fuzzy queries once and skip unused ranking work.
- Avoid unnecessary per-file metadata reads and evaluate eligible content predicates in ordered parallel batches.
- Request extended birth-time metadata only when a matcher or output consumer needs it.

- Upgrade rules_cc to 0.2.25 and express the macOS deployment target through Bazel's
  native settings for consistent Apple and hermetic LLVM toolchains.
- Add self-contained benchmark manifests, CPU-grouped scaling tables, and overlap-aware PR/main comparisons.

- Publish correctness-checked task comparisons with find, ripgrep, and fzf in benchmark history.

# 0.8.0

- Keep accelerated XZ CRC64 reads within their input buffers, including short and unaligned tails.

- Support native `productbuild` XAR seek layouts, bounded source ranges, and shared decoder/range-buffer budgets.
- Read XAR packages and streamed PBZX/CPIO payloads on Linux and macOS in the existing full binary.

- Upgrade toolchains_llvm to 1.9.1 and use Apple ld for macOS SDK 27 compatibility.

- Order release navigation by common tasks, with Documentation last.
- Add --help-format for all help targets, with format-aware width, color, and paging; remove
  the selector format shorthand.
- Publish generated Markdown notices for both lean and full builds, linked from release downloads.

- Double weekly/manual deep-fuzz campaigns to 600 seconds per target.

- Refresh coverage PR state during normal publication instead of privileged close/reopen events.

- Honor explicit configuration overrides when refreshing an already-published release site.

- Link release and PR benchmark pages to retained measurements for the exact referenced commit.

- Run and publish informational benchmarks after merge only; keep manual experiments artifact-only.

# 0.7.0

- Add informational paired benchmark history with raw measurements, variability, and explicit baselines.

- Allow serialized coverage backfill from an existing completed CI run without rerunning tests.

- Publish successful coverage despite unrelated CI failures, and show post-merge before
  pre-merge within each PR in the two-phase coverage view.

- Show one coverage result per PR, replacing pre-merge with exact-commit post-merge coverage
  when available; show both phases separately without repeated main runs or attempts.

- Refresh coverage overview ordering on PR merge, closure, or reopening without waiting for another CI run.

- Run Python tooling tests and resource benchmarks through explicit Aspect Bazel targets and a
  hermetic Python 3.13 interpreter; keep pre-commit focused on fast checks.

- Suggest similar registered flags, primaries, topics, and aliases for misspelled help selectors.

- Order coverage reports by actual PR merge and tag timestamps, without standalone main rows.
  Omit closed, unmerged PRs from the overview while retaining their reports.

- Rank fuzzy flag suggestions and show up to three candidates for misspelled options.

- Avoid redundant help spacing in console, Markdown, and man-page output while preserving
  intentional blank lines in code examples.

- Default help and width-controlled comparison summaries to `--width=auto:110`. Support custom
  automatic caps of at least 40 columns and persistent INI preferences, with explicit CLI overrides.

- Retain detailed coverage reports by CI run and attempt, with a published history index and
  correct ordering for PRs merged through an aggregation branch.

- Clarify safety provenance names and enforce ASCII source text while preserving Unicode output.
  Standardize multiline struct initializer formatting, including generated license registrations.

- Add a read-only benchmark harness for runtime logical content-read accounting on real files.

- Derive shared expression behavior and help relationships from registry capabilities.
  Treat every explicit archive mode consistently when archive support is unavailable.
- Escape control bytes in human-readable tables and tree labels, keeping unusual filenames
  on one display line and preserving newline distinctions in Markdown.

- Distinguish aggregate summary rows with `is_total` in JSONL and quote ambiguous data labels
  in text tables, preserving real groups named `total`.

- Use grouped scope headers in console comparison summaries and compact scope rows when the table
  exceeds `--width`. Preserve Unicode graphemes, numeric values, and machine-output accounting.

- Label console summary groupings and report omitted groups under `--top`, including
  machine-readable group counts in truncated JSONL tables.

- Export ordinary and comparison summaries as CSV/TSV with one shared schema and explicit row identity.

- Group archive members correctly with `--summary=hash`, using the active filesystem and
  configured hash algorithm and encoding consistently with `{hash}`.

- Identify every JSONL summary row by request, grouping, and scope, retaining template text
  and comparison roots so repeated and mixed summaries can be separated reliably.

- Extend `--explain` with effective worker limits, retained-state notes, active content-reading
  fields, and advisory expression costs without running the search or executing actions.

- Reject invalid field templates before traversal or actions, including configured summaries and
  comparison runs. Printf field escapes share quoted-placeholder parsing with ordinary templates.

- Explain inactive CLI modifiers for grep, diff, hash, shard, archive, and reduction output using the effective
  configuration; dormant configured defaults remain quiet and valid.

- Resolve the current behavior values shown by `--explain` from the complete configuration,
  including named selections and explicit files, instead of only command-line flags.
- Show named-profile availability, declared behavior, skip and validation reasons, and physical
  flag origins in configuration inspection, with small configuration recipes.

- Show effective safety decisions in `--explain`, including mandatory and profile origins,
  active modes, per-file categories, and expanded directory roots.

- Make default help a compact command and task guide, with direct routes to the complete
  flag inventory and detailed reference; wrap the help title to the requested width.
  Structure comparison guidance and keep safety tables readable with labeled rows on narrow terminals.

- Suggest close registered option and predicate spellings in usage errors, and explain when a
  short global must be moved before the roots; command arguments remain literal.

- Explain comparison exit status for automation, including summary output, output selection,
  filtered populations, and error precedence.

- Keep comparison summaries and listings physical when shard flags are present, preventing
  mixed sets from being assigned wholly to their representative's comparison category.

- Apply shard grouping once to each collected summary/histogram population, preserving collection
  placement and eliminating logical-set plus physical-member double counting.

- Explain physical files versus logical shard sets with tested examples covering classification,
  actions, collection placement, reductions, duplicates, and comparison.

- Resolve archive filename sniffing from the final traversal mode, so a later all-mode selection
  restores the filename gate after any-mode flags or named configuration selections.

- Add an order-and-limits guide covering actions, collections, summaries, packing,
  parallel traversal, and comparison populations.

- Add executable archive-safety recipes for inspection, new archives, member deletion,
  and writing reports only beneath a declared output root.

- Reject zero, negative, malformed, and overflowing histogram widths before traversal or actions,
  including settings selected from configuration files.

- Render Markdown histograms as bucket/value tables and reject unsupported histogram formats
  before traversal or actions, instead of silently emitting text bars.

- Comparison status and built-in grep output support JSONL records, including grep context and
  counts. Non-UTF-8 values use lossless tagged base64 objects. Unsupported comparison formats
  fail before traversal or expression actions.

- Reject duplicate normalized archive destinations before creating output. `--pack-duplicates=first`
  keeps the first collected input; dry-run validates the same member plan.
  Named roots (`--root=NAME=PATH`) give inputs distinct archive directories.

- Avoid duplicate comparison tables when combining `--compare=summary` with bare `--summary`,
  while retaining scoped totals and explicitly requested grouping tables.

- Lead the README with binary installation and common tasks before the detailed feature matrices.

- Correct the release archive description: lean and full executables have separate archives,
  each with its own matching debug symbols.

- Add regression coverage for Markdown table alignment after newline and pipe normalization.

- Reject malformed, negative, or overflowing buffer limits before traversal and actions,
  including limits selected from configuration files.

- Validate capture references in field consumers, including printf, grep templates, directory
  execution, and columns; escaped literal references no longer hide unused captures.
  Directory captures preserve implicit output consistently with ordinary captures.

- Correct binary-content help to state the exact 8,000-byte NUL sniff window.

# 0.6.0

- Raise compiled-output CI cache budgets to 1 GB uncompressed by default and 3 GB for ASan.

- Add `--summary-scope` for combined-root, per-root, and comparison statistics. In comparison mode,
  scopes select ordered column groups in one table: `compare` expands to `left-total,right-total`,
  `diff` to `left-only,right-only,different`, and each explicit category gets its own statistics.
  `left` and `right` alias the side totals; output labels use canonical names. Ordinary summaries
  default to `compare` during comparison and `all` otherwise; explicit scopes override that default.
  Scope selection requires an active ordinary summary; a comparison-results summary alone does not suffice.

- Support INI-only `${NAME}`, `${NAME:-DEFAULT}`, and `${NAME:?MESSAGE}` environment substitution,
  with literal quoting, single-argument values, and unchanged validation of expanded safety roots.

- Preserve targeted Bazel preparation traces for coverage and fuzz to diagnose remaining CI delays.

- Start coverage and fuzz CI jobs immediately while preserving all final merge checks.

- Run fuzz campaigns concurrently after preparing all targets, with CPU/memory bounds, isolated
  artifacts, per-target throughput statistics, and unchanged campaign time budgets.

- Raise coverage's CI cache allowance to 1.5 GB uncompressed and 1 GB compressed to retain more build outputs.

- Allow MSan caches up to 4 GB uncompressed and 1.2 GB compressed; report per-job cache sizes
  and repository-wide usage against the 10 GB budget in main's final CI job.

- Retire older main build caches immediately after a verified replacement upload, reducing
  generation overlap while preserving previous caches when the replacement is missing or oversized.

- Preserve more reusable CI compilation outputs by evicting large cache blobs before small ones
  with a 2.6 GB uncompressed allowance for sanitizer jobs; apply shared caching to deep fuzz and release reference generation.

- Reject invalid summary precision, negative or malformed top limits, comparison selections without
  comparison, and unsupported summary formats. `--top=0` consistently removes a prior limit;
  aligned summary output now works in comparison mode. Accept the implemented `--summary=owner` alias
  in CLI validation. Clarify summary/compare accounting and exports.

- Refresh bounded CI compiled-output caches on main, reuse them in PR and release builds,
  and remove closed-PR, tag-scoped, and superseded caches. GCC now caches compiled outputs.

- Add MBO Works artwork to the README and favicons to documentation and coverage Pages.

- Keep `main` first in the coverage index, then interleave PRs and releases newest-first by
  their merge or tagged commit in main's history.

- Give Markdown summaries descriptive headings and bullet lists for labels and notes, separate
  each table's note from the next summary,
  and align exact byte values with scaled sizes in console and Markdown tables.

- Render ordinary and comparison summary tables as Markdown with `--format=md` or `markdown`.

- Show count, count percentage, size, and size percentage in summary tables. Comparison results
  include directories and other matched entry types. Paired categories count each pair once and
  include both sides' bytes; side-total groups count their own entries and bytes. Each summary
  column group uses its own full pre-`--top` totals for percentages.

# 0.5.0

- Expand at most one related topic in focused flag help, with explicit selections checked at
  compile time. Full HTML/Markdown references retain clickable related pointers, man pages name
  their targets, and console long help stays compact; none expands navigation.

- Add copyable "See also" help commands across focused option, expression, and topic pages.
  Related controls follow the registry relationships, and HTML/Markdown links use stable targets.
  Full/long help keeps one copy of each detailed entry and topic without expanding navigation.

- Add contextual comparison help and related-control lists. Provide comprehensive `--help=regex`
  separately from `--help=regextype`, whose alphabetical value list is followed by the structured
  grammar reference and a link to `--help=regex`.

- Add `--compare=summary` as shorthand for status comparison with per-path output disabled and
  a comparison summary enabled. `--compare-select=none` or an empty value suppresses records;
  summaries always count all comparison outcomes.

- Add `--summary=compare` with status counts, percentages, and a total; bare `--summary`
  selects it during tree comparison, including empty comparisons.

# 0.4.0

- Use `--block-policy-categories=LIST` to select separate archive, temp, or output block controls
  per INI file; unlisted categories use ordinary file controls.

- Fail before actions on invalid configuration globals and undefined selected or composed configs.
- Reject command-line-only options and non-regular configuration targets; document directive scopes
  in individual help entries and apply INI pager settings to listing output.

- Fix the user config location at the effective OS account home under `.config/xff/config`;
  environment variables cannot redirect trusted configuration.
- Reject missing explicit `--xffrc` files and unreadable existing configuration, including
  dangling symlinks and requested skips. Missing automatic system/user files remain normal.
- Evaluate `.xffrc` admission controls in requested selector order, including composed configs.

- Make `--no-system-config` and `--no-user-config` exclude named sections while retaining existing
  globals by default. `--no-config` combines those exclusions and disables `.xffrc` autoloading.
- Add config-only `--require-system-globals` / `--no-require-system-globals` and
  `--require-user-globals` / `--no-require-user-globals` pairs. Each pair appears once per permitted
  file, in unsectioned globals; the system decision wins for user globals. Optional globals are
  excluded only when a skip is requested. Missing automatic files remain normal.
- Add config-selectable `--allow-xffrc` / `--no-allow-xffrc` controls for explicitly named config
  files, subject to the system policy gate and unavailable to the file being admitted.
- Document every config-only allow/deny control individually in `--help=config`, including its
  scope, permitted location, precedence, and multiplicity.
- Warn about overriding the same setting more than once within one logical config section while
  preserving last-value-wins behavior. Deliberately accumulating settings, expression primaries,
  separate sections and tiers, and all command-line repetitions remain unaffected.
- Apply named configuration and explicitly loaded config files at each command-line
  selector's exact position instead of flattening them ahead of all CLI options.
- Defer regex matcher binding until the final configuration has selected grammar
  and case, avoiding premature or duplicate compilation.
- Add `--re2` and `--pcre` as direct regex-engine selectors, plus BSD/macOS
  find-compatible `-E` using the configured extended grammar.
- Add the always-available `--regextype=ERE` engine for native POSIX extended
  regular expressions, with platform locale semantics and explicit NUL handling.
- Add distinct root-spanning traversal orders: `--sort=roots` sorts only the search operands,
  while `--sort=global` sorts the operands and walks each tree in deterministic depth-first order.
  Clarify the ordering, directory read-ahead, result buffering, post-order, and score-ranking
  contracts throughout the generated reference and parallel-walk design.
- Honor the selected path encoding in tree-comparison status records, including non-UTF-8 path
  bytes, instead of emitting raw relative paths independently of the output configuration.
- Apply every matching system-policy safety-class denial to mixed config lines, so a destructive
  primary cannot be hidden from an `@destructive` rule by a sensitive primary on the same line.
- Clarify concurrency and matching controls, including worker defaults and `-exec` timing,
  filesystem-native versus explicit case folding, block-size units, and regex grammar scope.

# 0.3.7

- Publish an immutable, versioned documentation website for every release, with stable links to
  the native standalone HTML reference, release notes, source documents, and coverage. Historical
  snapshots remain available while the site root follows the latest stable release.
- Restructure the generated reference with linked Markdown contents, clearer option groupings, a
  command-grammar overview, and dedicated output and tree-comparison guides. Console and man-page
  rendering remain compact, while HTML retains its single native navigation block.
- Make `--no-config` absolute: it now consults no system, user, or explicitly named config file,
  matching the expectation set by its name regardless of other config-option ordering.
- Clarify config-selector stacking, `--explain` output, and `-H` / `-L` / `-P` symlink traversal
  boundaries in the generated reference.
- Test the complete suite with the same Clang, size optimization, ThinLTO, and linker settings as
  the published binaries, eliminating duplicate release builds while smoke-testing the final
  stripped artifacts.
- Upgrade PCRE2 to 10.48, including its security fixes, Unicode 17 data, matching corrections, and
  memory-use improvements.
- Replace the legacy Bazel setup action and implicit cache with the current opt-in setup while
  retaining XFF's explicitly partitioned and size-bounded CI caches.

# 0.3.6

- Make `--compare` run two ordinary walks in parallel and compare their expression matches by
  relative path. It no longer silently enables Git ignores, skips VCS metadata, forces tree sorting,
  rejects expressions and file roots, or overrides `-P` / `-H` / `-L` symlink traversal.
- Make release installation instructions explicit for both supported platforms and both binaries,
  verifying each download before installation and generating its matching manual page. A generated
  man page now uses the binary's invocation name, including `xff_full` or an `xff` symlink. Downloads
  use a unique temporary directory and clean it on exit instead of polluting the caller's directory.
- Publish `xff` and `xff_full` symmetrically: each has a directly downloadable, fully stripped
  executable and a matching `.tar.zst` containing that executable and its separate debug file.

# 0.3.5

- Shrink the installed Linux release binaries with LLD string-tail and branch optimization, safe
  identical-code folding, and packed relative relocations. The measured AArch64 binaries are 12.9%
  smaller for `xff` and 7.3% smaller for `xff_full`, without changing macOS or non-release builds.

# 0.3.4

- Restore the four directly downloadable platform binaries and additionally publish each platform's
  debug bundle as a level-19 Zstandard-compressed `.tar.zst` archive; do not publish `.tar.gz`
  duplicates.
- Publish an attested `SHA256SUMS` manifest and include binary, checksum-verification, and generated
  man-page installation instructions in every release.

# 0.3.3

- Build release binaries with hermetic Clang, `-Oz`, and ThinLTO; strip the published executables
  and include their matching debug files in one archive per platform.
- Test the exact release binaries in the default Linux and macOS CI jobs, and use the same staging
  path for pull-request candidates and tagged releases.

# 0.3.2

- Check in formatter-clean C++ mirrors of embedded license bodies and verify each one against its
  authoritative license text with Bazel, giving every platform and build configuration the same
  source path while retaining deterministic regeneration checks.

- Add `--help=full:html`, a standalone semantic HTML5 rendering of the complete generated reference
  with responsive styling, dark-mode support, stable anchors, and no scripts or external assets.
  `--help=full:markdown` generates the Markdown form; `long` aliases `full`, and `--man` remains the
  conventional shortcut for `--help=full:roff`. New releases publish paired HTML and Markdown
  references generated from the same binary.

# 0.3.1

- Publish GitHub build-provenance attestations for every release binary, and link each release to
  its retained production coverage report and versioned `XFF.md` reference on MBO Works Pages.
- Derive release coverage instrumentation from the extras registry so every enabled extension is
  measured automatically rather than depending on a manually maintained filter.

# 0.3.0

- Transfer the repository to `mboworks/xff`, rename the root Bazel module to
  `mboworks_xff`, move coverage publishing to MBO Works Pages, and adopt the
  canonical MBO Works copyright identity. Historical tags and GitHub releases
  remain unchanged.
- Migrate MBO Works dependencies and repository references, including
  `mboworks_mbo` and `mboworks_bashtest`.
- Keep post-release CHANGELOG updates as ordinary human-reviewed pull requests;
  the release helper no longer arms them for automatic merging.
- Add a non-publishing `--dry-run` mode to the release helper so its clean-main,
  version, tag, and archive checks can be verified before creating a signed tag.

- Add `--compare[=status|diff] LEFT RIGHT` tree comparison. It applies each repository's
  `.gitignore` rules independently, skips VCS metadata, and compares text and binary files byte for
  byte. Status output reports `left-only`, `right-only`, `identical`, and `different` paths selected
  by `--compare-select`; diff output produces a unified tree patch. Unlike the one-sided `-diff`
  expression action, it inventories both roots and therefore finds right-only paths.
- Add `-similar[:WIDTH[:PCT%]] TARGET`, an exact reference-file near-duplicate matcher using unique
  contiguous word shingles and Jaccard overlap. It defaults to five-word shingles and an 80%
  threshold, treats punctuation and whitespace as word boundaries, folds ASCII case, and skips
  binary content. Whole-tree clustering remains a separate deferred reduction.
- `--help=styles` now documents xff's command-line grammar: double-dash whole-run flags versus
  single-dash expression primaries, where global hoisting stops, and why the deliberate `-n` / `-p`
  pair does not establish automatic one-letter aliases for other primaries.
- Fzf compatibility tests now pin exact normalized scores for documented compound queries and
  nearby boundary/gap variations, rather than merely checking that successful scores lie in range.
  End-to-end `-top` and `--max-results` tests likewise pin complete ordered output.
- `-fuzzy:fzf` now follows fzf's operator precedence for compound inverse, quote,
  anchor, and OR expressions, and its anchors ignore whitespace at the corresponding
  candidate edge. Tests carry fzf's documented compound query and upstream parser cases.
- Add `--max-results=N`, an aggregate ceiling for the implicit result listing across expression
  branches and independent `-first` / `-top` filters. It does not stop the walk or truncate
  reductions; explicit expression actions retain their positional semantics.
- Add exact expression-level `-top N` fuzzy result selection. Each use keeps its own stable best-N
  set; actions before it run while traversing and actions after it run only for the selected entries.
  Mixed fuzzy models or thresholds are rejected rather than silently imposing an arbitrary order.
- `-n PATTERN` and `-p PATTERN` are concise aliases for `-name PATTERN` and `-path PATTERN`.
  They resolve through the expression registry to the canonical primaries, so their matching,
  style, cost, and generated documentation are identical.
- Size inputs now spell their scale explicitly across `-size`, `-blocks`, `--block-size`, and
  `--buffer`: `B`/`kB`/`MB`/.../`EB` are decimal SI, while
  `KiB`/`MiB`/.../`EiB` are binary IEC. Find's historical `c`/`w`/`b` and
  `k`/`M`/`G`/.../`E` forms remain accepted with their original meanings. Multiplication overflow
  is rejected, and `--help=size` documents the complete shared vocabulary.
- `-fuzzy[:MODEL[:PCT%]] PATTERN` (and the `-ifuzzy` / path variants) selects
  `fzf`, plain `sequence`, `levenshtein` (`edit`), or character-bigram `shingles`
  matching and optionally requires a normalized match quality. The `fzf` model
  supports fzf's extended-search terms: space AND, `|` OR, exact/prefix/suffix
  operators, inverse terms, and escaped spaces.
  `{fuzzy}` and `--sort=score` use the same 0..100 score, which
  composes across an expression: AND keeps the weakest required match and OR the
  best successful alternative. Scores from different patterns are therefore
  comparable at the same quality threshold and predicate order does not choose the ranking accidentally.
  `--sort=score` rejects mixed models or thresholds rather than silently choosing between absolute similarity and
  distance above each matcher-specific threshold.

- Add the removable `@xff_asar` reader for Electron application archives. It exposes packed files,
  external `.asar.unpacked` members, directories, and links through the ordinary archive VFS; validates
  string offsets, bounds, and SHA-256 whole-file/block integrity metadata; and never invents metadata
  records as files. Container readers now compose through a deterministic multi-reader registry, so
  ASAR remains independent of libarchive and can coexist with every existing archive format.
- `--pack-option=@FILE.json` loads the existing writer-option vocabulary from a JSON object. String,
  integer, and boolean values map to the same validation and backend translation as inline
  `NAME=VALUE` options; file and inline forms compose in command-line order, so the last value for a
  name still wins.
- Add the removable `@xff_brotli` extra. It extends `@xff_archive` with streaming `.tar.br` / `.tbr`
  packing and traversal and raw `.br` single-file traversal, without letting libarchive absorb the
  feature accidentally. New archives use the self-identifying RFC 9841 framing format by default;
  `--pack-option=framing=raw` produces legacy RFC 7932 streams, and the reader accepts both.
- Extend the archive extra's native filter coverage to lzip/LZMA (`.tar.lz`, `.tar.lzma`, `.tlz`),
  LZ4 (`.tar.lz4`), and Unix compress (`.tar.Z`, `.taZ`) for both name-gated traversal and packing.
  Their single-file forms are recognized too. These codecs were already compiled into libarchive;
  the public suffix and writer tables now expose them consistently, and shorthand formats accept
  the same `level`/`threads` options as their long forms. Remove the internal `.br` claim because
  libarchive has no Brotli filter. Read filters now use an explicit standalone allowlist, and a
  committed Brotli-compressed tar proves the base extra cannot silently absorb a future libarchive
  Brotli implementation that belongs behind the separate Brotli extra.
- Native phar `.phar/stub.php` is explicitly part of the ordinary member model, including fields
  and statistics. A stored member at that reserved path wins over the synthetic stub, preserving
  one entry per path; help also distinguishes archive-write permission from an operation that uses it.
- `--archive` / `-z` dives for real in a build with the archive extra
  (`--//xff:xff_archive`): a container is visited as the file it is and then descends like
  a directory, so its members are ordinary entries at `container!member` paths that
  `-name`, `-type`, `-size` and friends match unchanged. `--archive=roots` (the xff-family
  default) dives only an archive named as a search root; `--archive=all` (`-z+`, or bare
  `--archive`) also dives archives met mid-walk, at the position a directory of that name
  would take under every `--sort`. `-prune`, `-quit` and `-maxdepth` apply to members as
  they do everywhere else. Without the extra, asking for diving remains a hard error.
- Under `--archive=all`, a file the walk meets is opened only if its NAME looks like a
  container (`.tar`, `.tgz`, `.zip`, `.jar`, `.phar`, ...), so walking a source tree no longer
  offers every `.cc` and every binary to the reader. `--archive-any` drops the gate, for an
  archive whose name says nothing. A file named on the command line is always opened.
- `--archive-separator` / `--archive-prefix` now reach the walk, so printed member paths
  round-trip through the flags that produced them.
- Everything that reads an entry works on members: `-content`, `-icontent`, `-rxc`,
  `-grep`, `-hash`, `-hasheq`, `{hash}` and `{lines}` read a member's bytes out of its
  container, so a member's digest equals the digest of the same bytes on disk.
- `--archive-depth=N` bounds how many CONTAINERS deep diving goes (default 1, so an
  archive inside an archive stays a plain member). A nested container has no path of its
  own, so its bytes are read out of its parent and mounted from memory; `-grep` and the
  rest then work at any depth. The cap is its own knob rather than part of `-maxdepth`,
  because nesting is where a decompression bomb lives.
- A plain text file is no longer mistaken for an archive. libarchive's "every format"
  set includes `mtree`, a magic-less text format, so `xff notes.txt` could report a
  bogus member; the reader now enables its formats explicitly and leaves `mtree` out.

- Native PHP phar archives dive: the mount path tries libarchive and then the phar reader,
  so a `.phar` lists its members and content predicates search them - including members
  compressed with deflate or bzip2, which the reader inflates itself (a phar compresses the
  member, not the container, so no libarchive format or filter applies). A member whose bytes
  do not match its declared uncompressed size is reported as corrupt rather than truncated.
  A native phar also exposes its executable PHP stub as `.phar/stub.php`, matching the real member
  used by tar- and zip-based phars. A stored member at that path wins without duplication; the
  synthetic stub is readable and searchable but cannot be deleted because the native format
  requires it.

- A write action on an archive member is refused instead of quietly doing nothing: `-delete`,
  `-exec`, `-execdir`, `-ok` and `-okdir` report that the member is read-only (exit 2, naming
  the path) or skip it under `--skip-unsupported`. Previously `-delete` exited 0 having deleted
  nothing and `-exec` handed the child a `container!member` path it could not open. The
  container itself is a real file, so actions on it still work.

- A compressed single file dives: `notes.txt.gz` presents one member named `notes.txt` (the
  name `gzip -d` restores) with its uncompressed size, and content predicates read it. A text
  file merely named `.gz` is not claimed, and `.tar.gz` is still read as the archive it is.
- A whole-file-compressed phar (`.phar.gz`, `.phar.bz2`) shows its members: the container is
  decompressed first and then offered to the readers again, so what is inside decides.

- `--//xff:xff_all` turns on every composable extra at once. Each extra links when its own
  flag or that one is set, so `--config=xff_docs` (which the committed reference is generated
  from, and which must document the full surface) is a single line instead of one per extra
  plus a comment asking whoever adds the next extra to remember.

- MemorySanitizer is a hard CI gate on Linux. The instrumented C++ standard library it
  needs now comes from the toolchain itself (`toolchains_llvm`'s `--features=msan` plus a
  prebuilt instrumented-libc++ overlay) instead of a cmake/ninja build of the LLVM
  runtimes in CI, so the cell is a plain `bazel test` and needs no suppression file.
- The hermetic LLVM toolchain moved to 22.1.8, matching that instrumented libc++.

# 0.2.0

Sharded-file awareness, hash verification, and a help system that is generated end to
end from one source of truth. The complete, always-current reference is
[XFF.md](XFF.md).

A sharded set (`data-000-of-003`, `data-001-of-003`, ...) is one logical file, and xff
can now treat it that way instead of listing every shard.

- `--shards[=auto|of|dotnum|underscore,...]` collapses each set to a single entry.
  `auto` (the bare flag) recognizes every built-in scheme: `<stem>-<i>-of-<n>`,
  7-Zip-style `<stem>.<NNN>` volumes, and `<stem>_<NNN>`.
- `--shards-show=first|wildcard|count` picks how a collapsed set is displayed - the
  representative shard's path, the masked name (`arc.???`), or the masked name plus the
  shard count. Incomplete sets are annotated, so a missing shard is visible rather than
  silently ignored.
- `--shards-dedup=first|mtime|error` decides what happens when two files claim the same
  index: keep the lexicographically first, keep the newest, or fail.
- `--shard-pattern=REGEX` registers a custom scheme for layouts the built-ins miss.
- `{shard}` renders the number of shards in the set, and the size / statistics fields
  aggregate across the whole set, so `--summary` and `--histogram` count logical files.

- `-hasheq EXPECTED` is true when the file's digest equals EXPECTED, a `{field}` template
  rendered per entry. `-hasheq {def.SUMS}` checks a manifest value and
  `! -hasheq {def.SUMS}` selects drift or corruption. `-hasheq:ALGO[/ENCODING]` shares the
  `-hash` spec grammar (sha256 / hex by default); hex comparison folds case.
- `--summary=hash` groups matches by digest, so identical files collapse into one bucket
  and the count column reads as a duplicate report.

- `--time-zone-suffix=auto|always|never` controls whether a time preset renders its
  trailing zone offset. Formats whose zone is part of their identity (`zulu`,
  `zulu-dense`, `asn1z`) always keep their `Z`.
- ASN.1 `GeneralizedTime` presets: `asn1` (also spelled `generalizedtime`) is
  `YYYYMMDDHHMMSS` in local time with an optional offset, and `asn1z` is the UTC form
  with a mandatory `Z`.

- `--help`, `--help=TOPIC`, `--man` and `--markdown` are now rendered from a single help
  document built out of the flag and expression registries, so they cannot drift from
  each other or from the implementation.
- New topics `--help=environment` (every variable xff reads) and `--help=help` (how the
  help system itself is organized), alongside the existing topic set.
- Plain help is colorized, word-wrapped with correct indentation, and respects `--width`
  as well as the terminal size.
- Valued flags document their values as an aligned table instead of an unreadable inline
  synopsis.
- `--pager[=auto|always|never]` (with `--no-pager`) pages the long documentation surfaces
  and never the file listing; `--man` is formatted through a roff formatter first, so it
  reads like `man xff`.

- `--summary` no longer prints a size column when nothing size-worthy was aggregated: a
  count-only summary now reports just the count.
- A topic's flags are no longer listed twice in the full reference.

- Sanitizer and lint coverage grew: clang-tidy runs as a hard CI gate over the whole
  tree, and MemorySanitizer joins AddressSanitizer and ThreadSanitizer on Linux (built
  against an instrumented libc++).
- Environment access is centralized behind one read-once, mutex-guarded cache, and flag
  values share a single option-value parser.

# 0.1.0

First release of xff (eXtended File Find): a `find(1)`-compatible file finder
with modern extensions. This is the basic tool - the core is implemented,
tested, and verified in CI on Linux and macOS.

Everything `find` does works the same way. On top of that, xff adds content and
type matching, structured output, per-run summaries and histograms, file
hashing, diffing, and safe deletes, under the find / xff / xfd / rg flavors. The
complete, always-current list of what is supported is the generated reference in
[XFF.md](XFF.md); the roadmap and open design questions are in [TODO.md](TODO.md).

Notable features not yet built (see [TODO.md](TODO.md)):

- archive diving (`--archive`)
- sharded-file handling
- hash verification (files can be hashed; checking a tree against a manifest is
  not done yet)
