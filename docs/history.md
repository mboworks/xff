# Development history

Resolved design records and investigations moved out of the actionable backlog. For current work, see
[`TODO.md`](../TODO.md).

This preserves settled decisions, shipped implementation notes, and investigations whose context
would otherwise be lost when the actionable backlog is simplified.

The former combined roadmap and completion ledger through PR #683 is retained separately in
[`history-roadmap.md`](history-roadmap.md).

## Resolved decisions

- **INVESTIGATED (2026-09-06): mold does not justify changing xff's Linux release linker.** The candidate was
  tested on native Linux AArch64 under Colima, with Bazel 9.2.0, hermetic Clang/LLD 22.1.8, and the
  official mold 2.40.4 binary. Both linkers consumed the same warmed non-LTO object cache; an explicit
  build ID forced real link actions, and each result was staged before switching configurations. The
  staged binaries identified the expected linker and passed `--version` and `--help` smoke checks.

  | Configuration       | LLD 22.1.8 | mold 2.40.4 | mold change |
  | :------------------ | ---------: | ----------: | ----------: |
  | lean link critical  |     0.23 s |      0.95 s |     +0.72 s |
  | full link critical  |     0.43 s |      0.44 s |     +0.01 s |
  | lean stripped       |  2,149,456 |   2,147,664 |    -1,792 B |
  | full stripped       |  4,728,304 |   4,725,456 |    -2,848 B |
  | combined `.tar.zst` |  3,794,872 |   3,875,217 |   +80,345 B |

  This non-LTO comparison isolates ordinary ELF linking but is not xff's release configuration.
  The actual `--config=release` build uses ThinLTO. Mold reaches that link and then fails because
  Clang supplies `LLVMgold.so`, which is absent from the downloaded LLVM 22 distribution.
  LLVM's documented external-linker path requires building a matching gold plugin from the LLVM
  source tree; LLD consumes the bitcode natively. Supplying that plugin would therefore expand the
  proposed linker bootstrap into an LLVM bootstrap as well.

  The BCR `mold` module exposes a normal `cc_binary`, not a registered C++ linker toolchain. Adding
  that target to `toolchains_llvm`'s `extra_linker_files` would make it available to link actions but
  introduces a toolchain cycle unless mold itself is built with a distinct bootstrap toolchain. That
  remains a potentially useful optional `toolchains_llvm` feature for projects that benefit from
  mold, but xff's measurements do not justify implementing it here: ordinary links were no faster,
  compressed distribution size regressed, and the real release link needs another large bootstrap.
  Xff retains LLD and proceeds with independently measured LLD size optimizations.

- **FIXED (2026-08-13): an unknown VALUE on a known global was silently ignored.** `--color=bogus`,
  `--sort=bogus`, `--case=bogus` and `--pager=bogus` all exited 0 and behaved as the default. It was
  uniform, so it read as a design choice rather than one flag's oversight - but it was the opposite
  of the choice #102 made for unknown flag NAMES ("unknown option" is a usage error precisely so a
  typo cannot be silently ignored), and the failure mode is worse for values: `--case=insensitve`
  matched case-sensitively and the run looked like it worked.
  - Both obstacles are resolved rather than worked around. A flag now DECLARES how its value is
    checked (`GlobalFlag::ValueCheck` = none / enum / bool / tri-state), so the tri-state flags keep
    the whole shared vocabulary while the enumerated ones check against their own `values` table -
    the same table the help prints, so the error and the documentation cannot disagree. And the check
    runs in ONE place in main, over the parsed globals, rather than inside each resolver: that is
    what lets it cover `--color` / `--width` / `--pager`, which are scanned from raw argv before the
    parse and have nowhere to report from. `kNone` stays the default, so free-text flags (paths,
    formats, regexes, comma lists that validate themselves) are untouched.
  - Precedent for the strict side already existed: `--skip-vcs`, `--diff-format`, `--diff-algorithm`
    and `--timezone` each rejected an unknown value, which is what made the silence elsewhere an
    inconsistency rather than a policy.
  - Writing it turned up three flags that accept MORE than they document (`--color-scheme`'s alias
    spellings, `--format=md`, and the reserved `--regextype=MATCH`, whose own error is better than a
    generic one). `ValueDoc.hidden` marks those: accepted by the check, omitted from the listing, so
    one table stays the source for both.

- **FIXED (2026-08-13): `on` / `off` were documented but not accepted.** `--time-zone-suffix`'s help
  listed `on` / `off` as synonyms of `always` / `never`, and the shared value parser did not accept
  them, so `--time-zone-suffix=off` silently kept the offset. `--gitignore` had the mirror problem: it
  compared the two literal strings `on` / `off` itself instead of using the shared parser, so
  `--gitignore=yes` silently did nothing and `=auto` was not accepted at all. Both now go through
  `values::ParseTristate`, which gained `on` / `off` - the spelling a switch-shaped flag reads best
  in, and the one the help had been promising.

- **DECIDED (user, 2026-08-13): colour comes from ONE resolved palette, `ls`-derived by default, and
  `-ls` uses it too.** Three statements, one design:
  1. if `xff .` colourises, `xff . -ls` must colourise as well - the same run colouring one and not
     the other is the bug;
  2. the colours should be _the ones `ls` uses_, i.e. `$LS_COLORS` (dircolors) is the DEFAULT source;
  3. a user may want xff's own scheme back - and, symmetrically, someone who likes the `ls` palette
     wants the plain listing to use it too. So the palette is a run-wide CHOICE, not a per-action one.
  - **Shape:** `--color-scheme=ls|xff` (spelling to confirm), resolved once per run beside
    `--color=WHEN`, and consulted by every colourised surface (the plain listing, `-ls`, and any
    future tabular output). `ls` means: parse `$LS_COLORS`, fall back to xff's built-in type scheme
    for anything it does not specify (and entirely, when the variable is unset); `xff` means the
    built-in scheme regardless. Default `ls`, per (2) - which is a BEHAVIOUR CHANGE for anyone whose
    `$LS_COLORS` differs from xff's scheme, and is the point.
  - **`-ls` colours the NAME column only,** as `ls -l` does; the metadata stays plain. Safe by
    construction: the path is the last column, so ANSI escapes cannot disturb the computed widths.
  - **Work:** a dircolors parser (the `key=value:` list: the two-letter type keys `di` / `ln` / `ex`
    / `fi` / `so` / `pi` / `bd` / `cd` / `or` / `mi` / `su` / `sg` / `tw` / `ow` / `st` / `ca`, plus
    the `*.ext=` per-extension entries, which xff's scheme has nothing equivalent to), the
    `$LS_COLORS` env var documented (the `check-env-documented` hook enforces it), the flag, and the
    `-ls` wiring. Two slices: the palette (parser + flag + plain listing), then `-ls`.
  - **SHIPPED** as `--color-scheme=auto|ls|merged|xff` (`auto` default; `ls+xff` / `ls-or-xff` /
    `default` name `auto`, `ls-and-xff` names `merged`), with `-ls` colouring the name column.
  - **`$LSCOLORS` (BSD / macOS) is read too, SHIPPED.** A macOS user who themes `ls` has BSD's
    variable and no dircolors setup at all, so reading only `$LS_COLORS` made "the colours ls uses"
    false on that platform. It is 11 fixed-position fg/bg letter pairs (`di` `ln` `so` `pi` `ex` `bd`
    `cd` `su` `sg` `tw` `ow`), letters `a`..`h` for the eight ANSI colours, uppercase = bold
    foreground / bright background, `x` = terminal default. `$LS_COLORS` WINS when both are set,
    being the richer format. Two consequences worth remembering: a value of any other length is
    ignored WHOLE (position is the key, so a short value would shift every later type by one) and
    then counts as no theme rather than an empty one; and BSD has no per-extension entries and no
    `fi` slot, so `merged` is the interesting scheme there - `ls` alone leaves every regular file
    plain.

- **BUG (help text): `-grep` context has only its LONG flags (raised 2026-08-13, FIXED).**
  `--context=SPEC`, `--after-context=N` and `--before-context=N` all work; the single-dash `-A` / `-B`
  / `-C` that #99 reserved were never added, and the flag help cited grep's spellings ("grep -A",
  "grep -C/-A/-B") in a way that read as if xff had them. The help now says which spelling is xff's
  and which is grep's. Whether the shorts should EXIST is the open question below, because `-A` is
  also the proposed archive umbrella.

- **RESOLVED (2026-08-13, user): the archive shorts are TWO families - `-z` reads, `-Z` writes.**
  The sign ladder measures ONE axis (how much to look at) and the CASE carries the capability:

  ```
                     read only    + write (--archive-write)
    none              -z-          (error: -Z- contradicts itself)
    roots (default)   -z           -Z
    all               -z+          -Z+
    any               -z++         -Z++
  ```

  This replaces the earlier `-z++` = "all, and writable", which mixed the axes: adding a `+` must
  never arm a destructive capability (the same principle #73 records for `--feature`), and a slipped
  shift key must change which axis you asked for, not both. It also frees the top read rung for
  `any` = `all` without the name gate (the older spelling `--archive-any` stays as a hidden alias),
  which is the "look everywhere, inside everything" convenience #185 asked for.
  - Arming is not doing: a `-Z` run still needs an action that writes (`-delete`, or `-exec` over an
    extracted copy), and `--safe` / `--dry-run` still apply. That is what makes case-as-capability
    acceptable here; if arming alone could destroy something it would need a whole word.
  - **Later wins per AXIS, and the axes stay independent (user, 2026-08-14).** `-z+ -Z++` widens the
    rung to `any` and arms writing; `-z++ -Z` narrows back to `roots`. A lower-case form never
    disarms, so `-Z++ -z-` means "member writing armed, reading OFF". Permission alone does nothing:
    with diving enabled, member `-delete` / exec operations consume it; with diving off, it is only
    meaningful alongside a creation sink such as `--pack`. `--pack` does not require `-Z++`, because
    creating a new ordinary archive is not mutation of an existing archive member; the combination
    instead states the useful "create without harvesting existing containers" intent. `-Z-` is therefore not an
    error (the earlier draft refused it) but the full RESET: reading off and writing disarmed,
    overriding an earlier flag or a config file. Its disarm is only observable once reading is turned
    back on (`-Z -Z- -z`), which is how the test pins it.
  - `-A` / `-B` / `-C` stay free for the grep family, as before. `-z*` stays rejected (a bare `-z*`
    errors in zsh and silently expands in bash), and the ladder stops at `++` in both families.
  - `--archive-depth` is deliberately NOT part of any rung: raising the decompression-bomb cap is a
    different decision from looking in more places.

- **Short primaries `-n` / `-p` for `-name` / `-path` (SHIPPED 2026-08-20; review reaffirmed).**
  These are xff shorthands, not borrowed fd spellings: fd's `-p` means `--full-path` and fd has no
  `-n`. The reason to keep them is narrower and stronger than "shorten popular words": name and path
  are the fundamental, symmetric basename/whole-path glob scopes, and both are used frequently in
  long expressions. Exact-token parsing means `-p` is not ambiguous with `-print`, `-prune`, or
  `-printf`, and each alias resolves to its canonical descriptor so behavior, style/cost metadata,
  and generated help cannot drift.

  **This is not a general one-letter-primary namespace.** `-r` / `-x` do not uniquely identify a
  regex grammar or operation, `-c` collides conceptually with content and count, and `-f` could mean
  file type. Add no such aliases mechanically; a future short needs its own compelling,
  unambiguous compatibility or usage case.

- **fd's `-g` / `--glob`: RESOLVED in `--help=styles`.** fd matches its single
  positional pattern as a REGEX by default; `--glob` switches that one pattern to glob semantics.
  xff has no positional pattern - the choice IS the primary (`-name` / `-path` glob, `-regex` /
  `-rxc` regex, `-regextype` to pick the grammar) - so the flag has nothing to switch. Also `-g` is
  already xff's gitignore toggle, and `--glob` would collide conceptually with `--regextype=GLOB`.
  Nothing was added to the grammar. The "Coming from fd" table in `--help=styles` maps fd's
  positional regex and `-g / --glob` forms to xff's explicit `-regex` / `-name` primaries and explains
  that the primary itself selects the grammar.

- **fzf-style scoring for `-fuzzy` (completed 2026-08-19).** fzf ranks with a Smith-Waterman-ish
  alignment score and takes an extended pattern syntax (`^prefix`, `suffix$`, `'exact`, `!negate`,
  space = AND, `|` = OR). Two separate questions:
  - **Where the pattern ends is NOT a problem.** fzf's query is itself ONE argument whose terms are
    space-separated inside it (`fzf --query "^src .cc$ !test"`), so the xff spelling is
    `-fuzzy '^src .cc$ !test'` - exactly the quoting `-name '*.cc'` already needs, with the term
    grammar living inside the token. An UNQUOTED multi-term form would need an `-exec`-style `;`
    terminator, which for a matcher reads worse than quoting and would be the only primary in the
    vocabulary to work that way.
  - **Shipped:** `-fuzzy:fzf` implements that extended query grammar, and the same primary selects
    `sequence`, `levenshtein` (`edit`), or `shingles`; an optional `:PCT%` gates each model. The
    implementation is pinned by upstream fzf's documented compound queries, its complete compound
    parser case, anchor whitespace behavior, and maintained algorithm cases.
    Scoring was the real work, and it is the same decision #168 records: a score implies an output
    ORDER. `--sort=score`, the alignment search, and exact expression-level `-top N` are shipped.

- **A shortcut for "all archive features": SHIPPED in the `-z` / `-Z` ladders.** `-z++` means read
  all containers without the name gate (the former `--archive=all --archive-any` mouthful), while
  `-Z++` selects the same read rung and separately arms writes. `--archive-any` remains the older,
  longer spelling. `--archive-depth` deliberately stays outside both ladders because raising the
  decompression-bomb cap is a separate decision from looking in more places.

- **An in-memory filesystem for unpacking (raised 2026-08-13).** What it can and cannot do, because
  the answer splits:
  - **An IN-PROCESS filesystem cannot serve `-exec`** - a child needs a path the kernel resolves - but
    a MOUNTED memory filesystem can, and one is usually already there (user, 2026-08-13):
    - **`/dev/shm` (and `$XDG_RUNTIME_DIR`) are tmpfs on Linux (SHIPPED),** so extracting there is
      memory-backed with a real path and no disk write. A temp-DIRECTORY choice, not new machinery:
      `ChooseExtractDirectory` tries `$XDG_RUNTIME_DIR`, then `/dev/shm`, then the ordinary temporary
      directory, and takes a candidate only when the member fits in a quarter of the space it reports
      free - a tmpfs is RAM shared with the whole machine, so a large member still lands on disk. macOS
      has no default equivalent and falls straight through; an `hdiutil attach ram://` disk would be a
      heavyweight per-run setup and is not attempted.
    - **`memfd_create` + `/proc/self/fd/N`** avoids a filesystem entirely and is seekable, but any
      tool that reopens the path by NAME or keys on the extension breaks, and it is Linux-only. Not a
      default; at most an opt-in for pipelines known to cope.
    - **A FUSE mount of the CONTAINER is the DIRECTION (user, 2026-08-13):** the fuse-archive /
      archivemount shape, where every tool gets a real path into the archive and nothing is extracted
      at all. See the dedicated item below; the shipped temporary file stays the portable fallback, and
      the cheap interim improvement is choosing a tmpfs directory for it where the platform has one.
  - **It already serves nesting.** A container inside a container has no path of its own, so
    `OpenContainerBytes` hands the inner reader the bytes its parent read - an in-memory container in
    all but name. Same for the phar rewrite, which is built in memory and written once.
- **DIRECTION (user, 2026-08-13): mount a container with FUSE (`@xff_fuse`), rather than extracting.**
  A mounted container is the answer the extraction flags approximate: `-exec`, `-execdir` and any
  external tool get a real path INTO the archive, `{}` renders as that path, no copy is made, and the
  "your child edited a copy" surprise disappears (a read-only mount makes an in-place editor fail
  honestly instead of succeeding against a temporary file). It is also the only shape that makes a
  member usable by a tool xff never launched.
  - **Our own FUSE server, not a shell-out to `fuse-archive`.** Delegating looks cheaper but loses
    exactly the formats xff added: fuse-archive reads what libarchive reads, so a native phar, a
    prefixed payload and the compressed-single-file case all fall out - and xff's reader already
    handles them behind `vfs::FileSystem`. A FUSE server over that interface serves every container xff
    can open, by construction, and keeps one notion of member paths.
  - **Shape:** a build-time extra (`--//xff:xff_fuse`, the `@xff_archive` pattern), libfuse on Linux and
    macFUSE on macOS (a kernel extension the USER installs, so the extra must degrade to extraction
    when it is absent rather than fail). Read-only first; a writable mount would be how `-delete` on a
    member and an in-place editor could work later, and it is a separate decision.
  - **DECIDED (2026-08-13, user): explicit flag, in-process server, and the rest as proposed.**
    A mount is process-global state other programs can see, it needs a user-installed kernel
    component on macOS, and not everyone will get it running - so it is an explicit `--archive-mount`
    rather than implicit-when-available, and the same command cannot behave differently on two
    machines by accident. The server runs in-process (a background FUSE thread) with one mount point
    per RUN under `$XDG_RUNTIME_DIR/xff/<pid>/` (else `$TMPDIR`), one subdirectory per container,
    read-only; unmount by RAII at exit plus a signal handler (INT / TERM / HUP) that unmounts and
    re-raises, `fusermount3 -uz` / `umount -f` as the crash path, and a startup sweep of our own
    `xff/<pid>` directories whose pid is gone. Mounts do NOT nest (only the outer container is
    mounted; an inner one is read by xff's own reader, since mounting it would mean materialising its
    bytes), and the WALK does not read through the mount - it keeps using the VFS, so a mount failure
    can change what `-exec` can reach but never what xff finds. Revisit any of it if it proves wrong
    in practice.
  - **What was on the list to decide:** the mount lifecycle (mount per run under a per-pid
    directory, unmounted at exit AND on a signal, with `fusermount -uz` / `umount -f` as the crash
    path, since a stale mount is worse than a stale temp file); whether the mount is implicit when
    available or an explicit flag (a mount is process-global state other programs can see, which argues
    for explicit); how `--archive-depth` maps onto nested mounts; and whether the walk itself should
    read through the mount (simpler: it keeps using the reader, and only child processes see the mount).
  - **Relation to the shipped flags:** `--archive-extract` becomes the portable fallback rather than the
    only mechanism, and `--archive-aggregate` / `--archive-delete` are unaffected.
  - **BUILD PLAN (2026-08-15, epic #183; each bullet one PR).** The one open architectural choice is
    how the extra reaches libfuse, and the ratified "degrade when absent" semantics decide it:
    **dlopen at runtime against vendored API headers** (`fuse_lowlevel.h` interface, permissive
    license), probing `libfuse3.so.3` on Linux and macFUSE's `libfuse.2.dylib` on macOS. Rejected:
    system `linkopts = ["-lfuse3"]` (breaks the hermetic build AND makes absence a startup failure
    instead of a degrade) and vendoring libfuse as a third_party module (its build wants a
    configure-generated config.h per platform, and macFUSE ships its OWN libfuse fork, so a vendored
    Linux build still needs the runtime path on macOS - all cost, no reuse). dlopen is the only shape
    where one binary runs everywhere and mounting is a capability probed per machine. 1. **@xff_fuse skeleton + runtime loader (SHIPPED)**: the extra module (pcre2/archive pattern,
    picked up by `tools/extras.py --wildcards` automatically), `FuseLoader` dlopening the
    platform fuse3 library and eagerly resolving the mount server's symbol set (14 symbols
    since 3b added the direntry builder + readlink) - so "available" MEANS mountable - and
    `FuseAvailable()`. Tests are environment-AGNOSTIC (Linux
    CI images tend to have libfuse3, macOS does not): they pin the invariants of both states.
    fuse2-only installations (older macFUSE) report unavailable by design; revisit when a real
    macFUSE user appears. 2. **Mount lifecycle (SHIPPED, directory half)**: `MountRoot` owns the per-RUN root
    `$XDG_RUNTIME_DIR/xff/<pid>/` (else tempdir) with RAII removal, per-container mount points
    (basename + counter on collision), and `StaleRoots()`/`SweepStaleRoots(unmounter)` - the
    sweep reports and removes dead-pid roots, calling an INJECTED unmounter per mount point so
    the process-spawning `fusermount3 -uz` / `umount -f` crash path and the signal handler land
    with the server (slice 3), which owns actual mounts. All plain-filesystem, tested without
    FUSE. 3. **The read-only FUSE server over `vfs::FileSystem`**, split again on inspection - the fuse3
    ABI surface is the risk, not the callbacks: - **3a. The fuse3 ABI declarations (SHIPPED, then
    superseded).** The first slice fetched fuse-3.18.2's headers. The final design removed that
    build dependency: `fuse_abi.h` now carries only the declarations and structure prefixes xff
    actually uses, transcribed against that release. `FuseApi`
    is the typed call surface: every loader symbol cast ONCE (the funneled dlsym-contract
    NOLINT) into those local declarations. No LGPL code or text is in the tree; xff's own
    Apache-2.0 extension notice lands with
    the slice that links the extra into `xff_full`. - **3b. The server itself (SHIPPED)**: `FuseServer::Mount` serves any `vfs::FileSystem`
    read-only - lookup/getattr/readdir(+release)/open/read/readlink filled by NAME into the
    fetched `fuse_lowlevel_ops`, an only-grows inode table, whole-member content held per
    open handle (decode once, kernel reads in chunks), one loop thread per mount with RAII
    exit-unmount-join-destroy teardown; INT/TERM/HUP ask every live session to exit before
    re-raising. `CrashUnmount` (`fusermount3 -uz` / `umount -f` via posix_spawnp) is the
    unmounter for slice 2's sweep seam. Linux CI mounts a fake filesystem and reads it back
    through the kernel with `--config=xff_fuse_tests_required` so that path can never silently skip
    (test action unsandboxed - /dev/fuse and setuid fusermount3 do not exist in the
    sandbox); macOS exercises the degrade. 4. **CLI**, split: - **4a. Build + identity plumbing (SHIPPED)** (user-flagged 2026-08-16: fuse was absent
    from `--help=extras`): `--//xff:xff_fuse` + `xff_fuse_on` (`xff_all` coverage), xff_full
    links @xff_fuse's registration TU, the `xff_extras_api` fuse slot
    (`MountSupportAvailable`) feeds `ExtraEnabled("fuse")`/`kKnownExtras`/`ExtraBuildFlag`,
    the `--help=extras` row, and the xff FUSE extra NOTICE component (Apache-2.0; the host's
    libfuse/macFUSE implementation is loaded at runtime and is not a binary component; each binary's notice
    renders its linked set, while the committed NOTICE is generated from the all-extras binary). - **4b-0. Path vocabulary (SHIPPED)**: the server resolved a lookup by joining parent and
    name with `/`, which only a local filesystem understands - the archive VFS spells a member
    `container!member`. Lookup now asks the FILESYSTEM (`ReadDir` reports each child's full
    path in its own vocabulary) instead of assembling one, and the fake in the test uses the
    `!` spelling so a slash-assuming server cannot pass again. The claim itself is pinned
    against REAL bytes by `test_data/mini.tar` - a committed 3.5 KiB uncompressed tar of raw
    512-byte blocks (`hello.txt`, `sub/a.bin`) - in `archive_fs_test`: the reported paths use
    the separator, those exact paths are the ones `Stat`/`ReadContent` answer to, a
    slash-joined path is rejected as InvalidArgument, and directories INSIDE a container keep
    ordinary slashes (so a consumer splits once at the container boundary and never re-joins). - **4b. The flag (SHIPPED)**: `--archive-mount` serves a member from a read-only MOUNT of
    its container instead of a copy. The seam is a mount FACTORY in `xff_extras_api::fuse`
    (registered by @xff_fuse next to the linked-in slot, so a binary cannot advertise mounting
    it lacks); `engine::MountedContainers` mounts once per container, splits the member path
    once at the container boundary, and answers the mounted path. `ExecTargetPath` asks it
    before the extractor, so `{}` is a path INSIDE the archive for `-exec` / `-execdir` and no
    copy is made. Mounting is a per-machine capability: absent extra = the standard hard
    error, absent runtime library or no permission = one line after the walk plus extraction
    (which is what makes the flag safe in a config file); armed without `--archive-extract`
    and unable to mount, the action is refused with a message naming both ways out.
    `IsExtracted` now ASKS the extractor rather than inferring from "differs from the entry's
    path", so a mounted path is never handed to `Release`.
    - **4b root cause (FIXED)**: mounts aborted their connection on the first read on Linux
      (ECONNABORTED, request in flight, daemon alive, no teardown). It was a USE-AFTER-FREE, and
      ThreadSanitizer is what finally named it ("data race in `~ArchiveFileSystem()`"): the walk
      owned the container's reader in a DIVE-SCOPED `unique_ptr` while `MountedContainers` keeps
      mounts for the whole run, so once the walk left the container every FUSE callback served
      freed memory - and a garbage reply is exactly what makes the kernel abort a connection.
      That single fact explains everything the hunt found confusing: it failed on x86_64 CI while
      passing on aarch64, differed between two invocations moments apart, and moved when two
      unrelated `std::cerr` lines were deleted. All allocator timing.
      **The API allowed it**, so the fix is in the type, not the caller: `MountFactory` /
      `MountContainer` / `FuseServer::Mount` take `std::shared_ptr<const vfs::FileSystem>`, and
      the header no longer says "`fs` must outlive the returned Mount" - a promise no compiler
      checks, about an object served from another thread for a whole run. That comment WAS the
      bug, written down. Taking ownership also DELETED bookkeeping: `PathFor` no longer takes a
      filesystem reference plus a separate owner (two parameters that had to agree, unchecked),
      and the struct pairing each mount with its reader is gone. `mount_test`'s
      `AMountKeepsTheReaderAliveAfterTheCallerDropsIt` pins the property through a `weak_ptr`
      with no FUSE involved, so it runs on macOS too.
      Two earlier diagnoses were WRONG and are recorded as such: the libfuse pin (3.18.2 headers
      against a 3.14 runtime) and the mode bits were real improvements that fixed nothing here.
      The pin stays at 3.14.0 on its own merit - libfuse guarantees only BACKWARD compatibility,
      so compile against the OLDEST runtime we must support.
      What hid all of it: the extra's mount tests SKIPPED whenever the loader reported no fuse3,
      ignoring the required-FUSE test contract, so the whole kernel
      path reported green without running. An unavailable loader is now fatal where the
      environment promises one, and it prints the loader's reason.
    - **The lesson, made a command**: `tools/fuse_linux_test.sh` (functional / `tsan` / `msan`)
      runs the mounting tests on Linux from a mac before pushing, because macOS skips every one of
      them and `bazel test` therefore goes green without executing a line of the kernel path. Its
      comments carry the traps: TSan needs reduced ASLR entropy on aarch64, the repo's own
      `--config=tsan` pulls an x86_64-only toolchain, and `msan` must force an EMULATED x86_64
      container because the instrumented libc++ overlay ships x86_64-linux only.
    - **The msan cell also needed the sanitizer cells to know what machine they are on**: `tsan`
      and `msan` are standalone jobs, not matrix cells, so they never installed fuse3 or enabled
      `--config=xff_fuse_tests_required`, and a runner that CAN mount was failing a test that demanded the
      refusal message. Both declare it now. Every mounting test skips under MSan (the dlopened
      system libfuse3 is uninstrumented, so its bytes read back as uninitialized) - in C++ via
      `MEMORY_SANITIZER`, in the shell test via an `XFF_MSAN` env from a `select()` on the same
      flag. And the 1000-member `archive_fs_test` moved to `size = "medium"`: 1589 ms natively
      times MSan's origin-tracking factor lands right on the small (60s) cliff.
- **4b follow-up (SHIPPED 2026-08-16)**: mboworks/bashtest 0.6.0's `skip_test` makes the two
  MSan-guarded CLI mount cases real skips instead of successful tests that merely print a skip
  line; `--no-skip` is available where an environment guarantee must turn any skip into a failure.

- **Bounded member CACHE (SHIPPED).** `-grep`, `{hash}` and `-cmp` on the same member used to
  each decompress it again. `MemberCache` (`member_cache.{h,cc}`) is a mutex-guarded LRU with a
  64 MiB byte cap per open container - the cap is the decompression-bomb concern, so oversized
  content is served but never stored - consulted by `ArchiveFileSystem::ReadContent`. Built as a
  cache keyed by member (the container is the filesystem instance), not as a general in-memory
  VFS with no second customer.

- **Modern (non-`find`) default time format: resolved to `space`.**
  `space` (`2026-06-22 14:30:00 +0100`) is the default: human-first (it matches
  GNU `ls --time-style=long-iso`/`full-iso` and `git log --date=iso`), still ISO-
  ordered so it sorts lexicographically, and parseable back by `ParseTimeString`.
  `--time-format` (config phase D4b) makes this a soft choice rather than a
  lock-in: `rfc3339` (`2026-06-22T14:30:00+01:00`) is one flag
  (`--time-format=rfc3339`) or one `.xffrc` line (`common: --time-format=rfc3339`)
  away for interchange-by-default, and machine consumers use `--format=jsonl`.
  (find's `-printf %t`, once implemented (#48), uses `asctime` per find.)

- **`--timezone` scope and spelling.**
  Shipped (config phase D4a) as `--timezone=ZONE`: overrides the zone used both
  to _interpret_ time-string arguments (`-newerXt`) and to _format_ time fields
  (`{atime}`/`{mtime}`/`{ctime}`/`{btime}`). Accepts `local`/empty,
  `utc`/`z`/`zulu`, and IANA names (`America/New_York`); an unknown zone is a
  usage error. The companion `--time-format=NAME` selector shipped alongside it
  (config phase D4b), and `-printf` (`%a`/`%c`/`%t` + `%Ak`/`%Ck`/`%Tk`) and `-ls`
  both render in the zone (#48). Both follow-ups have now shipped (with the #70
  datetime growth): (a) the `--tz=ZONE` short alias of `--timezone=ZONE`; (b)
  fixed-offset specs (`+05:30`, `-0800`, `+01`), which `ParseTimeZone` builds via
  `absl::FixedTimeZone` since `absl::LoadTimeZone` cannot parse them.

- **Project `.xffrc` layer: resolved - dropped entirely (Option B, 2026-07-06).**
  Decided against any auto-discovered project config (no ancestor cascade, no subtree
  scoping); config is system + user + an explicit `--xffrc=FILE` only. This supersedes the
  earlier subtree-scoping question (now moot). Full record + the `--xffrc` arming restriction
  are in the roadmap tail below ("Config: drop the project `.xffrc` layer").

- **DONE (2026-08-20): use `:` for an attached expression-primary qualification and `=` for a
  whole-run global assignment.** The earlier investigation
  surveyed only double-dash globals and therefore missed the surface that actually motivates the
  change: xff's single-dash expression primaries with attached payloads.

  **Proposed grammatical boundary:**

  - `--flag=VALUE` assigns a whole-run global. This remains true even when VALUE has an internal
    grammar: `--define=NAME=VALUE`, `--histogram=BUCKET[:MEASURE]`, and
    `--pack-option=NAME=VALUE`. The first `=` belongs to the global grammar; everything after it is
    that flag's value. In particular, `--define` cannot lose both equals: its VALUE really is a
    `NAME=VALUE` definition, matching make / CMake / Bazel precedent.
  - `-primary:QUALIFIER OPERAND...` qualifies one expression node. The colon is attached to the
    primary because the qualification changes HOW that node consumes or presents its ordinary
    operand; operands themselves remain separate argv tokens. A primary with no ordinary operand
    can still be qualified (`-text:posix`, `-hash:sha256`) - the distinction is grammatical scope,
    not whether the payload can loosely be called a value.
  - A colon inside a global VALUE or a field expression remains that value grammar's separator. It
    is at a different structural level and is not ambiguous: `--histogram=size:count` assigns the
    VALUE `size:count`, while `-fuzzy:fzf:80% foo` qualifies one `-fuzzy` node. Likewise
    `%{size:h}` and `{field:s/pat/repl/}` are self-contained format languages.

  **Complete attached-primary inventory (all `Descriptor::binding` users):**

  | spelling                      | attached payload's role                         | following operand |
  | :---------------------------- | :---------------------------------------------- | :---------------- |
  | `-text:FLAVOR`                | text-definition flavor                          | none              |
  | `-collect:[!]NAME`            | collection identity / deliberate-reuse marker   | none              |
  | `-fuzzy:MODEL[:PCT%]`         | matcher model and threshold                     | PATTERN           |
  | `-fuzzypath:MODEL[:PCT%]`     | matcher model and threshold                     | PATTERN           |
  | `-ifuzzy:MODEL[:PCT%]`        | matcher model and threshold                     | PATTERN           |
  | `-ifuzzypath:MODEL[:PCT%]`    | matcher model and threshold                     | PATTERN           |
  | `-diff:STYLE`                 | output style / context                          | TARGET            |
  | `-hash:ALGO[/ENCODING]`       | digest algorithm and rendering                  | none              |
  | `-hasheq:ALGO[/ENCODING]`     | digest algorithm and rendering                  | EXPECTED          |
  | `-grep:FORMAT`                | per-match output template                       | PATTERN           |
  | `-capture:[!]NAME[=REGEX]`    | binding identity, reuse marker, extraction rule | `CMD... ;`        |
  | `-capturedir:[!]NAME[=REGEX]` | binding identity, reuse marker, extraction rule | `CMD... ;`        |

  Bare forms keep their existing defaults (`-text`, `-collect`, `-fuzzy PATTERN`, `-diff TARGET`,
  `-hash`, and so on). The `=` inside `NAME=REGEX` also remains: it describes the binding's value,
  just as the inner `=` in `--define=NAME=VALUE` does. Arbitrary payloads remain representable:
  splitting `-grep:{line}={text}` at the FIRST colon leaves `{line}={text}` untouched.

  **Everything else was checked and is outside this rule:** all double-dash valued options remain
  global assignments; find-native primaries take separate operands; `-jN` and the `-g+` / `-z++` /
  `-Z++` / `-s+` families are compact compatibility or level spellings, not bindings; `+N` / `-N`
  comparison prefixes live in primary operands; and `--help=license=NAME` is a meta-topic grammar,
  not an executable option assignment.

  **Migration:** this is a hard pre-1.0 switch for the 12 primaries, rather than retaining `=` aliases
  indefinitely. Accepting both would preserve the very mixed grammar this decision removes, double
  the documented/tested surface, and make `=` look like a supported convention for future primaries.
  Old forms fail with a focused diagnostic naming the colon spelling. The registry binding parser,
  AST, generated help, cookbook, tests, config policy classification, examples/docs, completion
  inputs, and `--help=styles` conventions section all use the new boundary.

## Sanitizer verification: what runs where

**MSan CONFIRMED on x86_64 Linux, 2026-08-16: 124/124 Bazel test targets passed with no
MemorySanitizer findings.** Nine mounting cases were skipped by design (6 in `fuse_server_test`,
1 mount-factory case in `fuse_register_test`, 2 CLI `--archive-mount` cases), for the reason in
"Why every mounting test skips under MSan" below. Everything that does NOT cross into the
uninstrumented system libfuse ran and passed: archive parsing, the FUSE loader and API logic,
mount lifecycle, registration, and the ownership tests.

So the honest statement of coverage is: **no MSan defect exists in our own code, and real FUSE
kernel-mount behaviour is not exercised under MSan at all.** That is not a hole left open - it is
the deliberate boundary of what MSan can say about a dlopened library it did not instrument. The
kernel path has its own unsanitized and TSan coverage (CI, plus `tools/fuse_linux_test.sh`).

### Reproducing it

An **x86_64** Linux machine with `/dev/fuse` (an ordinary VM is fine; `sudo` only for two
packages). aarch64 will NOT do - see "why" below. Then:

```sh
sudo apt-get install -y fuse3 libfuse3-3    # the runtime the loader dlopens + the unmount helper
git clone https://github.com/mboworks/xff && cd xff
# The MSan cell (this is the command that was run):
bazel test //... $(tools/extras.py --wildcards) --config=xff_docs \
  --config=clang --config=msan --config=xff_fuse_tests_required

# The TSan cell, same shape (already green in CI, cheap to confirm):
bazel test //... $(tools/extras.py --wildcards) --config=xff_docs \
  --config=clang --config=tsan --config=xff_fuse_tests_required
```

Expected: all tests pass, with every MOUNTING test reporting SKIPPED under `--config=msan`
(see below for why that is correct rather than a cop-out).

### Why an x86_64 host specifically

MSan false-positives on anything it did not instrument, so the C++ standard library must be
instrumented too. `--config=msan` gets that from toolchains_llvm's `msan` feature, which swaps in
the instrumented libc++ overlay fetched by `MSAN_LIBCXX_URL` in `bazelmod/llvm.MODULE.bazel` - and
that overlay is published for **x86_64-linux only**. There is no aarch64 build to fetch.

Emulation was tried and does not work. `tools/fuse_linux_test.sh msan` forces a
`--platform linux/amd64` container, which on Apple silicon means qemu; the build gets as far as
linking and then dies with `clang: error: unable to execute command: No such file or directory`

- the toolchain's linker cannot run under the emulation layer. That mode is left in the script
  because it is correct on a real x86_64 host, and its comment says so.

### What IS verified locally, and how

`tools/fuse_linux_test.sh` (from a mac, via colima/docker) exists because **macOS has no fuse3, so
every mounting test skips there and `bazel test` goes green without executing a line of the kernel
path**. Two modes work on aarch64:

- `tools/fuse_linux_test.sh` - the fuse tests plus the CLI mount test, sandboxed as CI runs them.
- `tools/fuse_linux_test.sh tsan` - the CLI mount test under ThreadSanitizer. Needs `--privileged`
  and `sysctl vm.mmap_rnd_bits=28`: aarch64 TSan aborts with "unexpected memory mapping" under the
  default ASLR entropy. It drives the sanitizer through the container's gcc rather than
  `--config=tsan`, because the repo's config pulls the x86_64-only hermetic clang.

Note the container needs `git` installed (MODULE.bazel pulls toolchains_llvm and
hedron_compile_commands through `git_repository`, so a cold cache cannot even compute the repo
mapping) and roughly 25 GB of free VM disk for the hermetic LLVM.

### Why every mounting test skips under MSan (and why that is not a dodge)

A mount runs through the **dlopened system libfuse3**, which MSan did not instrument, so every byte
libfuse writes reads back as uninitialized and the process dies inside `fuse_opt_parse`. There is
nothing to fix in our code: the report is about libfuse's memory, not ours. The skips are therefore
deliberate and keyed off `--config=msan` in one place per language:

- C++: `#if defined(MEMORY_SANITIZER)` (the macro `--config=msan` defines) in `fuse_server_test`
  and in `fuse_register_test`'s factory case.
- Shell: an `XFF_MSAN` env var, set by a `select()` on `//xff:xff_msan_enabled` in
  `xff/cli/BUILD.bazel`, read by `_skip_under_msan` in `xff/cli/full_extras_test.sh`.

To check the guards compile and fire without an MSan toolchain at all:
`bazel test @xff_fuse//... --config=xff_full --copt=-DMEMORY_SANITIZER` (the factory case must
report SKIPPED and the rest must pass), and
`bazel test //xff/cli:full_extras_test --config=xff_full --//xff:xff_msan=true` (both mount cases
must print the skip line, and must NOT print it without the flag).

### Things that bit us, so nobody re-derives them

- **A skipped test looks exactly like a passing one.** `@xff_fuse//:fuse_server_test` reported
  "PASSED in 0.1s" on Linux CI while skipping every mount, because the required-FUSE check covered
  only mount FAILURE, not "no fuse3 here". The named Bazel config now makes both paths fatal and
  carries the same contract into the C++ and shell tests.
- **`tsan` and `msan` are standalone jobs, not matrix cells.** They did not install fuse3 or opt into
  `--config=xff_fuse_tests_required`, so a runner that CAN mount was failing a test that demanded
  the refusal message. Any new sanitizer job must make the same declaration.
- **MSan makes tests far slower.** `archive_fs_test` reads all 1000 members of `many.tar.gz` in
  1589 ms natively; under MSan's origin tracking that lands past the `small` (60s) budget, and it
  timed out with every assertion still holding. It is `size = "medium"` for that reason - a timeout,
  not a sanitizer finding.
- **Pipes hide exit codes.** `pre-commit run clang-tidy | grep -c warning` reports grep's status and
  discards the gate's; a gate script piped to `tail` reported success while aborting on an unbound
  variable. Capture output, then read the real exit code.

## Result-set shaping: -first, -top, -collect, --max-results (design of record, 2026-08-17)

Ratified with the user. Supersedes the `--top=N` / `--fuzzy-cutoff` sketch: **the first three are not
globals.** They are expression primaries, because the interesting behaviour is per-invocation and
positional, which a whole-run flag cannot express. `--max-results` is the aggregate exception.

### The insight that shapes everything else

A test that returns FALSE removes the entry from EVERYTHING downstream - the summary, the
histograms, the count, every action. So a truncating test can never also mean "summarise all of it
but show me a few": by the time the summary runs, the entries are gone. That is not a wrinkle to
work around; it is why `-collect` exists.

### The vocabulary

- **`-first N`** - a TEST, true for the first N entries it sees, false after. Stateful, and that is
  fine: a test only owes a truth value, and keeping a counter is its own business. State is
  PER INSTANCE (keyed to the AST node), which is exactly what a global could not do:
  `xff . \( -type f -first 10 \) -o \( -type d -first 5 \)` is ten files AND five directories.
  Truth is immediate, so it streams and its early stop is trivially safe.
- **`-top N`** - a TEST that keeps the N best by the expression's normalized fuzzy score. Quality
  thresholds belong to the matchers (`-fuzzy:PCT% PATTERN`), so `-fuzzy:80% foo -top 10` reads as
  “the ten best good matches” without coupling selection and ranking in one argument. Multiple fuzzy
  tests compose through the boolean AST: AND takes the minimum (the weakest required match), OR the
  maximum (the best successful alternative), and normalized 0..100 percentages make different
  patterns comparable when their thresholds are equal. A ranking operation rejects mixed thresholds: absolute
  similarity and distance above each predicate's threshold imply different orders, so choosing either silently
  would be arbitrary. A bare fuzzy test counts as `0%`. Multiple `-top` instances each keep their own bucket.
  - **The contract is EXACT**: the final result is precisely the N best. The general expression case
    retains every candidate until the node is resolved: an entry that loses `-top` can still enter an
    `-o` branch to its right, so dropping non-winners from a bounded heap would change expression
    truth and skip downstream actions. A conjunction-only fast path MAY keep just a bounded heap in
    future, but it is an optimisation and must never leak into semantics. Bloom filters do NOT fit
    here: they cannot rank and their false positives would put wrong entries in the result (see the
    shingling entry, where they DO fit).
  - **Therefore `-top` is a deferral point**: it cannot answer until the walk ends, so everything to
    its RIGHT is evaluated in a second pass over the survivors. This is a feature, not a cost -
    `-top 10 -exec rm {} \;` then deletes exactly those ten, which was the reading the user had from
    the start. The cutoff is the optimisation that permits an early stop (once N entries clear it,
    the running top N IS the final top N), never the thing that makes it correct.
- **`-collect[=NAME]`** - an ACTION that adds the entry to a named collection for a later sink.
  `-collect:NAME` needs no new parser work: `-capture:NAME cmd \;` already uses `Binding::kLabelRegex`
  from #68, and `-text:posix` shows the plain optional value. The unnamed form is just the default
  name, so there is no special case for "the anonymous one". Duplicate NAME copies `-capture`'s rule
  verbatim: an error, with an explicit override - two named sinks silently merging would only show up
  as a wrong summary. It holds every matched entry, so it wants `--buffer`'s row/byte budget
  vocabulary rather than growing unbounded.
  - **Presence of any `-collect` switches what `--summary` reads** (the collection instead of "what
    matched"). That is the rule find already has for the implicit `-print`, which an explicit action
    suppresses - so it is a rule users have learned once here, not a new special case. Presence is
    SYNTACTIC, so a `-collect` in a branch that never executes still switches the source and the
    summary is then empty; consistent with implicit-print, surprising exactly once, so it needs an
    example in the docs rather than discovery.

### Order is what selects the reading

```sh
-collect -top 10 -ls --summary   # collect all, list the 10 best, summary over ALL
-top 10 -collect --summary       # collect only the 10, summary over those, and NO listing
```

The second prints nothing extra because `-collect` is an action, so the implicit print is
suppressed and nothing else asked to print. Summary-only falls out of the existing rule instead of
needing a `--quiet`. This is the whole reason these are primaries: a position-independent global
(AGENTS.md hoists `--` globals deliberately) makes both spellings identical and neither reading
expressible.

### `--max-results` - and when it is pointless

The one genuine global here: an aggregate output ceiling. Call out plainly that it is only
irreducible when MULTIPLE capped filters are active - with a single `-first 10` the cap already IS
ten, and with no capped filter it is just `-first N` spelled as a global. Its unique job:

```sh
xff . \( -type f -first 10 \) -o \( -type d -first 5 \) --max-results 12
```

Fifteen pass the filters; only an aggregate bound can say twelve. It caps OUTPUT and does not stop
the walk, because a summary or count that silently went partial is the same failure as the one above,
one level up; an early stop stays an explicit opt-in.

### Build order (by machinery, not preference)

1. **`-first N`** - immediate truth, no deferral, per-instance counter. Standalone. SHIPPED (#559).
2. **`-collect[=NAME]`** - an action; makes `--summary` read the collection. SHIPPED. Two decisions
   the design left implicit, both settled by building it: `--histogram` switches its source together
   with `--summary` (a run where one reduced the collection and the other reduced the matches would
   report two different totals for one walk), and the collection OWNS its entries rather than storing
   the walk's `Visit`, whose path/name/root are borrowed views and whose metadata is a reference. The
   entry also keeps `Visit::fs_owner`, so collecting an archive member cannot outlive its reader -
   the same lifetime bug ThreadSanitizer caught in `--archive-mount`. `--buffer` now bounds the
   collection (a row window or a byte budget over the stored path/name/root text), and exceeding it
   is an ERROR (exit 2, naming the flag) rather than a silent truncation - a summary computed over
   part of the walk is indistinguishable from a correct one, which is the same reason
   `--max-results` caps output without stopping the walk. The DEFAULT is deliberately no cap: any
   number picked here would be a guess, and the point of the budget is that a run which would
   exhaust memory says so instead. A measured default is the open question if one is ever wanted.

   **Name reuse: the `!` modifier, not an override flag.** A NAME is an identifier
   (`[A-Za-z_][A-Za-z0-9_]*`), which is what reserves punctuation for modifiers, and a node that
   reuses a name an earlier node took must say so with `!`:
   `\( -type f -collect:all \) -o \( -type d -collect:!all \)`. This replaced the
   `--collect-override` / `--capture-override` globals outright, and the reason is the same one that
   made these primaries rather than globals: a whole-run flag loosens EVERY `-capture` / `-collect` in
   the command, including the ones the author never thought about, while the modifier loosens exactly
   the node it is written on. `--capture-override` is GONE (it was never released), and `-capture`
   now takes `-capture:!NAME` too. Settled with the user 2026-08-17.

3. **`-top N` SHIPPED (2026-08-20).** Exact evaluator-level deferral, not a buffered print trick:
   each node gathers the scored entries that actually reach it, selects its stable top N after the
   walk, then replays the expression from memoized prefixes. Actions and stateful tests to the left
   therefore run once during traversal; everything to the right runs only after that node's decision.
   Several nodes resolve in expression order, so a candidate routed into a later `-top` only after
   losing an earlier one joins the later node BEFORE its selection - its candidate set is complete,
   not an early snapshot. Stored visits own their strings and archive filesystem, as collections do.
   A score must precede the node on every reachable path; model and threshold must be uniform (bare
   means `0%`), and malformed / negative counts fail before traversal while zero keeps none. Complex
   end-to-end coverage pins exact ranking, before/after actions, independent nodes, late candidates,
   collection order, invalid domains, and zero.
4. **`--max-results` SHIPPED (2026-08-20).** The aggregate ceiling applies to the implicit result
   listing after the complete expression, across branches and independent caps. It does not stop the
   walk or truncate reductions, so summaries, histograms, counts, and packing remain complete.
   Explicit actions retain expression semantics: a global cannot retrospectively suppress `-print`
   or `-grep` records already emitted while deciding the expression, and `-exec` is not a listing;
   put `-first` / `-top` before an action to cap what reaches it. Last occurrence wins, zero lists
   none, and missing / malformed / negative counts are pre-walk usage errors. End-to-end coverage
   pins the 10-files-or-5-directories capped-to-12 case, complete reductions, explicit actions, last
   wins, zero, and every invalid form.

## Silence external warnings in the EXEC configuration too (from mboworks/mbo#332) - SHIPPED

Adapted from mbo's [#332](https://github.com/mboworks/mbo/pull/332), which found that the warning
policy applied only to the TARGET configuration, so anything built for the host escaped it in both
directions: first-party warnings were not errors, and external sources were not muted.

Two changes, and the first is the one that mattered:

- **`-w` instead of `-Wno-error` for external sources.** The downgrade was the wrong tool: it keeps
  the diagnostic and only stops it failing the build, so every compile still PRINTED it. With
  `--features=parse_headers` a third-party header is compiled as its own external TU, which is how
  `gmock.cc:47: '__COUNTER__' is a C2y extension` sat in every build log permanently.
- **The whole policy mirrored into the exec configuration** (`--host_cxxopt=-Werror`,
  `--host_features=external_include_paths`, and host counterparts of every `--per_file_copt` rule),
  which previously had no warning policy at all.

**The carve-out is the part that is NOT a copy of mbo.** The composable extras are FIRST-party code
that happens to live under `external/` (separate modules via `local_path_override`), so both mute
rules keep `,-external/xff_.*` and both configurations get an explicit
`external/xff_.*@-Wextra,-Wpedantic,...` rule. mbo's blanket `external/.*@-w` would have silently
muted every warning in @xff_archive, @xff_pcre2 and @xff_fuse.

Verified by planting a deliberate unused variable and building, rather than by reading the flags:

| Where                                     | Result                           |
| :---------------------------------------- | :------------------------------- |
| `xff/fuzzy/fuzzy.cc` (first-party)        | `error:` -Werror, fails          |
| `external/xff_archive+/archive_reader.cc` | `error:` -Werror, fails          |
| third-party external sources              | 0 warnings (was: on every build) |

**Resolved (2026-08-22): eliminate the compile-DB extractor's C/C++ driver mismatch.** The
[PR #611 clang-tidy job](https://github.com/mboworks/xff/actions/runs/32568629375/job/97020893058?pr=611)
again prints ~80 `error: invalid argument '-std=c99' not allowed with 'C++'` diagnostics. The
extractor probed third-party C targets (xz sets `-std=c99`) with `clang++`; warning flags could not
affect that language-mode error, and dropping those third-party entries only cleaned the resulting
database, not the refresh log. `compile_commands-update.sh` now names the hermetic, language-neutral
`clang` driver: the source extension and extracted flags select C versus C++, while clang-tidy can
still introspect the real driver for its target and resource directory. The output filter was
removed; the third-party C post-processing and first-party coverage guard remain.

## Compile-database coverage guard (clang-tidy cannot lint what the database omits)

`clang-tidy` lints only the files the database LISTS, so a source bazel compiles but the extractor
missed is silently unlinted, and the run still looks clean. That is not hypothetical: every extras
translation unit was absent once (the story is in `//:refresh_compile_commands`).

`compile_commands-update.sh` now asks bazel which first-party `.cc` files are in a `cc_*` rule
(`//xff/...`, the derived extras wildcards, `@xff_extras_api//...`) and fails if any lacks a database
entry, naming the files. Bazel's query is the authority on "compiled" rather than a `find` over the
tree - a file in no target is a different problem.

Two properties the implementation must keep, both learned the hard way while writing it:

- **The comparison goes through the same source-path remap as the rewrite pass.** Labels say
  `external/xff_archive+/archive_reader.cc`; the post-processed database says
  `extra_modules/archive/archive_reader.cc`. Comparing the raw spellings reports all 42 extras files
  as missing when every one of them is present (measured: it did, and the "finding" was mine).
- **Coverage, never equality.** The database legitimately holds entries the query does not name
  (third-party headers, which `--features=parse_headers` makes their own TUs) and legitimately omits
  the third-party C sources `tools/fix_compile_commands.py` drops.

Current state: 165 first-party sources expected, 0 missing. Verified the guard FAILS (exit 1, naming
the file) on a source that has no entry.

## squashfs: bottom of the stack, read first (decided 2026-08-18; reader shipped 2026-08-28)

Build it when a concrete "search inside a snap / AppImage / firmware image" need appears, not for
format completeness. Three parts in order: READ without lzo, then WRITE (create from a walk, then
repack for modification), then lzo if ever.

Structurally it is another container on the existing VFS seam and needs no FUSE at all - diving with
`--archive` never mounts anything - and it is in some ways nicer than tar, because it has a real
index (indexed directory lookup, indexed seeking within a file). Writing is multi-pass with
backpatching, which is the ordinary shape here (buffered tabular renderers, `--sort=score`,
`-collect`, the `-top` design). Two real constraints on writing: the output must be SEEKABLE, so
packing to a pipe gets refused, and buffering the tree needs the bounded-memory treatment `-collect`
has. Shipping without block dedup and tail-packing into fragments is valid but produces bigger images
than mksquashfs - a quality gap, not a correctness one.

The licensing is the load-bearing part, verified 2026-08-18:

- **libsquashfs (squashfs-tools-ng) is LGPLv3** (its tools GPLv3), so it is unusable here: xff ships
  statically linked single-file binaries, and LGPL-3 static linking obliges us to let recipients
  relink. That is an obligation change, not a NOTICE line. Its writer lives in the same library, so
  it does not solve packing either.
- **squashfuse is 2-clause BSD** and, despite the name, is a genuine reader: indexed lookup, indexed
  seeking, block caching, dedup and sparse files, xattrs, files over 4 GB. zlib / LZMA2 / LZ4 / zstd
  built in; lzo only via optional liblzo2 (GPL-2), which we simply do not enable - so lzo drops out
  by build configuration rather than by refusal logic.
- **libsqsh (sqsh-tools) 1.5.2 is BSD-2-Clause** and is the shipped reader. Its cextras dependency is
  BSD-2-Clause too. xff's overlay selects the static/memory mappers and zlib, LZMA2, LZ4, and zstd
  decoders, while deliberately omitting curl and lzo.
- squashfs-tools-ng keeps lzo out of libsquashfs for exactly the GPL-2 reason, which independently
  confirms the constraint.

The shipped slice is `@xff_squashfs`, gated by `--//xff:xff_squashfs` and composed by
`--//xff:xff_all`. It registers independently through `@xff_extras_api`, so enabling it does not pull
in libarchive. Raw images, Snaps, and AppImages become ordinary virtual member trees and use the same
predicates, fields, content reads, nesting, and summaries as other containers. AppImage's executable
prefix uses libsqsh's archive-offset API after a bounded scan for the `hsqs` superblock. A committed
fixture and constructed prefixed variants cover path-backed and retained-byte inputs. Creation and
rewrite remain later work because a correct SquashFS writer is separate, multi-pass work.

## Bashtest scratch files: use `test_tmpdir`, not hand-rolled paths

mboworks/bashtest already provides `${BASHTEST_TMPDIR}`, a scratch directory its own exit trap
removes, and 19 xff bashtests ignored it to hand-roll `mktemp -d` plus a per-case `rm -rf`. Beyond
the duplication, the cleanup is WRONG in the case that matters: bashtest keeps running after a failed
expectation, so a case that fails before its `rm` leaks its tree.

Bashtest now owns per-fixture allocation too. The convention is:

```sh
_tree() {
  local root
  root="$(test_tmpdir tree)"
  # Build the fixture under ${root}.
  echo "${root}"
}

root="$(_tree)"
```

`${FUNCNAME[1]}` (the calling test) was tried first and is wrong: the test's name then appears in
printed paths, where it can satisfy an `expect_output_not_contains`. That is not hypothetical -
`archive_test`'s "archive is off by default" case failed exactly that way. `test_tmpdir` is unique per
call, so a case that builds two trees needs nothing special and no mutable counter. Per
[`STYLE_SH.md`](../STYLE_SH.md), a helper returns the path through stdout (`root="$(_tree)"`) rather than
mutating a caller-named variable with `printf -v`.

**Complete: all 19 converted.** `archive_test`, `color_test`, `parity_test`, `first_test`, `ignore_test`, `collect_test`,
`content_test`, `fuzzy_test`, `grep_test`, `ls_test`, `exact_test`, `pager_test`, `cmp_test`,
`full_binary_test`, `ignore_files_test`, `help_topic_test`, `archive_pack_test`, `archive_dive_test`,
`summary_test`, `ignore_gitignore_test` - 19 of 19, with 214 `rm -rf` lines gone. The later repository-wide audit
also migrated every other bashtest-created directory, including diff/hash/histogram/csv fixtures,
man-page scratch, style/exit/exec helpers, and the cookbook's shared fixtures: 114 allocations across
33 test files now go through `test_tmpdir`. `tools/release_prep_test.sh` is the deliberate exception:
it is a standalone pre-commit test, not a bashtest, and owns its temporary repositories and cleanup.

The location-sensitive archive, summary, and git-ignore assertions pass unchanged; the conversion
needed fixture-lifetime work, not weakened matching. Unique allocation handles several fixtures per
case with no special treatment and avoids caller mutation, collisions, and test-name leakage.

## libfuse: no build dependency at all (SHIPPED 2026-08-18)

The FUSE extra used to compile against libfuse's fetched headers, which put xff in the position of
ARGUING that LGPL-2.1 section 5 permits it. The argument was sound - data structure layouts and
accessors are outside that section's restrictions, and its ten-line limit applies only to inline
functions - but xff ships statically linked single-file binaries, so the whole question was load
bearing on a reading of someone else's licence.

It is gone. `extra_modules/fuse/fuse_abi.h` declares the fuse3 lowlevel ABI itself, the
`http_archive` is commented out (kept as provenance and as a conformance tool: uncomment it plus the
`@libfuse//:fuse3_headers` deps to diff our declarations against the real ones after a libfuse
release), and no libfuse file is fetched, read, compiled, linked or shipped. The runtime library is
unchanged: `fuse_loader` dlopens whatever the host has.

**Fidelity rules the header follows**, since an ABI description is only worth its accuracy:

- Anything never dereferenced is an INCOMPLETE type (`fuse_session`, `fuse_req`, `fuse_conn_info`).
  What is not written cannot drift.
- Anything whose fields we touch carries the exact layout in libfuse's order, transcribed against
  fuse-3.18.2: `fuse_args`, `fuse_entry_param`, and `fuse_file_info` (whose nine one-bit flags plus
  `padding : 23` fill the first word, with `padding2` / `padding3` full words despite the bitfield
  spelling - we read `flags` and set `fh` and `keep_cache`, and those three only land correctly if
  the rest is reproduced).
- `fuse_lowlevel_ops` is declared as the PREFIX through `readdir`, the last op xff implements, with
  the unimplemented ops in between still declared because order IS the ABI. `op_size` stays the
  offset just past the last implemented op, never `sizeof`.
- `FUSE_ARGS_INIT` is not transcribed: the struct is three fields, so aggregate initialisation says
  the same thing without copying a macro.

**How a mistake surfaces:** the loader resolves every entry point by NAME, so a wrong signature or
offset cannot fail at link time - it corrupts at runtime. Linux CI is the guard, where
`fuse_server_test` and the `--archive-mount` CLI cases drive a real fuse3.

**Notice correction.** The intermediate empty-SPDX interoperability entry was also superseded. The
final model is recorded below: the component is xff's own Apache-2.0 FUSE extension, while the
runtime-provided libfuse/macFUSE implementation is not a component of the binary.

## libfuse: no build dependency, and the notice lists OUR code (SHIPPED 2026-08-18)

The FUSE extra used to compile against libfuse's fetched headers, which put xff in the position of
ARGUING that LGPL-2.1 section 5 permits it. The argument was sound - data structure layouts and
accessors sit outside that section's restrictions, and its ten-line limit applies only to inline
functions - but xff ships statically linked single-file binaries, so the whole question was
load-bearing on a reading of someone else's licence.

`extra_modules/fuse/fuse_abi.h` now declares the fuse3 lowlevel ABI itself. The `http_archive` is
commented out (kept as provenance and as a conformance tool: uncomment it plus the
`@libfuse//:fuse3_headers` deps to diff our declarations against the real ones after a libfuse
release), and no libfuse file is fetched, read, compiled, linked or shipped. `fuse_loader` still
dlopens whatever the host has.

**Fidelity rules the header follows**, because an ABI description is only worth its accuracy:

- Anything never dereferenced is an INCOMPLETE type (`fuse_session`, `fuse_req`, `fuse_conn_info`).
  What is not written cannot drift.
- Anything whose fields we touch carries the exact layout in libfuse's order, transcribed against
  fuse-3.18.2: `fuse_args`, `fuse_entry_param`, and `fuse_file_info` - whose nine one-bit flags plus
  `padding : 23` fill the first word, with `padding2` / `padding3` full words despite the bitfield
  spelling. We read `flags` and set `fh` and `keep_cache`, and those three only land correctly if the
  rest is reproduced.
- `fuse_lowlevel_ops` is the PREFIX through `readdir`, the last op xff implements, with the
  unimplemented ops in between still declared, because order IS the ABI. `op_size` stays the offset
  just past the last implemented op, never `sizeof`.
- `FUSE_ARGS_INIT` is not transcribed: the struct has three fields, so aggregate initialisation says
  the same thing without copying a macro.

**How a mistake surfaces:** the loader resolves entry points by NAME, so a wrong signature or offset
cannot fail at link time - it corrupts at runtime. Linux CI is the guard, where `fuse_server_test` and
the `--archive-mount` CLI cases drive a real fuse3.

**The notice model, corrected twice before it was right.** The old entry claimed
`libfuse [LGPL-2.1-only]` and "compiled against its headers". That misstated the project (LGPL-2.1
covers `include/`, `lib/`, `meson.build`; everything else is **GPL-2.0**), asserted `-only` more
firmly than the LICENSE file supports, and named the runtime library wrongly - on macOS it is macFUSE,
under its own terms. An empty SPDX was tried next and is also wrong: `[]` reads as public domain.

The rule that settles it: **list what you USE, and our own code is Apache-2.0.** So the registered
component is `xff FUSE extra (@xff_fuse)` with SPDX `Apache-2.0`, whose text describes the libfuse
interaction as INFORMATION - our own declarations, host implementation loaded at runtime - and libfuse
is not registered at all, because none of its code is in the binary. A test pins both halves.

**Consistency follow-up (SHIPPED, PR #569):** `@xff_archive` and `@xff_pcre2` now register their own
Apache-2.0 extension entries before the third-party components they link, matching `@xff_fuse` and
making the manifest complete and uniform.
