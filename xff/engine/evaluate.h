// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef XFF_ENGINE_EVALUATE_H_
#define XFF_ENGINE_EVALUATE_H_

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "mbo/diff/diff_options.h"
#include "mbo/types/optional_ref.h"
#include "xff/datetime/datetime.h"
#include "xff/engine/collect.h"
#include "xff/engine/extract.h"
#include "xff/engine/mount.h"
#include "xff/engine/walk.h"
#include "xff/parser/ast.h"
#include "xff/presentation/format/format.h"
#include "xff/vfs/filesystem.h"

namespace xff::exec {
class ParallelExec;  // bounded concurrent `-exec/-execdir ... ;` runner (xff/exec/exec.h)
}  // namespace xff::exec

namespace xff::engine {

// Output sink for actions: receives one fully-formed record per action firing
// (path plus terminator -- "p\n" for -print, "p\0" for -print0).
using EmitFn = absl::FunctionRef<void(std::string_view)>;

// Traversal-control side-channel, set by control actions during evaluation and
// read by the driver: -prune asks the walk not to descend into the current
// directory; -quit asks it to stop the traversal entirely. Like `emit`, these
// fire as a side effect of reaching the action node, so short-circuit governs
// whether they are set.
struct Control {
  bool prune = false;
  bool quit = false;
  // Set by a predicate that cannot be evaluated correctly on this entry's
  // filesystem/kernel -- an "impossible task", e.g. -Btime where the birth time is
  // unrecorded. Holds a short static reason (empty means none). The driver turns it
  // into a hard error naming the path (exit 2) or, under --skip-unsupported, a
  // warning, skipping the entry. (Rule: fail when correctness is impossible; warn
  // when degraded-but-correct.) The predicate also returns false (it cannot match).
  std::string_view unsupported;
  // Storage for a reason that is COMPOSED rather than static (it names the action, or carries the
  // error that made the task impossible). Set `unsupported` to view this after assigning it; a
  // static reason needs neither. Kept separate so the common case stays a bare view of a literal.
  std::string unsupported_storage;

  // Records a composed reason, keeping it alive for as long as the Control lives.
  void SetUnsupported(std::string reason) {
    unsupported_storage = std::move(reason);
    unsupported = unsupported_storage;
  }
};

// Stable identity of an expression node owned by the parsed expression tree.
// The wrapper makes address identity explicit without exposing a nullable pointer.
class ExprIdentity final {
 public:
  explicit ExprIdentity(const parser::Expr& expr) : expr_(expr) {}

  [[nodiscard]] const parser::Expr& Get() const { return expr_.get(); }

  friend bool operator==(ExprIdentity lhs, ExprIdentity rhs) {
    return std::addressof(lhs.Get()) == std::addressof(rhs.Get());
  }

  friend bool operator<(ExprIdentity lhs, ExprIdentity rhs) {
    return std::less{}(std::addressof(lhs.Get()), std::addressof(rhs.Get()));
  }

 private:
  std::reference_wrapper<const parser::Expr> expr_;
};

// The truth and fuzzy value of an expression evaluation. `deferred` means evaluation reached an
// undecided result-set predicate and deliberately stopped before anything to its right.
struct EvaluationResult {
  std::optional<ExprIdentity> waiting_at;
  std::optional<int> fuzzy;
  bool matched = false;
  bool deferred = false;
};

using DeferredDecisions = std::map<ExprIdentity, bool>;
using EvaluationMemo = std::map<ExprIdentity, EvaluationResult>;
using FirstCounts = std::map<ExprIdentity, int>;
using ExecBatches = std::map<ExprIdentity, std::map<std::string, std::vector<std::string>>>;

// Inputs shared by one deferred evaluation pass. Decisions answer result-set predicates resolved by
// the driver; the memo prevents completed prefix nodes from running twice during replay.
struct DeferredEvaluation {
  const DeferredDecisions& decisions;
  EvaluationMemo& memo;
};

// Per-evaluation environment threaded through Evaluate for one visited entry.
// Bundles what an expression node may read -- the entry, the action sink, the
// filesystem, the reference clock -- plus the traversal-control side-channel, so
// predicates and actions take them from one place rather than a long parameter
// list. Evaluation-wide options (e.g. modern -exec field substitution) attach
// here as the engine grows.
struct EvalContext {
  const Visit& visit;  // the entry being evaluated (constant across the expression)
  EmitFn emit;         // action output sink (-print/-print0/...)
  // File-output sink for -fprint/-fprint0/-fprintf/-fls: (filename, record). The
  // driver appends to each named file (opened once, truncating); empty -> the
  // file actions are inert (in-process callers that wire no sink).
  std::function<void(std::string_view, std::string_view)> emit_file;
  // Row sink for -ls's aligned output: receives the entry's -ls columns as cells
  // (inode, blocks, perms, ...) for the driver to feed to a format::ColumnBuffer.
  // Empty -> -ls falls back to a single-space-joined line (in-process callers).
  std::function<void(std::vector<std::string>)> emit_ls_row;
  // --color + --color-scheme, for the NAME column of -ls / -fls: the SGR parameter for THIS entry, or
  // empty for "print it plain". Resolved by the driver, which already resolves the palette once per
  // run - so -ls colours exactly what the plain listing colours, from the same theme, and the
  // evaluator needs no colour library. Only the name is wrapped, as `ls -l` does, and the name is the
  // LAST column, so the escapes cannot disturb the computed widths.
  std::string_view ls_color;
  // -ls size-column rendering: human units (iec/si) or, when nullopt, raw bytes.
  // The driver resolves it from --human + the style (xff -> human, find -> bytes).
  std::optional<format::SizeUnits> ls_size_units;
  const vfs::FileSystem& fs;                  // backs predicates that read the source (e.g. -empty on a directory)
  absl::Time now;                             // single reference instant for age tests (-mtime/-mmin)
  absl::TimeZone tz = absl::LocalTimeZone();  // zone for interpreting time-string args (-newerXt); --timezone
  std::string_view time_format;               // --time-format: default for a time field with no {:qualifier}
  // --time-zone-suffix: whether a time field's named preset renders its zone suffix; forwarded to the
  // fields RenderContext (kAuto keeps the preset default, kNever suppresses, kAlways forces one).
  datetime::ZoneSuffix zone_suffix = datetime::ZoneSuffix::kAuto;
  std::uint64_t block_size = 512;  // --block-size: bytes per -size block (bare value / 'b'); find's 512
  // FS-native name matching: when true, the case-sensitive name predicates
  // (-name/-path) fold case for this entry because it lives on a case-folding
  // volume and the xff-style default is in effect (no --exact). The driver
  // resolves it per entry from a per-device IsCaseSensitive probe; it is always
  // false in the find style and under --exact (byte-exact matching). The `i`
  // variants (-iname/-ipath) fold regardless.
  bool fold_name_case = false;
  // The normalized 0..100 fuzzy quality composed across the expression for {fuzzy}, --sort=score,
  // and -top. Null when no consumer needs it; reset by the driver per entry.
  mbo::types::OptionalRef<std::optional<int>> fuzzy_score;
  // --summary=hash-verification: the verdict produced when this entry reaches its -hasheq predicate.
  // Null when that reduction is inactive; an empty optional means short-circuiting has not reached
  // -hasheq, while true / false records the predicate's actual result independent of the expression's
  // eventual truth value.
  mbo::types::OptionalRef<std::optional<bool>> hash_verification;
  // Result-set predicates such as -top and -shard-status are resolved after the walk. During the
  // first evaluation, an undecided predicate returns its frontier (and -top's composed score) in
  // EvaluationResult. During replay this capability supplies earlier decisions and memoizes
  // completed prefix nodes, so actions and stateful tests to the left never run twice.
  mbo::types::OptionalRef<DeferredEvaluation> deferred;
  // The fuzzy score composed by the expression immediately to this node's left. EvaluateResult
  // maintains it while descending an AND RHS; -top consumes it as its ranking key.
  std::optional<int> incoming_fuzzy_score;
  // --count / -c: -grep prints one `path:count` per file (its matching-line count)
  // instead of the lines, rg -c style; supersedes -grep:FORMAT. Only -grep reads it.
  bool grep_count = false;
  // --context / --before-context / --after-context (grep -C/-B/-A): lines of context -grep prints
  // before and after each match (0 = none, the default). Resolved once before the walk; only -grep
  // reads them. Superseded by --count.
  std::size_t grep_before = 0;
  std::size_t grep_after = 0;
  // --diff-algorithm=naive|direct|myers: the diff engine -diff uses (mbo::diff). Empty ->
  // myers (the default). Validated once before the walk; only -diff reads it.
  std::string_view diff_algorithm;
  // --diff-ignore=<tokens>: comma-separated normalization tokens for -diff (ws / change /
  // trail / blank / case), validated once before the walk. Empty -> exact. Only -diff reads it.
  std::string_view diff_ignore;
  // --diff-ignore-matching=REGEX: -diff ignores lines matching this RE2 (compiled per -diff
  // entry; the pattern is validated once before the walk). Empty -> no line filter.
  std::string_view diff_ignore_matching;
  // --diff-format=u|c|n|y|...: the default -diff output format (built-in unified). A per-action
  // -diff:STYLE letter overrides it. Resolved once before the walk; only -diff reads it.
  mbo::diff::DiffOptions::OutputFormat diff_format = mbo::diff::DiffOptions::OutputFormat::kUnified;
  // --diff-context=N (and --context=N when symmetric): the default -diff context size (built-in 3).
  // --context feeds it only when before==after; --diff-context overrides --context. A per-action
  // -diff:uN overrides both. Resolved once before the walk; only -diff reads it.
  std::size_t diff_context = 3;
  // --hash-algorithm=ALGO / --hash-encoding=hex|base64: the default digest for a bare -hash
  // action and a bare {hash} field (empty -> sha256 / hex). Validated once before the walk;
  // an explicit -hash:ALGO[/ENCODING] or {hash:...} qualifier overrides per node.
  std::string_view hash_algorithm;
  std::string_view hash_encoding;
  Control& control;          // collects -prune/-quit requests
  bool exec_fields = false;  // --exec-fields: render -exec tokens through the field vocabulary
  mbo::types::OptionalRef<std::vector<std::string>> captures;                 // -regex groups for gated -exec {0}..{N}
  mbo::types::OptionalRef<const std::map<std::string, std::string>> defines;  // --define values
  mbo::types::OptionalRef<std::map<std::string, std::string>> outputs;        // mutable -capture results
  // -first N: how many entries each instance has admitted so far, keyed by its AST node so two
  // `-first` in different branches keep separate budgets. Owned by the driver for the whole run
  // (unlike `outputs`, which is per entry) - the count is the point. Emission is single-threaded
  // (see walk.h), so a plain map needs no synchronisation.
  mbo::types::OptionalRef<FirstCounts> first_counts;
  // -collect[:NAME]: where the action holds entries for a post-walk sink (--summary reads the
  // collection instead of what matched). Owned by the driver for the whole run, and it OWNS what it
  // stores, because a Visit is borrowed (see collect.h). Empty -> -collect is inert.
  mbo::types::OptionalRef<Collections> collections;
  std::function<bool(std::string_view)> confirm;  // -ok prompt sink: returns true to run the command; empty -> decline
  // Accumulates matched items per `-exec/-execdir ... +` node for the end-of-walk
  // batch flush: outer key the Expr node, inner key the directory ("" for -exec's
  // single global batch; the entry's dir for -execdir's per-directory batches).
  // Empty disables batching (the action no-ops).
  mbo::types::OptionalRef<ExecBatches> exec_batches;
  // Bounded concurrent runner for `-exec/-execdir ... ;` under -j>1: when set, the
  // serial-`;` action launches the child here (returning true on launch) instead of
  // running it synchronously. Empty -> the action runs synchronously (find's default,
  // and -j 1). The `+` batch forms always go through exec_batches, never this.
  mbo::types::OptionalRef<exec::ParallelExec> parallel_exec;
  // --archive-extract: where an exec-family action may write an archive member so a child process can
  // open it (see ExtractedMembers). Empty - the default - keeps -exec / -execdir / -ok / -okdir a clean
  // refusal on a member, because there is no path to hand the child.
  mbo::types::OptionalRef<ExtractedMembers> extract;

  // --archive-mount: where an exec-family action may get a member's path from a MOUNT of its
  // container instead of a copy. Consulted before `extract`, and empty (or answering nothing when
  // this machine cannot mount) simply leaves extraction in charge.
  mbo::types::OptionalRef<MountedContainers> mounts;
  // --archive-delete: where `-delete` records an archive MEMBER it should remove. Collected rather
  // than removed on the spot, because removing one means rewriting the whole container - which the
  // walk is reading from at that moment - so the driver applies them per container after the walk.
  // Empty - the default - keeps `-delete` a clean refusal on a member.
  mbo::types::OptionalRef<std::vector<std::string>> archive_deletions;
};

// Evaluates a parsed find expression against one visited entry and returns its
// overall truth value, mirroring find:
//   - tests: -name/-iname/-path/-ipath/-type/-true/-false against the entry;
//   - operators: !/-not, -a/-and, -o/-or, with short-circuit;
//   - actions: -print/-print0 write a record via `emit`; -prune/-quit set `control`.
// -name/-iname glob the basename, -path/-ipath glob the whole path; the `i`
// variants fold case (fnmatch, matching GNU find). Short-circuit means actions
// to the right of a failed -a (or in the unused branch of -o) do not fire.
// `context` carries the entry, the action sink, the filesystem, the reference
// clock, and the -prune/-quit side-channel (see EvalContext).
bool Evaluate(const parser::Expr& expr, EvalContext& context);

// Evaluates like Evaluate, but exposes -top deferral to the run driver. With no deferred plumbing in
// the context this is equivalent to `{Evaluate(...), score, false}`.
EvaluationResult EvaluateDeferred(const parser::Expr& expr, EvalContext& context);

// True if `expr` contains any action node (-print, ...). The driver uses this
// to decide whether an implicit -print applies: find adds -print only when the
// expression has no action of its own.
bool ContainsAction(const parser::Expr& expr);

// Validates every `-size` argument in `expr`, returning the first malformed one as
// an InvalidArgument status (unknown unit, an over-64-bit unit like Z/Y, or a
// missing/non-numeric count) or Ok when all are well-formed. The driver calls this
// before the walk so a bad `-size` is a usage error (exit 2) rather than a silent
// per-entry no-match, matching find's parse-time rejection. Style-independent: the
// size units (incl. the T/P/E continuation) are valid in every flavor.
absl::Status ValidateSizeArgs(const parser::Expr& expr);

// Validates the --diff-ignore token list and the --diff-ignore-matching regex, returning the
// first problem as an InvalidArgument (an unknown token names it; a bad regex carries RE2's
// diagnostic) or Ok. Each comma-separated token in `tokens` must be one of ws / change / trail
// / blank / case (empty tokens are skipped); a non-empty `matching` must be a valid RE2. The
// driver calls it once before the walk so a bad value is a usage error (exit 2) rather than a
// silent per-entry no-op; -diff then trusts the validated values.
absl::Status ValidateDiffIgnore(std::string_view tokens, std::string_view matching);

// Parses a `--diff-format` value into an mbo output format: the letters u/c/n/y (matching the
// -diff:STYLE token) or the long names unified/context/normal/side-by-side; nullopt for an
// unknown value. `none` is intentionally not a format (it is the per-action -diff:none silencer,
// not a default the walk carries). The driver uses it to validate + resolve the global once
// before the walk; -diff then falls back to the resolved format when its own token omits one.
std::optional<mbo::diff::DiffOptions::OutputFormat> ParseDiffFormatFlag(std::string_view flag);

// Validates every `-hash:ALGO[/ENCODING]` spec in `expr`, returning the first malformed one as
// an InvalidArgument (unknown algorithm or encoding, naming it) or Ok. The driver calls it before
// the walk so a bad `-hash` spec is a usage error (exit 2) rather than a silent per-entry no-op,
// matching how the other payload primaries validate up front.
absl::Status ValidateHashArgs(const parser::Expr& expr);

// Parses a `--block-size=SIZE` value into bytes: a bare number is bytes; legacy
// c/w/k/M/G/T/P/E units are binary; explicit B/kB/.../EB units are SI and
// KiB/.../EiB units are IEC. Lowercase 'b' and overflow are rejected. The result is
// for a bare `-size N` and the `-size Nb` unit (find's default is 512). Returns an
// InvalidArgument status (naming the problem) for a malformed or zero size.
absl::StatusOr<std::uint64_t> ParseBlockSize(std::string_view spec);

// The `-ls` columns for one entry, in display order: inode, 1 KiB blocks, symbolic
// permissions, link count, owner, group, size, time (in `tz`), path. The size is
// rendered in `size_units` (human iec/si) or, when nullopt, as raw bytes. The
// aligned renderer (format::ColumnBuffer) consumes these; LsRecord joins them
// single-spaced for the unbuffered fallback.
std::vector<std::string> LsCells(
    const Visit& visit,
    absl::Time now,
    absl::TimeZone tz,
    std::optional<format::SizeUnits> size_units,
    std::string_view color = {});

// One `-ls` column's alignment and minimum (also fixed, when --buffer=off) width.
struct LsColumn {
  format::Align align;
  std::size_t min_width;
};

// The `-ls` column layout (alignment + minimum width per column), in LsCells order:
// numeric columns right-aligned, text left; the driver builds the --buffer
// format::ColumnBuffer from this. The minimums are ls -l-like defaults for the
// flexible fields (owner/group/size), a fixed 10 for the permission string, and 0
// for the trailing path.
[[nodiscard]] absl::Span<const LsColumn> LsColumns();

// The -printf directive vocabulary for `--help=printf`, as {code, description} rows:
// find's % directives (the kPrintfDirectives table), the a/c/t + Ak/Ck/Tk time families,
// the \ escapes, and xff's %{field} escape. evaluate_test guards that it documents every
// directive in PrintfDirectiveLetters().
[[nodiscard]] absl::Span<const std::pair<std::string_view, std::string_view>> PrintfDocs();

// The letters of find's -printf % directives (the kPrintfDirectives keys), for the
// `--help=printf` coverage guard.
std::string PrintfDirectiveLetters();

// The -size unit vocabulary for `--help=size`, as {code, description} rows: the unit
// legacy suffixes, explicit SI/IEC suffixes, the block default, and +/- comparison.
// evaluate_test guards that it covers SizeUnitSuffixes().
[[nodiscard]] absl::Span<const std::pair<std::string_view, std::string_view>> SizeUnitDocs();

// The -size unit suffixes (the kSizeUnits keys), for the `--help=size` coverage guard.
std::string SizeUnitSuffixes();

}  // namespace xff::engine

#endif  // XFF_ENGINE_EVALUATE_H_
