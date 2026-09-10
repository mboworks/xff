# Design: find flavors (POSIX / GNU / BSD) and xff's compatibility target

Status: design-of-record. Companion to [`design-config.md`](design-config.md)
(the style/feature/config mechanism this builds on).

## Question

`find` is not one tool. The POSIX "original", GNU findutils, and the BSD/macOS
find diverge in both vocabulary and spelling. xff bills itself as a drop-in
replacement, so: which find does its `find` style target, and do we need more
than one flavor?

## Decision

**One permissive `find` style = POSIX (original) `union` GNU `union` BSD.** Accept
every dialect's spelling wherever they do not contradict; the `xff` style adds
the genuine xff inventions on top. We do **not** split into `find-gnu` /
`find-bsd` flavors.

Rationale:

1. **xff already implements the union.** It ships BSD's birthtime predicates
   (`-Bmin`/`-Btime`) _and_ GNU's `-printf`/`-mmin`/`-regextype`. Two strict
   flavors would force _retracting_ features from each (find-gnu rejecting
   `-Bmin`, find-bsd rejecting `-printf`), which is backwards for a replacement.
2. **The vocabulary divergence is mostly additive** - different spellings for
   similar capabilities. The union accepts most GNU, BSD, and POSIX expressions
   without selecting a platform flavor. It is not a promise that every script is
   behaviorally identical: contradictory spellings and defaults are resolved
   explicitly below, and unimplemented entries remain identified in the survey.
3. **The `find` vs `xff` line is about xff inventions**, not GNU-vs-BSD spelling.
   Strict `--config=find` rejects xff-only primitives (`-println`, `-capture`,
   `{field}` substitution, compound durations); it stays lenient about which real
   find dialect you wrote.
4. If an "enforce GNU-portability" need ever appears, that is a future
   `--feature`/`--config` knob (per `design-config.md`), not a reason to fork the
   core now.

## Divergence survey

Probed against `/usr/bin/find` (BSD) on macOS and GNU findutils on Linux. (The
interactive `find` on the dev box is `bfs`, itself a GNU+BSD superset; CI uses the
genuine tools.)

| Feature                             | POSIX | GNU  | BSD | xff |
| ----------------------------------- | ----- | ---- | --- | --- |
| `-mtime` integer days               | yes   | yes  | yes | yes |
| `-mtime N[smhdw]` unit suffix       | no    | no   | yes | yes |
| `-mmin` / `-amin` / `-cmin`         | no    | yes  | yes | yes |
| `-printf` / `-fprintf` / `-fls`     | no    | yes  | no  | yes |
| `-regextype`                        | no    | yes  | no  | yes |
| `-E` (extended-regex global flag)   | no    | no   | yes | tbd |
| `-daystart`                         | no    | yes  | no  | yes |
| `-Bmin` / `-Btime` (birthtime)      | no    | no   | yes | yes |
| `-perm -mode` (all-of)              | yes   | yes  | yes | yes |
| `-perm /mode` (any-of)              | no    | yes  | no  | yes |
| `-perm +mode` (any-of)              | no    | no   | yes | yes |
| `-type f,d` (OR-list)               | no    | yes  | no  | yes |
| `-samefile` / `-wholename`          | no    | yes  | no  | yes |
| compound duration (`3 weeks 3 hrs`) | no    | no\* | no  | xff |

`*` GNU `get_date` (used by `-newermt`) does accept compound spans, so compound
on `-newermt` is find-compatible; compound on `-mtime` is novel to xff.

## Contradiction resolutions

The few cases where the _same_ input means _different_ things, resolved toward the
most-capable reading:

- **`-type f,d`**: GNU = "f OR d"; BSD silently uses just "f". -> take GNU's OR.
- **`-perm`**: `+mode` (BSD/old-GNU) and `/mode` (GNU) both = any-of; `-mode` =
  all-of; bare = exact (POSIX). Accept all three forms with those meanings.
- **`-size` suffixes**: shared core (512-byte blocks default, `c` = bytes,
  `k`/`M`/`G`); accept BSD's extra `T`/`P` as a superset.
- **regex default flavor**: GNU emacs / BSD basic / xff RE2 - xff uses its RE2
  module with `-regextype` to select, documented as xff's choice.

## Consequence for the time predicates

This settles the rich-time-comparison gating:

- `-mtime` / `-atime` / `-ctime` **single unit suffix** (`-1h`, `+2d`, `+1w`) is
  **BSD-native** -> find-compatible, available in the `find` style too, **not**
  xff-gated.
- **Compound** duration on `-mtime` (`-"3 weeks 3 hours"`) is novel -> xff-only,
  gated to the `xff` style.
- Compound on `-newermt` matches GNU `get_date` -> find-compatible, ungated.

## Conformance

The conformance suite compares xff to the _system_ find, so it runs against GNU
on Linux and BSD on macOS. Vocabulary available in only one dialect is
**platform-gated** in the suite (test `-printf` only where GNU is present,
`-mtime` unit suffixes only where BSD is). Primaries common to both with
identical semantics are tested everywhere; intentional contradiction resolutions
such as the regex default need xff-specific expectations rather than an equality
claim against both tools.
