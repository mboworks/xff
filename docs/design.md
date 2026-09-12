# xff - Design Document

> **xff** - _eXtended File Find_: a massively improved Linux `find`.
> Status: **Draft** · Started 2026-06-07 · Org: MBO Works

## Purpose

_One-paragraph summary of what xff is and why it exists. (To refine once the goals/features list is complete.)_

## Design Principles

**Decision hierarchy - where to look for each answer, in order:**

1. **Match `find`.** Anything `find` already does, we do (defaults, syntax, semantics). Drop-in is the contract.
2. **Match the closest prior art.** For functionality beyond `find` that we integrate (content search ← grep/ripgrep; ignore files ← ripgrep/fd; …), follow the established convention as far as is reasonable.
3. **Invent our own** - only when neither `find` nor prior art gives a reasonable answer.

**Overriding constraints (everywhere past the `find`-compat surface):** _logical above all_, then _self-consistent_ (never contradict our own established conventions). Illogical or self-inconsistent prior art is **not** adopted - e.g. shell `set +x` polarity was rejected in favour of chmod-style suffix signs `-x` / `-x+` / `-x-`.

**Escalation:** anything that cannot satisfy the above is flagged for detailed discussion - never decided silently.

**Known edge (confirmed):** `find` is occasionally illogical itself (operator-precedence traps, `-delete` implies `-depth`, default `-print`, exit codes). On the **drop-in surface fidelity wins** - faithful compatibility _is_ the logical requirement there - and the clean/logical version is offered as **our own default/extension** (modern behaviour, promoted via config).

## Goals

_What xff must achieve. (Captured from Marcus's list - discussion deferred.)_

- Full support for modern `find` command lines - the complete expression/predicate set, including `-exec` and friends (drop-in compatible with how people use `find`).
- Run on macOS and \*nix (Linux + BSD). Windows is not a priority.
- **Security & safety by default - a prime goal.** _Security_ (against untrusted input: traversed configs, archives, remote sources) is paramount and never traded away. _Safety_ (guarding the user from accidental destructive operations) governs xff's own surface and defaults; on the drop-in `find` surface, find's behaviour is preserved for fidelity (a documented sharp edge, mitigated by visible warnings). All safety/security mechanisms are **self-documenting** - surfaced in flag help, `--explain`, and docs so users see them and learn the model.

## Features

_Concrete capabilities. (Captured from Marcus's list - discussion deferred.)_

- Layered config system: per-user (global) **and** per-directory/local files that cascade like `.gitignore`, so complex argument sets and controls can be declared where they're relevant instead of retyped on the command line.
- Multiple regex flavors - only the relevant ones: **PCRE**, **RE2**, and **C++** (`std::regex`) for now.
- `.gitignore`-respecting behavior that is both configurable (via the config system) and controllable from the command line (toggle + scope).
- Support for multiple search roots in a single invocation.
- Content filtering: match on file **contents**, including via regex and **negative / inverted regex** (select files whose contents do - or do **not** - match).
  - **Shipped vocabulary (2026-06-28):** `-content` / `-icontent` match a **literal** substring (the `i` form folds ASCII case); `-rxc` / `-irxc` match an **RE2 regular expression** (unanchored, the content counterpart of `-regex`'s whole-path match). Binary files are skipped by default (NUL-byte sniff over the first 8 KiB, rg-style). All four are xff extensions (`--config=find` rejects them) and `Cost::kExpensive`. The name `-grep` was deliberately rejected **for the predicate**: grep's BRE/ERE/`-P` flavor baggage makes it ambiguous for a matcher, so the `-contains` / `-grep` placeholders used elsewhere in this document are superseded by `-content` / `-rxc` for the _filter_.
    - **Match-output action - `-grep` (shipped, #79).** The rg-style line output is an **action**, not a predicate: `-grep PATTERN` prints each content line matching an RE2 regex as `path:line:text` (grep's piped form), skipping binaries, and returns true iff it printed a line (grep-like exit). As a _pattern-less-flavor output action_ the earlier `-grep` objection dissolves - the flavor lives in `--regextype` (shipped: `RE2` regex default / `EXACT` literal; grep-`MATCH` and `PCRE` reserved, #85), not the name. It is self-contained (its own pattern, composable with the full find predicate set: `-mtime -7 -grep TODO`), so `-content`/`-rxc` stay untouched as file filters. Value over piping to grep/rg: find-grade file selection + one-pass fusion with xff actions. **`-grep:FORMAT`** overrides the default output with a field template, adding `{line}` (number), `{text}` (the matched line), `{match}` (the matched substring, grep `-o`), and `{column}` (its 1-based start) to the entry vocabulary (`{path}`/`{name}`/...). **`--count` / `-c`** (rg `-c`) makes `-grep` print one `path:count` per matching file (its matching-line count) instead of the lines. Deferred: `--regextype` modes beyond RE2/EXACT (grep-`MATCH`/PCRE, #85) and context (`-A/-B/-C`). Also still to come for the filters: negative content match (`! -rxc`/`-content`), `--max-contentsize`, `--all-text`, and input `--encoding`.
- File classification: match by media type. **Shipped:** `-mime GLOB` (xff) matches the media (MIME) type derived from the extension against a shell glob - `-mime 'image/*'` selects png/jpeg/... at once; an unknown/absent extension is `application/octet-stream`. The lean core carries common types, the removable `mime-db` extra supplies a comprehensive MIT-licensed vocabulary, and repeatable `--mime-vocabulary=FILE` JSON layers provide deterministic local mappings and metadata (`--mime-conflicts=error|first|last`). The `{mime-*}` fields expose the selected type, category, description, charset, compressibility, and source. This remains name-based (does not inspect content or gate on `-type`); content classification is a separate next layer.
- Language classification is likewise a canonical-record vocabulary with derived exact-filename and
  longest-suffix indexes. The lean core carries common languages; the removable, Brotli-compressed
  GitHub Linguist extra adds the comprehensive MIT-licensed records without inflating the lean
  binary. Repeatable `--lang-db=FILE` layers and
  `--lang-conflicts=error|first|last` control local mappings. Filename matching remains
  case-sensitive, suffix and language matching fold case, aliases match the canonical record, and
  `{lang}` retains its canonical spelling. Linguist's content, modeline, and shebang heuristics are
  intentionally not reproduced.
- Ability to dive into archives - descend into archive files (tar / zip / …) and search within them as if they were directories.
- Statistics / aggregation: collect stats over the matched set - total file size, file counts, and similar.
  - Per-file-type breakdowns: counts and sizes grouped by file type / extension.
- Sharded-file awareness: recognize sharded file sets (e.g. `f-000-of-003`, `f-001-of-003`, `f-002-of-003`) and collapse them into a single entry - either a wildcard form (`f-???-of-003`) or a representative shard (`f-000-of-003`).
- Output formats - pluggable renderers over one structured result model:
  - **Machine:** NUL-delimited (`-print0`), JSON / JSONL (object per hit + summary object), CSV / TSV.
  - **Human:** plain list (default, colorized on TTY), aligned monospace columns, GFM **markdown tables with vertically-aligned source**, tree / grouped.
  - Stats reuse the same tabular renderers (per-type breakdowns, top-N, histograms render as aligned / markdown tables).

## Cross-cutting concerns

_Accepted._

- **Safety:** dry-run + `--confirm` required for any destructive action (delete / move / overwrite); safe defaults.
- **Determinism:** parallel traversal is unordered - offer `--sort` / deterministic mode for reproducible output (tests, diffs).
- **`--explain`:** print the parsed predicate tree / execution plan for a query (debugging + teaching the expression syntax).
- **Shell completions:** generate bash / zsh / fish completions.

## Tech / Implementation

_Captured from Marcus's list - discussion deferred._

- Language: **C++23** (consider **C++26** if it materially helps parallel / thread management).

## Non-Goals

_Explicitly out of scope._

- First-class Windows support (may happen to work, but not a target).
- In-place content replacement / sed-like editing (and similar content-mutating abilities) - **at least for now** (revisitable).

## Open Questions

- ~~**Scope:** pure file-finder vs. find + integrated content search?~~ **Resolved** (by the content-filtering feature): xff is a **find + content-search hybrid**. To discuss: this puts xff in `ripgrep`/`sift` territory as well as `fd`/`find`'s.
- ~~**External / remote filesystems (e.g. SFTP):** in scope?~~ **Resolved as planned:** the VFS seam was built (`xff/vfs`, local backend today; archive diving adds the second backend). Remote backends (SFTP / object stores) stay **post-v1** - the seam makes them additive, so nothing in the current surface has to anticipate them.

## Notes / Decisions

_Discussion outcomes land here after the list is complete._

### Output rendering

- **Two deliberate output modes (by design, not a limitation):**
  - _Un-buffered / un-aligned (streaming)_ - plain / JSONL / NUL / CSV emit row-by-row: low latency, constant memory, ideal for pipelines and huge result sets.
  - _Buffered / aligned_ - aligned-column / markdown-table / stats collect the full result set to compute column widths, then render. Alignment is inherently buffered, so the un-aligned streaming mode is the intentional escape hatch.
- **Display-width correctness:** vertical alignment uses grapheme-cluster + East-Asian-width (`wcwidth`) math, not byte / codepoint counts - required for CJK / emoji / combining marks. (Matches the project rule that tables must be vertically aligned.)
- Right-align numeric columns; GFM alignment markers (`:--`, `--:`); escape `|`, newlines, and control chars for markdown / CSV (JSON / NUL are binary-safe).

### CLI grammar & parser

- **Shape:** `xff [--long globals] <dir…> [find expression]`. The **first directory is a semantic mode switch** - order-independent globals before it, position-dependent (left→right, short-circuit) find expression after. `--` is the explicit boundary tiebreaker (and for paths starting with `-`/`+`).
- **Dialect:** GNU-`find` canonical; BSD/macOS globals also accepted (`-H/-L/-P`, clustered `-EXdsx`, `-f path`).
- **Parser = its own modular, parse-only library:** `argv → AST` (globals, roots, expression tree) + diagnostics; no traversal / IO. Driven by a **declarative grammar registry** (one descriptor per option/predicate/action: name, region, arity, dialect, cost-tier, purity, toggle-style). The registry is the single source of truth - the parser, `--help`, completions, `--explain`, and the cost-warning are all _derived_ from it.
- **Left region:** custom getopt-style pre-scan (clustered shorts + `--long` + boundary). Abseil is **not** used here (can't cluster shorts / attached values). **Right region:** recursive-descent over the find expression.

### Evaluation & optimization

- **Strict left→right evaluation** (exact find semantics, short-circuit `-a`/`-o`); **no reordering by default**.
- **Cost-aware advisory warning** (stderr, on by default, suppress via `--no-warn` / find's `-nowarn`): fires when an _expensive_ predicate (opens/reads a file - `-contains`, archive dive, hashing) precedes a _cheaper_ one **within the same `-a` conjunction**. Never suggests moves across `-o`, `( )`, or actions. May print the concrete fix - it is the always-on lite slice of `--explain`.
- **Opt-in `--optimize`** (semantics-preserving reorder of pure tests within conjunctions) is **post-v1**, gated by `--explain`.

### Flag / toggle conventions

- **Long:** `--X` / `--no-X` (hyphenated). Separator-less `--noX` **deferred** (ambiguous with flag names starting "no").
- **Short toggles:** chmod-style **suffix signs** - `-x` (bare = default), `-x+` (on), `-x-` (off). Prefix `+` **rejected** (shell `set` polarity is cursed/illogical).
- **Clustering:** a `+`/`-` binds to the immediately-preceding sign-accepting toggle (`-Hg+x` = H, g·on, x); signs valid only on registered toggles. `+` is **not** a leading sigil (so `+foo` paths need no escaping).
- **Provenance:** every toggle stores `enum{unset, …}` (not `bool`) so CLI > config > default resolves; _unset_ ≠ _off_.

### Ignore / filter family

_All default to find-compatible (ignore nothing / show everything); a user's config promotes to modern behaviour._

- **`.gitignore` stack** (nested gitignores + `.git/info/exclude` + global `core.excludesFile`): `-g` / `--gitignore`, ternary - bare `-g` = **auto** (respect iff in a git repo), `-g+` = on, `-g-` = off; unconfigured default = **off**. _Shipped (full stack): per-directory nested `.gitignore`, the bare-`-g` auto ternary (git-repo detection via `xff/filesystem/repo`), the `-g+` / `-g-` (= `--gitignore=on` / `=off`) short spellings, and - when in a repo - a repo-root-anchored walk that honors ancestor `.gitignore` above the search root, then `.git/info/exclude`, then git's global `core.excludesFile` (else `~/.config/git/ignore`) at the bottom._
- **`.ignore`** (generic, non-git): `--ignore-files` (long-only).
- **`.xffignore`**: our own tool-specific ignore file.
- **Master "show everything":** `-u` / `--no-ignore` (disables _all_ ignore-file processing; rg/fd convention).
- **Explicit patterns:** `--exclude <glob>` / `--include <glob>`.

### Dotfiles vs hidden (distinct concepts)

- **`--dotfiles`:** name begins with `.` (portable; Linux's only "hidden" notion). Default **shown**; `--no-dotfiles` / `-…-` to suppress. **Long-only** (`-d` is find's depth-first).
- **`--hidden`:** OS attribute - macOS `UF_HIDDEN` (`chflags hidden`), Windows `FILE_ATTRIBUTE_HIDDEN`. Default **shown**. Distinct from `--dotfiles`; inert on Linux.

### Case sensitivity

- **Name matching:** find per-predicate `-iname` / `-ipath` / `-iregex` (case-insensitive variants); case-**sensitive** default (tier 1).
- **Content matching:** `-i` / `--ignore-case` (grep/rg, tier 2). Optional global `--ignore-case` default, per-predicate overridable.

### Security & safety (review #2)

A **prime goal** (see Goals). Two strands:

- **Security** (untrusted external input - configs, archives, remote sources): always paramount; no `find`-compat tension since find lacks these features.
- **Safety** (the user's own destructive ops): paramount on xff's own surface. On the `find` drop-in surface, find primaries keep find behaviour (e.g. `-delete` deletes) for fidelity - but xff emits a **visible, suppressible safety warning** (stderr; behaviour unchanged, so scripts are unaffected). xff's _own_ destructive actions are guarded (dry-run + `--confirm`); a global `--dry-run` is always available; find's `-ok`/`-okdir` are honoured.

**Config system (detailed spec):** [`design-config.md`](design-config.md) is authoritative. System
`/etc/xff.ini` contains unsectioned globals and plain named sections; trusted global controls govern
skipping automatic files, accepting explicit files, and arming dangerous config directives. User
config and explicit `--xffrc=FILE` files share the same INI grammar and parsed representation.
Each name is declared once per file and may be refined by other files; `--config=NAME` composes
configurations. `--config=NAME` and the invocation name
select configurations. Invalid global lines are ignored individually with diagnostics;
invalid named sections are disabled atomically and transitively. There is no special policy-section
language, general `--feature` mechanism, or auto-discovered project config layer.

**Safe mode:** opt-in `--safe` (a.k.a. `--no-destructive`) **hard-refuses** destructive (`-delete`, future built-in mutators) and dangerous (`-exec`/`-execdir`/`-ok`) operations - distinct from `--dry-run` (which previews). Off by default (preserves drop-in), but a cautious user sets it as their personal default in user-global config; override per-invocation with `--no-safe` (CLI > config). Granular `--no-exec` / `--no-delete` available. Ideal as a CI guardrail. Refusals self-documenting.

### Virtual entries: archives & remote (review #3)

Archive members and remote files have **no real filesystem path**. The VFS tags each entry by source - `real-fs` / `archive-member` / `remote` - with a **read-only** flag; everything downstream branches on it.

- **Representation:** machine output (JSON) uses structured fields (`container` + `member`); human output uses the JAR-style marker `container!member`. The separator doubles as a relative/absolute indicator: `pkg.tar!foo/bar` (relative member, normal) vs `pkg.tar!/foo/bar` (**absolute** stored path - unusual, and exactly the Zip-Slip red flag → flagged). `!` needs shell-quoting and is escaped if it occurs in a real name.
- **Actions are FS-only.** Virtual entries are read-only ⇒ excluded from `-delete`/`-exec`/`-execdir` with a self-documenting skip ("read-only archive member, action skipped [safety]"). **Tests/matching/printing/stats do apply** (content matching reads member bytes). Extract-to-temp for `-exec` is opt-in, **post-v1**.
- **Diving is opt-in** (find-compat: an archive is one file). The control is the ordered enum `--archive[=none|roots|all]` with the short `-z-` / `-z` / `-z+` (bare `--archive` = `all`); `find` defaults to `none`, the xff-family flavors to `roots` (a named archive root dives, mid-walk ones need `all`). When on, members enumerate as virtual entries. **Size = uncompressed (logical)** in human output; compressed exposed in JSON. Stats label uncompressed totals (which can dwarf disk usage).
- **Security (untrusted input - prime goal):** decompression-bomb limits (max expansion ratio / total bytes / member count / nesting depth); Zip-Slip sanitization (reject/flag `..`-escaping and absolute member paths); never follow archive symlinks out of bounds.
- **Streaming:** virtual entries are sequential (no `mmap`); content + negative-match (#6) stream/decompress fully. Remote = network stream.
- **Remote (SFTP, deferred) uses the same model:** `remote` source, read-only, URI scheme (`sftp://host/path`), actions FS-only.

### Regex engines (review #4)

- **Two engines only; `std::regex` dropped** (slow, stdlib-inconsistent, ReDoS-prone, no unique capability). The ECMAScript-familiar syntax C++ devs expect is offered as a PCRE2 flavor, not a separate engine.
- **RE2 = default** - linear-time, Unicode, ReDoS-immune (the safe default per the prime goal); no backrefs/lookaround.
- **PCRE2 = opt-in power engine** (backrefs, lookaround) with **configurable safety limits** - match limit / backtracking-depth limit / heap limit (`pcre2_set_match_limit` / `set_depth_limit` / `set_heap_limit`). Sane default bounds ReDoS; raise or lower via flags + config. Limit-exceeded errors self-document ("PCRE2 match limit exceeded - pathological pattern, or raise `--pcre-match-limit` [safety]").
- **Explicit selection; no silent fallback** - opt into PCRE2 with `--regextype=PCRE2` (the one grammar selector, shared with RE2 / ERE / EXACT / FNMATCH / GLOB / SHGLOB); a pattern needing PCRE2-only features under RE2 → clear error + suggestion, never a silent switch. A build without the PCRE2 extra linked says so self-documentingly.
- **find's `-regex`/`-regextype` (its own grammar; tier 1)** backed by **RE2 via grammar translation** (BRE/ERE/egrep/awk → RE2), PCRE2 for exotic/Emacs constructs; cross-platform-consistent, fidelity validated by the Phase-1 conformance suite, deviations documented.
- Patterns are **not portable** across engines (documented).

### Output encoding - non-UTF-8 paths & content (review #5)

POSIX paths are byte strings, not guaranteed UTF-8. The shipped behavior is
format-specific rather than one uniform encoding mode:

- **Plain** writes path bytes verbatim by default. `--path-encoding=escape`
  C-escapes backslashes and control bytes; printable bytes at and above `0x80`
  still pass through unchanged. The flag affects plain output only.
- **NUL / `-print0`** writes exact path bytes followed by NUL and is the reliable
  byte-faithful interchange format.
- **CSV** applies RFC 4180 quoting; **TSV** escapes its delimiters and
  line-breaking controls; **JSONL** escapes JSON syntax and ASCII controls;
  **Markdown** escapes its cell delimiters. These transforms preserve their
  record structure but do not validate or transcode non-UTF-8 high bytes.

Consequently, a non-UTF-8 path can still make JSONL or Markdown invalid as text.
A future policy may add strict, lossy, or explicitly byte-encoded structured
output, but no such mode is currently implemented. Any choice must make the
representation detectable rather than silently changing a string's meaning.

### Content-match cost (review #6)

- **Negative content match (`! -contains …`) is intrinsically slow** - proving absence needs a full read (no early-exit, unlike positive match). Top cost-tier; feeds the ordering warning (put a cheap `-type f`/`-name` first to short-circuit most files).
- **Literal prefilter mitigates:** for a required-literal, _absent_ → conclude "no match" after one fast SIMD scan (regex skipped); _present_ → run the regex. Pure-literal negation = a single memchr-class sweep.
- **`--max-contentsize`** caps how much of a file is read for content matching (named to avoid confusion with a size _filter_ / find's `-size`). A file over the cap is **excluded from content tests** - never silently asserted to "not contain" the pattern (no claiming absence on unread bytes). Self-documenting skip.
- **Users are told it's slow:** `--explain` shows it, and an advisory note fires when negative content matching runs over a large set (same self-documenting ethos as safety).
- Worst case = negative match over archive/remote (full decompress / network read;
  #3 limits apply). Directory listings may be read ahead, but expression and
  content evaluation remain on the single coordinator thread.

### Parallel exec (review #7)

- **`-exec ... ;` / `-execdir ... ;` concurrency follows `--jobs`.** At
  `-j 1` each child runs synchronously and its status can gate the expression to
  its right. At `-j N` with `N > 1`, up to `N` children may be outstanding and
  the action reports success on launch because the status is not known yet. The
  persona-scoped default worker count therefore also chooses this behavior when
  `-j` is omitted.
- **`-ok`/`-okdir` always stay serial** because interactive prompts cannot run
  concurrently. The `... +` forms remain end-of-walk batches and propagate
  failures independently of the semicolon-form runner.
- **Concurrent children inherit stdout and stderr directly.** xff does not buffer
  or reorder their output, so it may interleave. Use `-j 1`, redirect inside the
  command, or make each child emit atomically when output ordering matters.
- Strict synchronous find action semantics require `-j 1`; sorting is a separate
  traversal-order choice.

### macOS / cross-platform correctness (review #8)

- **Name-match strictness - `--exact` (shipped #45; decided 2026-06-28).** The
  default is the **filesystem-native, naturally-expected** behavior: the xff style
  matches `-name` / `-path` the way the entry's own volume resolves names - **case
  folding on a case-insensitive volume** (APFS / HFS+ in their default config, NTFS,
  exFAT), byte-exact on a case-sensitive one (ext4 and friends) - so a lookup the OS
  would satisfy case-insensitively also matches here. **`--exact` opts out**, forcing
  verbatim byte-exact comparison regardless of the FS; the **find style is always
  byte-exact** (drop-in faithful), as is the conservative in-process default. Backed
  by a `vfs` per-volume `IsCaseSensitive` probe (`pathconf(_PC_CASE_SENSITIVE)` on
  macOS/BSD; conservative case-sensitive fallback on Linux and when unprobeable). The
  `-iname` / `-ipath` variants fold regardless; regex keeps its own `-iregex`.
  - **Scope is case only for now.** Normalization (NFC/NFD) and fuzzy/similarity
    matching were an earlier sketch (`--exact+` byte-exact / `--exact-` fuzzy);
    they are **deferred** - the shipped flag is the boolean case opt-out above, and
    the tri-state `+`/`-` spellings can layer on later if a concrete need appears.
- **Birthtime** (`-Btime`/`-Bmin`/`-Bnewer`, BSD-compat; GNU lacks them): backed by `st_birthtime` (macOS/BSD) / `statx(STATX_BTIME)` (Linux ≥4.11).
- **Impossible tasks fail by default:** a predicate that can't be evaluated correctly on a given kernel/FS (e.g. `-Btime` where birthtime is unrecorded) → **hard error**, self-documenting, naming the unsupported path/FS. Opt-in `--skip-unsupported` downgrades to warn-and-skip. (Rule: **fail when correctness is impossible; warn when degraded-but-correct.**)
- **FSEvents (macOS) vs inotify/fanotify (Linux):** the Phase-4 index/watch abstracts behind a platform interface (deferred).
- All platform-specific metadata (btime, normalization/case caps, watch) lives behind the VFS/platform layer; the engine stays platform-agnostic.

### VFS capability and host-adapter boundaries

The VFS has two deliberately different contracts. Consumers use the small capability-oriented
`vfs::FileSystem` interface (`ReadDir`, `Stat`, `ReadLink`, content reads, access checks, and
capability-scoped writes), with `Status`/`StatusOr` results and no descriptors or platform types.
Write capabilities include creating temporary files, writing/replacing bytes, truncating, and
removing entries where xff explicitly needs them. Each backend implements only the capabilities it
can provide; unsupported operations return structured errors.

The local backend keeps a separate internal host adapter for POSIX, `std::filesystem`, and platform
metadata primitives, including file creation and mutation. That adapter is the only place where
host handles and C APIs appear, keeping
annotations and portability code at the immediate boundary. Archive, memory, and remote backends
never need to expose those implementation details, and engine code cannot accidentally bypass the
VFS by opening a host path directly.

### Exit-code model (review #9)

One consistent table; the mode decides whether "no-match" is reachable:

- `0` - success (and, in match-sensitive mode, ≥1 match).
- `1` - **no match** (only reachable in match-sensitive mode).
- `2` - error (bad args, fatal IO, impossible predicate per #8).

- **Default = find semantics:** match status doesn't affect exit (`0` if it ran, `2` on error; `1` never returned) - drop-in faithful. `2`-for-error satisfies both find's ">0 on error" and grep's "2 = error".
- **Match-sensitive exit is opt-in:** `-q`/`--quiet` (grep-style - suppress output, exit by match) and/or `--exit-match` (keep output, exit by match); only then is `1` reachable.
- **Per-file access errors** (e.g. permission denied): traversal continues (like find), final exit `2` (unless softened by the error policy); fatal errors abort with `2`. Details to stderr.

### Binary detection & content encoding (review #10)

Content-matching path only (`-contains`/`-grep`); the find side is unaffected.

- **Binary files: skipped by default** for content matching (NUL-byte heuristic, rg-style - content search is for text). **`--all-text`** (long-only; `-a` is _not_ reused - it clashes with the expression operator `-a`/`-and`) forces treat-as-text; skipped-binary counts surfaced via `--explain`.
- **Input content encoding: UTF-8 by default**, with BOM auto-detect + UTF-16 transcode (rg-style). **`--encoding` / `-E <enc>`** forces an input encoding for BOM-less content; undecodable content → matched as raw bytes (best-effort) or specify `-E`.
- **Name disambiguation:** `--encoding`/`-E` = _input content_ decoding (rg-aligned, the expected meaning); the #5 _output_ encoding is renamed **`--path-encoding`**. Two distinct concepts, two distinct names.

### Sharded files (review #11, #84)

**Off by default; opt-in `--shards`** (find sees the separate files, so the default stays drop-in
faithful). v1 is **display + stats only**: `--shards` collapses a shard set into one logical entry
for _listing and aggregate stats_, while matching and actions stay per real shard (find-faithful
underneath, no ambiguous group-exec). A **reassembled-content view** (so `-grep` / `-content` /
`-hash` / `-size` see the concatenated whole) is a **v2 built on the #83 archive vfs backend**, where
it is most useful (a split archive reassembles, then dives); it is out of scope for v1.

**Sharding is not rotation.** A shard set is _parts of one logical whole_ (concatenate / belong
together). Rotated logs (`app.log.1`, `app.log-20260101`) and storage segments (SSTables, WAL) are
_independent_ files, not parts of one entity, and are deliberately **out of scope** - the detector
must never fold them into a set.

**A shard scheme varies along six axes**, and the model expresses all of them:

1. **Index position** - suffix (`.001`), infix before the extension (`.part1.rar`, `-s001.vmdk`), or
   replaces the extension (`.z01`).
2. **Index form** - numeric (with pad width) or alpha base-26 (`aa`); **0-based or 1-based**.
3. **Declared total** - present (`-of-010`, enables completeness) or absent (contiguity only).
4. **Special boundary member** - a differently-named first (`.rar`) or last (`.zip`) volume.
5. **Trailing opaque suffix** - a generation / attempt id excluded from shard identity (see dedup).
6. **Ordering rule** - numeric ascending, alpha ascending, or "special member sorts first / last".

**Capture vocabulary** (one engine for built-ins and custom patterns; a scheme is defined by these
named captures):

| Capture | Meaning                                                                                      |
| :------ | :------------------------------------------------------------------------------------------- |
| `stem`  | the logical base name shared by every member; identifies the set                             |
| `index` | the shard's position within the set (numeric or alpha; padding / base is part of the scheme) |
| `total` | optional declared count; when present, drives completeness validation                        |
| `dup`   | optional opaque tail (e.g. a generation hash); **excluded from identity**, drives dedup      |

Logical shard identity is `(stem, index, total?)`; the set identity is `(stem, total-or-scheme)`.

**The optional tail (`dup`) is a cross-cutting modifier, not its own scheme.** Any scheme may carry an
optional trailing tail after the shard name so the same logical shard can be _regenerated_ under a new
name (redundant generators race, the first to finish wins and the stragglers are killed). The tail is
described by **one regex** carrying **exactly one capturing group** - that group is the opaque `dup`
(excluded from identity); everything _outside_ the group is literal context that must match as-is, so
the separator lives inside the regex rather than being a separate optional knob. The default is
`` `\.([0-9a-fA-F]{8,})` `` (a literal dot then a run of at least 8 hex digits, matching the common
Google 16-hex id) - deliberately conservative, so an ordinary extension (`.tfrecord`, `.parquet`) is
_not_ mistaken for a tail. `--shard-tail=REGEX` overrides it (base64, a fixed width, a `_gen-` prefix,
...); an empty value disables the tail; a custom `--shard-pattern` instead sets it per-scheme via its
`dup` capture. The tail is matched against the string immediately after the shard number, and any
extension after it is preserved separately (it belongs to the set, not the tail). So
`data-00000-of-00010` and `data-00000-of-00010.a1b2c3d4e5f6a1b2` are the same logical shard
`00000-of-00010`; the tail never enters identity or completeness.

**Built-in catalog** (`--shards[=auto|SCHEME,...]`, default `auto` tries all):

| Scheme        | Example                                         | Notes                                            |
| :------------ | :---------------------------------------------- | :----------------------------------------------- |
| `of`          | `data-00000-of-00010[.<tail>]`                  | TF/TFRecord; 0-based, declared total, opt tail   |
| `part`        | `part-00000`, `part-r-00000-<uuid>.parquet`     | Spark/Hadoop; optional `r-`/`m-` infix, no total |
| `webdataset`  | `shard-000042.tar`, `imagenet-train-001281.tar` | bare zero-padded numeric suffix before ext       |
| `split-alpha` | `xaa`, `prefixab`                               | `split(1)` default; alpha base-26                |
| `split-num`   | `x00`, `prefix01`                               | `split --numeric-suffixes`                       |
| `dotnum`      | `foo.tar.001`                                   | 7-Zip volumes / generic; 1-based, min-3-digit    |
| `underscore`  | `foo_001`                                       | numeric underscore suffix                        |
| `zip-split`   | `arc.z01, arc.z02, …, arc.zip`                  | **special last member** `.zip` (axis 4)          |
| `rar-old`     | `arc.rar, arc.r00, arc.r01, …`                  | **special first member** `.rar` (axis 4)         |
| `rar-part`    | `arc.part1.rar` / `arc.part001.rar`             | index infix before ext, 1-based                  |
| `vmdk-split`  | `disk-s001.vmdk`                                | index infix `-sNNN` before ext                   |

The special-boundary schemes (`zip-split`, `rar-old`, `rar-part`) are **detected** in v1 (cheap); the
_reassembly_ that makes a split archive divable is the post-#83 v2.

**Custom pattern** (full-control escape hatch): `--shard-pattern=REGEX`, repeatable, using the named
captures above, e.g.

```
--shard-pattern='(?<stem>.*)-(?<index>\d+)-of-(?<total>\d+)(?:\.(?<dup>[0-9a-f]+))?'
```

**Policy flags** (representation and dedup are orthogonal to enabling / scheme selection):

- `--shards-show=wildcard|first|count` - display the set as a wildcard `f-???-of-003` (default), a
  representative real path, or just a count.
- `--shards-dedup=first|mtime|error` - resolve the `dup` case (the same logical shard generated more
  than once by redundant generators): keep the first-seen, the newest, or flag it as an error.
- `--shard-tail=REGEX` - override the opaque-tail regex for the built-in schemes; exactly one
  capturing group (the `dup`), everything else literal (so the separator is in the regex). Default
  `` `\.([0-9a-fA-F]{8,})` ``; an empty value disables the tail.
- `-shard-status complete|incomplete|superfluous` - a deferred expression test over the physical
  files reaching that node. Valid representatives inherit their logical set's complete/incomplete
  state; redundant same-index copies and indices outside a declared total are superfluous. The
  cohort is per-directory and deliberately respects predicates to the left, while tests/actions to
  the right run only after classification. Without `--shards` this exposes every selected physical
  file; `--shards` remains the independent logical display mode.

**Completeness is surfaced, not hidden:** when the name encodes a total (`-of-MMM`), a set missing
members is flagged - `f-???-of-003 (2/3 - INCOMPLETE)` (a data-validation feature). Completeness
counts **distinct indices**, never files, so redundant `dup` copies never mask a gap. With no declared
total, contiguous runs collapse and detectable gaps are flagged. An index outside a declared total
is excluded from the logical set and surfaced as superfluous, so it cannot make the set complete or
inflate its aggregate size.

**References** (real-world naming conventions surveyed for the built-in catalog):

- Multi-volume archives overview: <https://www.file-extensions.org/article/useful-information-about-multi-volume-archives>
- WinRAR volumes (`.partN.rar`, `.rNN`): <https://documentation.help/WinRAR/HELPArcVolumes.htm>
- 7-Zip volume naming (`.001`): <https://sourceforge.net/p/sevenzip/discussion/45797/thread/6599d448/>
- ZIP split volumes (`.z01` … `.zip`): <https://en.wikipedia.org/wiki/ZIP_(file_format)>
- WebDataset sharding (`shard-NNNNNN.tar`): <https://rom1504.github.io/webdataset/sharding/>
- PyTorch large-dataset I/O: <https://pytorch.org/blog/efficient-pytorch-io-library-for-large-datasets-many-files-many-gpus/>
