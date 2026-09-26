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

// FNM_CASEFOLD and POSIX fnmatch() are hidden by glibc under the strict
// `-std=c++23` we build with; request them explicitly. No effect on macOS.
#if defined(__linux__) && !defined(_GNU_SOURCE)
# define _GNU_SOURCE 1
#endif

#include "xff/engine/evaluate.h"

#include <fnmatch.h>
#include <grp.h>
#include <pwd.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/time/time.h"
#include "mbo/container/limited_map.h"
#include "mbo/container/limited_set.h"
#include "mbo/diff/diff.h"
#include "mbo/diff/diff_options.h"
#include "mbo/file/artefact.h"
#include "mbo/status/status_macros.h"
#include "re2/re2.h"
#include "xff/content/line_match.h"
#include "xff/datetime/datetime.h"
#include "xff/engine/walk.h"
#include "xff/exec/exec.h"
#include "xff/fuzzy/fuzzy.h"
#include "xff/hash/hash.h"
#include "xff/matching/language/language.h"
#include "xff/matching/mime/mime.h"
#include "xff/matching/regex/regex.h"
#include "xff/matching/similarity/similarity.h"
#include "xff/parser/ast.h"
#include "xff/presentation/fields/fields.h"
#include "xff/presentation/patch/patch.h"
#include "xff/presentation/render/render.h"
#include "xff/registry/descriptor.h"
#include "xff/values/values.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {
namespace {

template<typename T>
mbo::types::OptionalRef<const T> AsConstOptionalRef(const mbo::types::OptionalRef<T> value) {
  return value.has_value() ? mbo::types::OptionalRef<const T>{*value} : std::nullopt;
}

// The OS-native line terminator used by -println/-printfln (xff extensions).
// LF today; a Windows build would select "\r\n". Centralized so both actions
// agree and the platform choice lives in one place.
constexpr std::string_view kOsLineEnding = "\n";

bool Fnmatch(std::string_view pattern, std::string_view text, int flags) {
  return ::fnmatch(std::string(pattern).c_str(), std::string(text).c_str(), flags) == 0;
}

// The (letter, type) pairs find's -type/-xtype letters denote, alphabetical by
// letter: matching an entry is then one membership test (contains).
constexpr auto kTypeChars = mbo::container::MakeLimitedSet(
    std::pair{'b', vfs::FileType::kBlockDevice},
    std::pair{'c', vfs::FileType::kCharDevice},
    std::pair{'d', vfs::FileType::kDirectory},
    std::pair{'f', vfs::FileType::kRegular},
    std::pair{'l', vfs::FileType::kSymlink},
    std::pair{'p', vfs::FileType::kFifo},
    std::pair{'s', vfs::FileType::kSocket});

bool MatchesTypeChar(char letter, vfs::FileType type) {
  return kTypeChars.contains(std::pair{letter, type});
}

// A valid -type/-xtype letter (the keys of kTypeChars), to reject an unknown one.
bool IsTypeChar(char letter) {
  return std::string_view("bcdflps").contains(letter);
}

// find's -type / -xtype argument: a single type letter, or (GNU extension) a
// comma-separated list like "f,d" that matches if the entry is any listed type.
// An empty, multi-character, or unknown element fails the whole match.
bool MatchesType(std::string_view arg, vfs::FileType type) {
  if (arg.empty()) {
    return false;
  }
  bool matched = false;
  while (true) {
    const std::size_t comma = arg.find(',');
    const std::string_view item = arg.substr(0, comma);
    if (item.size() != 1 || !IsTypeChar(item.front())) {
      return false;
    }
    matched = matched || MatchesTypeChar(item.front(), type);
    if (comma == std::string_view::npos) {
      break;
    }
    arg.remove_prefix(comma + 1);
  }
  return matched;
}

// find's %y type letter: the inverse of MatchesType's mapping.
char TypeLetter(vfs::FileType type) {
  switch (type) {
    case vfs::FileType::kBlockDevice: return 'b';
    case vfs::FileType::kCharDevice: return 'c';
    case vfs::FileType::kDirectory: return 'd';
    case vfs::FileType::kFifo: return 'p';
    case vfs::FileType::kRegular: return 'f';
    case vfs::FileType::kSocket: return 's';
    case vfs::FileType::kSymlink: return 'l';
    case vfs::FileType::kUnknown: return 'U';
  }
  return 'U';
}

// The directory component of `path` (find's %h): everything before the last
// '/', "/" for a root-level child, or "." when there is no '/'.
std::string_view Dirname(std::string_view path) {
  const std::string_view::size_type slash = path.rfind('/');
  if (slash == std::string_view::npos) {
    return ".";
  }
  if (slash == 0) {
    return "/";
  }
  return path.substr(0, slash);
}

// Permission bits as octal without leading zeros (find's %m): %o of mode & 07777.
std::string OctalPerm(std::uint32_t mode) {
  const unsigned bits = mode & 07777U;
  std::string out;
  for (int shift = 9; shift >= 0; shift -= 3) {
    const unsigned digit = (bits >> static_cast<unsigned>(shift)) & 7U;
    if (!out.empty() || digit != 0) {
      out.push_back(static_cast<char>('0' + digit));
    }
  }
  return out.empty() ? "0" : out;
}

// The 10-char symbolic permission string for -ls: a type char then user/group/
// other rwx triplets, with setuid/setgid/sticky shown as s/S/t/T (like ls).
std::string SymbolicPerms(vfs::FileType type, std::uint32_t mode) {
  std::string out(10, '-');
  switch (type) {
    case vfs::FileType::kBlockDevice: out[0] = 'b'; break;
    case vfs::FileType::kCharDevice: out[0] = 'c'; break;
    case vfs::FileType::kDirectory: out[0] = 'd'; break;
    case vfs::FileType::kFifo: out[0] = 'p'; break;
    case vfs::FileType::kRegular: out[0] = '-'; break;
    case vfs::FileType::kSocket: out[0] = 's'; break;
    case vfs::FileType::kSymlink: out[0] = 'l'; break;
    case vfs::FileType::kUnknown: out[0] = '?'; break;
  }
  static constexpr std::string_view kRwx = "rwx";
  for (int i = 0; i < 9; ++i) {
    if ((mode & (1U << static_cast<unsigned>(8 - i))) != 0U) {
      out[1 + i] = kRwx[i % 3];
    }
  }
  if ((mode & 04000U) != 0U) {  // setuid
    out[3] = out[3] == 'x' ? 's' : 'S';
  }
  if ((mode & 02000U) != 0U) {  // setgid
    out[6] = out[6] == 'x' ? 's' : 'S';
  }
  if ((mode & 01000U) != 0U) {  // sticky
    out[9] = out[9] == 'x' ? 't' : 'T';
  }
  return out;
}

// The -ls time column: "Mon DD HH:MM" for entries within ~6 months of `now`,
// else "Mon DD  YYYY" (like ls). Rendered in `tz`.
std::string LsTime(absl::Time mtime, absl::Time now, absl::TimeZone tz) {
  const bool recent = mtime > now - absl::Hours(24 * 182) && mtime < now + absl::Hours(1);
  return absl::FormatTime(recent ? "%b %e %H:%M" : "%b %e  %Y", mtime, tz);
}

// find's %u/%g: the owner/group name, or the numeric id when the user/group
// database has no entry for it (the reverse of ResolveUid/ResolveGid).
std::string UserName(std::uint32_t uid) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe): single-threaded CLI/test path
  if (const struct passwd* const pw = ::getpwuid(uid); pw != nullptr) {
    return pw->pw_name;
  }
  return std::to_string(uid);
}

std::string GroupName(std::uint32_t gid) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe): single-threaded CLI/test path
  if (const struct group* const gr = ::getgrgid(gid); gr != nullptr) {
    return gr->gr_name;
  }
  return std::to_string(gid);
}

// find's -printf \ escapes: the literal character each backslash sequence emits.
constexpr auto kPrintfEscapes = mbo::container::MakeLimitedMap(
    std::pair{'0', '\0'},
    std::pair{'\\', '\\'},
    std::pair{'n', '\n'},
    std::pair{'r', '\r'},
    std::pair{'t', '\t'});

// One -printf % directive: appends its expansion for `visit` to `out`. Captureless
// lambdas so the table below is a constexpr LimitedMap, like the engine's kDispatch.
using PrintfDirective = void (*)(std::string& out, const Visit& visit);

// find's -printf % directives, keyed by letter (alphabetical; '%' emits a literal %).
constexpr auto kPrintfDirectives = mbo::container::MakeLimitedMap(
    std::pair<char, PrintfDirective>{'%', [](std::string& out, const Visit&) { out.push_back('%'); }},
    std::pair<char, PrintfDirective>{
        'G', [](std::string& out, const Visit& v) { absl::StrAppend(&out, v.metadata.gid); }},
    std::pair<char, PrintfDirective>{
        'U', [](std::string& out, const Visit& v) { absl::StrAppend(&out, v.metadata.uid); }},
    std::pair<char, PrintfDirective>{'d', [](std::string& out, const Visit& v) { absl::StrAppend(&out, v.depth); }},
    std::pair<char, PrintfDirective>{'f', [](std::string& out, const Visit& v) { out.append(v.name); }},
    std::pair<char, PrintfDirective>{
        'g', [](std::string& out, const Visit& v) { out.append(GroupName(v.metadata.gid)); }},
    std::pair<char, PrintfDirective>{'h', [](std::string& out, const Visit& v) { out.append(Dirname(v.path)); }},
    std::pair<char, PrintfDirective>{
        'i', [](std::string& out, const Visit& v) { absl::StrAppend(&out, v.metadata.ino); }},
    std::pair<char, PrintfDirective>{
        'm', [](std::string& out, const Visit& v) { out.append(OctalPerm(v.metadata.mode)); }},
    std::pair<char, PrintfDirective>{
        'n', [](std::string& out, const Visit& v) { absl::StrAppend(&out, v.metadata.nlink); }},
    std::pair<char, PrintfDirective>{'p', [](std::string& out, const Visit& v) { out.append(v.path); }},
    std::pair<char, PrintfDirective>{
        's', [](std::string& out, const Visit& v) { absl::StrAppend(&out, v.metadata.size); }},
    std::pair<char, PrintfDirective>{
        'u', [](std::string& out, const Visit& v) { out.append(UserName(v.metadata.uid)); }},
    std::pair<char, PrintfDirective>{
        'y', [](std::string& out, const Visit& v) { out.push_back(TypeLetter(v.metadata.type)); }});

// The entry time a -printf time directive refers to: a/A -> atime, c/C -> ctime,
// t/T -> mtime (find's %a/%c/%t and the %Ak/%Ck/%Tk strftime families).
absl::Time PrintfTime(const vfs::Metadata& md, char which) {
  switch (which) {
    case 'A':
    case 'a': return md.atime;
    case 'C':
    case 'c': return md.ctime;
    default: return md.mtime;
  }
}

// A symlink's target for {target} (defined below with the other field helpers); read
// only when a -printf format actually references a field, so plain find -printf pays
// no readlink.
std::string LinkTarget(const EvalContext& ctx);

// Renders a -printf / -fprintf FORMAT against `ctx`'s entry. Expands find's % directives
// and \ escapes via the tables above. Supported %: p path, f name, h dir, s size, m octal
// perm, d depth, y type, i inode, n links, u/g owner name, U/G owner id; the time families
// a/c/t (asctime form) and Ak/Ck/Tk (strftime conversion k on atime/ctime/mtime), rendered
// in ctx.tz; %% literal; \: n t r \\ \0.
//
// xff: `%{NAME}` / `%{NAME:qualifier}` expands the brace field vocabulary -- the same
// fields as --format ({relpath} {core} {suffix} {target} {def.NAME} {env.NAME} {size:h},
// time qualifiers, the s/// rewrite, ...) -- so a per-entry action reaches fields find's %
// set does not name. A bare `{...}` stays literal (printf formats legitimately contain
// braces). Malformed field escapes are rejected by preflight; the strict find style also
// rejects otherwise valid `%{...}` before the walk (EnforceStyle).
// Unknown %/\ directives are emitted literally.
// NOLINTNEXTLINE(readability-function-cognitive-complexity): cohesive dispatch
std::string FormatPrintf(std::string_view format, const EvalContext& ctx) {
  const bool has_field = absl::StrContains(format, "%{");
  const std::string link = has_field ? LinkTarget(ctx) : std::string();  // backs {target}
  const fields::RenderContext field_ctx{
      .path = ctx.visit.path,
      .root = ctx.visit.root,
      .link_target = link,
      .metadata = ctx.visit.metadata,
      .depth = ctx.visit.depth,
      .fs = ctx.fs,
      .tz = ctx.tz,
      .time_format = ctx.time_format,
      .zone_suffix = ctx.zone_suffix,
      .hash_algorithm = ctx.hash_algorithm,
      .hash_encoding = ctx.hash_encoding,
      .captures = AsConstOptionalRef(ctx.captures),
      .defines = ctx.defines,
      .outputs = AsConstOptionalRef(ctx.outputs),
  };
  std::string out;
  for (std::string_view::size_type i = 0; i < format.size(); ++i) {
    const char ch = format[i];
    if (ch == '\\' && i + 1 < format.size()) {
      const char esc = format[++i];
      if (const auto it = kPrintfEscapes.find(esc); it != kPrintfEscapes.end()) {
        out.push_back(it->second);
      } else {
        out.push_back('\\');  // unknown escape: emit the backslash and char literally
        out.push_back(esc);
      }
    } else if (ch == '%' && i + 1 < format.size()) {
      const char directive = format[++i];
      if (directive == '{') {
        const std::optional<std::size_t> length = fields::PlaceholderSize(format.substr(i));
        if (!length.has_value()) {
          out.append("%{");
        } else {
          absl::StrAppend(&out, fields::Render(format.substr(i, *length), field_ctx));
          i += *length - 1;
        }
      } else if (directive == 'a' || directive == 'c' || directive == 't') {
        absl::StrAppend(&out, datetime::FormatTime(PrintfTime(ctx.visit.metadata, directive), "asctime", ctx.tz));
      } else if ((directive == 'A' || directive == 'C' || directive == 'T') && i + 1 < format.size()) {
        const char conv = format[++i];  // %Tk etc.: strftime conversion k on the chosen time
        absl::StrAppend(
            &out, datetime::FormatTime(
                      PrintfTime(ctx.visit.metadata, directive), absl::StrCat("%", std::string(1, conv)), ctx.tz));
      } else if (const auto it = kPrintfDirectives.find(directive); it != kPrintfDirectives.end()) {
        it->second(out, ctx.visit);
      } else {
        out.push_back('%');  // unknown directive: emit the percent and char literally
        out.push_back(directive);
      }
    } else {
      out.push_back(ch);
    }
  }
  return out;
}

// find's legacy `-size` unit suffixes -> bytes per unit. c=byte, w=2-byte word; k/M/G/T/P/E
// are the binary multiples 2^10..2^60. c/w/k/M/G and T/P are find-native (BSD accepts
// up to P); E (exabyte) is an xff continuation of the same scale -- a strict superset
// (no find-valid input changes meaning), available in every style. The next prefixes
// (Z/Y/...) name a real magnitude but 2^70+ overflows the 64-bit byte count, so they
// are rejected (see ParseSizeSpec). The block unit 'b' (and a bare, suffix-less -size
// value) is NOT in this map: it is the configurable block size (--block-size, default
// 512), resolved per run and applied in ParseSizeSpec.
// A constexpr map, per the style's preference for a uniform key -> value mapping.
// Divergence: entries are listed in ascending magnitude (the natural size scale
// c < w < k < ... < E) rather than alphabetically as elsewhere -- the progression
// mirrors how the units relate. MakeLimitedMap sorts by key, so this is for the reader.
using SizeUnitPair = std::pair<char, std::uint64_t>;
constexpr auto kSizeUnits = mbo::container::MakeLimitedMap(
    SizeUnitPair{'c', 1},                                                  // byte
    SizeUnitPair{'w', 2},                                                  // 2-byte word
    SizeUnitPair{'k', 1'024},                                              // 2^10 kibibyte
    SizeUnitPair{'M', 1'024ULL * 1'024},                                   // 2^20 mebibyte
    SizeUnitPair{'G', 1'024ULL * 1'024 * 1'024},                           // 2^30 gibibyte
    SizeUnitPair{'T', 1'024ULL * 1'024 * 1'024 * 1'024},                   // 2^40 tebibyte
    SizeUnitPair{'P', 1'024ULL * 1'024 * 1'024 * 1'024 * 1'024},           // 2^50 pebibyte
    SizeUnitPair{'E', 1'024ULL * 1'024 * 1'024 * 1'024 * 1'024 * 1'024});  // 2^60 exbibyte

// Size prefixes one step beyond E: each names a real magnitude (zetta/yotta/ronna/
// quetta) but 2^70+ exceeds the 64-bit byte count, so xff rejects them with a clear
// message rather than silently mis-sizing.
constexpr std::string_view kOversizedUnits = "ZYRQ";

// find's historical `-size` block unit: 512 bytes. The default for the bare value
// and the 'b' suffix; --block-size overrides it (see ParseBlockSize).
constexpr std::uint64_t kDefaultBlockSize = 512;

struct SizeSpec {
  char compare = '=';                      // '+' greater than, '-' less than, '=' exactly
  std::uint64_t want = 0;                  // the count, in `unit`s
  std::uint64_t unit = kDefaultBlockSize;  // bytes per unit
};

// Parses a `-size` argument `[+|-]N[unit]` into a SizeSpec, or returns an
// InvalidArgument status naming the problem (unknown unit, an over-64-bit unit, or
// a missing/non-numeric count). A bare value and the 'b' suffix use `block_size`
// (find's 512 by default; --block-size overrides). Used to reject a bad value before
// the walk and, defensively, by MatchesSize.
absl::StatusOr<SizeSpec> ParseSizeSpec(std::string_view arg, std::uint64_t block_size = kDefaultBlockSize) {
  const std::string_view original = arg;
  SizeSpec spec;
  spec.unit = block_size;  // no suffix -> the (configurable) block unit
  if (!arg.empty() && (arg.front() == '+' || arg.front() == '-')) {
    spec.compare = arg.front();
    arg.remove_prefix(1);
  }
  const std::size_t suffix_at = arg.find_first_not_of("0123456789");
  const std::string_view number = suffix_at == std::string_view::npos ? arg : arg.substr(0, suffix_at);
  const std::string_view suffix = suffix_at == std::string_view::npos ? std::string_view{} : arg.substr(suffix_at);
  if (!suffix.empty()) {
    if (suffix == "b") {
      spec.unit = block_size;  // 'b' = blocks (--block-size, default 512)
    } else if (suffix.size() == 1 && kSizeUnits.contains(suffix.front())) {
      spec.unit = kSizeUnits.at(suffix.front());
    } else if (const std::uint64_t explicit_unit = values::ParseByteUnit(suffix).value_or(0); explicit_unit != 0) {
      spec.unit = explicit_unit;
    } else {
      if (absl::StrContains(kOversizedUnits, suffix.front())) {
        return absl::InvalidArgumentError(
            absl::StrCat(
                "'", original, "': size unit '", suffix,
                "' exceeds xff's 64-bit byte range; the largest units are EB, EiB, and legacy E"));
      }
      return absl::InvalidArgumentError(absl::StrCat("'", original, "': unknown size unit '", suffix, "'"));
    }
  }
  if (number.empty()) {
    return absl::InvalidArgumentError(absl::StrCat("'", original, "': missing numeric size"));
  }
  if (!absl::SimpleAtoi(number, &spec.want)) {
    return absl::InvalidArgumentError(absl::StrCat("'", original, "': size is not an unsigned 64-bit number"));
  }
  return spec;
}

// Matches `-size [+|-]N[unit]`: find's legacy bcwkMGTPE suffixes plus explicit
// SI B/kB/.../EB and IEC KiB/.../EiB suffixes. The file size is rounded UP to the
// chosen unit, as find does; a bare value / 'b' uses `block_size`.
// A malformed arg never matches (it is rejected before the walk; see ValidateSizeArgs).
bool MatchesSize(std::string_view arg, std::uint64_t size_bytes, std::uint64_t block_size) {
  const absl::StatusOr<SizeSpec> spec = ParseSizeSpec(arg, block_size);
  if (!spec.ok()) {
    return false;
  }
  const std::uint64_t size_in_units = (size_bytes / spec->unit) + (size_bytes % spec->unit != 0 ? 1 : 0);
  if (spec->compare == '+') {
    return size_in_units > spec->want;
  }
  if (spec->compare == '-') {
    return size_in_units < spec->want;
  }
  return size_in_units == spec->want;
}

// Matches a plain integer metadata field (e.g. -links) with an optional +/-
// prefix: `+N` greater than N, `-N` less than N, `N` exactly.
bool MatchesNumeric(std::string_view arg, std::uint64_t value) {
  char compare = '=';
  if (!arg.empty() && (arg.front() == '+' || arg.front() == '-')) {
    compare = arg.front();
    arg.remove_prefix(1);
  }
  if (arg.empty()) {
    return false;
  }
  std::uint64_t want = 0;
  for (const char digit : arg) {
    if (digit < '0' || digit > '9') {
      return false;
    }
    want = (want * 10) + static_cast<std::uint64_t>(digit - '0');
  }
  if (compare == '+') {
    return value > want;
  }
  if (compare == '-') {
    return value < want;
  }
  return value == want;
}

// Like MatchesNumeric, but over a signed count: find's -used day delta is
// negative when a file's access time predates its status-change time. The
// argument N is still non-negative (a leading +/- is the comparison operator).
bool MatchesSignedNumeric(std::string_view arg, std::int64_t value) {
  char compare = '=';
  if (!arg.empty() && (arg.front() == '+' || arg.front() == '-')) {
    compare = arg.front();
    arg.remove_prefix(1);
  }
  if (arg.empty()) {
    return false;
  }
  std::int64_t want = 0;
  for (const char digit : arg) {
    if (digit < '0' || digit > '9') {
      return false;
    }
    want = (want * 10) + static_cast<std::int64_t>(digit - '0');
  }
  if (compare == '+') {
    return value > want;
  }
  if (compare == '-') {
    return value < want;
  }
  return value == want;
}

// Matches find's `-perm` over the permission bits (incl. setuid/setgid/sticky):
//   MODE   exact match;  -MODE  all of MODE's bits set;  /MODE  any of them set.
// MODE is either octal (0644, 644) or a chmod-style symbolic mode (`u+w`,
// `go=r`, comma-separated clauses); ParseSymbolicPerm resolves the latter.
// Resolves one chmod-style symbolic clause ("u+w", "go=r", "+x", "u+s", ...)
// to `want`, applied from find's zero base with no umask. Returns nullopt on a
// syntax error. 'X' is treated as 'x' (find resolves -perm with no per-file
// context, so the conditional-execute form degenerates to plain execute).
// NOLINTNEXTLINE(readability-function-cognitive-complexity): cohesive dispatch
std::optional<std::uint32_t> ApplyPermClause(std::string_view clause, std::uint32_t want) {
  bool user = false;
  bool group = false;
  bool other = false;
  std::size_t idx = 0;
  for (; idx < clause.size(); ++idx) {
    const char who = clause[idx];
    if (who == 'u') {
      user = true;
    } else if (who == 'g') {
      group = true;
    } else if (who == 'o') {
      other = true;
    } else if (who == 'a') {
      user = group = other = true;
    } else {
      break;
    }
  }
  if (!user && !group && !other) {
    user = group = other = true;  // an omitted "who" behaves as 'a' (find applies no umask)
  }
  if (idx >= clause.size()) {
    return std::nullopt;  // missing operator
  }
  const char op = clause[idx++];
  if (op != '+' && op != '-' && op != '=') {
    return std::nullopt;
  }
  bool read = false;
  bool write = false;
  bool exec = false;
  bool setid = false;
  bool sticky = false;
  for (; idx < clause.size(); ++idx) {
    switch (clause[idx]) {
      case 'X':
      case 'x': exec = true; break;
      case 'r': read = true; break;
      case 's': setid = true; break;
      case 't': sticky = true; break;
      case 'w': write = true; break;
      default: return std::nullopt;
    }
  }
  std::uint32_t pattern = 0;
  std::uint32_t who_mask = 0;
  if (user) {
    pattern |= (read ? 0400U : 0U) | (write ? 0200U : 0U) | (exec ? 0100U : 0U) | (setid ? 04000U : 0U);
    who_mask |= 04700U;
  }
  if (group) {
    pattern |= (read ? 0040U : 0U) | (write ? 0020U : 0U) | (exec ? 0010U : 0U) | (setid ? 02000U : 0U);
    who_mask |= 02070U;
  }
  if (other) {
    pattern |= (read ? 0004U : 0U) | (write ? 0002U : 0U) | (exec ? 0001U : 0U);
    who_mask |= 01007U;
  }
  if (sticky) {
    pattern |= 01000U;
  }
  if (op == '+') {
    want |= pattern;
  } else if (op == '-') {
    want &= ~pattern;
  } else {
    want = (want & ~who_mask) | pattern;  // '=' clears the affected classes first
  }
  return want;
}

// Parses a full symbolic mode (comma-separated clauses) from a zero base, or
// nullopt on any syntax error.
std::optional<std::uint32_t> ParseSymbolicPerm(std::string_view spec) {
  if (spec.empty()) {
    return std::nullopt;
  }
  std::uint32_t want = 0;
  while (true) {
    const std::size_t comma = spec.find(',');
    const std::optional<std::uint32_t> applied = ApplyPermClause(spec.substr(0, comma), want);
    if (!applied.has_value()) {
      return std::nullopt;
    }
    want = *applied;
    if (comma == std::string_view::npos) {
      break;
    }
    spec.remove_prefix(comma + 1);
  }
  return want;
}

bool MatchesPerm(std::string_view arg, std::uint32_t mode) {
  char op = '=';
  if (!arg.empty() && (arg.front() == '-' || arg.front() == '/')) {
    op = arg.front();  // '-' all-of, '/' (GNU) any-of, bare exact
    arg.remove_prefix(1);
  } else if (arg.size() > 1 && arg.front() == '+' && arg.find_first_not_of("01234567", 1) == std::string_view::npos) {
    op = '+';  // BSD '+octal' is any-of (like '/'); a symbolic "+r" stays an exact mode (== 0444)
    arg.remove_prefix(1);
  }
  if (arg.empty()) {
    return false;
  }
  std::uint32_t want = 0;
  if (arg.find_first_not_of("01234567") == std::string_view::npos) {
    for (const char digit : arg) {
      want = (want * 8) + static_cast<std::uint32_t>(digit - '0');
    }
  } else {
    const std::optional<std::uint32_t> symbolic = ParseSymbolicPerm(arg);
    if (!symbolic.has_value()) {
      return false;  // neither octal nor a valid symbolic mode
    }
    want = *symbolic;
  }
  const std::uint32_t bits = mode & 07777U;  // permission + setuid/setgid/sticky bits
  if (op == '-') {
    return (bits & want) == want;  // all requested bits set
  }
  if (op == '/' || op == '+') {
    return want == 0 || (bits & want) != 0;  // any requested bit set
  }
  return bits == want;  // exact
}

// find's -empty: an empty regular file (size 0) or a directory with no entries.
bool IsEmpty(const Visit& visit, const vfs::FileSystem& fs) {
  if (visit.metadata.type == vfs::FileType::kRegular) {
    return visit.metadata.size == 0;
  }
  if (visit.metadata.type == vfs::FileType::kDirectory) {
    const absl::StatusOr<std::vector<vfs::Entry>> children = fs.ReadDir(visit.path);
    return children.ok() && children->empty();
  }
  return false;  // find -empty matches only empty regular files and directories
}

// find's -newer FILE: the entry was modified more recently than FILE. FILE is
// stat'd (following symlinks); a missing/unreadable reference makes it false.
// (FILE is re-stat'd per entry for now; resolving it once is a later optimization.)
bool IsNewerThan(const Visit& visit, std::string_view reference, const vfs::FileSystem& fs) {
  const absl::StatusOr<vfs::Metadata> ref =
      fs.StatFields(reference, /*follow_symlinks=*/true, vfs::MetadataFields::kBasic);
  return ref.ok() && visit.metadata.mtime > ref->mtime;
}

// -samefile FILE: the entry is the same file as FILE, i.e. shares its inode AND
// device (so hard links to FILE match). FILE is stat'd following symlinks; a
// missing/unreadable reference makes it false. FILE is re-stat'd per entry for
// now, like IsNewerThan; resolving it once is a later optimization.
bool IsSameFile(const Visit& visit, std::string_view reference, const vfs::FileSystem& fs) {
  const absl::StatusOr<vfs::Metadata> ref =
      fs.StatFields(reference, /*follow_symlinks=*/true, vfs::MetadataFields::kBasic);
  return ref.ok() && visit.metadata.ino == ref->ino && visit.metadata.dev == ref->dev;
}

// Selects a timestamp by find's X/Y letter: a=access, c=inode-change, m=modify,
// B=birth. Birth time is optional (only some kernels/filesystems record it), so
// 'B' may yield no value; a/c/m are always present (returned as an engaged option).
std::optional<absl::Time> TimeField(const vfs::Metadata& metadata, char field) {
  switch (field) {
    case 'a': return metadata.atime;
    case 'c': return metadata.ctime;
    case 'B': return metadata.btime;  // empty when birthtime is unrecorded
    default: return metadata.mtime;   // 'm'
  }
}

// find's -newerXY (X,Y in {a,B,c,m}): the entry's X-time is more recent than the
// reference FILE's Y-time. The reference is stat'd following symlinks; a missing
// reference makes it false, as does an unrecorded birth time on either side (X=B
// or Y=B). (The Y=t time-string form is handled in EvalNewerXY.)
bool IsNewerXY(const Visit& visit, char x, char y, std::string_view reference, const vfs::FileSystem& fs) {
  const absl::StatusOr<vfs::Metadata> ref = fs.StatFields(
      reference, /*follow_symlinks=*/true, y == 'B' ? vfs::MetadataFields::kBirthTime : vfs::MetadataFields::kBasic);
  if (!ref.ok()) {
    return false;
  }
  const std::optional<absl::Time> lhs = TimeField(visit.metadata, x);
  const std::optional<absl::Time> rhs = TimeField(*ref, y);
  return lhs.has_value() && rhs.has_value() && *lhs > *rhs;
}

// BSD's `-mtime`/`-atime`/`-ctime` trailing unit suffix -> the duration of one
// unit. A constexpr map, per the style's preference for a uniform key -> value
// mapping over a switch. Like kSizeUnits, the entries are listed in ascending
// magnitude (s < m < h < d < w) rather than alphabetically -- the natural time
// scale. MakeLimitedMap sorts by key internally, so this ordering is for the reader.
using TimeUnitPair = std::pair<char, absl::Duration>;
constexpr auto kTimeUnits = mbo::container::MakeLimitedMap(
    TimeUnitPair{'s', absl::Seconds(1)},      // second
    TimeUnitPair{'m', absl::Minutes(1)},      // minute
    TimeUnitPair{'h', absl::Hours(1)},        // hour
    TimeUnitPair{'d', absl::Hours(24)},       // day
    TimeUnitPair{'w', absl::Hours(24 * 7)});  // week

// find's -mtime/-mmin: the entry was modified N units ago -- 24h for -mtime, one
// minute for -mmin -- with any fractional unit discarded (floor), so a 2.9-day
// file is "2 days". +N means strictly more than N units ago, -N strictly fewer.
bool MatchesTime(std::string_view arg, absl::Time mtime, absl::Time now, absl::Duration unit, bool allow_unit_suffix) {
  char compare = '=';
  if (!arg.empty() && (arg.front() == '+' || arg.front() == '-')) {
    compare = arg.front();
    arg.remove_prefix(1);
  }
  if (arg.empty()) {
    return false;
  }
  // BSD unit suffix: -mtime/-atime/-ctime accept a trailing s/m/h/d/w that
  // overrides the predicate's default unit (e.g. "-mtime -1h"). find-compatible
  // (BSD); the GNU -mmin/-amin/-cmin family keeps integer minutes (no suffix).
  if (allow_unit_suffix && (arg.back() < '0' || arg.back() > '9')) {
    const auto it = kTimeUnits.find(arg.back());
    if (it == kTimeUnits.end()) {
      return false;  // unrecognised suffix
    }
    unit = it->second;
    arg.remove_suffix(1);
    if (arg.empty()) {
      return false;
    }
  }
  std::int64_t want = 0;
  for (const char digit : arg) {
    if (digit < '0' || digit > '9') {
      return false;
    }
    want = (want * 10) + (digit - '0');
  }
  const auto units = static_cast<std::int64_t>((now - mtime) / unit);
  if (compare == '+') {
    return units > want;
  }
  if (compare == '-') {
    return units < want;
  }
  return units == want;
}

// xff word/compound duration on -mtime/-atime/-ctime (e.g. "-3 weeks 3 hours",
// "+2 days"): '+' = older than the span, '-' = younger than it. The span reuses
// ParseTimeString ("<span> ago" is the instant the span reaches back to), so the
// full relative grammar (compound terms, calendar units) carries over. The form
// requires an explicit sign; an unparseable span never matches. This is the lone
// xff-only time form (gated out of --config=find); see docs/design-find-flavors.md.
bool MatchesAge(std::string_view arg, absl::Time time, absl::Time now, absl::TimeZone tz) {
  if (arg.empty() || (arg.front() != '+' && arg.front() != '-')) {
    return false;
  }
  const char compare = arg.front();
  arg.remove_prefix(1);
  const std::optional<absl::Time> ref = datetime::ParseTimeString(absl::StrCat(arg, " ago"), now, tz);
  if (!ref.has_value()) {
    return false;
  }
  return compare == '+' ? time < *ref : time > *ref;
}

// Parses an all-digits string as an unsigned id, or nullopt otherwise.
std::optional<std::uint32_t> ParseId(std::string_view text) {
  if (text.empty()) {
    return std::nullopt;
  }
  std::uint32_t id = 0;
  for (const char ch : text) {
    if (ch < '0' || ch > '9') {
      return std::nullopt;
    }
    id = (id * 10) + static_cast<std::uint32_t>(ch - '0');
  }
  return id;
}

// find's -user/-group NAME: resolve NAME via the user/group database and compare
// to the entry's owner. If NAME is not a known user/group but is all-digits it is
// taken as a literal id (GNU find behaviour); an unknown non-numeric name yields
// no match here (failing the run on it is deferred to the exit-code work).
std::optional<std::uint32_t> ResolveUid(std::string_view name) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe): single-threaded CLI/test path
  if (const struct passwd* const pw = ::getpwnam(std::string(name).c_str()); pw != nullptr) {
    return static_cast<std::uint32_t>(pw->pw_uid);
  }
  return ParseId(name);
}

std::optional<std::uint32_t> ResolveGid(std::string_view name) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe): single-threaded CLI/test path
  if (const struct group* const gr = ::getgrnam(std::string(name).c_str()); gr != nullptr) {
    return static_cast<std::uint32_t>(gr->gr_gid);
  }
  return ParseId(name);
}

// An optional reference to a node's pre-compiled matcher: empty when the node has
// no regex (no pattern, or it failed to compile). Explicit about the optionality,
// unlike a raw pointer.
using MatcherRef = std::optional<std::reference_wrapper<const regex::Matcher>>;

// Builds a MatcherRef from a node's Expr::matcher (a shared_ptr, possibly null).
MatcherRef AsRef(const std::shared_ptr<const regex::Matcher>& matcher) {
  if (matcher == nullptr) {
    return std::nullopt;
  }
  return std::cref(*matcher);
}

// find's -regex/-iregex: `matcher` (the node's pre-compiled Expr::matcher) must
// match the whole path (not a substring). An empty matcher (no pattern, or it
// failed to compile) matches nothing. When `captures` is present (gated -exec is
// active) a match records its groups there ([0] the whole match, 1..N the groups)
// for the {0}..{N} placeholders.
bool MatchesRegex(
    MatcherRef matcher,
    std::string_view path,
    mbo::types::OptionalRef<std::vector<std::string>> captures) {
  if (!matcher.has_value()) {
    return false;
  }
  const regex::Matcher& re = matcher->get();
  if (!captures.has_value()) {
    return re.FullMatch(path);
  }
  std::optional<std::vector<std::string>> groups = re.FullMatchCaptures(path);
  if (!groups.has_value()) {
    return false;
  }
  *captures = std::move(*groups);
  return true;
}

// Applies a -capture extraction matcher (Expr::matcher, pre-compiled from the
// optional :NAME=REGEX) to `text`: returns capture group 1, or the whole match
// when the regex has no groups, or empty when it does not fully match (or the
// matcher is empty -- no/uncompilable extraction regex).
std::string ExtractCapture(MatcherRef matcher, std::string_view text) {
  if (!matcher.has_value()) {
    return "";
  }
  const std::optional<std::vector<std::string>> groups = matcher->get().FullMatchCaptures(text);
  if (!groups.has_value()) {
    return "";
  }
  return groups->size() > 1 ? (*groups)[1] : (*groups)[0];
}

// --- Per-primary handlers. One free function per leaf test/action, each reading
// what it needs from `expr` and `ctx`. The dispatch table below maps a registry
// name to its handler, so evaluation is a constexpr-map lookup, not a linear
// name scan. Signature is uniform: (const Expr&, EvalContext&) -> bool. ---

bool EvalTrue(const parser::Expr&, EvalContext&) {
  return true;
}

bool EvalFalse(const parser::Expr&, EvalContext&) {
  return false;
}

// -name/-iname (and -path/-ipath below): the descriptor's fold_case selects
// FNM_CASEFOLD, so the case-insensitive variant is registry data, not a separate
// handler. -name and -iname both dispatch here. ctx.fold_name_case additionally
// folds the case-sensitive variant when FS-native matching is in effect (the
// entry is on a case-folding volume, xff style, no --exact), so -name matches
// the way the filesystem itself resolves names.
bool EvalName(const parser::Expr& expr, EvalContext& ctx) {
  const int flags = (expr.descriptor->fold_case || ctx.fold_name_case || expr.case_fold) ? FNM_CASEFOLD : 0;
  return !expr.args.empty() && Fnmatch(expr.args.front(), ctx.visit.name, flags);
}

// The selected fuzzy model (see //xff/fuzzy), over whichever text the primary matches. Case follows
// the same three-way rule -name uses: the always-folding variant, the FS-native default, or an
// explicit --case.
bool EvalFuzzyOn(const parser::Expr& expr, EvalContext& ctx, std::string_view subject) {
  const bool fold = expr.descriptor->fold_case || ctx.fold_name_case || expr.case_fold;
  if (expr.args.empty()) {
    return false;
  }
  if (!ctx.fuzzy_score.has_value() && !expr.fuzzy_threshold.has_value()) {
    if (expr.fuzzy_model == parser::FuzzyModel::kSequence) {
      return fuzzy::Matches(expr.args.front(), subject, fold);
    }
    if (expr.fuzzy_model == parser::FuzzyModel::kFzf) {
      return expr.fuzzy_query ? expr.fuzzy_query->Matches(subject, fold)
                              : fuzzy::FzfQuery(expr.args.front()).Matches(subject, fold);
    }
  }
  std::optional<int> percent;
  switch (expr.fuzzy_model) {
    case parser::FuzzyModel::kFzf:
      percent = expr.fuzzy_query ? expr.fuzzy_query->Percent(subject, fold)
                                 : fuzzy::FzfPercent(expr.args.front(), subject, fold);
      break;
    case parser::FuzzyModel::kSequence: percent = fuzzy::SequencePercent(expr.args.front(), subject, fold); break;
    case parser::FuzzyModel::kLevenshtein: percent = fuzzy::LevenshteinPercent(expr.args.front(), subject, fold); break;
    case parser::FuzzyModel::kShingles: percent = fuzzy::ShinglePercent(expr.args.front(), subject, fold); break;
  }
  if (ctx.fuzzy_score.has_value()) {
    *ctx.fuzzy_score = percent;
  }
  return percent.has_value() && (!expr.fuzzy_threshold.has_value() || *percent >= *expr.fuzzy_threshold);
}

// -first N: true for the first N entries this INSTANCE sees, false afterwards. A test may keep
// state; all it owes is a truth value. The count is keyed by the AST node, so
// `\( -type f -first 10 \) -o \( -type d -first 5 \)` is ten files AND five directories - which is
// exactly what a whole-run global could not express. N is validated before the walk.
// -collect[:NAME]: hold the entry for a post-walk sink instead of printing it. An ACTION, so it is
// always true and (being an action) suppresses the implicit -print - which is what makes
// `-first 10 -collect --summary` a summary with no listing, with no --quiet needed.
bool EvalCollect(const parser::Expr& expr, EvalContext& ctx) {
  if (!ctx.collections.has_value()) {
    return true;  // no sink wired (an in-process caller); collecting is a no-op, not a failure
  }
  const std::string_view name = expr.args.empty() || expr.args.front().empty() ? kDefaultCollection : expr.args.front();
  ctx.collections->Add(name, ctx.visit);
  return true;
}

bool EvalFirst(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty() || !ctx.first_counts.has_value()) {
    return false;
  }
  int limit = 0;
  if (!absl::SimpleAtoi(expr.args.front(), &limit) || limit <= 0) {
    return false;
  }
  int& seen = (*ctx.first_counts)[ExprIdentity{expr}];
  if (seen >= limit) {
    return false;
  }
  ++seen;
  return true;
}

// -fuzzy / -ifuzzy: the BASENAME, the fzf-ish default.
bool EvalFuzzy(const parser::Expr& expr, EvalContext& ctx) {
  return EvalFuzzyOn(expr, ctx, ctx.visit.name);
}

// -fuzzypath / -ifuzzypath: the whole PATH, the -path to -fuzzy's -name.
bool EvalFuzzyPath(const parser::Expr& expr, EvalContext& ctx) {
  return EvalFuzzyOn(expr, ctx, ctx.visit.path);
}

bool EvalPath(const parser::Expr& expr, EvalContext& ctx) {
  const int flags = (expr.descriptor->fold_case || ctx.fold_name_case || expr.case_fold) ? FNM_CASEFOLD : 0;
  return !expr.args.empty() && Fnmatch(expr.args.front(), ctx.visit.path, flags);
}

// {target} render support: a symlink's target text (find %l) for the field
// vocabulary, empty for a non-symlink or on a read error. Symlink-gated, so a
// non-symlink costs no syscall.
std::string LinkTarget(const EvalContext& ctx) {
  if (ctx.visit.metadata.type != vfs::FileType::kSymlink) {
    return "";
  }
  const absl::StatusOr<std::string> target = ctx.fs.ReadLink(ctx.visit.path);
  return target.ok() ? *target : std::string();
}

// -lname/-ilname: glob the symlink's *target* text (the link is never resolved).
// Only a symlink can match; the descriptor's fold_case selects -ilname's
// FNM_CASEFOLD, mirroring -name/-iname.
bool EvalLname(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty() || ctx.visit.metadata.type != vfs::FileType::kSymlink) {
    return false;
  }
  const absl::StatusOr<std::string> target = ctx.fs.ReadLink(ctx.visit.path);
  if (!target.ok()) {
    return false;
  }
  const int flags = (expr.descriptor->fold_case || expr.case_fold) ? FNM_CASEFOLD : 0;
  return Fnmatch(expr.args.front(), *target, flags);
}

// Both -regex and -iregex map here: case sensitivity is baked into the matcher the
// parser compiled (iregex folds case), so the handler just matches the path.
bool EvalRegex(const parser::Expr& expr, EvalContext& ctx) {
  return MatchesRegex(AsRef(expr.matcher), ctx.visit.path, ctx.captures);
}

// Reads the regular-file content a content predicate should search, or nullopt when
// there is nothing to search: a non-regular entry, an unreadable file, or a binary
// file. "Binary" is grep/ripgrep's heuristic -- a NUL byte in the sniffed prefix --
// so content search skips binaries by default instead of emitting noise. The whole
// file is read (hence the predicates' Cost::kExpensive); the prefix sniff only
// decides the binary skip.
std::optional<std::string> ContentToSearch(const Visit& visit, const vfs::FileSystem& fs) {
  if (visit.metadata.type != vfs::FileType::kRegular) {
    return std::nullopt;  // only regular files have searchable content
  }
  absl::StatusOr<std::string> content = fs.ReadContent(visit.path);
  if (!content.ok()) {
    return std::nullopt;  // unreadable: a non-match here (the walk surfaces the read error itself)
  }
  const std::string_view prefix(content->data(), std::min(content->size(), content::kBinaryNulSniffBytes));
  if (absl::StrContains(prefix, '\0')) {
    return std::nullopt;  // a NUL in the sniff window marks the file binary; skip it
  }
  return *std::move(content);
}

// nullopt when `visit` is not a readable regular file; otherwise whether its content is binary -- a
// NUL in the first 8 KiB, the SAME heuristic ContentToSearch uses, so -text / -binary classify a file
// exactly as -grep / -content skip it. Backs -text (content is text, i.e. == false) and -binary
// (== true); both stay false for a non-regular or unreadable entry (nullopt), so they are not
// complements.
std::optional<bool> FileContentIsBinary(const Visit& visit, const vfs::FileSystem& fs) {
  if (visit.metadata.type != vfs::FileType::kRegular) {
    return std::nullopt;
  }
  const absl::StatusOr<std::string> content = fs.ReadContent(visit.path);
  if (!content.ok()) {
    return std::nullopt;
  }
  const std::string_view prefix(content->data(), std::min(content->size(), content::kBinaryNulSniffBytes));
  return absl::StrContains(prefix, '\0');
}

// Whether `content` satisfies the -text FLAVOR (empty flavor == git). One leading UTF-8 BOM is
// transparent. git: no NUL in the first kBinaryNulSniffBytes (the default heuristic,
// EOL-agnostic). The strict flavors forbid a NUL ANYWHERE and pin the line ending, requiring a final
// terminator (or an empty file, which is vacuously complete): posix = no CR, ends with LF; windows =
// CRLF only (no bare CR/LF), ends with CRLF; apple = no LF, ends with CR. A non-empty file with no
// proper terminator matches only git; mixed endings match only git.
bool TextMatchesFlavor(std::string_view content, std::string_view flavor) {
  constexpr std::string_view kUtf8Bom = "\xef\xbb\xbf";
  if (content.starts_with(kUtf8Bom)) {
    content.remove_prefix(kUtf8Bom.size());
  }
  if (flavor.empty() || flavor == "git") {
    return !absl::StrContains(content.substr(0, std::min(content.size(), content::kBinaryNulSniffBytes)), '\0');
  }
  if (absl::StrContains(content, '\0')) {
    return false;  // a strict flavor is not text if a NUL appears anywhere
  }
  if (flavor == "posix") {
    return !absl::StrContains(content, '\r') && (content.empty() || content.back() == '\n');
  }
  if (flavor == "apple") {
    return !absl::StrContains(content, '\n') && (content.empty() || content.back() == '\r');
  }
  if (flavor == "windows") {  // pure CRLF: no bare CR, no bare LF; ends with CRLF (or empty)
    for (std::size_t i = 0; i < content.size(); ++i) {
      const bool bare_cr = content[i] == '\r' && (i + 1 == content.size() || content[i + 1] != '\n');
      const bool bare_lf = content[i] == '\n' && (i == 0 || content[i - 1] != '\r');
      if (bare_cr || bare_lf) {
        return false;
      }
    }
    return content.empty() || (content.size() >= 2 && content[content.size() - 2] == '\r' && content.back() == '\n');
  }
  return false;  // unreachable: the parser validates the flavor token
}

// xff -text[=git|posix|windows|apple] / -binary: classify a regular file's content. -text matches
// text (bare / =git: the NUL heuristic; the other flavors add a no-NUL-anywhere + line-ending
// discipline); -binary matches binary. Neither matches a non-regular or unreadable entry.
bool EvalText(const parser::Expr& expr, EvalContext& ctx) {
  if (ctx.visit.metadata.type != vfs::FileType::kRegular) {
    return false;
  }
  const absl::StatusOr<std::string> content = ctx.fs.ReadContent(ctx.visit.path);
  if (!content.ok()) {
    return false;
  }
  return TextMatchesFlavor(*content, expr.text_flavor);
}

bool EvalBinary(const parser::Expr& /*expr*/, EvalContext& ctx) {
  return FileContentIsBinary(ctx.visit, ctx.fs) == true;
}

// Shared body for the -eofnl / -eofcr / -eofcrlf final-terminator lints: TRUE for a regular,
// readable file whose content ends with `terminator`, or is empty (a zero-line file is vacuously
// complete). Tests ONLY the final terminator -- the content-class axis is -text -- so `-text -eofnl`
// is a well-formed (POSIX-style) text file and `-text ! -eofnl` the missing-final-newline lint, and
// `-text:windows -eofcrlf` / `-text:apple -eofcr` are their CRLF / CR analogues. Orthogonal on
// purpose: bundling the binary heuristic here would make `! -eof*` sweep in binaries and non-files.
bool EvalEofTerminator(EvalContext& ctx, std::string_view terminator) {
  if (ctx.visit.metadata.type != vfs::FileType::kRegular) {
    return false;
  }
  const absl::StatusOr<std::string> content = ctx.fs.ReadContent(ctx.visit.path);
  if (!content.ok()) {
    return false;
  }
  return content->empty() || absl::EndsWith(*content, terminator);
}

// xff -eofnl: content ends with LF (a CRLF file, ending in "\r\n", also ends in LF and so matches).
bool EvalEofnl(const parser::Expr& /*expr*/, EvalContext& ctx) {
  return EvalEofTerminator(ctx, "\n");
}

// xff -eofcr: content ends with a bare CR (the classic-Mac / -text:apple terminator). A CRLF file
// ends in LF, not CR, so it does NOT match -eofcr.
bool EvalEofcr(const parser::Expr& /*expr*/, EvalContext& ctx) {
  return EvalEofTerminator(ctx, "\r");
}

// xff -eofcrlf: content ends with CRLF (the Windows / -text:windows terminator).
bool EvalEofcrlf(const parser::Expr& /*expr*/, EvalContext& ctx) {
  return EvalEofTerminator(ctx, "\r\n");
}

// xff -content / -icontent: the file's content contains the argument as a literal
// substring; the -icontent variant folds ASCII case (the descriptor's fold_case,
// like -iname). Non-regular, unreadable, and binary files do not match (see
// ContentToSearch). The literal form sidesteps grep's regex-flavor ambiguity; -rxc
// is the regex counterpart.
bool EvalContent(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty()) {
    return false;
  }
  const std::optional<std::string> content = ContentToSearch(ctx.visit, ctx.fs);
  if (!content.has_value()) {
    return false;
  }
  return (expr.descriptor->fold_case || expr.case_fold) ? absl::StrContainsIgnoreCase(*content, expr.args.front())
                                                        : absl::StrContains(*content, expr.args.front());
}

// xff -rxc / -irxc: the file's content matches the regular expression anywhere (RE2
// PartialMatch, unanchored -- the content counterpart of -regex's whole-path
// FullMatch). The matcher is pre-compiled by the parser, with case folding baked in
// for -irxc. Non-regular, unreadable, and binary files do not match.
bool EvalRxc(const parser::Expr& expr, EvalContext& ctx) {
  const MatcherRef matcher = AsRef(expr.matcher);
  if (!matcher.has_value()) {
    return false;
  }
  const std::optional<std::string> content = ContentToSearch(ctx.visit, ctx.fs);
  return content.has_value() && matcher->get().PartialMatch(*content);
}

// xff -cmp TARGET: true when the entry's content is byte-for-byte identical to
// TARGET's (TRUE = same, like cmp(1)); false when they differ or either side is
// missing/unreadable. TARGET is a field template rendered per entry, so
// `xff A ! -cmp '{def.B}/{relpath}'` lists files that differ from their counterpart
// under tree B. Byte-exact and binary-safe (reads raw content, not ContentToSearch);
// text normalization (--diff-ignore) is -diff's concern. Cost::kExpensive (two reads).
std::string RenderTarget(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty()) {
    return {};
  }
  const std::string link = LinkTarget(ctx);  // owns the {target} text for the render below
  return fields::Template::Compile(expr.args.front())
      .Render(
          fields::RenderContext{
              .path = ctx.visit.path,
              .root = ctx.visit.root,
              .link_target = link,
              .metadata = ctx.visit.metadata,
              .depth = ctx.visit.depth,
              .fs = ctx.fs,
              .tz = ctx.tz,
              .time_format = ctx.time_format,
              .zone_suffix = ctx.zone_suffix,
              .hash_algorithm = ctx.hash_algorithm,
              .hash_encoding = ctx.hash_encoding,
              .captures = AsConstOptionalRef(ctx.captures),
              .defines = ctx.defines,
              .outputs = AsConstOptionalRef(ctx.outputs),
          });
}

bool EvalCmp(const parser::Expr& expr, EvalContext& ctx) {
  const std::string target = RenderTarget(expr, ctx);
  if (target.empty()) {
    return false;  // no target resolved (e.g. an empty template) -> treat as differing
  }
  const absl::StatusOr<std::string> lhs = ctx.fs.ReadContent(ctx.visit.path);
  const absl::StatusOr<std::string> rhs = ctx.fs.ReadContent(target);
  return lhs.ok() && rhs.ok() && *lhs == *rhs;  // byte-exact; TRUE = identical content
}

// xff -similar[:WIDTH[:PCT%]] TARGET: compare text as unique contiguous word shingles. This is an
// exact Jaccard calculation for one reference, not the MinHash approximation needed by a future
// all-pairs clustering reduction.
bool EvalSimilar(const parser::Expr& expr, EvalContext& ctx) {
  const std::optional<std::string> lhs = ContentToSearch(ctx.visit, ctx.fs);
  const std::string target = RenderTarget(expr, ctx);
  if (!lhs.has_value() || target.empty()) {
    return false;
  }
  const absl::StatusOr<std::string> rhs = ctx.fs.ReadContent(target);
  if (!rhs.ok() || absl::StrContains(rhs->substr(0, content::kBinaryNulSniffBytes), '\0')) {
    return false;
  }
  return similarity::WordShinglePercent(*lhs, *rhs, expr.similarity_width) >= expr.similarity_threshold;
}

namespace {

// The output selectors a -diff:STYLE token maps to. `format` and `context` are optional: an
// omitted one falls back to the resolved global default (--diff-format / --diff-context /
// --context, themselves defaulting to unified / 3), so the per-action token wins only over the
// parts it names. `silent` is -diff:none (compute but do not print). The token is already
// syntactically valid (the parser checked it).
struct DiffStyle {
  std::optional<mbo::diff::DiffOptions::OutputFormat> format;
  std::optional<std::size_t> context;
  bool silent = false;
};

DiffStyle ParseDiffStyle(std::string_view style) {
  using OutputFormat = mbo::diff::DiffOptions::OutputFormat;
  if (style.empty()) {
    return {};  // bare -diff: format and context both fall back to the resolved globals
  }
  if (style == "none") {
    return {.silent = true};  // compute-but-silent (a normalized matcher)
  }
  DiffStyle result;
  switch (style.front()) {
    case 'c': result.format = OutputFormat::kContext; break;
    case 'n': result.format = OutputFormat::kNormal; break;
    case 'u': result.format = OutputFormat::kUnified; break;
    case 'y': result.format = OutputFormat::kSideBySide; break;
    default: break;  // unreachable: validated in the parser
  }
  std::size_t context = 0;
  if (style.size() > 1 && absl::SimpleAtoi(style.substr(1), &context)) {
    result.context = context;  // explicit context count (uN / cN / yN) overrides the global
  }
  return result;
}

// A file looks binary when a NUL byte appears in its leading bytes (like GNU diff / grep):
// -diff text-diffs only, so a binary side is byte-compared with a stderr note instead.
bool LooksBinary(std::string_view data) {
  return absl::StrContains(data.substr(0, content::kBinaryNulSniffBytes), '\0');
}

// One --diff-ignore token's effect: sets its normalization bool on the DiffOptions.
// Captureless so the table is a constexpr LimitedMap (the mbo bools are bit-fields, so a
// pointer-to-member cannot address them; a setter can). Like the engine's kPrintfDirectives.
using DiffIgnoreSetter = void (*)(mbo::diff::DiffOptions&);

// The --diff-ignore token vocabulary (ws = all whitespace, change = whitespace changes, trail =
// trailing whitespace, blank = blank lines, case = letter case, eofnl = a missing final newline).
// Single source of truth for the token set -- both the apply path (EvalDiff) and the pre-walk
// validation (ValidateDiffIgnore) go through it. (There is no `lead`/`eol` token: leading
// whitespace is subsumed by `change`/`ws`, and CRLF-vs-LF by `trail`, since a `\r` is trailing
// whitespace.) An unknown token is a usage error.
constexpr auto kDiffIgnoreTokens = mbo::container::MakeLimitedMap(
    std::pair<std::string_view, DiffIgnoreSetter>{
        "blank", [](mbo::diff::DiffOptions& opts) { opts.ignore_blank_lines = true; }},
    std::pair<std::string_view, DiffIgnoreSetter>{
        "case", [](mbo::diff::DiffOptions& opts) { opts.ignore_case = true; }},
    std::pair<std::string_view, DiffIgnoreSetter>{
        "change", [](mbo::diff::DiffOptions& opts) { opts.ignore_consecutive_space = true; }},
    std::pair<std::string_view, DiffIgnoreSetter>{
        "eofnl", [](mbo::diff::DiffOptions& opts) { opts.ignore_missing_final_newline = true; }},
    std::pair<std::string_view, DiffIgnoreSetter>{
        "trail", [](mbo::diff::DiffOptions& opts) { opts.ignore_trailing_space = true; }},
    std::pair<std::string_view, DiffIgnoreSetter>{
        "ws", [](mbo::diff::DiffOptions& opts) { opts.ignore_all_space = true; }});

// Applies the --diff-ignore token list and --diff-ignore-matching regex onto `options`.
// Each token runs its setter via kDiffIgnoreTokens; an unknown token is an InvalidArgument
// (naming it). A non-empty `matching` is compiled into options.ignore_matching_lines; a
// regex that does not compile is an InvalidArgument (carrying RE2's diagnostic). Shared by
// EvalDiff (per entry) and ValidateDiffIgnore (once, pre-walk, to reject a bad value early).
absl::Status ApplyDiffIgnore(std::string_view tokens, std::string_view matching, mbo::diff::DiffOptions& options) {
  for (const std::string_view token : absl::StrSplit(tokens, ',', absl::SkipEmpty())) {
    const auto it = kDiffIgnoreTokens.find(token);
    if (it == kDiffIgnoreTokens.end()) {
      return absl::InvalidArgumentError(
          absl::StrCat("unknown --diff-ignore token '", token, "' (use ws, change, trail, blank, case, or eofnl)"));
    }
    it->second(options);
  }
  if (!matching.empty()) {
    // log_errors(false): a bad pattern is reported via our own message (below) carrying RE2's
    // error(), so RE2 must not also LOG(ERROR) it (which, pre-InitializeLog, is noisy on stderr).
    RE2::Options re2_options;
    re2_options.set_log_errors(false);
    options.ignore_matching_lines.emplace(matching, re2_options);
    if (!options.ignore_matching_lines->ok()) {
      return absl::InvalidArgumentError(
          absl::StrCat("invalid --diff-ignore-matching regex: ", options.ignore_matching_lines->error()));
    }
  }
  return absl::OkStatus();
}

}  // namespace

namespace {
bool EmitCreationPatch(const parser::Expr& expr, EvalContext& ctx) {
  const auto type = ctx.visit.metadata.type;
  if (type == vfs::FileType::kDirectory || expr.diff_style == "none") {
    return false;
  }
  if (type != vfs::FileType::kRegular && type != vfs::FileType::kSymlink) {
    ctx.control.SetUnsupported("-diff creation patches require regular files or symlinks");
    return false;
  }
  const auto content =
      type == vfs::FileType::kSymlink ? ctx.fs.ReadLink(ctx.visit.path) : ctx.fs.ReadContent(ctx.visit.path);
  if (!content.ok()) {
    ctx.control.SetUnsupported(absl::StrCat("-diff cannot read source: ", content.status().message()));
    return false;
  }
  std::string_view path = ctx.visit.path;
  if (path == ctx.visit.root) {
    path = ctx.visit.name;
  } else if (path.starts_with(ctx.visit.root)) {
    path.remove_prefix(ctx.visit.root.size());
    if (path.starts_with('/')) {
      path.remove_prefix(1);
    }
  }
  while (path.starts_with("./")) {
    path.remove_prefix(2);
  }
  std::string_view mode = "100644";
  if (type == vfs::FileType::kSymlink) {
    mode = "120000";
  } else if ((ctx.visit.metadata.mode & 0111U) != 0) {
    mode = "100755";
  }
  const auto rendered = patch::Creation(path, *content, mode);
  if (!rendered.ok()) {
    ctx.control.SetUnsupported(std::string(rendered.status().message()));
    return false;
  }
  ctx.emit(*rendered);
  return false;  // An added file differs from a nonexistent file, even when empty.
}
}  // namespace

// xff -diff[:STYLE] TARGET: an ACTION that diffs the entry against TARGET (a field template,
// like -cmp) via mbo::diff and returns TRUE = same (silent when equal; prints the diff and is
// false on a difference). STYLE picks the output (u3 default / c / n / y / none = silent). A
// binary side is byte-compared with a `Binary files A and B differ` note on stderr, never a
// text diff. Missing/unreadable target -> differs (false). Cost::kExpensive (two reads).
bool EvalDiff(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty() || expr.args.front() == "/dev/null") {
    return EmitCreationPatch(expr, ctx);
  }
  const std::string link = LinkTarget(ctx);  // owns the {target} text for the render below
  const std::string target = fields::Template::Compile(expr.args.front())
                                 .Render(
                                     fields::RenderContext{
                                         .path = ctx.visit.path,
                                         .root = ctx.visit.root,
                                         .link_target = link,
                                         .metadata = ctx.visit.metadata,
                                         .depth = ctx.visit.depth,
                                         .fs = ctx.fs,
                                         .tz = ctx.tz,
                                         .time_format = ctx.time_format,
                                         .zone_suffix = ctx.zone_suffix,
                                         .hash_algorithm = ctx.hash_algorithm,
                                         .hash_encoding = ctx.hash_encoding,
                                         .captures = AsConstOptionalRef(ctx.captures),
                                         .defines = ctx.defines,
                                         .outputs = AsConstOptionalRef(ctx.outputs),
                                     });
  if (target.empty()) {
    return false;  // no target resolved -> treat as differing
  }
  const absl::StatusOr<std::string> lhs_data = ctx.fs.ReadContent(ctx.visit.path);
  const absl::StatusOr<std::string> rhs_data = ctx.fs.ReadContent(target);
  if (!lhs_data.ok() || !rhs_data.ok()) {
    return false;  // missing / unreadable -> differs
  }
  // A binary side is byte-compared, not text-diffed: equal is silent; a difference notes it on
  // stderr (like `diff`/`cmp`) and is false.
  if (LooksBinary(*lhs_data) || LooksBinary(*rhs_data)) {
    if (*lhs_data == *rhs_data) {
      return true;
    }
    std::cerr << "Binary files " << ctx.visit.path << " and " << target << " differ\n";
    return false;
  }
  const DiffStyle style = ParseDiffStyle(expr.diff_style);
  mbo::diff::DiffOptions options;  // default-constructed (myers, unified, ctx 3) then tuned
  // A -diff:STYLE token overrides only the parts it names; an omitted format/context falls back to
  // the resolved global default (--diff-format / --diff-context / --context, else unified / 3).
  options.output_format = style.format.value_or(ctx.diff_format);
  options.context_size = style.context.value_or(ctx.diff_context);
  // Git-style header: an empty time_format omits the per-file mtime (`--- a/one.txt`), so the
  // output is reproducible regardless of the compared files' timestamps or the local time zone.
  options.time_format = "";
  if (const std::optional<mbo::diff::DiffOptions::Algorithm> algo =
          mbo::diff::DiffOptions::ParseAlgorithmFlag(ctx.diff_algorithm);
      algo.has_value()) {
    options.algorithm = *algo;
  }
  // --diff-ignore / --diff-ignore-matching normalization. The values were validated before
  // the walk (ValidateDiffIgnore), so this only configures; the returned status is ignored.
  ApplyDiffIgnore(ctx.diff_ignore, ctx.diff_ignore_matching, options).IgnoreError();
  const absl::StatusOr<std::string> diff = mbo::diff::Diff::FileDiff(
      mbo::file::Artefact{.data = *lhs_data, .name = std::string(ctx.visit.path)},
      mbo::file::Artefact{.data = *rhs_data, .name = target}, options);
  if (!diff.ok()) {
    return false;  // a diff failure -> treat as differing
  }
  if (diff->empty()) {
    return true;  // identical (FileDiff returns "" when the sides are equal)
  }
  if (!style.silent) {
    ctx.emit(*diff);
  }
  return false;  // differ
}

// The entry's digest, read through the filesystem the entry came FROM. That indirection is what
// lets -hash / -hasheq work on an archive member: hashing by path would look for `a.tar!x` on the
// real filesystem and find nothing. Both routes read the whole entry anyway.
std::optional<std::string> DigestOfEntry(const EvalContext& ctx, const hash::AlgoEncoding& spec) {
  const absl::StatusOr<std::string> content = ctx.fs.ReadContent(ctx.visit.path);
  if (!content.ok()) {
    return std::nullopt;  // unreadable / non-regular -> no digest, which every caller treats as a miss
  }
  return hash::HashData(spec.algo, *content, spec.encoding);
}

// xff -hash[:ALGO[/ENCODING]]: an ACTION that prints the entry's digest and path as
// `<digest>  <path>` (the `sha256sum` layout, so the output feeds `<algo>sum -c`). The spec
// picks the algorithm and hex/base64 rendering; empty parts fall back to --hash-algorithm /
// --hash-encoding (sha256 / hex). Reads the file (Cost::kExpensive); an unreadable file emits
// nothing. Like -print it is always true. The spec was validated before the walk
// (ValidateHashArgs), so a parse failure here defensively no-ops.
bool EvalHash(const parser::Expr& expr, EvalContext& ctx) {
  const std::string_view default_algo = ctx.hash_algorithm.empty() ? "sha256" : ctx.hash_algorithm;
  const hash::Encoding default_encoding = hash::ParseEncoding(ctx.hash_encoding).value_or(hash::Encoding::kHex);
  const std::optional<hash::AlgoEncoding> spec = hash::ParseSpec(expr.hash_spec, default_algo, default_encoding);
  if (!spec.has_value()) {
    return true;
  }
  const std::optional<std::string> digest = DigestOfEntry(ctx, *spec);
  if (digest.has_value()) {
    ctx.emit(absl::StrCat(*digest, "  ", ctx.visit.path, "\n"));
  }
  return true;
}

// xff -hasheq EXPECTED: TRUE when the entry's digest equals EXPECTED, the manifest-verification
// companion of -hash. EXPECTED is a field template rendered per entry (so `-hasheq {def.SUMS}`
// checks against a sidecar value, and `! -hasheq {def.SUMS}` lists drift). The algorithm / encoding
// come from `-hasheq:ALGO[/ENCODING]` (or the --hash-algorithm / --hash-encoding defaults), exactly
// like -hash; the hex comparison folds case since sha256sum and SRI differ only in case. An empty
// EXPECTED, an unreadable file, or a bad spec is FALSE (no match), so drift-selection is safe.
// Cost::kExpensive (reads the whole file).
bool EvalHasheq(const parser::Expr& expr, EvalContext& ctx) {
  const auto verdict = [&](bool matched) {
    if (ctx.hash_verification.has_value()) {
      *ctx.hash_verification = matched;
    }
    return matched;
  };
  if (expr.args.empty()) {
    return verdict(false);
  }
  const std::string link = LinkTarget(ctx);  // owns the {target} text for the render below
  const std::string expected = fields::Template::Compile(expr.args.front())
                                   .Render(
                                       fields::RenderContext{
                                           .path = ctx.visit.path,
                                           .root = ctx.visit.root,
                                           .link_target = link,
                                           .metadata = ctx.visit.metadata,
                                           .depth = ctx.visit.depth,
                                           .fs = ctx.fs,
                                           .tz = ctx.tz,
                                           .time_format = ctx.time_format,
                                           .zone_suffix = ctx.zone_suffix,
                                           .hash_algorithm = ctx.hash_algorithm,
                                           .hash_encoding = ctx.hash_encoding,
                                           .captures = AsConstOptionalRef(ctx.captures),
                                           .defines = ctx.defines,
                                           .outputs = AsConstOptionalRef(ctx.outputs),
                                       });
  if (expected.empty()) {
    return verdict(false);  // no expected hash resolved (e.g. an unset {def.X}) -> mismatch
  }
  const std::string_view default_algo = ctx.hash_algorithm.empty() ? "sha256" : ctx.hash_algorithm;
  const hash::Encoding default_encoding = hash::ParseEncoding(ctx.hash_encoding).value_or(hash::Encoding::kHex);
  const std::optional<hash::AlgoEncoding> spec = hash::ParseSpec(expr.hash_spec, default_algo, default_encoding);
  if (!spec.has_value()) {
    return verdict(false);  // defensively no-op; ValidateHashArgs rejects a bad spec before the walk
  }
  const std::optional<std::string> digest = DigestOfEntry(ctx, *spec);
  if (!digest.has_value()) {
    return verdict(false);  // unreadable / non-regular file -> mismatch (selected by `! -hasheq`)
  }
  // Hex digests fold case (sha256sum lowercases, some SRI-style tools upper-case), but base64 is
  // case-sensitive by definition (A-Z and a-z are distinct symbols), so only hex compares loosely.
  return verdict(
      spec->encoding == hash::Encoding::kHex ? absl::EqualsIgnoreCase(*digest, expected) : *digest == expected);
}

// A view over one explicit grep pattern or the union used by default match output.
class GrepMatchers {
 public:
  explicit GrepMatchers(const regex::Matcher& matcher) : single_(matcher) {}

  explicit GrepMatchers(
      absl::Span<const std::shared_ptr<const regex::Matcher>> matchers,
      bool word = false,
      bool rg = false)
      : matchers_(matchers), word_(word), rg_(rg) {}

  bool PartialMatch(std::string_view text) const {
    if (word_) {
      return FindFirst(text).has_value();
    }
    if (single_.has_value()) {
      return single_->PartialMatch(text);
    }
    return std::ranges::any_of(matchers_, [text](const auto& matcher) { return matcher->PartialMatch(text); });
  }

  std::optional<std::pair<std::size_t, std::size_t>> FindFirst(std::string_view text, std::size_t start = 0) const {
    if (single_.has_value()) {
      return single_->FindFirst(text, start);
    }
    std::optional<std::pair<std::size_t, std::size_t>> best;
    for (const auto& matcher : matchers_) {
      auto span = matcher->FindFirst(text, start);
      while (word_ && span && !WordBoundaries(text, *span)) {
        if (span->first == text.size()) {
          span.reset();
          break;
        }
        span = matcher->FindFirst(text, span->first + 1);
      }
      if (span.has_value()
          && (!best.has_value() || span->first < best->first
              || (!rg_ && span->first == best->first && span->second > best->second))) {
        best = span;
      }
    }
    return best;
  }

  std::vector<std::pair<std::size_t, std::size_t>> FindAll(std::string_view text) const {
    if (single_.has_value()) {
      return single_->FindAll(text);
    }
    std::vector<std::pair<std::size_t, std::size_t>> spans;
    std::size_t start = 0;
    while (start <= text.size()) {
      const auto span = FindFirst(text, start);
      if (!span.has_value()) {
        break;
      }
      if (span->second != 0 || rg_) {
        spans.push_back(*span);
      }
      if (span->second != 0) {
        start = span->first + span->second;
      } else if (span->first == text.size()) {
        break;
      } else {
        start = span->first + 1;
      }
    }
    return spans;
  }

 private:
  static bool WordBoundaries(std::string_view text, std::pair<std::size_t, std::size_t> span) {
    const auto word = [](unsigned char chr) { return absl::ascii_isalnum(chr) || chr == '_' || chr >= 128; };
    const auto end = span.first + span.second;
    return (span.first == 0 || !word(text.at(span.first - 1))) && (end == text.size() || !word(text.at(end)));
  }

  mbo::types::OptionalRef<const regex::Matcher> single_;
  absl::Span<const std::shared_ptr<const regex::Matcher>> matchers_;
  bool word_ = false;
  bool rg_ = false;
};

std::string GrepPatternJson(const parser::Expr& expr) {
  if (expr.args.size() == 1) {
    return absl::StrCat(R"(,"pattern":)", render::JsonValue(expr.args.front()));
  }
  std::string result = R"(,"patterns":[)";
  std::string_view separator;
  for (const auto& pattern : expr.args) {
    absl::StrAppend(&result, separator, render::JsonValue(pattern));
    separator = ",";
  }
  result += "]";
  return result;
}

void EmitGrepCount(const parser::Expr& expr, const EvalContext& ctx, std::size_t count) {
  if (ctx.grep_json) {
    ctx.emit(
        absl::StrCat(
            R"({"record":"grep","kind":"count","path":)", render::JsonValue(ctx.visit.path), R"(,"root":)",
            render::JsonValue(ctx.visit.root), GrepPatternJson(expr), R"(,"count":)", count, "}\n"));
  } else {
    ctx.emit(absl::StrCat(ctx.grep.filename ? absl::StrCat(ctx.visit.path, ":") : "", count, "\n"));
  }
}

void EmitGrepLine(const parser::Expr& expr, const EvalContext& ctx, const content::ContextLine& line) {
  if (ctx.grep_json) {
    ctx.emit(
        absl::StrCat(
            R"({"record":"grep","kind":)", render::JsonValue(line.is_match ? "match" : "context"), R"(,"path":)",
            render::JsonValue(ctx.visit.path), R"(,"root":)", render::JsonValue(ctx.visit.root), GrepPatternJson(expr),
            R"(,"line":)", line.number, R"(,"group":)", line.group, R"(,"text":)", render::JsonValue(line.text),
            "}\n"));
  } else {
    const std::string_view separator = line.is_match ? ":" : "-";
    ctx.emit(
        absl::StrCat(
            ctx.grep.filename ? absl::StrCat(ctx.visit.path, separator) : "",
            ctx.grep.line_number ? absl::StrCat(line.number, separator) : "", line.text, "\n"));
  }
}

void EmitGrepRecord(
    const parser::Expr& expr,
    const EvalContext& ctx,
    const content::ContextLine& line,
    std::optional<std::pair<std::size_t, std::size_t>> span) {
  const std::string_view match_text =
      span.has_value() ? line.text.substr(span->first, span->second) : std::string_view{};
  const std::optional<std::size_t> match_column = span.has_value() ? std::optional(span->first + 1) : std::nullopt;
  if (expr.grep_template == nullptr) {
    content::ContextLine output = line;
    if (ctx.grep.only_matching) {
      output.text = match_text;
    }
    EmitGrepLine(expr, ctx, output);
    return;
  }
  ctx.emit(
      expr.grep_template->Render(
          fields::RenderContext{
              .path = ctx.visit.path,
              .root = ctx.visit.root,
              .link_target = LinkTarget(ctx),
              .metadata = ctx.visit.metadata,
              .depth = ctx.visit.depth,
              .fs = ctx.fs,
              .tz = ctx.tz,
              .time_format = ctx.time_format,
              .zone_suffix = ctx.zone_suffix,
              .hash_algorithm = ctx.hash_algorithm,
              .hash_encoding = ctx.hash_encoding,
              .captures = AsConstOptionalRef(ctx.captures),
              .defines = ctx.defines,
              .outputs = AsConstOptionalRef(ctx.outputs),
              .line_number = line.number,
              .line_text = line.text,
              .match_text = match_text,
              .match_column = match_column,
          })
      + "\n");
}

void EmitGrepPath(const parser::Expr& expr, const EvalContext& ctx) {
  if (ctx.grep_json) {
    ctx.emit(
        absl::StrCat(
            R"({"record":"grep","kind":"file","path":)", render::JsonValue(ctx.visit.path), R"(,"root":)",
            render::JsonValue(ctx.visit.root), GrepPatternJson(expr), R"(,"selected":)",
            ctx.grep.output == GrepOptions::Output::kFilesWithMatches ? "true" : "false", "}\n"));
  } else {
    ctx.emit(absl::StrCat(ctx.visit.path, "\n"));
  }
}

std::size_t CountGrepPortions(
    const std::vector<content::ContextLine>& lines,
    const GrepMatchers& matcher,
    bool invert) {
  if (invert) {
    return 0;
  }
  std::size_t count = 0;
  for (const auto& line : lines) {
    count += matcher.FindAll(line.text).size();
  }
  return count;
}

void EmitSelectedGrepLines(
    const parser::Expr& expr,
    const EvalContext& ctx,
    const std::vector<content::ContextLine>& lines,
    const GrepMatchers& matcher,
    bool with_context) {
  bool first = true;
  std::size_t previous_group = 0;
  for (const auto& line : lines) {
    if (with_context && (!ctx.grep_json || expr.grep_template != nullptr) && !first && line.group != previous_group) {
      ctx.emit("--\n");
    }
    first = false;
    previous_group = line.group;
    if (ctx.grep.max_columns != 0 && line.text.size() > ctx.grep.max_columns) {
      auto omitted = line;
      omitted.text = line.is_match ? "[Omitted long matching line]" : "[Omitted long context line]";
      EmitGrepRecord(expr, ctx, omitted, std::nullopt);
      continue;
    }
    if (ctx.grep.only_matching && !(ctx.grep.rg_mode && ctx.grep.invert)) {
      if (!ctx.grep.invert) {
        for (const auto& span : matcher.FindAll(line.text)) {
          EmitGrepRecord(expr, ctx, line, span);
        }
      }
    } else {
      EmitGrepRecord(
          expr, ctx, line,
          expr.grep_template != nullptr && line.is_match && !ctx.grep.invert ? matcher.FindFirst(line.text)
                                                                             : std::nullopt);
    }
  }
}

// xff -grep PATTERN: the line-output companion of -rxc. Prints each line of the
// file's content that matches, as `path:lineno:text` (grep's piped form). The
// pattern is pre-compiled by the parser under the run's --regextype grammar (RE2 by
// default, the literal engine under EXACT, PCRE2 when built in). Matching is per line,
// so a pattern with no '\n' selects individual lines the way grep does; non-regular,
// unreadable, and binary files yield nothing (see ContentToSearch). Returns true
// according to selected lines (or their absence for files-without-match), even when
// only-matching suppresses empty/inverted portions. Expression OR and quiet mode use that truth.
bool EvalGrepMatchers(
    const parser::Expr& expr,
    EvalContext& ctx,
    const GrepMatchers& matcher,
    std::optional<std::string_view> supplied = std::nullopt) {
  if (expr.args.empty() && !supplied) {
    return false;
  }
  const std::optional<std::string> owned = supplied ? std::nullopt : ContentToSearch(ctx.visit, ctx.fs);
  const auto content = supplied ? supplied : owned ? std::optional<std::string_view>(*owned) : std::nullopt;
  if (!content.has_value()) {
    return false;
  }
  const auto is_match = [&](std::string_view line) { return matcher.PartialMatch(line) != ctx.grep.invert; };
  const auto output = ctx.grep.output;
  const bool line_output = output == GrepOptions::Output::kLines;
  const bool only_matching = ctx.grep.only_matching && !(ctx.grep.rg_mode && ctx.grep.invert);
  const bool with_context = line_output && !only_matching && (ctx.grep_before > 0 || ctx.grep_after > 0);
  const auto lines = content::CollectLineMatchesWithContext(
      *content, is_match, with_context ? ctx.grep_before : 0, with_context ? ctx.grep_after : 0);
  const bool any_match = !lines.empty();
  if (output == GrepOptions::Output::kFilesWithMatches || output == GrepOptions::Output::kFilesWithoutMatch) {
    const bool selected = any_match == (output == GrepOptions::Output::kFilesWithMatches);
    if (selected) {
      EmitGrepPath(expr, ctx);
    }
    return selected;
  }
  if (!line_output) {
    std::size_t count = lines.size();
    if ((output == GrepOptions::Output::kCountMatches || only_matching) && !(ctx.grep.rg_mode && ctx.grep.invert)) {
      count = CountGrepPortions(lines, matcher, ctx.grep.invert);
    }
    if (any_match) {
      EmitGrepCount(expr, ctx, count);
    }
    return any_match;
  }
  EmitSelectedGrepLines(expr, ctx, lines, matcher, with_context);
  return any_match;
}

bool EvalGrep(const parser::Expr& expr, EvalContext& ctx) {
  if (!expr.matcher) {
    return false;
  }
  return EvalGrepMatchers(expr, ctx, GrepMatchers(*expr.matcher));
}

bool EvalType(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && MatchesType(expr.args.front(), ctx.visit.metadata.type);
}

// xff -mime GLOB: match the entry's media type (derived from its extension via the
// mime table) against a shell glob, so -mime 'image/*' selects png/jpeg/... at once.
// Matching is ALWAYS case-insensitive: MIME type/subtype names are case-insensitive per
// RFC 2045 / 6838, so 'IMAGE/*' and 'image/*' behave the same. Both sides are ASCII-lowered and
// glob-compared, so it is independent of --case / -i / -s (which govern the text matchers, not
// this derived vocabulary). Content is not read; an unknown or absent extension is
// application/octet-stream. (See mime::TypeForName for the deferred richer-data plan.)
bool EvalMime(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty()
         && Fnmatch(
             absl::AsciiStrToLower(expr.args.front()), absl::AsciiStrToLower(mime::TypeForName(ctx.visit.name)), 0);
}

// xff -lang GLOB: match the entry's programming/markup language (from its extension or filename
// via the language table) against a shell glob, case-insensitively, so -lang 'c*' selects C / C++
// / C# / CSS / Clojure at once. Content is not read; an unrecognized name has no language (the
// empty string), which only a `*` / empty pattern matches.
bool EvalLang(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty()) {
    return false;
  }
  const std::optional<language::LanguageInfo> info = language::InfoForName(ctx.visit.name);
  if (!info.has_value()) {
    return Fnmatch(expr.args.front(), "", FNM_CASEFOLD);
  }
  if (Fnmatch(expr.args.front(), info->name, FNM_CASEFOLD)) {
    return true;
  }
  return std::ranges::any_of(
      info->aliases, [&](std::string_view alias) { return Fnmatch(expr.args.front(), alias, FNM_CASEFOLD); });
}

// -xtype: like -type, but for a symlink it tests the type of the link's *target*
// (the link is followed). A broken symlink is reported as a symlink, so
// "-xtype l" matches it, matching GNU find under the default -P.
bool EvalXtype(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty()) {
    return false;
  }
  if (ctx.visit.metadata.type != vfs::FileType::kSymlink) {
    return MatchesType(expr.args.front(), ctx.visit.metadata.type);
  }
  const absl::StatusOr<vfs::Metadata> target =
      ctx.fs.StatFields(ctx.visit.path, /*follow_symlinks=*/true, vfs::MetadataFields::kBasic);
  const vfs::FileType type = target.ok() ? target->type : vfs::FileType::kSymlink;
  return MatchesType(expr.args.front(), type);
}

// -fstype: matches when the filesystem holding the entry has the given type
// name (e.g. "ext2/ext3", "apfs", "tmpfs", "nfs"). The recognised names are
// platform-specific -- macOS/BSD report `f_fstypename` verbatim, Linux maps the
// `statfs` magic to a find-compatible name -- so a portable expression usually
// tests a single known value. An unqueryable path never matches.
bool EvalFstype(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty()) {
    return false;
  }
  const absl::StatusOr<std::string> type = ctx.fs.FsType(ctx.visit.path);
  return type.ok() && *type == expr.args.front();
}

bool EvalSize(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && MatchesSize(expr.args.front(), ctx.visit.metadata.size, ctx.block_size);
}

// xff's -blocks: -size's exact grammar, but against the ALLOCATED space (st_blocks *
// 512 bytes) instead of the apparent size. So `-blocks +0` selects files that occupy
// any disk, `-blocks 1` files in one --block-size block (default 512), `-blocks +1M`
// files using more than a mebibyte. The honest "disk-occupancy" counterpart to -size.
bool EvalBlocks(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && MatchesSize(expr.args.front(), ctx.visit.metadata.blocks * 512U, ctx.block_size);
}

bool EvalLinks(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && MatchesNumeric(expr.args.front(), ctx.visit.metadata.nlink);
}

bool EvalInum(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && MatchesNumeric(expr.args.front(), ctx.visit.metadata.ino);
}

// find's -used N: the entry was last accessed N days after its status last
// changed, i.e. trunc((atime - ctime) / day). The delta is negative when the
// access predates the status change; N uses the usual N / +N / -N comparison.
bool EvalUsed(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty()) {
    return false;
  }
  const std::int64_t seconds = absl::ToInt64Seconds(ctx.visit.metadata.atime - ctx.visit.metadata.ctime);
  return MatchesSignedNumeric(expr.args.front(), seconds / 86'400);
}

bool EvalUid(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && MatchesNumeric(expr.args.front(), ctx.visit.metadata.uid);
}

bool EvalGid(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && MatchesNumeric(expr.args.front(), ctx.visit.metadata.gid);
}

bool EvalUser(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty()) {
    return false;
  }
  const std::optional<std::uint32_t> uid = ResolveUid(expr.args.front());
  return uid.has_value() && ctx.visit.metadata.uid == *uid;
}

bool EvalGroup(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty()) {
    return false;
  }
  const std::optional<std::uint32_t> gid = ResolveGid(expr.args.front());
  return gid.has_value() && ctx.visit.metadata.gid == *gid;
}

// find's -nouser / -nogroup: the entry's owner uid / group gid has no entry in
// the passwd / group database (an orphaned id).
bool EvalNouser(const parser::Expr&, EvalContext& ctx) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe): single-threaded CLI/test path
  return ::getpwuid(ctx.visit.metadata.uid) == nullptr;
}

bool EvalNogroup(const parser::Expr&, EvalContext& ctx) {
  // NOLINTNEXTLINE(concurrency-mt-unsafe): single-threaded CLI/test path
  return ::getgrgid(ctx.visit.metadata.gid) == nullptr;
}

bool EvalPerm(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && MatchesPerm(expr.args.front(), ctx.visit.metadata.mode);
}

bool EvalEmpty(const parser::Expr&, EvalContext& ctx) {
  return IsEmpty(ctx.visit, ctx.fs);
}

// find's -sparse: the file has holes -- fewer 512-byte blocks allocated than its
// apparent size needs (st_blocks * 512 < st_size). A zero-size file is not sparse.
bool EvalSparse(const parser::Expr&, EvalContext& ctx) {
  const vfs::Metadata& md = ctx.visit.metadata;
  return md.size > 0 && md.blocks * 512U < md.size;
}

// find's -readable/-writable/-executable: the current user can access the entry
// for that mode (a real access() probe, not just a mode-bit guess).
bool EvalReadable(const parser::Expr&, EvalContext& ctx) {
  return ctx.fs.Access(ctx.visit.path, vfs::AccessMode::kRead);
}

bool EvalWritable(const parser::Expr&, EvalContext& ctx) {
  return ctx.fs.Access(ctx.visit.path, vfs::AccessMode::kWrite);
}

bool EvalExecutable(const parser::Expr&, EvalContext& ctx) {
  return ctx.fs.Access(ctx.visit.path, vfs::AccessMode::kExecute);
}

bool EvalNewer(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && IsNewerThan(ctx.visit, expr.args.front(), ctx.fs);
}

bool EvalSamefile(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && IsSameFile(ctx.visit, expr.args.front(), ctx.fs);
}

// A birth-time predicate hit an entry whose filesystem/kernel did not record a
// birth time: it cannot be evaluated correctly here (an impossible task). Flags the
// entry on the control side-channel for the driver (hard error, or warn-and-skip
// under --skip-unsupported) and returns false, since the predicate cannot match.
bool ReportNoBtime(EvalContext& ctx) {
  ctx.control.unsupported = "birth time is not recorded (the filesystem or kernel does not support it)";
  return false;
}

// -newerXY (X,Y in {a,B,c,m}); -newerXt (Y=t) compares the X-time to a time string.
// Shared by every 8-char -newer* name; reads X/Y from the descriptor. When X=B and
// the entry's birth time is unrecorded the comparison is impossible (ReportNoBtime).
// A Y=B reference whose birth time is unrecorded stays a silent no-match (it is the
// reference file's filesystem, not the walked entry's, that is at issue).
bool EvalNewerXY(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty()) {
    return false;
  }
  const std::string_view name = expr.descriptor->name;
  const char letter = name[6];
  if (letter == 'B' && !ctx.visit.metadata.btime.has_value()) {
    return ReportNoBtime(ctx);  // the walked entry's birth time is required but absent
  }
  if (name[7] == 't') {
    const std::optional<absl::Time> ref = datetime::ParseTimeString(expr.args.front(), ctx.now, ctx.tz);
    const std::optional<absl::Time> field = TimeField(ctx.visit.metadata, letter);
    return ref.has_value() && field.has_value() && *field > *ref;
  }
  return IsNewerXY(ctx.visit, letter, name[7], expr.args.front(), ctx.fs);
}

// find's -anewer/-cnewer: the entry's access/change time is newer than the
// reference file's modification time -- the classic spellings of -neweram /
// -newercm. (-newer itself is mtime-vs-mtime, handled by EvalNewer above.)
bool EvalAnewer(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && IsNewerXY(ctx.visit, 'a', 'm', expr.args.front(), ctx.fs);
}

bool EvalCnewer(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty() && IsNewerXY(ctx.visit, 'c', 'm', expr.args.front(), ctx.fs);
}

bool EvalMtime(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty()) {
    return false;
  }
  const std::string_view arg = expr.args.front();
  if (absl::StrContains(arg, ' ')) {  // xff word/compound duration (e.g. "-3 weeks 3 hours")
    return MatchesAge(arg, ctx.visit.metadata.mtime, ctx.now, ctx.tz);
  }
  return MatchesTime(arg, ctx.visit.metadata.mtime, ctx.now, absl::Hours(24), /*allow_unit_suffix=*/true);
}

bool EvalMmin(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty()
         && MatchesTime(
             expr.args.front(), ctx.visit.metadata.mtime, ctx.now, absl::Minutes(1), /*allow_unit_suffix=*/false);
}

bool EvalAtime(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty()) {
    return false;
  }
  const std::string_view arg = expr.args.front();
  if (absl::StrContains(arg, ' ')) {  // xff word/compound duration
    return MatchesAge(arg, ctx.visit.metadata.atime, ctx.now, ctx.tz);
  }
  return MatchesTime(arg, ctx.visit.metadata.atime, ctx.now, absl::Hours(24), /*allow_unit_suffix=*/true);
}

bool EvalAmin(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty()
         && MatchesTime(
             expr.args.front(), ctx.visit.metadata.atime, ctx.now, absl::Minutes(1), /*allow_unit_suffix=*/false);
}

bool EvalCtime(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty()) {
    return false;
  }
  const std::string_view arg = expr.args.front();
  if (absl::StrContains(arg, ' ')) {  // xff word/compound duration
    return MatchesAge(arg, ctx.visit.metadata.ctime, ctx.now, ctx.tz);
  }
  return MatchesTime(arg, ctx.visit.metadata.ctime, ctx.now, absl::Hours(24), /*allow_unit_suffix=*/true);
}

bool EvalCmin(const parser::Expr& expr, EvalContext& ctx) {
  return !expr.args.empty()
         && MatchesTime(
             expr.args.front(), ctx.visit.metadata.ctime, ctx.now, absl::Minutes(1), /*allow_unit_suffix=*/false);
}

// BSD's -Btime/-Bmin: the birth (creation) time, the -mtime/-mmin of `btime`. Birth
// time is optional -- only some kernels/filesystems record it -- so an entry without
// it cannot satisfy the predicate and is flagged as an impossible task (see
// ReportNoBtime); the driver fails or, under --skip-unsupported, warns and skips.
bool EvalBtime(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty()) {
    return false;
  }
  if (!ctx.visit.metadata.btime.has_value()) {
    return ReportNoBtime(ctx);
  }
  const std::string_view arg = expr.args.front();
  if (absl::StrContains(arg, ' ')) {  // xff word/compound duration
    return MatchesAge(arg, *ctx.visit.metadata.btime, ctx.now, ctx.tz);
  }
  return MatchesTime(arg, *ctx.visit.metadata.btime, ctx.now, absl::Hours(24), /*allow_unit_suffix=*/true);
}

bool EvalBmin(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.empty()) {
    return false;
  }
  if (!ctx.visit.metadata.btime.has_value()) {
    return ReportNoBtime(ctx);
  }
  return MatchesTime(
      expr.args.front(), *ctx.visit.metadata.btime, ctx.now, absl::Minutes(1), /*allow_unit_suffix=*/false);
}

// Record builders shared by the stdout actions (-print/-print0/-ls) and their
// file-writing counterparts (-fprint/-fprint0/-fls), so both emit identical bytes.
std::string PrintRecord(const Visit& visit) {
  return absl::StrCat(visit.path, "\n");
}

std::string Print0Record(const Visit& visit) {
  std::string record(visit.path);
  record.push_back('\0');
  return record;
}

// find's -ls: an `ls -dils`-style line -- inode, 1 KiB blocks, symbolic
// permissions, link count, owner, group, size, time, and path -- rendered in `tz`.
// The single-space-joined fallback when no aligning row sink is wired (e.g. -fls,
// in-process callers); the aligned stdout path goes through LsCells + ColumnBuffer.
std::string LsRecord(const Visit& visit, absl::Time now, absl::TimeZone tz, std::string_view color = {}) {
  // fallback: raw bytes, and the same name-only colouring the aligned path applies
  return absl::StrCat(absl::StrJoin(LsCells(visit, now, tz, std::nullopt, color), " "), "\n");
}

bool EvalPrint(const parser::Expr&, EvalContext& ctx) {
  ctx.emit(PrintRecord(ctx.visit));
  return true;
}

bool EvalPrint0(const parser::Expr&, EvalContext& ctx) {
  ctx.emit(Print0Record(ctx.visit));
  return true;
}

bool EvalLs(const parser::Expr&, EvalContext& ctx) {
  // Aligned path: hand the columns to the driver's ColumnBuffer. Without a row sink
  // (in-process callers, no --buffer wiring) fall back to the single-spaced line.
  if (ctx.emit_ls_row) {
    ctx.emit_ls_row(LsCells(ctx.visit, ctx.now, ctx.tz, ctx.ls_size_units, ctx.ls_color));
  } else {
    ctx.emit(LsRecord(ctx.visit, ctx.now, ctx.tz, ctx.ls_color));
  }
  return true;
}

bool EvalPrintf(const parser::Expr& expr, EvalContext& ctx) {
  if (!expr.args.empty()) {
    ctx.emit(FormatPrintf(expr.args.front(), ctx));  // no implicit newline; the format owns it
  }
  return true;
}

bool EvalPrintln(const parser::Expr&, EvalContext& ctx) {
  ctx.emit(absl::StrCat(ctx.visit.path, kOsLineEnding));  // xff: -print with the OS line ending
  return true;
}

bool EvalPrintfln(const parser::Expr& expr, EvalContext& ctx) {
  if (!expr.args.empty()) {  // xff: -printf plus the OS line ending appended
    ctx.emit(absl::StrCat(FormatPrintf(expr.args.front(), ctx), kOsLineEnding));
  }
  return true;
}

// find's -fprint FILE / -fprint0 FILE / -fls FILE / -fprintf FILE FORMAT: the
// -print/-print0/-ls/-printf output, written to a named file instead of stdout.
// The xff -fprintln / -fprintfln add the OS line ending, the file-writing
// counterparts of -println / -printfln. The driver opens each file once
// (truncating) and appends across firings; with no file sink wired the actions
// are inert (in-process callers that pass none).
bool EvalFprint(const parser::Expr& expr, EvalContext& ctx) {
  if (!expr.args.empty() && ctx.emit_file) {
    ctx.emit_file(expr.args.front(), PrintRecord(ctx.visit));
  }
  return true;
}

// xff: -fprint with the OS line ending (the file-writing form of -println).
bool EvalFprintln(const parser::Expr& expr, EvalContext& ctx) {
  if (!expr.args.empty() && ctx.emit_file) {
    ctx.emit_file(expr.args.front(), absl::StrCat(ctx.visit.path, kOsLineEnding));
  }
  return true;
}

bool EvalFprint0(const parser::Expr& expr, EvalContext& ctx) {
  if (!expr.args.empty() && ctx.emit_file) {
    ctx.emit_file(expr.args.front(), Print0Record(ctx.visit));
  }
  return true;
}

bool EvalFls(const parser::Expr& expr, EvalContext& ctx) {
  if (!expr.args.empty() && ctx.emit_file) {
    ctx.emit_file(expr.args.front(), LsRecord(ctx.visit, ctx.now, ctx.tz));
  }
  return true;
}

// -fprintf takes FILE then FORMAT; the format owns its own terminator, like -printf.
bool EvalFprintf(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.size() >= 2 && ctx.emit_file) {
    ctx.emit_file(expr.args.front(), FormatPrintf(expr.args[1], ctx));
  }
  return true;
}

// xff: -fprintf plus the OS line ending (the file-writing form of -printfln);
// FILE then FORMAT, like -fprintf.
bool EvalFprintfln(const parser::Expr& expr, EvalContext& ctx) {
  if (expr.args.size() >= 2 && ctx.emit_file) {
    ctx.emit_file(expr.args.front(), absl::StrCat(FormatPrintf(expr.args[1], ctx), kOsLineEnding));
  }
  return true;
}

// A write action on a VIRTUAL entry (an archive member today) cannot be carried out: the path exists
// only inside its container, so removing it is impossible and handing it to a child process would hand
// over a path no `open()` can resolve. Refusing through `control.unsupported` puts it on the same
// footing as any other impossible task - a hard error naming the path (exit 2), or a warning that skips
// the entry under --skip-unsupported - rather than a silent no-op or a command that fails obscurely.
//
// Returns true when the action must NOT proceed, having recorded the reason.
// The path an exec-family action should hand its child for this entry, or nullopt when there is none
// and the action must be refused (which this reports, naming the entry's problem).
//
// For an ordinary file that is the entry's own path. For an archive MEMBER there is no path a process
// can open, so `--archive-extract` (ctx.extract) is what makes the action possible at all: the member
// is written to a temporary file and the child is handed that. Without the flag the action is refused
// exactly as before - extracting behind the user's back would silently turn "run this over the files
// you matched" into "run it over copies", which changes what an in-place tool edits.
//
// The returned path stays valid until `ctx.extract` releases it (the caller, after a synchronous
// child) or the run ends (a `+` batch or a -j child, neither of which has finished here).
std::optional<std::string> ExecTargetPath(EvalContext& ctx, std::string_view action) {
  if (ctx.visit.metadata.source == vfs::Source::kLocalFs) {
    return std::string(ctx.visit.path);
  }
  // A mount serves the member in place, so it is preferred over copying it out; when mounting is
  // disarmed or impossible this answers nothing and extraction takes over unchanged.
  if (ctx.mounts.has_value()) {
    if (std::optional<std::string> mounted = ctx.mounts->PathFor(ctx.visit.fs_owner, ctx.visit.path);
        mounted.has_value()) {
      return mounted;
    }
  }
  if (!ctx.extract.has_value()) {
    ctx.control.SetUnsupported(
        absl::StrCat(
            action,
            " cannot run on an archive member: a member has no path a process can open"
            " (use --archive-mount to run it on the member in place, or --archive-extract to run it"
            " on a temporary copy)"));
    return std::nullopt;
  }
  absl::StatusOr<std::string> extracted = ctx.extract->Extract(ctx.fs, ctx.visit.path);
  if (!extracted.ok()) {
    ctx.control.SetUnsupported(absl::StrCat(action, " could not extract the member: ", extracted.status().message()));
    return std::nullopt;
  }
  return *std::move(extracted);
}

// Whether `path` is a temporary copy this action made (rather than the entry's own path or a path
// inside a MOUNT), so the caller knows to release it once the child it was made for has finished.
// Asked of the extractor rather than inferred from "it differs from the entry's path": a mounted
// member also differs, and releasing one would be a request to delete something inside a read-only
// mount - harmless only because Release ignores paths it never handed out.
bool IsExtracted(const EvalContext& ctx, std::string_view path) {
  return ctx.extract.has_value() && absl::c_contains(ctx.extract->Held(), path);
}

bool EvalDelete(const parser::Expr&, EvalContext& ctx) {
  if (ctx.visit.metadata.source != vfs::Source::kLocalFs) {
    auto policy = ctx.archive_mutations;
    policy.dry_run = false;  // permitted previews only queue names; publication remains disabled.
    const auto deletion = policy.Delete();
    const auto writing = policy.Write(true);
    if (!deletion.ok() || !writing.ok()) {
      ctx.control.mutation_error = !deletion.ok() ? deletion : writing;
      return false;
    }
    if (!ctx.archive_deletions.has_value()) {
      ctx.control.SetUnsupported(
          "-delete cannot remove an archive member: members are read-only"
          " (use --archive-delete to rewrite the container without it)");
      return false;  // nothing was deleted, so the action is false as well as reported
    }
    // Recorded, not removed: the container is open and being walked right now, and removing a member
    // means writing the container again from its remaining members. The driver does that per
    // container once the walk is done, so `-delete` is true here in the same sense `-exec ... +` is.
    ctx.archive_deletions->emplace_back(ctx.visit.path);
    return true;
  }
  ctx.control.mutation_error = ctx.fs.Remove(ctx.visit.path);
  return ctx.control.mutation_error.ok();
}

// Renders every -exec/-execdir token through the field vocabulary ({}, {name},
// {path}, {root}, {capture.*}, ...) for --exec-fields, yielding the final argv.
// `path` is what {} and {path} render as: the entry's own path, or the temporary copy an extracted
// member was written to (the child can only open the latter).
std::vector<std::string> RenderExecArgv(const parser::Expr& expr, const EvalContext& ctx, std::string_view path) {
  const std::string link = LinkTarget(ctx);  // outlives render_ctx (its {target} view)
  const fields::RenderContext render_ctx{
      .path = path,
      .root = ctx.visit.root,
      .link_target = link,
      .metadata = ctx.visit.metadata,
      .depth = ctx.visit.depth,
      .fs = ctx.fs,
      .tz = ctx.tz,
      .time_format = ctx.time_format,
      .zone_suffix = ctx.zone_suffix,
      .hash_algorithm = ctx.hash_algorithm,
      .hash_encoding = ctx.hash_encoding,
      .captures = AsConstOptionalRef(ctx.captures),
      .defines = ctx.defines,
      .outputs = AsConstOptionalRef(ctx.outputs),
  };
  std::vector<std::string> argv;
  argv.reserve(expr.args.size());
  for (const std::string& token : expr.args) {
    argv.push_back(fields::Render(token, render_ctx));
  }
  return argv;
}

bool EvalExec(const parser::Expr& expr, EvalContext& ctx) {
  const std::optional<std::string> target = ExecTargetPath(ctx, "-exec");
  if (!target.has_value()) {
    return false;
  }
  if (expr.exec_batch) {
    // `-exec ... +`: queue the full path in the single global ("") bucket; the
    // command runs at end-of-walk (RunFind flushes each batch node in ARG_MAX
    // chunks). The action is true per entry. An extracted member stays extracted
    // until the run ends, because that is when its command finally runs.
    if (ctx.exec_batches.has_value()) {
      (*ctx.exec_batches)[ExprIdentity{expr}][""].emplace_back(*target);
    }
    return true;
  }
  if (ctx.parallel_exec.has_value()) {
    // -j>1: launch the child on the bounded runner. find-exact substitutes {} ->
    // path inside Launch; --exec-fields renders first (no {} remains, empty target).
    // True per entry -- the exit status is collected at the end-of-walk Drain. An
    // extracted member is not released here either: the child is still running.
    if (ctx.exec_fields) {
      ctx.parallel_exec->Launch(RenderExecArgv(expr, ctx, *target), /*target=*/{}, /*dir=*/{});
    } else {
      ctx.parallel_exec->Launch(expr.args, *target, /*dir=*/{});
    }
    return true;
  }
  // find-exact substitutes only {} (-> path); --exec-fields renders each token through the field
  // vocabulary first. Either way the child has finished when this returns, so a temporary copy made
  // for it can go.
  const bool ok =
      ctx.exec_fields ? exec::ExecuteArgs(RenderExecArgv(expr, ctx, *target)) : exec::Execute(expr.args, *target);
  if (IsExtracted(ctx, *target)) {
    ctx.extract->Release(*target);
  }
  return ok;
}

// Splits an entry path into the directory that -execdir/-okdir run the child in
// and the "./<basename>" that find substitutes for {}. A top-level entry (no
// slash) runs in "."; an entry at the root ("/x") runs in "/".
struct ExecDir {
  std::string dir;
  std::string brace;  // "./<basename>"
};

ExecDir SplitExecDir(std::string_view path) {
  const auto slash = path.find_last_of('/');
  if (slash == std::string_view::npos) {
    return {.dir = ".", .brace = absl::StrCat("./", path)};  // top-level entry: run in the current directory
  }
  return {
      .dir = (slash == 0) ? "/" : std::string(path.substr(0, slash)),
      .brace = absl::StrCat("./", path.substr(slash + 1)),
  };
}

// Builds the -ok/-okdir confirmation prompt: each command token with "{}" replaced
// by `subst`, space-joined, then "? " (find's interactive form).
std::string OkPrompt(const std::vector<std::string>& args, std::string_view subst) {
  std::string prompt;
  for (const std::string& token : args) {
    if (!prompt.empty()) {
      prompt += ' ';
    }
    std::string rendered = token;
    for (std::size_t pos = 0; (pos = rendered.find("{}", pos)) != std::string::npos; pos += subst.size()) {
      rendered.replace(pos, 2, std::string(subst));
    }
    prompt += rendered;
  }
  prompt += "? ";
  return prompt;
}

bool EvalExecdir(const parser::Expr& expr, EvalContext& ctx) {
  const std::optional<std::string> path = ExecTargetPath(ctx, "-execdir");
  if (!path.has_value()) {
    return false;
  }
  // Like -exec, but the child runs with its working directory set to the directory
  // containing the matched entry, and find-exact {} expands to "./<basename>". An extracted member's
  // directory is the temporary one it was written to, which is the only directory it has.
  const ExecDir target = SplitExecDir(*path);
  if (expr.exec_batch) {
    // `-execdir ... +`: queue the "./<basename>" under its directory; RunFind runs
    // the command once per directory (cwd = that dir) at end-of-walk.
    if (ctx.exec_batches.has_value()) {
      (*ctx.exec_batches)[ExprIdentity{expr}][target.dir].push_back(target.brace);
    }
    return true;
  }
  if (ctx.parallel_exec.has_value()) {
    // -j>1: launch in the entry's directory (cwd = target.dir). find-exact maps {}
    // -> ./basename inside Launch; --exec-fields renders first (no {} remains).
    if (ctx.exec_fields) {
      ctx.parallel_exec->Launch(RenderExecArgv(expr, ctx, *path), /*target=*/{}, target.dir);
    } else {
      ctx.parallel_exec->Launch(expr.args, target.brace, target.dir);
    }
    return true;
  }
  // find-exact maps {} -> ./basename; --exec-fields renders each token through the field vocabulary
  // (which still sees the full path/root) and spawns in the entry's directory.
  const bool ok = ctx.exec_fields ? exec::ExecuteArgsInDir(RenderExecArgv(expr, ctx, *path), target.dir)
                                  : exec::ExecuteInDir(expr.args, target.dir, target.brace);
  if (IsExtracted(ctx, *path)) {
    ctx.extract->Release(*path);
  }
  return ok;
}

bool EvalOk(const parser::Expr& expr, EvalContext& ctx) {
  const std::optional<std::string> target = ExecTargetPath(ctx, "-ok");
  if (!target.has_value()) {
    return false;
  }
  // Like -exec, but prompt on stderr (find-exact: {} -> path) and run only on an
  // affirmative reply. Declined, or no confirmer wired -> false, per find. The prompt shows the path
  // the child will get, so an extracted member is visibly a temporary copy before anything runs.
  const bool ok = ctx.confirm && ctx.confirm(OkPrompt(expr.args, *target)) && exec::Execute(expr.args, *target);
  if (IsExtracted(ctx, *target)) {
    ctx.extract->Release(*target);
  }
  return ok;
}

bool EvalOkdir(const parser::Expr& expr, EvalContext& ctx) {
  const std::optional<std::string> path = ExecTargetPath(ctx, "-okdir");
  if (!path.has_value()) {
    return false;
  }
  // -okdir is to -execdir what -ok is to -exec: prompt (showing {} -> ./basename),
  // then on an affirmative reply run the command in the matched entry's directory.
  const ExecDir target = SplitExecDir(*path);
  const bool ok = ctx.confirm && ctx.confirm(OkPrompt(expr.args, target.brace))
                  && exec::ExecuteInDir(expr.args, target.dir, target.brace);
  if (IsExtracted(ctx, *path)) {
    ctx.extract->Release(*path);
  }
  return ok;
}

// Shared body of -capture/-capturedir: render the command through the field
// vocabulary, run it (in `dir`, or our directory when `dir` is empty/"."), capture
// stdout, strip trailing newlines, optionally regex-extract, and bind {capture.NAME}.
bool RunCapture(const parser::Expr& expr, EvalContext& ctx, std::string_view dir) {  // args = [NAME, REGEX, cmd...]
  if (!ctx.outputs.has_value()) {
    return true;  // unwired: binding is a no-op, but -capture is always true
  }
  // The command renders through the field vocabulary so {} -> path and prior
  // {capture.*}/{def.*}/{N} resolve (left-to-right chaining).
  const std::string link = LinkTarget(ctx);  // outlives render_ctx (its {target} view)
  const fields::RenderContext render_ctx{
      .path = ctx.visit.path,
      .root = ctx.visit.root,
      .link_target = link,
      .metadata = ctx.visit.metadata,
      .depth = ctx.visit.depth,
      .fs = ctx.fs,
      .tz = ctx.tz,
      .time_format = ctx.time_format,
      .zone_suffix = ctx.zone_suffix,
      .hash_algorithm = ctx.hash_algorithm,
      .hash_encoding = ctx.hash_encoding,
      .captures = AsConstOptionalRef(ctx.captures),
      .defines = ctx.defines,
      .outputs = AsConstOptionalRef(ctx.outputs),
  };
  std::vector<std::string> command;
  command.reserve(expr.args.size() - 2);
  for (std::size_t i = 2; i < expr.args.size(); ++i) {
    command.push_back(fields::Render(expr.args[i], render_ctx));
  }
  std::string value;
  if (const std::optional<std::string> out = exec::CaptureOutput(command, dir); out.has_value()) {
    value = *out;
    while (!value.empty() && value.back() == '\n') {
      value.pop_back();  // strip trailing newline(s)
    }
    if (!expr.args[1].empty()) {
      value = ExtractCapture(AsRef(expr.matcher), value);  // optional regex extraction (matcher pre-compiled)
    }
  }
  (*ctx.outputs)[expr.args[0]] = std::move(value);  // bind {capture.NAME} (last wins)
  return true;                                      // a binding side effect; always true
}

bool EvalCapture(const parser::Expr& expr, EvalContext& ctx) {
  return RunCapture(expr, ctx, /*dir=*/{});
}

// -capturedir: -capture run in the matched entry's directory (the -execdir of -capture).
bool EvalCapturedir(const parser::Expr& expr, EvalContext& ctx) {
  return RunCapture(expr, ctx, Dirname(ctx.visit.path));
}

bool EvalPrune(const parser::Expr&, EvalContext& ctx) {
  ctx.control.prune = true;  // do not descend into this directory; -prune is always true
  return true;
}

bool EvalQuit(const parser::Expr&, EvalContext& ctx) {
  ctx.control.quit = true;  // stop the whole traversal after this entry
  return true;
}

// Engine-side dispatch entry for one primary. A struct (not a bare function
// pointer) so per-primary engine config can be added here as it is designed,
// without changing the table or its call site. Declarative metadata (kind,
// arity, and the coming mode/feature/safety classification) lives on the
// registry Descriptor (the SOT, below the parser); the eval function is the one
// piece that must be engine-side, so it lives here and is keyed by the same
// name. A registry/dispatch consistency test guards the pairing.
using EvalFn = bool (*)(const parser::Expr&, EvalContext&);

struct EvalEntry {
  EvalFn eval = nullptr;
};

using DispatchPair = std::pair<std::string_view, EvalEntry>;
constexpr auto kDispatch = mbo::container::MakeLimitedMap(
    DispatchPair{"-Bmin", {&EvalBmin}},    // capital 'B' sorts before the lowercase entries
    DispatchPair{"-Btime", {&EvalBtime}},  // (ASCII), so the birth-time pair leads the table
    DispatchPair{"-amin", {&EvalAmin}},
    DispatchPair{"-anewer", {&EvalAnewer}},
    DispatchPair{"-atime", {&EvalAtime}},
    DispatchPair{"-binary", {&EvalBinary}},
    DispatchPair{"-blocks", {&EvalBlocks}},
    DispatchPair{"-capture", {&EvalCapture}},
    DispatchPair{"-capturedir", {&EvalCapturedir}},
    DispatchPair{"-cmin", {&EvalCmin}},
    DispatchPair{"-cmp", {&EvalCmp}},
    DispatchPair{"-cnewer", {&EvalCnewer}},
    DispatchPair{"-collect", {&EvalCollect}},
    DispatchPair{"-content", {&EvalContent}},
    DispatchPair{"-ctime", {&EvalCtime}},
    DispatchPair{"-delete", {&EvalDelete}},
    DispatchPair{"-diff", {&EvalDiff}},
    DispatchPair{"-empty", {&EvalEmpty}},
    DispatchPair{"-eofcr", {&EvalEofcr}},
    DispatchPair{"-eofcrlf", {&EvalEofcrlf}},
    DispatchPair{"-eofnl", {&EvalEofnl}},
    DispatchPair{"-exec", {&EvalExec}},
    DispatchPair{"-execdir", {&EvalExecdir}},
    DispatchPair{"-executable", {&EvalExecutable}},
    DispatchPair{"-false", {&EvalFalse}},
    DispatchPair{"-fls", {&EvalFls}},
    DispatchPair{"-fprint", {&EvalFprint}},
    DispatchPair{"-fprint0", {&EvalFprint0}},
    DispatchPair{"-fprintf", {&EvalFprintf}},
    DispatchPair{"-fprintfln", {&EvalFprintfln}},
    DispatchPair{"-fprintln", {&EvalFprintln}},
    DispatchPair{"-fstype", {&EvalFstype}},
    DispatchPair{"-gid", {&EvalGid}},
    DispatchPair{"-grep", {&EvalGrep}},
    DispatchPair{"-group", {&EvalGroup}},
    DispatchPair{"-hash", {&EvalHash}},
    DispatchPair{"-hasheq", {&EvalHasheq}},
    DispatchPair{"-icontent", {&EvalContent}},
    DispatchPair{"-ilname", {&EvalLname}},
    DispatchPair{"-iname", {&EvalName}},
    DispatchPair{"-inum", {&EvalInum}},
    DispatchPair{"-ipath", {&EvalPath}},
    DispatchPair{"-iregex", {&EvalRegex}},
    DispatchPair{"-irxc", {&EvalRxc}},
    DispatchPair{"-iwholename", {&EvalPath}},
    DispatchPair{"-lang", {&EvalLang}},
    DispatchPair{"-links", {&EvalLinks}},
    DispatchPair{"-lname", {&EvalLname}},
    DispatchPair{"-ls", {&EvalLs}},
    DispatchPair{"-mime", {&EvalMime}},
    DispatchPair{"-mmin", {&EvalMmin}},
    DispatchPair{"-mtime", {&EvalMtime}},
    DispatchPair{"-first", {&EvalFirst}},
    DispatchPair{"-fuzzy", {&EvalFuzzy}},
    DispatchPair{"-fuzzypath", {&EvalFuzzyPath}},
    DispatchPair{"-ifuzzy", {&EvalFuzzy}},
    DispatchPair{"-ifuzzypath", {&EvalFuzzyPath}},
    DispatchPair{"-name", {&EvalName}},
    DispatchPair{"-newer", {&EvalNewer}},
    DispatchPair{"-newerBB", {&EvalNewerXY}},  // birthtime -newerXY combos (BSD-compat)
    DispatchPair{"-newerBa", {&EvalNewerXY}},
    DispatchPair{"-newerBc", {&EvalNewerXY}},
    DispatchPair{"-newerBm", {&EvalNewerXY}},
    DispatchPair{"-newerBt", {&EvalNewerXY}},
    DispatchPair{"-neweraB", {&EvalNewerXY}},
    DispatchPair{"-neweraa", {&EvalNewerXY}},
    DispatchPair{"-newerac", {&EvalNewerXY}},
    DispatchPair{"-neweram", {&EvalNewerXY}},
    DispatchPair{"-newerat", {&EvalNewerXY}},
    DispatchPair{"-newerca", {&EvalNewerXY}},
    DispatchPair{"-newercc", {&EvalNewerXY}},
    DispatchPair{"-newercm", {&EvalNewerXY}},
    DispatchPair{"-newerct", {&EvalNewerXY}},
    DispatchPair{"-newercB", {&EvalNewerXY}},
    DispatchPair{"-newermB", {&EvalNewerXY}},
    DispatchPair{"-newerma", {&EvalNewerXY}},
    DispatchPair{"-newermc", {&EvalNewerXY}},
    DispatchPair{"-newermm", {&EvalNewerXY}},
    DispatchPair{"-newermt", {&EvalNewerXY}},
    DispatchPair{"-nogroup", {&EvalNogroup}},
    DispatchPair{"-nouser", {&EvalNouser}},
    DispatchPair{"-ok", {&EvalOk}},
    DispatchPair{"-okdir", {&EvalOkdir}},
    DispatchPair{"-path", {&EvalPath}},
    DispatchPair{"-perm", {&EvalPerm}},
    DispatchPair{"-print", {&EvalPrint}},
    DispatchPair{"-print0", {&EvalPrint0}},
    DispatchPair{"-printf", {&EvalPrintf}},
    DispatchPair{"-printfln", {&EvalPrintfln}},
    DispatchPair{"-println", {&EvalPrintln}},
    DispatchPair{"-prune", {&EvalPrune}},
    DispatchPair{"-quit", {&EvalQuit}},
    DispatchPair{"-readable", {&EvalReadable}},
    DispatchPair{"-regex", {&EvalRegex}},
    DispatchPair{"-rxc", {&EvalRxc}},
    DispatchPair{"-samefile", {&EvalSamefile}},
    DispatchPair{"-similar", {&EvalSimilar}},
    DispatchPair{"-size", {&EvalSize}},
    DispatchPair{"-sparse", {&EvalSparse}},
    DispatchPair{"-text", {&EvalText}},
    DispatchPair{"-true", {&EvalTrue}},
    DispatchPair{"-type", {&EvalType}},
    DispatchPair{"-uid", {&EvalUid}},
    DispatchPair{"-used", {&EvalUsed}},
    DispatchPair{"-user", {&EvalUser}},
    DispatchPair{"-wholename", {&EvalPath}},
    DispatchPair{"-writable", {&EvalWritable}},
    DispatchPair{"-xtype", {&EvalXtype}});

bool EvaluatePredicate(const parser::Expr& expr, EvalContext& ctx) {
  // O(log n) dispatch on the descriptor name. A name not in the table (e.g. a
  // traversal option like -maxdepth, consumed by the walk, not per entry) is a
  // no-op that evaluates true, matching the previous fall-through.
  const auto it = kDispatch.find(expr.descriptor->name);
  return it == kDispatch.end() || it->second.eval(expr, ctx);
}

}  // namespace

std::vector<std::string> LsCells(
    const Visit& visit,
    absl::Time now,
    absl::TimeZone tz,
    std::optional<format::SizeUnits> size_units,
    std::string_view color) {
  const vfs::Metadata& md = visit.metadata;
  const std::uint64_t blocks_1k = (md.blocks + 1) / 2;  // 512-byte blocks -> 1 KiB blocks (rounded up)
  // SizeAligned, not Size: the cell is right-aligned as a whole, so the unit needs its own
  // right-alignment for a bare-byte row to line its `B` up under the `B` of `kB`.
  std::string size = size_units.has_value() ? format::SizeAligned(md.size, *size_units) : std::to_string(md.size);
  // The NAME carries the colour and the metadata never does, as in `ls -l`; empty means plain.
  std::string name = color.empty() ? std::string(visit.path) : absl::StrCat("\033[", color, "m", visit.path, "\033[0m");
  return {
      std::to_string(md.ino),   std::to_string(blocks_1k), SymbolicPerms(md.type, md.mode),
      std::to_string(md.nlink), UserName(md.uid),          GroupName(md.gid),
      std::move(size),          LsTime(md.mtime, now, tz), std::move(name),
  };
}

absl::Span<const LsColumn> LsColumns() {
  static constexpr auto kColumns = std::to_array<LsColumn>({
      {.align = format::Align::kRight, .min_width = 8},  // inode
      {.align = format::Align::kRight, .min_width = 5},  // 1 KiB blocks
      {.align = format::Align::kLeft, .min_width = 10},  // symbolic permissions (fixed width)
      {.align = format::Align::kRight, .min_width = 2},  // link count
      {.align = format::Align::kLeft, .min_width = 8},   // owner
      {.align = format::Align::kLeft, .min_width = 8},   // group
      {.align = format::Align::kRight, .min_width = 8},  // size (bytes)
      {.align = format::Align::kLeft, .min_width = 12},  // time
      {.align = format::Align::kLeft, .min_width = 0},   // path (trailing, unpadded)
  });
  return kColumns;
}

namespace {

bool IsFuzzyOnlyExpression(const parser::Expr& expr) {
  switch (expr.kind) {
    case parser::Expr::Kind::kPredicate: return expr.descriptor->binding == registry::Binding::kFuzzy;
    case parser::Expr::Kind::kNot: return false;
    case parser::Expr::Kind::kAnd:
    case parser::Expr::Kind::kOr:
    case parser::Expr::Kind::kXor: return IsFuzzyOnlyExpression(*expr.lhs) && IsFuzzyOnlyExpression(*expr.rhs);
    case parser::Expr::Kind::kNand:
    case parser::Expr::Kind::kNor:
    case parser::Expr::Kind::kXnor:
    case parser::Expr::Kind::kComma: return false;
  }
  return false;
}

std::optional<int> MinScore(std::optional<int> lhs, std::optional<int> rhs) {
  if (!lhs.has_value()) {
    return rhs;
  }
  if (!rhs.has_value()) {
    return lhs;
  }
  return std::min(*lhs, *rhs);
}

std::optional<int> MaxScore(std::optional<int> lhs, std::optional<int> rhs) {
  if (!lhs.has_value()) {
    return rhs;
  }
  if (!rhs.has_value()) {
    return lhs;
  }
  return std::max(*lhs, *rhs);
}

EvaluationResult EvaluateResult(const parser::Expr& expr, EvalContext& context);

EvaluationResult EvaluateChild(const parser::Expr& node, EvalContext& context) {
  if (context.deferred.has_value()) {
    if (const auto found = context.deferred->memo.find(ExprIdentity{node}); found != context.deferred->memo.end()) {
      return found->second;
    }
  }
  std::optional<int> score;
  mbo::types::OptionalRef<std::optional<int>> outer = context.fuzzy_score;
  if (outer.has_value()) {
    context.fuzzy_score.set_ref(score);
  } else {
    context.fuzzy_score.reset();
  }
  EvaluationResult result = EvaluateResult(node, context);
  if (outer.has_value()) {
    context.fuzzy_score.set_ref(*outer);
  } else {
    context.fuzzy_score.reset();
  }
  if (!(result.deferred || result.unknown) && context.deferred.has_value()) {
    context.deferred->memo.emplace(ExprIdentity{node}, result);
  }
  return result;
}

EvaluationResult EvaluateTop(const parser::Expr& expr, EvalContext& context) {
  if (context.deferred.has_value()) {
    if (const auto found = context.deferred->decisions.find(ExprIdentity{expr});
        found != context.deferred->decisions.end()) {
      return {
          .fuzzy = found->second ? context.incoming_fuzzy_score : std::nullopt,
          .matched = found->second,
      };
    }
  }
  if (context.deferred.has_value()) {
    return {.waiting_at = ExprIdentity{expr}, .fuzzy = context.incoming_fuzzy_score, .deferred = true};
  }
  return {};
}

EvaluationResult EvaluateShardStatus(const parser::Expr& expr, EvalContext& context) {
  if (context.deferred.has_value()) {
    if (const auto found = context.deferred->decisions.find(ExprIdentity{expr});
        found != context.deferred->decisions.end()) {
      return {.matched = found->second};
    }
  }
  if (context.deferred.has_value()) {
    return {.waiting_at = ExprIdentity{expr}, .deferred = true};
  }
  return {};
}

EvaluationResult EvaluateAnd(const parser::Expr& expr, EvalContext& context) {
  EvaluationResult lhs = EvaluateChild(*expr.lhs, context);
  if ((lhs.deferred || lhs.unknown) || !lhs.matched) {
    return lhs;
  }
  const std::optional<int> outer_incoming = context.incoming_fuzzy_score;
  context.incoming_fuzzy_score = MinScore(outer_incoming, lhs.fuzzy);
  const EvaluationResult rhs = EvaluateChild(*expr.rhs, context);
  context.incoming_fuzzy_score = outer_incoming;
  if ((rhs.deferred || rhs.unknown)) {
    return rhs;
  }
  return {.fuzzy = rhs.matched ? MinScore(lhs.fuzzy, rhs.fuzzy) : std::nullopt, .matched = rhs.matched};
}

EvaluationResult EvaluateOr(const parser::Expr& expr, EvalContext& context) {
  EvaluationResult lhs = EvaluateChild(*expr.lhs, context);
  if ((lhs.deferred || lhs.unknown)) {
    return lhs;
  }
  if (!lhs.matched) {
    return EvaluateChild(*expr.rhs, context);
  }
  if (context.fuzzy_score.has_value() && IsFuzzyOnlyExpression(*expr.rhs)) {
    const EvaluationResult rhs = EvaluateChild(*expr.rhs, context);
    if ((rhs.deferred || rhs.unknown)) {
      return rhs;
    }
    return {.fuzzy = rhs.matched ? MaxScore(lhs.fuzzy, rhs.fuzzy) : lhs.fuzzy, .matched = true};
  }
  return lhs;
}

EvaluationResult EvaluateNand(const parser::Expr& expr, EvalContext& context) {
  const EvaluationResult lhs = EvaluateChild(*expr.lhs, context);
  if ((lhs.deferred || lhs.unknown) || !lhs.matched) {
    return (lhs.deferred || lhs.unknown) ? lhs : EvaluationResult{.matched = true};
  }
  const EvaluationResult rhs = EvaluateChild(*expr.rhs, context);
  return (rhs.deferred || rhs.unknown) ? rhs : EvaluationResult{.matched = !rhs.matched};
}

EvaluationResult EvaluateNor(const parser::Expr& expr, EvalContext& context) {
  const EvaluationResult lhs = EvaluateChild(*expr.lhs, context);
  if ((lhs.deferred || lhs.unknown) || lhs.matched) {
    return (lhs.deferred || lhs.unknown) ? lhs : EvaluationResult{.matched = false};
  }
  const EvaluationResult rhs = EvaluateChild(*expr.rhs, context);
  return (rhs.deferred || rhs.unknown) ? rhs : EvaluationResult{.matched = !rhs.matched};
}

EvaluationResult EvaluateXor(const parser::Expr& expr, EvalContext& context) {
  const EvaluationResult lhs = EvaluateChild(*expr.lhs, context);
  if ((lhs.deferred || lhs.unknown)) {
    return lhs;
  }
  const EvaluationResult rhs = EvaluateChild(*expr.rhs, context);
  if ((rhs.deferred || rhs.unknown)) {
    return rhs;
  }
  return {
      .fuzzy = lhs.matched != rhs.matched ? (lhs.matched ? lhs.fuzzy : rhs.fuzzy) : std::nullopt,
      .matched = lhs.matched != rhs.matched,
  };
}

EvaluationResult EvaluateXnor(const parser::Expr& expr, EvalContext& context) {
  const EvaluationResult lhs = EvaluateChild(*expr.lhs, context);
  if ((lhs.deferred || lhs.unknown)) {
    return lhs;
  }
  const EvaluationResult rhs = EvaluateChild(*expr.rhs, context);
  return (rhs.deferred || rhs.unknown) ? rhs : EvaluationResult{.matched = lhs.matched == rhs.matched};
}

void PreviewExecution(const parser::Expr& expr, EvalContext& context) {
  std::string preview = absl::StrCat("would execute ", expr.descriptor->name, " for ", context.visit.path, ":");
  const bool capture = expr.descriptor->binds_capture;
  const std::size_t first = capture ? 2 : 0;
  const bool in_dir = expr.descriptor->execute_in_directory;
  const auto target = SplitExecDir(context.visit.path);
  std::vector<std::string> args;
  if (context.exec_fields && !capture) {
    args = RenderExecArgv(expr, context, context.visit.path);
  } else {
    for (std::size_t index = first; index < expr.args.size(); ++index) {
      std::string arg = expr.args[index];
      const std::string_view subst = in_dir ? std::string_view(target.brace) : context.visit.path;
      for (std::size_t pos = 0; (pos = arg.find("{}", pos)) != std::string::npos; pos += subst.size()) {
        arg.replace(pos, 2, subst);
      }
      args.push_back(std::move(arg));
    }
  }
  for (const std::string& arg : args) {
    absl::StrAppend(&preview, " '", absl::CEscape(arg), "'");
  }
  if (in_dir) {
    absl::StrAppend(&preview, " (cwd ", target.dir, ")");
  }
  absl::StrAppend(&preview, "\n");
  context.emit(preview);
}

EvaluationResult EvaluateResult(const parser::Expr& expr, EvalContext& context) {
  switch (expr.kind) {
    case parser::Expr::Kind::kPredicate: {
      if (expr.descriptor->needs_metadata || (expr.grep_template != nullptr && expr.grep_template->NeedsBirthTime())) {
        context.control.metadata_error = context.visit.EnsureMetadata();
        if (!context.control.metadata_error.ok()) {
          return {.unknown = true};
        }
      }
      if (context.dry_run && expr.descriptor->safety == registry::Safety::kSecurity) {
        PreviewExecution(expr, context);
        return {.unknown = true};
      }
      if (expr.descriptor->control == registry::Control::kTop) {
        return EvaluateTop(expr, context);
      }
      if (expr.descriptor->control == registry::Control::kShardStatus) {
        return EvaluateShardStatus(expr, context);
      }
      const bool matched = EvaluatePredicate(expr, context);
      return {.fuzzy = context.fuzzy_score.has_value() ? *context.fuzzy_score : std::nullopt, .matched = matched};
    }
    case parser::Expr::Kind::kNot: {
      const EvaluationResult value = EvaluateChild(*expr.lhs, context);
      return (value.deferred || value.unknown) ? value : EvaluationResult{.matched = !value.matched};
    }
    case parser::Expr::Kind::kAnd: return EvaluateAnd(expr, context);
    case parser::Expr::Kind::kOr: return EvaluateOr(expr, context);
    case parser::Expr::Kind::kNand: return EvaluateNand(expr, context);
    case parser::Expr::Kind::kNor: return EvaluateNor(expr, context);
    case parser::Expr::Kind::kXor: return EvaluateXor(expr, context);
    case parser::Expr::Kind::kXnor: return EvaluateXnor(expr, context);
    case parser::Expr::Kind::kComma: {
      const EvaluationResult lhs = EvaluateChild(*expr.lhs, context);
      return (lhs.deferred || lhs.unknown) ? lhs : EvaluateChild(*expr.rhs, context);
    }
  }
  return {.matched = true};
}

}  // namespace

absl::StatusOr<MatchOutput> PrepareMatchOutput(const parser::Expr& expression) {
  MatchOutput result;
  std::vector<std::reference_wrapper<const parser::Expr>> pending{std::cref(expression)};
  while (!pending.empty()) {
    const auto& node = pending.back().get();
    pending.pop_back();
    if (node.rhs) {
      pending.push_back(std::cref(*node.rhs));
    }
    if (node.lhs) {
      pending.push_back(std::cref(*node.lhs));
    }
    if (!node.descriptor.has_value() || !node.descriptor->content_match || node.args.empty()) {
      continue;
    }
    if (node.descriptor->regex_argument) {
      if (!node.matcher) {
        return absl::InvalidArgumentError("unbound content matcher");
      }
      result.matchers.push_back(node.matcher);
    } else {
      MBO_ASSIGN_OR_RETURN(
          auto matcher, regex::Matcher::Compile(
                            node.args.front(), node.descriptor->fold_case || node.case_fold, regex::Grammar::kExact));
      result.matchers.push_back(std::make_shared<const regex::Matcher>(std::move(matcher)));
    }
    result.source.args.push_back(node.args.front());
  }
  if (result.matchers.empty()) {
    return absl::InvalidArgumentError("requires a content predicate such as -rxc or -content");
  }
  return result;
}

namespace {
absl::StatusOr<regex::Matcher> CompileRgPattern(
    std::string_view original,
    const parser::RgSearch& search,
    regex::Grammar grammar,
    bool fold_case) {
  std::string pattern(original);
  if (grammar == regex::Grammar::kExact && (search.word || search.line)) {
    pattern.clear();
    for (const char chr : original) {
      if (absl::StrContains(R"(\.^$|()[]{}*+?)", chr)) {
        pattern.push_back('\\');
      }
      pattern.push_back(chr);
    }
    grammar = regex::Grammar::kRe2;
  }
  if (search.line) {
    pattern = absl::StrCat("^(?:", pattern, ")$");
  }
  return regex::Matcher::Compile(pattern, fold_case, grammar);
}
}  // namespace

absl::StatusOr<MatchOutput> PrepareRgOutput(
    const parser::Command& command,
    const vfs::FileSystem& fs,
    bool fold_case,
    bool smart_case) {
  if (!command.rg) {
    return absl::InvalidArgumentError("rg search is not configured");
  }
  const auto& search = *command.rg;
  MatchOutput result;
  for (const auto& input : search.patterns) {
    if (!input.file) {
      if (absl::StrContains(input.value, '\n')) {
        return absl::InvalidArgumentError("rg multiline patterns are not supported");
      }
      result.source.args.push_back(input.value);
      continue;
    }
    MBO_ASSIGN_OR_RETURN(const auto content, fs.ReadContent(input.value));
    std::size_t start = 0;
    while (start < content.size()) {
      const auto end = content.find('\n', start);
      auto line = std::string_view(content).substr(start, end == std::string::npos ? end : end - start);
      if (line.ends_with('\r')) {
        line.remove_suffix(1);
      }
      result.source.args.emplace_back(line);
      start = end == std::string::npos ? content.size() : end + 1;
    }
  }
  const bool uppercase = std::ranges::any_of(result.source.args, [](const std::string& pattern) {
    return std::ranges::any_of(pattern, [](char chr) { return chr >= 'A' && chr <= 'Z'; });
  });
  for (const auto& original : result.source.args) {
    MBO_ASSIGN_OR_RETURN(
        auto matcher, CompileRgPattern(original, search, command.grammar, fold_case || (smart_case && !uppercase)));
    result.matchers.push_back(std::make_shared<const regex::Matcher>(std::move(matcher)));
  }
  return result;
}

absl::StatusOr<bool> EmitRgOutput(const MatchOutput& output, const parser::RgSearch& search, EvalContext& context) {
  if (context.visit.metadata.type != vfs::FileType::kRegular) {
    return false;
  }
  MBO_ASSIGN_OR_RETURN(const auto content, context.fs.ReadContent(context.visit.path));
  if (!search.text && absl::StrContains(content, '\0')) {
    return false;
  }
  return EvalGrepMatchers(
      output.source, context, GrepMatchers(output.matchers, search.word && !search.line, true), content);
}

bool EmitMatchOutput(const MatchOutput& output, EvalContext& context) {
  return EvalGrepMatchers(output.source, context, GrepMatchers(output.matchers));
}

bool Evaluate(const parser::Expr& expr, EvalContext& context) {
  const EvaluationResult result = EvaluateDeferred(expr, context);
  return !(result.deferred || result.unknown) && result.matched;
}

EvaluationResult EvaluateDeferred(const parser::Expr& expr, EvalContext& context) {
  const EvaluationResult result = EvaluateResult(expr, context);
  if (context.fuzzy_score.has_value()) {
    *context.fuzzy_score = result.fuzzy;
  }
  return result;
}

bool ContainsAction(const parser::Expr& expr) {
  switch (expr.kind) {
    // -prune and capture actions do NOT suppress the implicit print:
    // -prune per find's "no actions other than -prune" rule; either capture is a
    // binding side effect (not output). -quit and the print actions do suppress.
    case parser::Expr::Kind::kPredicate:
      return expr.descriptor->kind == registry::Kind::kAction && !expr.descriptor->preserves_implicit_output;
    case parser::Expr::Kind::kNot: return ContainsAction(*expr.lhs);
    case parser::Expr::Kind::kAnd:
    case parser::Expr::Kind::kOr:
    case parser::Expr::Kind::kNand:
    case parser::Expr::Kind::kNor:
    case parser::Expr::Kind::kXor:
    case parser::Expr::Kind::kXnor:
    case parser::Expr::Kind::kComma: return ContainsAction(*expr.lhs) || ContainsAction(*expr.rhs);
  }
  return false;  // Unreachable: every Expr::Kind returns above.
}

DiffDefaultDependencies InspectDiffDefaults(
    std::string_view style,
    mbo::diff::DiffOptions::OutputFormat default_format) {
  const DiffStyle parsed = ParseDiffStyle(style);
  using OutputFormat = mbo::diff::DiffOptions::OutputFormat;
  const OutputFormat format = parsed.format.value_or(default_format);
  return {
      .format = !parsed.silent && !parsed.format.has_value(),
      .context = !parsed.silent && !parsed.context.has_value()
                 && (format == OutputFormat::kUnified || format == OutputFormat::kContext),
  };
}

std::optional<mbo::diff::DiffOptions::OutputFormat> ParseDiffFormatFlag(std::string_view flag) {
  using OutputFormat = mbo::diff::DiffOptions::OutputFormat;
  // The -diff:STYLE letters plus the long names, each mapping to one mbo output format. Keys are
  // alphabetical so the constexpr LimitedMap stays sorted. `none` is deliberately absent: it is
  // the per-action silencer, not a default the walk carries.
  static constexpr auto kFormats = mbo::container::MakeLimitedMap(
      std::pair<std::string_view, OutputFormat>{"c", OutputFormat::kContext},
      std::pair<std::string_view, OutputFormat>{"context", OutputFormat::kContext},
      std::pair<std::string_view, OutputFormat>{"n", OutputFormat::kNormal},
      std::pair<std::string_view, OutputFormat>{"normal", OutputFormat::kNormal},
      std::pair<std::string_view, OutputFormat>{"side-by-side", OutputFormat::kSideBySide},
      std::pair<std::string_view, OutputFormat>{"u", OutputFormat::kUnified},
      std::pair<std::string_view, OutputFormat>{"unified", OutputFormat::kUnified},
      std::pair<std::string_view, OutputFormat>{"y", OutputFormat::kSideBySide});
  const auto it = kFormats.find(flag);
  if (it == kFormats.end()) {
    return std::nullopt;
  }
  return it->second;
}

// Pre-walk validation of the --diff-ignore / --diff-ignore-matching values: runs the same
// ApplyDiffIgnore the per-entry -diff uses against a throwaway DiffOptions, so the token set and
// the regex share one source of truth with the apply path. See the header for the contract.
absl::Status ValidateDiffIgnore(std::string_view tokens, std::string_view matching) {
  mbo::diff::DiffOptions options;
  return ApplyDiffIgnore(tokens, matching, options);
}

absl::Status ValidateSizeArgs(const parser::Expr& expr) {
  if (expr.kind == parser::Expr::Kind::kPredicate) {
    const bool size_like = expr.descriptor.has_value() && expr.descriptor->size_argument;
    if (size_like && !expr.args.empty()) {
      if (const absl::Status status = ParseSizeSpec(expr.args.front()).status(); !status.ok()) {
        return status;
      }
    }
    return absl::OkStatus();
  }
  if (expr.lhs != nullptr) {
    if (const absl::Status status = ValidateSizeArgs(*expr.lhs); !status.ok()) {
      return status;
    }
  }
  if (expr.rhs != nullptr) {
    if (const absl::Status status = ValidateSizeArgs(*expr.rhs); !status.ok()) {
      return status;
    }
  }
  return absl::OkStatus();
}

absl::Status ValidateHashArgs(const parser::Expr& expr) {
  if (expr.kind == parser::Expr::Kind::kPredicate) {
    // -hash and -hasheq share the :ALGO[/ENCODING] spec grammar (Binding::kHash), so both validate here.
    if (expr.descriptor.has_value() && expr.descriptor->binding == registry::Binding::kHash
        && !expr.hash_spec.empty()) {
      // Only the spec's explicit parts matter here, so validate against a concrete default.
      if (!hash::ParseSpec(expr.hash_spec, "sha256", hash::Encoding::kHex).has_value()) {
        return absl::InvalidArgumentError(
            absl::StrCat(
                "'", expr.descriptor->name, ":", expr.hash_spec,
                "': unknown algorithm or encoding (ALGO[/ENCODING]; encoding is hex or base64)"));
      }
    }
    return absl::OkStatus();
  }
  if (expr.lhs != nullptr) {
    if (const absl::Status status = ValidateHashArgs(*expr.lhs); !status.ok()) {
      return status;
    }
  }
  if (expr.rhs != nullptr) {
    if (const absl::Status status = ValidateHashArgs(*expr.rhs); !status.ok()) {
      return status;
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<std::uint64_t> ParseBlockSize(std::string_view spec) {
  // N[unit]: a bare number is bytes (unlike -size, where bare means blocks). Accept
  // find's legacy binary suffixes plus explicit SI/IEC byte units. Lowercase 'b' is
  // rejected because a block size measured in blocks is circular.
  const std::size_t suffix_at = spec.find_first_not_of("0123456789");
  const std::string_view number = suffix_at == std::string_view::npos ? spec : spec.substr(0, suffix_at);
  const std::string_view suffix = suffix_at == std::string_view::npos ? std::string_view{} : spec.substr(suffix_at);
  std::uint64_t unit = 1;
  if (!suffix.empty()) {
    if (suffix == "b") {
      return absl::InvalidArgumentError(
          absl::StrCat("'", spec, "': 'b' is a block count, not a valid block-size unit; use B for bytes"));
    }
    if (suffix.size() == 1 && kSizeUnits.contains(suffix.front())) {
      unit = kSizeUnits.at(suffix.front());
    } else if (const std::uint64_t explicit_unit = values::ParseByteUnit(suffix).value_or(0); explicit_unit != 0) {
      unit = explicit_unit;
    } else {
      return absl::InvalidArgumentError(absl::StrCat("'", spec, "': invalid block-size unit '", suffix, "'"));
    }
  }
  if (number.empty()) {
    return absl::InvalidArgumentError(absl::StrCat("'", spec, "': missing number"));
  }
  std::uint64_t value = 0;
  if (!absl::SimpleAtoi(number, &value)) {
    return absl::InvalidArgumentError(absl::StrCat("'", spec, "': not an unsigned 64-bit number"));
  }
  if (value == 0) {
    return absl::InvalidArgumentError(absl::StrCat("'", spec, "': block size must be positive"));
  }
  if (value > std::numeric_limits<std::uint64_t>::max() / unit) {
    return absl::InvalidArgumentError(absl::StrCat("'", spec, "': block size overflows the 64-bit byte range"));
  }
  return value * unit;
}

std::string PrintfDirectiveLetters() {
  std::string letters;
  for (const auto& [letter, fn] : kPrintfDirectives) {
    letters.push_back(letter);
  }
  return letters;
}

absl::Span<const std::pair<std::string_view, std::string_view>> PrintfDocs() {
  static constexpr auto kDocs = std::to_array<std::pair<std::string_view, std::string_view>>({
      {"%p", "the entry's path"},
      {"%f", "file name (basename)"},
      {"%h", "leading directories (dirname)"},
      {"%d", "depth below the starting point"},
      {"%y", "type letter (f d l b c p s)"},
      {"%s", "size in bytes"},
      {"%i", "inode number"},
      {"%n", "number of hard links"},
      {"%m", "permission bits, octal"},
      {"%u", "owner user name (numeric uid if unknown)"},
      {"%g", "owner group name (numeric gid if unknown)"},
      {"%U", "numeric user id"},
      {"%G", "numeric group id"},
      {"%a %c %t", "access / change / modification time (asctime form)"},
      {"%Ak %Ck %Tk", "atime / ctime / mtime via strftime conversion k (e.g. %TY, %Tj)"},
      {"%%", "a literal percent"},
      {R"(\n \t \r \\ \0)", "newline, tab, carriage return, backslash, NUL"},
      {"%{NAME}", "xff: the {field} vocabulary (%{relpath}, %{size:h}, %{def.X}); see --help=fields"},
      {"%{NAME:qual}",
       "xff: a field with a :qualifier -- time format, {size:h}, s/// rewrite, or path component "
       "(see --help=fields for the full qualifier list)"},
  });
  return kDocs;
}

std::string SizeUnitSuffixes() {
  std::string suffixes;
  for (const auto& [suffix, bytes] : kSizeUnits) {
    suffixes.push_back(suffix);
  }
  return suffixes;
}

absl::Span<const std::pair<std::string_view, std::string_view>> SizeUnitDocs() {
  static constexpr auto kDocs = std::to_array<std::pair<std::string_view, std::string_view>>({
      {"c", "legacy byte unit"},
      {"w", "legacy 2-byte word unit"},
      {"b / bare", "blocks (512 bytes by default; --block-size overrides)"},
      {"k / M / G / T / P / E", "legacy binary units (1024 through 1024^6 bytes)"},
      {"B", "bytes (explicit)"},
      {"kB / MB / GB / TB / PB / EB", "SI units (powers of 1000)"},
      {"KiB / MiB / GiB / TiB / PiB / EiB", "IEC units (powers of 1024)"},
      {"+N / -N", "greater than / less than N units; a bare N matches exactly"},
  });
  return kDocs;
}

}  // namespace xff::engine
