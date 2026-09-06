<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# 0.3.4

- Restore the four directly downloadable platform binaries and additionally publish each platform's
  debug bundle as a level-19 Zstandard-compressed `.tar.zst` archive; do not publish `.tar.gz`
  duplicates.

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
