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

// getpwuid()/getgrgid() are POSIX, hidden by glibc under the strict -std=c++23
// build; request them explicitly. No effect on macOS.
#if defined(__linux__) && !defined(_GNU_SOURCE)
# define _GNU_SOURCE 1
#endif

#include "xff/presentation/fields/fields.h"

#include <grp.h>
#include <pwd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/time/time.h"
#include "mbo/container/limited_map.h"
#include "xff/content/line_match.h"
#include "xff/datetime/datetime.h"
#include "xff/env/env.h"
#include "xff/hash/hash.h"
#include "xff/matching/language/language.h"
#include "xff/matching/mime/mime.h"
#include "xff/matching/regex/regex.h"
#include "xff/vfs/entry.h"

namespace xff::fields {
namespace {

namespace stdfs = ::std::filesystem;
using ::mbo::StringOrView;

constexpr std::string_view kEnvironmentNamespace = "env.";
constexpr std::string_view kDefinitionNamespace = "def.";
constexpr std::string_view kCaptureNamespace = "capture.";

// find's -printf %y / the {type} field letter, keyed by file type (kUnknown and any
// unmapped value fall through to 'U'). A constexpr map, per the style's preference
// for a uniform key -> value mapping over a switch.
using TypeLetterPair = std::pair<vfs::FileType, char>;
constexpr auto kTypeLetters = mbo::container::MakeLimitedMap(
    TypeLetterPair{vfs::FileType::kBlockDevice, 'b'},
    TypeLetterPair{vfs::FileType::kCharDevice, 'c'},
    TypeLetterPair{vfs::FileType::kDirectory, 'd'},
    TypeLetterPair{vfs::FileType::kFifo, 'p'},
    TypeLetterPair{vfs::FileType::kRegular, 'f'},
    TypeLetterPair{vfs::FileType::kSocket, 's'},
    TypeLetterPair{vfs::FileType::kSymlink, 'l'});

char TypeLetter(vfs::FileType type) {
  const auto it = kTypeLetters.find(type);
  return it == kTypeLetters.end() ? 'U' : it->second;  // kUnknown / unmapped -> 'U'
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

// The ls -l / stat %A symbolic access string for {access}: a type char followed by
// the owner/group/other rwx triples, with setuid/setgid ('s'/'S') and sticky
// ('t'/'T') folded onto the execute positions. Type char is ls-style ('-' for a
// regular file, not the {type} letter 'f').
std::string AccessString(vfs::FileType type, std::uint32_t mode) {
  std::string out;
  switch (type) {  // alphabetical by enum name
    case vfs::FileType::kBlockDevice: out.push_back('b'); break;
    case vfs::FileType::kCharDevice: out.push_back('c'); break;
    case vfs::FileType::kDirectory: out.push_back('d'); break;
    case vfs::FileType::kFifo: out.push_back('p'); break;
    case vfs::FileType::kRegular: out.push_back('-'); break;
    case vfs::FileType::kSocket: out.push_back('s'); break;
    case vfs::FileType::kSymlink: out.push_back('l'); break;
    case vfs::FileType::kUnknown: out.push_back('?'); break;
  }
  // One rwx triple: r, w, then x with the special (setid/sticky) bit folded in.
  const auto triple = [&out, mode](unsigned r, unsigned w, unsigned x, unsigned special, char set, char clr) {
    out.push_back((mode & r) != 0 ? 'r' : '-');
    out.push_back((mode & w) != 0 ? 'w' : '-');
    const bool exec = (mode & x) != 0;
    out.push_back((mode & special) != 0 ? (exec ? set : clr) : (exec ? 'x' : '-'));
  };
  triple(0400U, 0200U, 0100U, 04000U, 's', 'S');  // owner + setuid
  triple(0040U, 0020U, 0010U, 02000U, 's', 'S');  // group + setgid
  triple(0004U, 0002U, 0001U, 01000U, 't', 'T');  // other + sticky
  return out;
}

// Owner / group name from the password / group database, falling back to the
// numeric id when there is no entry (matching find's %u/%g).
std::string OwnerName(std::uint32_t uid) {
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

// Formats a timestamp for a time field. The qualifier is a datetime preset name
// (find/iso/space/epoch) or a custom absl::FormatTime pattern; empty defaults to
// the "space" ISO form. Rendered in `tz` (the local zone unless --timezone).
StringOrView FormatTimeField(
    absl::Time time,
    std::string_view qualifier,
    absl::TimeZone tz,
    datetime::ZoneSuffix suffix) {
  return datetime::FormatTime(time, qualifier, tz, suffix);
}

// Human-readable size ({size:h}): bytes under 1 KiB as a plain count, otherwise
// a 1024-based unit with one (truncated) decimal, e.g. 1536 -> "1.5K".
std::string HumanSize(std::uint64_t bytes) {
  if (bytes < 1'024) {
    return std::to_string(bytes);
  }
  static constexpr std::string_view kUnits = "KMGTPE";
  std::uint64_t scale = 1'024;
  int unit = 0;
  while (bytes >= scale * 1'024 && unit + 1 < 6) {
    scale *= 1'024;
    ++unit;
  }
  std::string out = std::to_string(bytes / scale);
  out.push_back('.');
  out.push_back(static_cast<char>('0' + ((bytes % scale) * 10 / scale)));
  out.push_back(kUnits[unit]);
  return out;
}

// Per-field renderers. The signature is uniform so they can share one dispatch
// table: (key, qualifier, ctx). `key` is the bound argument for dynamic fields
// (a capture index, an {env.NAME} var, ...), unused (unnamed) by the builtins.
// `path`-derived fields build their own std::filesystem::path.
StringOrView PathField(std::string_view, std::string_view, const RenderContext& ctx) {
  return ctx.path;
}

StringOrView RootField(std::string_view, std::string_view, const RenderContext& ctx) {
  return ctx.root;  // command-line search root (find %H); empty when unset
}

// {target}: a symlink's target text (find %l), the driver having resolved it via
// ReadLink; empty for a non-symlink. Composes with the path-component qualifier
// ({target:name}, {target:core}, ...) like any path-valued field.
StringOrView TargetField(std::string_view, std::string_view, const RenderContext& ctx) {
  return ctx.link_target;
}

// {relpath}: the entry's path relative to the search root it was reached from
// (find's %P) -- the root prefix and its trailing separator removed. Empty for the
// root operand itself, and (best-effort) the whole path when no root is recorded or
// the path is not root-prefixed. Mirrors engine::RelativeTo so `-cmp`/`-diff` targets
// like '{def.B}/{relpath}' address the parallel entry under another tree.
StringOrView RelpathField(std::string_view, std::string_view, const RenderContext& ctx) {
  const std::string_view path = ctx.path;
  const std::string_view root = ctx.root;
  if (root.empty() || path == root) {
    return path == root ? std::string_view{} : path;
  }
  if (path.size() > root.size() && path.starts_with(root)) {
    std::string_view rest = path.substr(root.size());
    while (!rest.empty() && rest.front() == '/') {
      rest.remove_prefix(1);
    }
    return rest;
  }
  return path;  // not root-prefixed (should not happen from the walk)
}

StringOrView DirField(std::string_view, std::string_view, const RenderContext& ctx) {
  std::string parent = stdfs::path(std::string(ctx.path)).parent_path().string();
  if (parent.empty()) {
    return ".";  // find's %h is "." when there is no directory part
  }
  return {std::move(parent)};
}

StringOrView NameField(std::string_view, std::string_view, const RenderContext& ctx) {
  return stdfs::path(std::string(ctx.path)).filename().string();
}

StringOrView StemField(std::string_view, std::string_view, const RenderContext& ctx) {
  return stdfs::path(std::string(ctx.path)).stem().string();
}

StringOrView ExtField(std::string_view, std::string_view, const RenderContext& ctx) {
  std::string ext = stdfs::path(std::string(ctx.path)).extension().string();  // includes the leading '.'
  if (!ext.empty()) {
    ext.erase(0, 1);
  }
  return {std::move(ext)};
}

StringOrView SuffixesField(std::string_view, std::string_view, const RenderContext& ctx) {
  std::string filename = stdfs::path(std::string(ctx.path)).filename().string();
  const std::string::size_type dot = filename.find('.', 1);  // all extensions; a leading dot is not one
  if (dot == std::string::npos) {
    return "";
  }
  filename.erase(0, dot);
  return {std::move(filename)};
}

// {suffix}: the LAST extension WITH its leading dot (pathlib's .suffix: ".gz"),
// complementing {ext} (no dot: "gz") and {suffixes} (all of them: ".tar.gz").
StringOrView SuffixField(std::string_view, std::string_view, const RenderContext& ctx) {
  return stdfs::path(std::string(ctx.path)).extension().string();  // ".gz", or "" when none
}

// {core}: the filename with ALL extensions removed (foo.tar.gz -> foo), the complement
// of {suffixes} ({core} + {suffixes} == {name}, as {stem} + {suffix} == {name}).
StringOrView CoreField(std::string_view, std::string_view, const RenderContext& ctx) {
  std::string filename = stdfs::path(std::string(ctx.path)).filename().string();
  const std::string::size_type dot = filename.find('.', 1);  // first extension; a leading dot is not one
  if (dot != std::string::npos) {
    filename.erase(dot);
  }
  return {std::move(filename)};
}

StringOrView DepthField(std::string_view, std::string_view, const RenderContext& ctx) {
  return std::to_string(ctx.depth);
}

// {line} / {text}: the 1-based number and text of the current -grep match line;
// both empty outside a -grep line (line_number unset), so they no-op elsewhere.
StringOrView LineField(std::string_view, std::string_view, const RenderContext& ctx) {
  return ctx.line_number.has_value() ? std::to_string(*ctx.line_number) : std::string();
}

StringOrView TextField(std::string_view, std::string_view, const RenderContext& ctx) {
  return ctx.line_number.has_value() ? ctx.line_text : std::string_view{};
}

// {match} / {column}: the matched substring on a -grep line (grep -o) and its
// 1-based byte column; empty/absent unless the driver computed a match span.
StringOrView MatchField(std::string_view, std::string_view, const RenderContext& ctx) {
  return ctx.match_column.has_value() ? ctx.match_text : std::string_view{};
}

StringOrView ColumnField(std::string_view, std::string_view, const RenderContext& ctx) {
  return ctx.match_column.has_value() ? std::to_string(*ctx.match_column) : std::string();
}

// {hash} / {hash:ALGO[/ENCODING]}: the digest of the entry's file content. The qualifier is an
// algorithm name optionally followed by /ENCODING (e.g. {hash:md5}, {hash:sha256/base64},
// {hash:/base64}); empty parts fall back to the --hash-algorithm / --hash-encoding defaults
// (sha256 / hex when unset). An unknown algorithm or encoding, or an unreadable file, renders
// empty (the field convention). Reads the file, so it is expensive.
StringOrView HashField(std::string_view, std::string_view qualifier, const RenderContext& ctx) {
  const std::string_view default_algo = ctx.hash_algorithm.empty() ? "sha256" : ctx.hash_algorithm;
  const hash::Encoding default_encoding = hash::ParseEncoding(ctx.hash_encoding).value_or(hash::Encoding::kHex);
  const std::optional<hash::AlgoEncoding> spec = hash::ParseSpec(qualifier, default_algo, default_encoding);
  if (!spec.has_value()) {
    return "";  // unknown algorithm or encoding -> empty, like an unknown field
  }
  if (!ctx.fs.has_value()) {
    return hash::HashFile(spec->algo, ctx.path, spec->encoding).value_or("");
  }
  // Through the entry's own filesystem when there is one: a member's bytes live in its container,
  // and HashFile would find nothing at `a.tar!x`. Both routes read the whole entry either way.
  const absl::StatusOr<std::string> content = ctx.fs->ReadContent(ctx.path);
  if (!content.ok()) {
    return "";  // unreadable -> empty, the field convention
  }
  return hash::HashData(spec->algo, *content, spec->encoding).value_or("");
}

// The line count of the entry, read the same way {hash} reads it: through the filesystem the entry
// came from when there is one (so an archive member counts its own lines), else by path.
std::optional<std::size_t> ReadEntryLineCount(const RenderContext& ctx) {
  if (!ctx.fs.has_value()) {
    return content::FileLineCount(ctx.path);
  }
  const absl::StatusOr<std::string> content = ctx.fs->ReadContent(ctx.path);
  if (!content.ok()) {
    return std::nullopt;
  }
  return content::ContentLineCount(*content);
}

// {lines}: the number of text lines in the entry's file content (like `wc -l`, but also counting a
// final line with no trailing newline). Empty for a non-regular, unreadable, or binary file (a NUL
// byte in the first 8 KiB, grep/ripgrep's heuristic, so binaries render nothing rather than a
// misleading count). Reads the file, so it is expensive; composes with --summary / -printf to tally
// lines across matches.
StringOrView LinesField(std::string_view, std::string_view, const RenderContext& ctx) {
  if (ctx.metadata.type != vfs::FileType::kRegular) {
    return "";  // only regular files have countable content
  }
  const std::optional<std::size_t> lines = ReadEntryLineCount(ctx);
  return lines.has_value() ? std::to_string(*lines) : "";
}

// {shard}: the number of shards in a collapsed --shards set; empty outside shard mode (so it
// no-ops in a normal -printf / --template, like {line}). Size-like fields aggregate across the
// set separately (the context's metadata.size is the set total).
StringOrView ShardField(std::string_view, std::string_view, const RenderContext& ctx) {
  return ctx.shard_count.has_value() ? std::to_string(*ctx.shard_count) : "";
}

// {fuzzy}: normalized quality composed across the successful fuzzy predicates; empty when the
// expression has none. AND keeps the weakest required score, OR the best successful alternative.
StringOrView FuzzyField(std::string_view, std::string_view, const RenderContext& ctx) {
  return ctx.fuzzy_score.has_value() ? std::to_string(*ctx.fuzzy_score) : "";
}

// {lang} / {language}: the entry's programming/markup language (github-linguist name, e.g. "C++",
// "Python"), from its filename/extension via the language table; empty when unrecognized. Content
// is not read, so it is cheap; composes with --summary group-by to tally files per language.
StringOrView LanguageField(std::string_view, std::string_view, const RenderContext& ctx) {
  const std::string name = stdfs::path(std::string(ctx.path)).filename().string();
  return language::LanguageForName(name);
}

std::optional<language::LanguageInfo> LanguageInfo(const RenderContext& ctx) {
  return language::InfoForName(stdfs::path(std::string(ctx.path)).filename().string());
}

StringOrView LanguageTypeField(std::string_view, std::string_view, const RenderContext& ctx) {
  const auto info = LanguageInfo(ctx);
  return info.has_value() ? info->type : std::string_view{};
}

StringOrView LanguageColorField(std::string_view, std::string_view, const RenderContext& ctx) {
  const auto info = LanguageInfo(ctx);
  return info.has_value() ? info->color : std::string_view{};
}

StringOrView LanguageGroupField(std::string_view, std::string_view, const RenderContext& ctx) {
  const auto info = LanguageInfo(ctx);
  return info.has_value() ? info->group : std::string_view{};
}

StringOrView LanguageSourceField(std::string_view, std::string_view, const RenderContext& ctx) {
  const auto info = LanguageInfo(ctx);
  return info.has_value() ? info->source : std::string_view{};
}

StringOrView MimeField(std::string_view, std::string_view, const RenderContext& ctx) {
  const std::string name = stdfs::path(std::string(ctx.path)).filename().string();
  return mime::TypeForName(name);
}

mime::TypeInfo MimeInfo(const RenderContext& ctx) {
  return mime::InfoForName(stdfs::path(std::string(ctx.path)).filename().string());
}

StringOrView MimeCategoryField(std::string_view, std::string_view, const RenderContext& ctx) {
  return std::string(MimeInfo(ctx).Category());
}

StringOrView MimeDescriptionField(std::string_view, std::string_view, const RenderContext& ctx) {
  return MimeInfo(ctx).description;
}

StringOrView MimeCharsetField(std::string_view, std::string_view, const RenderContext& ctx) {
  return MimeInfo(ctx).charset;
}

StringOrView MimeCompressibleField(std::string_view, std::string_view, const RenderContext& ctx) {
  const std::optional<bool> value = MimeInfo(ctx).compressible;
  if (!value.has_value()) {
    return {};
  }
  if (*value) {
    return "yes";
  }
  return "no";
}

StringOrView MimeSourceField(std::string_view, std::string_view, const RenderContext& ctx) {
  return MimeInfo(ctx).source;
}

StringOrView SizeField(std::string_view, std::string_view qualifier, const RenderContext& ctx) {
  return qualifier == "h" ? HumanSize(ctx.metadata.size) : std::to_string(ctx.metadata.size);
}

// {blocks}: allocated 512-byte blocks (st_blocks, find's %b); {blocks:h} the
// human-readable allocated bytes. The on-disk counterpart to {size} (apparent).
StringOrView BlocksField(std::string_view, std::string_view qualifier, const RenderContext& ctx) {
  return qualifier == "h" ? HumanSize(ctx.metadata.blocks * 512U) : std::to_string(ctx.metadata.blocks);
}

StringOrView TypeField(std::string_view, std::string_view, const RenderContext& ctx) {
  // NOLINTNEXTLINE(modernize-return-braced-init-list): braces would build initializer_list<char>{1,c}
  return std::string(1, TypeLetter(ctx.metadata.type));
}

StringOrView InodeField(std::string_view, std::string_view, const RenderContext& ctx) {
  return std::to_string(ctx.metadata.ino);
}

StringOrView LinksField(std::string_view, std::string_view, const RenderContext& ctx) {
  return std::to_string(ctx.metadata.nlink);
}

// A bare {mtime} (no {:qualifier}) uses the --time-format default (ctx.time_format,
// itself empty -> "space"); an explicit {mtime:iso} qualifier always wins.
StringOrView MtimeField(std::string_view, std::string_view qualifier, const RenderContext& ctx) {
  return FormatTimeField(ctx.metadata.mtime, qualifier.empty() ? ctx.time_format : qualifier, ctx.tz, ctx.zone_suffix);
}

StringOrView AtimeField(std::string_view, std::string_view qualifier, const RenderContext& ctx) {
  return FormatTimeField(ctx.metadata.atime, qualifier.empty() ? ctx.time_format : qualifier, ctx.tz, ctx.zone_suffix);
}

StringOrView CtimeField(std::string_view, std::string_view qualifier, const RenderContext& ctx) {
  return FormatTimeField(ctx.metadata.ctime, qualifier.empty() ? ctx.time_format : qualifier, ctx.tz, ctx.zone_suffix);
}

StringOrView BtimeField(std::string_view, std::string_view qualifier, const RenderContext& ctx) {
  const std::string_view spec = qualifier.empty() ? ctx.time_format : qualifier;
  return ctx.metadata.btime.has_value() ? FormatTimeField(*ctx.metadata.btime, spec, ctx.tz, ctx.zone_suffix) : "";
}

StringOrView ModeField(std::string_view, std::string_view, const RenderContext& ctx) {
  return OctalPerm(ctx.metadata.mode);
}

StringOrView UserField(std::string_view, std::string_view, const RenderContext& ctx) {
  return OwnerName(ctx.metadata.uid);
}

StringOrView GroupField(std::string_view, std::string_view, const RenderContext& ctx) {
  return GroupName(ctx.metadata.gid);
}

// {uid} / {gid}: numeric owner / group ids (find's %U/%G), complementing the name
// fields {user}/{group}. {dev}: the device number (find's %D). {access}: the ls -l /
// stat %A symbolic permission string, complementing the octal {mode}/{perm}.
StringOrView UidField(std::string_view, std::string_view, const RenderContext& ctx) {
  return std::to_string(ctx.metadata.uid);
}

StringOrView GidField(std::string_view, std::string_view, const RenderContext& ctx) {
  return std::to_string(ctx.metadata.gid);
}

StringOrView DevField(std::string_view, std::string_view, const RenderContext& ctx) {
  return std::to_string(ctx.metadata.dev);
}

StringOrView AccessField(std::string_view, std::string_view, const RenderContext& ctx) {
  return AccessString(ctx.metadata.type, ctx.metadata.mode);
}

StringOrView EmptyField(std::string_view, std::string_view, const RenderContext&) {
  return "";  // unknown field -> empty
}

// Constexpr field-name -> renderer table, built once at compile time via mbo's
// LimitedMap. Aliases (file/name, ext/extension, mode/perm) share a renderer;
// the empty name backs {}, find's full-path placeholder (an alias for {path}).
using FieldEntry = std::pair<std::string_view, FieldFn>;
constexpr auto kFieldTable = mbo::container::MakeLimitedMap(
    FieldEntry{"", &PathField},  // {} -> full path (find's -exec placeholder)
    FieldEntry{"access", &AccessField},
    FieldEntry{"atime", &AtimeField},
    FieldEntry{"blocks", &BlocksField},
    FieldEntry{"btime", &BtimeField},
    FieldEntry{"column", &ColumnField},
    FieldEntry{"core", &CoreField},
    FieldEntry{"ctime", &CtimeField},
    FieldEntry{"depth", &DepthField},
    FieldEntry{"dev", &DevField},
    FieldEntry{"dir", &DirField},
    FieldEntry{"ext", &ExtField},
    FieldEntry{"extension", &ExtField},
    FieldEntry{"file", &NameField},
    FieldEntry{"fuzzy", &FuzzyField},
    FieldEntry{"gid", &GidField},
    FieldEntry{"group", &GroupField},
    FieldEntry{"hash", &HashField},
    FieldEntry{"inode", &InodeField},
    FieldEntry{"lang", &LanguageField},
    FieldEntry{"lang-color", &LanguageColorField},
    FieldEntry{"lang-group", &LanguageGroupField},
    FieldEntry{"lang-source", &LanguageSourceField},
    FieldEntry{"lang-type", &LanguageTypeField},
    FieldEntry{"language", &LanguageField},
    FieldEntry{"line", &LineField},
    FieldEntry{"lines", &LinesField},
    FieldEntry{"links", &LinksField},
    FieldEntry{"match", &MatchField},
    FieldEntry{"mime", &MimeField},
    FieldEntry{"mime-category", &MimeCategoryField},
    FieldEntry{"mime-charset", &MimeCharsetField},
    FieldEntry{"mime-compressible", &MimeCompressibleField},
    FieldEntry{"mime-description", &MimeDescriptionField},
    FieldEntry{"mime-source", &MimeSourceField},
    FieldEntry{"mode", &ModeField},
    FieldEntry{"mtime", &MtimeField},
    FieldEntry{"name", &NameField},
    FieldEntry{"owner", &UserField},
    FieldEntry{"path", &PathField},
    FieldEntry{"perm", &ModeField},
    FieldEntry{"relpath", &RelpathField},
    FieldEntry{"root", &RootField},
    FieldEntry{"shard", &ShardField},
    FieldEntry{"size", &SizeField},
    FieldEntry{"stem", &StemField},
    FieldEntry{"suffix", &SuffixField},
    FieldEntry{"suffixes", &SuffixesField},
    FieldEntry{"target", &TargetField},
    FieldEntry{"text", &TextField},
    FieldEntry{"type", &TypeField},
    FieldEntry{"uid", &UidField},
    FieldEntry{"user", &UserField});

// Resolves a field name to its renderer; an unknown name renders empty.
FieldFn LookupField(std::string_view name) {
  const auto it = kFieldTable.find(name);
  return it == kFieldTable.end() ? &EmptyField : it->second;
}

struct ParsedField {
  std::string_view name;
  std::string qualifier;
  std::size_t next;
};

// Parse one placeholder, preserving quoted qualifiers that contain literal braces.
// The returned name borrows tmpl; the dequoted qualifier owns its contents.
std::optional<ParsedField> ParseField(std::string_view tmpl, std::size_t start) {
  std::size_t pos = start + 1;
  const std::size_t name_begin = pos;
  while (pos < tmpl.size() && tmpl[pos] != ':' && tmpl[pos] != '}') {
    ++pos;
  }
  if (pos >= tmpl.size()) {
    return std::nullopt;
  }
  const std::string_view name = tmpl.substr(name_begin, pos - name_begin);
  if (tmpl[pos] == '}') {
    return ParsedField{.name = name, .qualifier = {}, .next = pos + 1};
  }
  ++pos;
  if (pos < tmpl.size() && tmpl[pos] == '"') {
    ++pos;
    std::string value;
    while (pos < tmpl.size() && tmpl[pos] != '"') {
      if (tmpl[pos] == '\\' && pos + 1 < tmpl.size() && (tmpl[pos + 1] == '"' || tmpl[pos + 1] == '\\')) {
        value.push_back(tmpl[pos + 1]);
        pos += 2;
      } else {
        value.push_back(tmpl[pos]);
        ++pos;
      }
    }
    if (pos + 1 >= tmpl.size() || tmpl[pos + 1] != '}') {
      return std::nullopt;
    }
    return ParsedField{.name = name, .qualifier = std::move(value), .next = pos + 2};
  }
  const std::size_t end = tmpl.find('}', pos);
  if (end == std::string_view::npos) {
    return std::nullopt;
  }
  return ParsedField{.name = name, .qualifier = std::string(tmpl.substr(pos, end - pos)), .next = end + 1};
}

// Parses a field name that is a run of digits ({0},{1},...) into a capture index;
// Returns -1 for an empty name, a non-digit, or an index outside the int range.
int CaptureIndex(std::string_view name) {
  if (name.empty()) {
    return -1;  // {} is the path alias, not a capture
  }
  int value = 0;
  for (const char ch : name) {
    if (ch < '0' || ch > '9') {
      return -1;
    }
    const int digit = ch - '0';
    if (value > (std::numeric_limits<int>::max() - digit) / 10) {
      return -1;
    }
    value = (value * 10) + digit;
  }
  return value;
}

// Renders a numeric {0}..{N} placeholder: `key` is the digit run; reads the
// matching regex capture from the context ([0] whole match, 1..N groups), empty
// when captures are unset or the index is out of range.
StringOrView CaptureField(std::string_view key, std::string_view, const RenderContext& ctx) {
  const int index = CaptureIndex(key);
  // ResolveName dispatches here only for a non-negative, digit-only capture index.
  if (!ctx.captures.has_value() || std::cmp_greater_equal(index, ctx.captures->size())) {
    return "";
  }
  return std::string_view((*ctx.captures)[static_cast<std::size_t>(index)]);
}

// Renders {env.NAME}: `key` is NAME; the process environment value, or empty
// when unset. Read through the env cache (the single getenv site, xff/env).
StringOrView EnvField(std::string_view key, std::string_view, const RenderContext&) {
  return env::Get(key).value_or("");
}

// Renders {def.NAME}: `key` is NAME; the --define value, or empty when undefined.
StringOrView DefField(std::string_view key, std::string_view, const RenderContext& ctx) {
  if (!ctx.defines.has_value()) {
    return "";
  }
  const auto it = ctx.defines->find(std::string(key));
  return it == ctx.defines->end() ? std::string_view{} : std::string_view(it->second);
}

// Renders {capture.NAME}: `key` is NAME; a -capture result, empty when unset.
StringOrView OutputField(std::string_view key, std::string_view, const RenderContext& ctx) {
  if (!ctx.outputs.has_value()) {
    return "";
  }
  const auto it = ctx.outputs->find(std::string(key));
  return it == ctx.outputs->end() ? std::string_view{} : std::string_view(it->second);
}

// Resolves a placeholder name to a renderer and its bound key: a numeric
// {0}..{N} -> a regex capture; the {env.NAME} namespace -> the environment;
// otherwise a builtin field from the table (empty key). New namespaces
// ({def.*}, {capture.*}) slot in here.
std::pair<FieldFn, std::string> ResolveName(std::string_view name) {
  if (CaptureIndex(name) >= 0) {
    return {&CaptureField, std::string(name)};
  }
  if (name.starts_with(kEnvironmentNamespace)) {
    return {&EnvField, std::string(name.substr(kEnvironmentNamespace.size()))};
  }
  if (name.starts_with(kDefinitionNamespace)) {
    return {&DefField, std::string(name.substr(kDefinitionNamespace.size()))};
  }
  if (name.starts_with(kCaptureNamespace)) {
    return {&OutputField, std::string(name.substr(kCaptureNamespace.size()))};
  }
  return {LookupField(name), std::string()};
}

// A qualifier is a sed-style rewrite when it is `s` followed by a punctuation
// delimiter (s/.../.../, s#...#...#, ...) -- distinct from the format qualifiers
// (h, iso, epoch, a strftime %..., or a quoted "...").
bool IsRewriteQualifier(std::string_view qualifier) {
  return qualifier.size() >= 2 && qualifier[0] == 's' && std::ispunct(static_cast<unsigned char>(qualifier[1])) != 0;
}

// One sed command in a rewrite chain: an RE2 pattern, its replacement, and the flags (`g`/`i`).
struct RewriteOp {
  std::string_view pattern;
  std::string_view replacement;
  std::string_view flags;
};

// Splits a rewrite / extraction qualifier into its `;`-separated command chain. `spec` starts with
// the mode letter (`s` or `m`); each command is `[letter]<delim>PAT<delim>REPL<delim>[flags]`, and a
// command after `;` may repeat the letter or omit it (an omitted letter is a substitution). The
// delimiter is the char right after the (optional) letter, so a `;` inside PAT/REPL is not a
// separator - only a `;` after the flags ends a command. Returns empty on any malformed command
// (mirroring the single-command "leave the value alone on bad input" stance). No delimiter-escaping,
// matching the original single-command behavior (pick a delimiter the pattern does not use).
std::vector<RewriteOp> ParseRewriteChain(std::string_view spec) {
  std::vector<RewriteOp> ops;
  std::size_t pos = 0;
  bool first = true;
  while (pos < spec.size()) {
    if (spec[pos] == 's' || spec[pos] == 'm') {
      ++pos;  // the mode letter (required on the first command, optional after `;`)
    } else if (first) {
      return {};
    }
    first = false;
    if (pos >= spec.size()) {
      return {};
    }
    const char delim = spec[pos];
    if (std::ispunct(static_cast<unsigned char>(delim)) == 0) {
      return {};
    }
    ++pos;
    const std::size_t pat = pos;
    while (pos < spec.size() && spec[pos] != delim) {
      ++pos;
    }
    if (pos >= spec.size()) {
      return {};  // no closing delimiter after the pattern
    }
    const std::string_view pattern = spec.substr(pat, pos - pat);
    ++pos;
    const std::size_t repl = pos;
    while (pos < spec.size() && spec[pos] != delim) {
      ++pos;
    }
    if (pos >= spec.size()) {
      return {};  // no closing delimiter after the replacement
    }
    const std::string_view replacement = spec.substr(repl, pos - repl);
    ++pos;
    const std::size_t flags = pos;
    while (pos < spec.size() && spec[pos] != ';') {
      ++pos;
    }
    ops.push_back({.pattern = pattern, .replacement = replacement, .flags = spec.substr(flags, pos - flags)});
    if (pos < spec.size() && spec[pos] == ';') {
      ++pos;  // consume the command separator
      if (pos == spec.size()) {
        return {};
      }
    }
  }
  return ops;
}

// Compiles every op's pattern (case-insensitive when its flags carry `i`), or nullopt if any fails
// to compile - so a chain with a bad pattern is a whole no-op (the leave-alone stance).
std::optional<std::vector<regex::Matcher>> CompileChain(const std::vector<RewriteOp>& ops) {
  std::vector<regex::Matcher> matchers;
  matchers.reserve(ops.size());
  for (const RewriteOp& op : ops) {
    absl::StatusOr<regex::Matcher> matcher =
        regex::Matcher::Compile(op.pattern, /*case_insensitive=*/absl::StrContains(op.flags, 'i'));
    if (!matcher.ok()) {
      return std::nullopt;
    }
    matchers.push_back(*std::move(matcher));
  }
  return matchers;
}

// Applies a sed-style rewrite chain `s<delim>PAT<delim>REPL<delim>[flags][;...]` to `value`: each
// command is an RE2 substitution (`g` all matches, `i` case-insensitive), applied left to right. A
// malformed spec or any uncompilable pattern leaves the value unchanged.
std::string ApplyRewrite(std::string_view value, std::string_view spec) {
  const std::vector<RewriteOp> ops = ParseRewriteChain(spec);
  const std::optional<std::vector<regex::Matcher>> matchers = CompileChain(ops);
  if (ops.empty() || !matchers.has_value()) {
    return std::string(value);
  }
  std::string out(value);
  for (std::size_t i = 0; i < ops.size(); ++i) {
    out = (*matchers)[i].Rewrite(out, ops[i].replacement, /*global=*/absl::StrContains(ops[i].flags, 'g'));
  }
  return out;
}

// A qualifier is a per-line extraction when it is `m` followed by a punctuation delimiter
// (m/.../.../, m,...,...,, ...) -- the line-oriented, list-producing sibling of the `s` rewrite.
bool IsExtractQualifier(std::string_view qualifier) {
  return qualifier.size() >= 2 && qualifier[0] == 'm' && std::ispunct(static_cast<unsigned char>(qualifier[1])) != 0;
}

// Applies a per-line extraction chain `m<delim>PAT<delim>REPL<delim>[flags][;...]` to `value`: splits
// it into lines, and for each line that matches the FIRST command's pattern emits its RE2 rewrite,
// then runs the remaining commands as substitutions on that per-line value. Non-matching lines are
// dropped by the first command, so it filters as well as transforms. A malformed spec or any
// uncompilable pattern yields an empty list (matching ApplyRewrite's leave-it-alone stance).
std::vector<std::string> ExtractLines(std::string_view value, std::string_view spec) {
  const std::vector<RewriteOp> ops = ParseRewriteChain(spec);
  const std::optional<std::vector<regex::Matcher>> matchers = CompileChain(ops);
  if (ops.empty() || !matchers.has_value()) {
    return {};
  }
  const bool first_global = absl::StrContains(ops.front().flags, 'g');
  std::vector<std::string> out;
  for (const std::string_view line : absl::StrSplit(value, '\n')) {
    if (!matchers->front().PartialMatch(line)) {
      continue;  // the first command filters: only matching lines contribute
    }
    std::string current = matchers->front().Rewrite(line, ops.front().replacement, first_global);
    for (std::size_t i = 1; i < ops.size(); ++i) {  // remaining commands substitute on the survivor
      current = (*matchers)[i].Rewrite(current, ops[i].replacement, /*global=*/absl::StrContains(ops[i].flags, 'g'));
    }
    out.push_back(std::move(current));
  }
  return out;
}

// A stream reducer collapses an m// per-line value stream to a single scalar, so the extraction is
// valid in a scalar context (-printf / --template / -exec / --columns). It is a terminal `;`-chain
// segment in FUNCTION notation: v1 ships `join(SEP)` (join the stream with SEP; bare `join` = "\n",
// `join()` = "" concatenate). The numeric reducers (sum/avg/min/max/count/first/last) are reserved
// for the same slot -- per-field, so nothing here rules out numeric aggregation. Unlike s/// / m//
// (whose regex args are delimiter-hostile, hence sed-style delimiters), a reducer's arg is simple, so
// parens read cleanly.
struct Reducer {
  std::string separator = "\n";
};

// A `;`-chain segment is a reducer iff its leading [a-z]+ run is one of these keywords. Only `join`
// is implemented today; a bare `sum` etc. still parses as a (malformed) rewrite -- reserving the
// names is a later step.
bool IsReducerKeyword(std::string_view word) {
  return word == "join";
}

// Unescape a join separator argument: \t \n become the control char, any other `\x` becomes `x` (so
// `\)` is a literal ')' and `\\` a backslash); everything else is literal. `join(, )` -> ", ".
std::string UnescapeSeparator(std::string_view arg) {
  std::string out;
  for (std::size_t i = 0; i < arg.size(); ++i) {
    if (arg[i] == '\\' && i + 1 < arg.size()) {
      const char next = arg[++i];
      out.push_back(next == 't' ? '\t' : next == 'n' ? '\n' : next);
    } else {
      out.push_back(arg[i]);
    }
  }
  return out;
}

// The leading lower-case run at `pos` (a candidate reducer keyword).
std::string_view LeadingWord(std::string_view spec, std::size_t pos) {
  std::size_t end = pos;
  while (end < spec.size() && std::islower(static_cast<unsigned char>(spec[end])) != 0) {
    ++end;
  }
  return spec.substr(pos, end - pos);
}

// The index of the terminating ';' (or spec.size()) of the `;`-chain segment starting at `pos`. A
// reducer segment is a keyword optionally followed by `(ARG)` (with `\)` escaping inside); a rewrite
// segment is `[sm]?<delim>PAT<delim>REPL<delim>flags`, where a ';' is a boundary only AFTER the third
// delimiter -- so a ';' inside PAT/REPL is protected, matching ParseRewriteChain.
// NOLINTNEXTLINE(readability-function-cognitive-complexity): cohesive dispatch
std::size_t SegmentEnd(std::string_view spec, std::size_t pos) {
  if (IsReducerKeyword(LeadingWord(spec, pos))) {
    std::size_t cursor = pos + LeadingWord(spec, pos).size();
    if (cursor < spec.size() && spec[cursor] == '(') {
      for (++cursor; cursor < spec.size() && spec[cursor] != ')';) {
        cursor += (spec[cursor] == '\\' && cursor + 1 < spec.size()) ? 2 : 1;
      }
      if (cursor < spec.size()) {
        ++cursor;  // consume ')'
      }
    }
    while (cursor < spec.size() && spec[cursor] != ';') {
      ++cursor;
    }
    return cursor;
  }
  std::size_t cursor = pos;
  if (cursor < spec.size() && (spec[cursor] == 's' || spec[cursor] == 'm')) {
    ++cursor;
  }
  if (cursor >= spec.size()) {
    return spec.size();
  }
  const char delim = spec[cursor];
  int delims = 1;
  for (++cursor; cursor < spec.size(); ++cursor) {
    if (delims < 3) {
      if (spec[cursor] == delim) {
        ++delims;
      }
    } else if (spec[cursor] == ';') {
      return cursor;
    }
  }
  return spec.size();
}

// An m// extract qualifier split into its per-line stream chain, an optional terminal reducer, and
// the post-reducer scalar chain. With no reducer the whole spec is the stream (a pure value stream:
// the --summary key, or the #136 scalar error). The FIRST reducer segment is the pivot: everything
// before it (minus the separating ';') is the per-line chain; everything after is a scalar s/// chain
// applied to the reduced value ({field:m/.../\1/;s/x/y/;join(, );s/a/b/} = extract+map per line,
// join, then rewrite the scalar).
struct Pipeline {
  std::string_view stream;
  std::optional<Reducer> reducer;
  std::string_view scalar;
};

// NOLINTNEXTLINE(readability-function-cognitive-complexity): cohesive dispatch
Pipeline SplitPipeline(std::string_view spec) {
  for (std::size_t pos = 0; pos < spec.size();) {
    const std::size_t end = SegmentEnd(spec, pos);
    const std::string_view word = LeadingWord(spec, pos);
    if (IsReducerKeyword(word)) {
      Reducer reducer;
      const std::size_t after = pos + word.size();
      if (after < spec.size() && spec[after] == '(') {
        std::size_t arg_end = after + 1;
        while (arg_end < spec.size() && spec[arg_end] != ')') {
          arg_end += (spec[arg_end] == '\\' && arg_end + 1 < spec.size()) ? 2 : 1;
        }
        reducer.separator = UnescapeSeparator(spec.substr(after + 1, arg_end - (after + 1)));
      }
      return {
          .stream = pos == 0 ? std::string_view{} : spec.substr(0, pos - 1),
          .reducer = reducer,
          .scalar = end < spec.size() ? spec.substr(end + 1) : std::string_view{},
      };
    }
    pos = end < spec.size() ? end + 1 : end;
  }
  return {.stream = spec, .reducer = std::nullopt, .scalar = {}};
}

// Whether an m// extract qualifier ends its pipeline in a reducer -- so it is scalar-valued, not a
// value stream. The scalar-context guard (#136) rejects only UNREDUCED extractions.
bool HasReducer(std::string_view spec) {
  return SplitPipeline(spec).reducer.has_value();
}

// The path-component qualifier keywords ({field:KEYWORD}). Sorted (for readability).
constexpr auto kPathComponents = std::to_array<std::string_view>(
    {"basename", "core", "dir", "ext", "extension", "file", "name", "path", "stem", "suffix", "suffixes"});

// A qualifier is a path-component extraction when it is one of the keywords above --
// applied post-render (treating the field's value as a path), like the s/// rewrite.
bool IsPathComponent(std::string_view qualifier) {
  return absl::c_contains(kPathComponents, qualifier);
}

// Treats `value` as a path and extracts `component`, mirroring the flat path fields
// so any path-valued field composes: {target:stem}, {path:name}, {def.B:dir}, ...
// {core} = the filename with ALL extensions removed (foo.tar.gz -> foo), the
// complement of {suffixes}; {path} (and any unknown keyword) is the identity.
std::string PathComponent(std::string_view value, std::string_view component) {
  const stdfs::path path{std::string(value)};
  if (component == "dir") {
    const std::string parent = path.parent_path().string();
    return parent.empty() ? "." : parent;
  }
  if (component == "name" || component == "basename" || component == "file") {
    return path.filename().string();
  }
  if (component == "stem") {
    return path.stem().string();
  }
  if (component == "ext" || component == "extension") {
    const std::string ext = path.extension().string();  // includes the leading '.'
    return ext.empty() ? ext : ext.substr(1);
  }
  if (component == "suffix") {
    return path.extension().string();  // last extension WITH its dot
  }
  const std::string filename = path.filename().string();
  const std::string::size_type dot = filename.find('.', 1);  // first extension; a leading dot is not one
  if (component == "suffixes") {
    return dot == std::string::npos ? "" : filename.substr(dot);
  }
  if (component == "core") {
    return dot == std::string::npos ? filename : filename.substr(0, dot);
  }
  return std::string(value);  // "path" (whole) or an unrecognised keyword -> identity
}

absl::Status ValidateRewriteChain(std::string_view spec) {
  const std::vector<RewriteOp> ops = ParseRewriteChain(spec);
  if (ops.empty()) {
    return absl::InvalidArgumentError("malformed field rewrite chain");
  }
  for (const RewriteOp& op : ops) {
    if (op.flags.find_first_not_of("gi") != std::string_view::npos) {
      return absl::InvalidArgumentError(absl::StrCat("unknown rewrite flags '", op.flags, "' (use g or i)"));
    }
    const absl::Status status = regex::ValidateRe2Rewrite(op.pattern, op.replacement, absl::StrContains(op.flags, 'i'));
    if (!status.ok()) {
      return status;
    }
  }
  return absl::OkStatus();
}

bool IsValidJoin(std::string_view spec) {
  if (spec == "join") {
    return true;
  }
  if (!spec.starts_with("join(")) {
    return false;
  }
  for (std::size_t pos = 5; pos < spec.size(); ++pos) {
    if (spec[pos] == '\\') {
      ++pos;
    } else if (spec[pos] == ')') {
      return pos + 1 == spec.size();
    }
  }
  return false;
}

absl::Status ValidateTransform(std::string_view spec) {
  if (!IsExtractQualifier(spec)) {
    return ValidateRewriteChain(spec);
  }
  const Pipeline pipeline = SplitPipeline(spec);
  if (const absl::Status status = ValidateRewriteChain(pipeline.stream); !status.ok()) {
    return status;
  }
  if (!pipeline.reducer.has_value()) {
    return absl::OkStatus();
  }
  const std::size_t start = pipeline.stream.size() + 1;
  const std::size_t end = SegmentEnd(spec, start);
  if (!IsValidJoin(spec.substr(start, end - start))) {
    return absl::InvalidArgumentError("malformed join reducer");
  }
  if (end < spec.size()) {
    return ValidateRewriteChain(pipeline.scalar);
  }
  return absl::OkStatus();
}

absl::Status ValidateFieldName(std::string_view name) {
  return IsKnownField(name) ? absl::OkStatus() : absl::InvalidArgumentError(absl::StrCat("unknown field '", name, "'"));
}

absl::Status ValidateNativeQualifier(FieldFn renderer, std::string_view qualifier) {
  if (IsRewriteQualifier(qualifier) || IsExtractQualifier(qualifier)) {
    return ValidateTransform(qualifier);
  }
  if (qualifier.empty() || IsPathComponent(qualifier)) {
    return absl::OkStatus();
  }
  if (renderer == &MtimeField || renderer == &AtimeField || renderer == &CtimeField || renderer == &BtimeField) {
    return absl::OkStatus();  // Custom time patterns may contain arbitrary literal text.
  }
  if (renderer == &HashField) {
    return hash::ParseSpec(qualifier, "sha256").has_value()
               ? absl::OkStatus()
               : absl::InvalidArgumentError(absl::StrCat("invalid hash algorithm or encoding '", qualifier, "'"));
  }
  if ((renderer == &SizeField || renderer == &BlocksField) && qualifier == "h") {
    return absl::OkStatus();
  }
  return absl::InvalidArgumentError(absl::StrCat("unsupported field qualifier '", qualifier, "'"));
}

}  // namespace

std::optional<std::size_t> PlaceholderSize(std::string_view text) {
  if (text.empty() || text.front() != '{') {
    return std::nullopt;
  }
  const std::optional<ParsedField> parsed = ParseField(text, 0);
  return parsed.has_value() ? std::optional(parsed->next) : std::nullopt;
}

std::vector<Template> PrintfTemplates(std::string_view format) {
  std::vector<Template> templates;
  for (std::size_t pos = 0; pos + 1 < format.size(); ++pos) {
    if (format[pos] == '\\') {
      ++pos;
    } else if (format[pos] == '%') {
      const char directive = format[++pos];
      if (directive == '{') {
        const std::string_view tail = format.substr(pos);
        const std::optional<std::size_t> length = PlaceholderSize(tail);
        templates.push_back(Template::Compile(tail.substr(0, length.value_or(tail.size()))));
        pos += length.value_or(tail.size()) - 1;
      } else if (directive == 'A' || directive == 'C' || directive == 'T') {
        ++pos;
      }
    }
  }
  return templates;
}

Template Template::Compile(std::string_view tmpl) {
  Template compiled;
  std::string literal;
  const auto flush_literal = [&] {
    if (!literal.empty()) {
      compiled.segments_.push_back({.literal = std::move(literal)});
      literal.clear();  // restore the moved-from buffer to a known-empty state
    }
  };
  for (std::string_view::size_type i = 0; i < tmpl.size();) {
    const char ch = tmpl[i];
    if (ch == '{' && i + 1 < tmpl.size() && tmpl[i + 1] == '{') {
      literal.push_back('{');
      i += 2;
    } else if (ch == '}' && i + 1 < tmpl.size() && tmpl[i + 1] == '}') {
      literal.push_back('}');
      i += 2;
    } else if (ch == '{') {
      std::optional<ParsedField> field = ParseField(tmpl, i);
      if (!field.has_value()) {  // not a well-formed placeholder -> literal '{'
        compiled.validation_.Update(
            absl::InvalidArgumentError(absl::StrCat("malformed field placeholder at byte ", i)));
        literal.push_back(ch);
        ++i;
        continue;
      }
      flush_literal();
      compiled.validation_.Update(ValidateFieldName(field->name));
      auto [fn, key] = ResolveName(field->name);  // builtin field, {0}..{N} capture, or {env.NAME}
      std::string& qualifier = field->qualifier;
      compiled.validation_.Update(ValidateNativeQualifier(fn, qualifier));
      // Classify the qualifier: an s/// rewrite, an m/// per-line extraction, or a path-component
      // extraction is a post-render transform; anything else is the field's own format argument.
      const Segment::PostProcess post = IsRewriteQualifier(qualifier)   ? Segment::PostProcess::kRewrite
                                        : IsExtractQualifier(qualifier) ? Segment::PostProcess::kExtract
                                        : IsPathComponent(qualifier)    ? Segment::PostProcess::kComponent
                                                                        : Segment::PostProcess::kNone;
      compiled.segments_.push_back({.fn = fn, .key = std::move(key), .qualifier = std::move(qualifier), .post = post});
      i = field->next;
    } else {
      if (ch == '}') {
        compiled.validation_.Update(absl::InvalidArgumentError(absl::StrCat("unescaped closing brace at byte ", i)));
      }
      literal.push_back(ch);
      ++i;
    }
  }
  flush_literal();
  return compiled;
}

absl::Status Template::Validate() const {
  return validation_;
}

bool Template::ReferencesCapture(std::string_view name) const {
  return std::any_of(segments_.begin(), segments_.end(), [&](const Segment& segment) {
    return segment.fn == &OutputField && segment.key == name;
  });
}

hash::DefaultUsage Template::HashDefaultsUsed() const {
  hash::DefaultUsage usage;
  for (const Segment& segment : segments_) {
    if (segment.fn != &HashField) {
      continue;
    }
    const std::string_view qualifier =
        segment.post == Segment::PostProcess::kNone ? segment.qualifier : std::string_view{};
    const auto spec = hash::ParseSpec(qualifier, "sha256");
    if (spec.has_value()) {
      usage.algorithm |= spec->defaults.algorithm;
      usage.encoding |= spec->defaults.encoding;
    }
  }
  return usage;
}

std::size_t Template::ContentFieldCount() const {
  return static_cast<std::size_t>(absl::c_count_if(
      segments_, [](const Segment& segment) { return segment.fn == &HashField || segment.fn == &LinesField; }));
}

std::string Template::Render(const RenderContext& context) const {
  std::string out;
  for (const Segment& segment : segments_) {
    if (segment.fn != nullptr) {
      // A post-render transform (s/// rewrite or path-component) renders the field
      // with no qualifier, then transforms the value; otherwise the qualifier is the
      // field's own format argument.
      const bool transform = segment.post != Segment::PostProcess::kNone;
      const StringOrView field_value =
          segment.fn(segment.key, transform ? std::string_view{} : segment.qualifier, context);
      const std::string_view value = field_value.view();
      switch (segment.post) {
        case Segment::PostProcess::kComponent: out.append(PathComponent(value, segment.qualifier)); break;
        case Segment::PostProcess::kNone: out.append(value); break;
        case Segment::PostProcess::kRewrite: out.append(ApplyRewrite(value, segment.qualifier)); break;
        // A per-line extraction is a value stream. A terminal reducer (`;join(...)`) collapses it to
        // a scalar (then a post-reducer s/// chain rewrites that scalar); without one it has no single
        // scalar value, so its scalar projection is the matches newline-joined -- but the scalar-
        // context guard (#136) rejects an unreduced extraction up front, so that branch is a fallback.
        case Segment::PostProcess::kExtract: {
          const Pipeline pipeline = SplitPipeline(segment.qualifier);
          const std::vector<std::string> stream = ExtractLines(value, pipeline.stream);
          if (pipeline.reducer.has_value()) {
            std::string scalar = absl::StrJoin(stream, pipeline.reducer->separator);
            if (!pipeline.scalar.empty()) {
              scalar = ApplyRewrite(scalar, pipeline.scalar);
            }
            out.append(scalar);
          } else {
            out.append(absl::StrJoin(stream, "\n"));
          }
          break;
        }
      }
    } else {
      out.append(segment.literal);
    }
  }
  return out;
}

std::optional<std::vector<std::string>> Template::AsExtraction(const RenderContext& context) const {
  // Only a template that is exactly one UNREDUCED `{field:m/.../.../}` extraction is a value stream;
  // anything else (a literal, several segments, a non-m field, or a reducer-terminated m// that is
  // scalar-valued) is an ordinary scalar template.
  if (segments_.size() != 1) {
    return std::nullopt;
  }
  const Segment& segment = segments_.front();
  if (segment.fn == nullptr || segment.post != Segment::PostProcess::kExtract || HasReducer(segment.qualifier)) {
    return std::nullopt;
  }
  const StringOrView value = segment.fn(segment.key, std::string_view{}, context);  // base value, no qualifier
  return ExtractLines(value.view(), segment.qualifier);
}

bool Template::IsExtraction() const {
  return segments_.size() == 1 && segments_.front().fn != nullptr
         && segments_.front().post == Segment::PostProcess::kExtract && !HasReducer(segments_.front().qualifier);
}

bool Template::HasUnreducedExtraction() const {
  return absl::c_any_of(segments_, [](const Segment& segment) {
    return segment.fn != nullptr && segment.post == Segment::PostProcess::kExtract && !HasReducer(segment.qualifier);
  });
}

std::string Render(std::string_view tmpl, std::string_view path, const vfs::Metadata& metadata, int depth) {
  return Template::Compile(tmpl).Render(RenderContext{.path = path, .metadata = metadata, .depth = depth});
}

std::string Render(std::string_view tmpl, const RenderContext& context) {
  return Template::Compile(tmpl).Render(context);
}

absl::Span<const FieldDoc> FieldDocs() {
  static constexpr auto kFileAlias = std::to_array<std::string_view>({"file"});
  static constexpr auto kExtensionAlias = std::to_array<std::string_view>({"extension"});
  static constexpr auto kLanguageAlias = std::to_array<std::string_view>({"language"});
  static constexpr auto kOwnerAlias = std::to_array<std::string_view>({"owner"});
  static constexpr auto kPermissionAlias = std::to_array<std::string_view>({"perm"});
  static constexpr auto kDocs = std::to_array<FieldDoc>({
      // Path & name.
      {
          .name = "path",
          .aliases = {},
          .group = "path",
          .header = "Path & name",
          .summary = "full path as traversed ({} is an alias)",
      },
      {
          .name = "relpath",
          .aliases = {},
          .group = "path",
          .header = "Path & name",
          .summary = "path relative to the search root (find %P)",
      },
      {
          .name = "root",
          .aliases = {},
          .group = "path",
          .header = "Path & name",
          .summary = "the search root it was reached from (find %H)",
      },
      {
          .name = "dir",
          .aliases = {},
          .group = "path",
          .header = "Path & name",
          .summary = "directory containing the entry",
      },
      {
          .name = "name",
          .aliases = kFileAlias,
          .group = "path",
          .header = "Path & name",
          .summary = "final path component (the file name)",
      },
      {
          .name = "stem",
          .aliases = {},
          .group = "path",
          .header = "Path & name",
          .summary = "name without its last extension",
      },
      {
          .name = "core",
          .aliases = {},
          .group = "path",
          .header = "Path & name",
          .summary = "name without all extensions (foo.tar.gz -> foo)",
      },
      {
          .name = "ext",
          .aliases = kExtensionAlias,
          .group = "path",
          .header = "Path & name",
          .summary = "last extension, no dot (gz)",
      },
      {
          .name = "suffix",
          .aliases = {},
          .group = "path",
          .header = "Path & name",
          .summary = "last extension, with dot (.gz)",
      },
      {
          .name = "suffixes",
          .aliases = {},
          .group = "path",
          .header = "Path & name",
          .summary = "all extensions, with dots (.tar.gz)",
      },
      {
          .name = "target",
          .aliases = {},
          .group = "path",
          .header = "Path & name",
          .summary = "a symlink's target (find %l); else empty",
      },
      // Type & size.
      {
          .name = "type",
          .aliases = {},
          .group = "type",
          .header = "Type & size",
          .summary = "entry type letter (f, d, l, ...)",
      },
      {
          .name = "lang",
          .aliases = kLanguageAlias,
          .group = "type",
          .header = "Type & size",
          .summary = "language by extension/filename (C++, Python, ...; empty if unknown)",
      },
      {
          .name = "lang-type",
          .aliases = {},
          .group = "type",
          .header = "Type & size",
          .summary = "language kind (programming, markup, data, or prose); empty when unspecified",
      },
      {
          .name = "lang-color",
          .aliases = {},
          .group = "type",
          .header = "Type & size",
          .summary = "language display colour (#RRGGBB); empty when unspecified",
      },
      {
          .name = "lang-group",
          .aliases = {},
          .group = "type",
          .header = "Type & size",
          .summary = "parent language group; empty when the language is not grouped",
      },
      {
          .name = "lang-source",
          .aliases = {},
          .group = "type",
          .header = "Type & size",
          .summary = "vocabulary provenance for the language; empty when unspecified",
      },
      {
          .name = "mime",
          .aliases = {},
          .group = "type",
          .header = "Type & size",
          .summary = "media (MIME) type by extension (text/plain, image/png; application/octet-stream if unknown)",
      },
      {
          .name = "mime-category",
          .aliases = {},
          .group = "type",
          .header = "Type & size",
          .summary = "top-level media category (application, image, text, ...)",
      },
      {
          .name = "mime-description",
          .aliases = {},
          .group = "type",
          .header = "Type & size",
          .summary = "media-type description from the active vocabulary; empty when unspecified",
      },
      {
          .name = "mime-charset",
          .aliases = {},
          .group = "type",
          .header = "Type & size",
          .summary = "default media-type charset; empty when unspecified",
      },
      {
          .name = "mime-compressible",
          .aliases = {},
          .group = "type",
          .header = "Type & size",
          .summary = "whether the media type is normally compressible (yes/no; empty when unknown)",
      },
      {
          .name = "mime-source",
          .aliases = {},
          .group = "type",
          .header = "Type & size",
          .summary = "vocabulary provenance for the media type; empty when unspecified",
      },
      {
          .name = "size",
          .aliases = {},
          .group = "type",
          .header = "Type & size",
          .summary = "size in bytes ({size:h} human-readable)",
      },
      {
          .name = "blocks",
          .aliases = {},
          .group = "type",
          .header = "Type & size",
          .summary = "512-byte blocks allocated",
      },
      {.name = "inode", .aliases = {}, .group = "type", .header = "Type & size", .summary = "inode number"},
      {.name = "links", .aliases = {}, .group = "type", .header = "Type & size", .summary = "hard-link count"},
      {.name = "dev", .aliases = {}, .group = "type", .header = "Type & size", .summary = "device number"},
      {
          .name = "depth",
          .aliases = {},
          .group = "type",
          .header = "Type & size",
          .summary = "depth below the root (0 at a root operand)",
      },
      // Content.
      {
          .name = "hash",
          .aliases = {},
          .group = "content",
          .header = "Content",
          .summary = "file digest; {hash:ALGO[/ENCODING]} picks the algorithm (default sha256) and hex/base64",
      },
      {
          .name = "lines",
          .aliases = {},
          .group = "content",
          .header = "Content",
          .summary = "text line count (empty for a binary/unreadable file); reads the file",
      },
      {
          .name = "fuzzy",
          .aliases = {},
          .group = "content",
          .header = "Content",
          .summary = "normalized fuzzy quality (AND = weakest requirement, OR = best alternative)",
      },
      {
          .name = "shard",
          .aliases = {},
          .group = "content",
          .header = "Content",
          .summary = "with --shards, the number of shards in the set (empty otherwise); size-like fields "
                     "then aggregate across the set",
      },
      // Owner & mode.
      {
          .name = "user",
          .aliases = kOwnerAlias,
          .group = "owner",
          .header = "Owner & mode",
          .summary = "owner user name (alias {owner}; find %u)",
      },
      {.name = "group", .aliases = {}, .group = "owner", .header = "Owner & mode", .summary = "owner group name"},
      {.name = "uid", .aliases = {}, .group = "owner", .header = "Owner & mode", .summary = "owner numeric user id"},
      {.name = "gid", .aliases = {}, .group = "owner", .header = "Owner & mode", .summary = "owner numeric group id"},
      {
          .name = "mode",
          .aliases = kPermissionAlias,
          .group = "owner",
          .header = "Owner & mode",
          .summary = "permission bits, octal",
      },
      {
          .name = "access",
          .aliases = {},
          .group = "owner",
          .header = "Owner & mode",
          .summary = "symbolic permissions (ls -l / stat %A)",
      },
      // Time (each takes an optional {:FORMAT} qualifier; see below).
      {.name = "atime", .aliases = {}, .group = "time", .header = "Time", .summary = "last access time"},
      {.name = "mtime", .aliases = {}, .group = "time", .header = "Time", .summary = "last modification time"},
      {.name = "ctime", .aliases = {}, .group = "time", .header = "Time", .summary = "inode change time"},
      {
          .name = "btime",
          .aliases = {},
          .group = "time",
          .header = "Time",
          .summary = "creation/birth time (where supported)",
      },
      // Grep context (populated only during -grep:FORMAT; empty elsewhere).
      {
          .name = "line",
          .aliases = {},
          .group = "grep",
          .header = "Grep context",
          .summary = "1-based number of the matching line",
      },
      {.name = "text", .aliases = {}, .group = "grep", .header = "Grep context", .summary = "the full matching line"},
      {
          .name = "match",
          .aliases = {},
          .group = "grep",
          .header = "Grep context",
          .summary = "the matched substring (grep -o)",
      },
      {
          .name = "column",
          .aliases = {},
          .group = "grep",
          .header = "Grep context",
          .summary = "1-based column of the match",
      },
  });
  return kDocs;
}

const FieldHelpDocs& FieldSyntaxDocs() {
  static const FieldHelpDocs kDocs = [] {
    const std::string path_components = absl::StrCat(
        "path component of the value: ", absl::StrJoin(kPathComponents, "|"),
        "; any path-valued field composes, e.g. {relpath:stem}, {def.B:dir}");
    return FieldHelpDocs{
        .brace_rules =
            {
                "`{{` and `}}` emit literal braces",
                "`{}` is an alias for `{path}`",
                "unknown names, malformed placeholders, and unsupported qualifiers fail before actions",
                "valid fields whose runtime value is absent render empty (for example, an unset `{env.NAME}`)",
                "a quoted qualifier may contain a literal `}`",
                "rewrite patterns, replacements, `g`/`i` flags, and `join` syntax are checked before traversal",
            },
        .dynamic_namespaces =
            {
                {.term = "{0}..{N}", .description = "-regex captures ({0} the whole match, {1}..{N} the groups)"},
                {
                    .term = absl::StrCat("{", kEnvironmentNamespace, "NAME}"),
                    .description = "a process environment variable",
                },
                {.term = absl::StrCat("{", kDefinitionNamespace, "NAME}"), .description = "a --define value"},
                {.term = absl::StrCat("{", kCaptureNamespace, "NAME}"), .description = "a -capture command result"},
            },
        .qualifiers =
            {
                {
                    .term = "{mtime:FMT}",
                    .description =
                        "time format: strftime (%Y-%m-%d) or preset (iso, epoch); see --time-format / --timezone",
                },
                {.term = "{size:h}", .description = "human-readable size"},
                {
                    .term = "{name:s/RE/R/f}",
                    .description = "RE2 rewrite of the value (flags g=all, i=ignore-case; any delimiter)",
                },
                {
                    .term = "{text:m/RE/R/f}",
                    .description = "per-line extraction: a value stream, e.g. a --summary key (m//, s///'s "
                                   "list-producing sibling)",
                },
                {
                    .term = "{text:m/RE/R/;join(SEP)}",
                    .description =
                        "reduce the stream to one scalar (join, SEP default newline) so m// is usable in a scalar "
                        "context (-printf / --template / -exec); reducers are function-notation, e.g. join(, )",
                },
                {.term = "{path:COMP}", .description = path_components},
            },
        .qualifier_pipeline =
            "An m// extraction is a left-to-right pipeline: s/// maps whatever is flowing (each line, then "
            "the scalar), and a terminal reducer such as join collapses the stream to one scalar.",
        .qualifier_example =
            "  {text:m/PAT/REP/;s/PAT/REP/;join(SEP);s/PAT/REP/}\n"
            "       |________| |________| |_______| |________|\n"
            "       extract    map each   reduce    rewrite\n"
            "       per line   line       stream    scalar",
        .printf_note =
            "For -printf's own % directives (%p %f %s %t ...) and the `%{field}` escape that bridges them "
            "to this vocabulary, see the Printf directives (`--help=-printf`).",
    };
  }();
  return kDocs;
}

absl::Span<const std::string_view> FieldNames() {
  static constexpr auto kNames = [] {
    std::array<std::string_view, kFieldTable.size()> names{};
    std::ranges::transform(kFieldTable, names.begin(), [](const auto& field) { return field.first; });
    return names;
  }();
  return kNames;
}

bool IsKnownField(std::string_view spec) {
  const std::string_view name = spec.substr(0, spec.find(':'));  // strip an optional :qualifier
  return LookupField(name) != &EmptyField                        // a builtin field (incl. "" -> {} path alias)
         || CaptureIndex(name) >= 0                              // a {0}..{N} regex capture
         || (name.starts_with(kEnvironmentNamespace) && name.size() > kEnvironmentNamespace.size())
         || (name.starts_with(kDefinitionNamespace) && name.size() > kDefinitionNamespace.size())
         || (name.starts_with(kCaptureNamespace) && name.size() > kCaptureNamespace.size());
}

absl::Span<const std::string_view> PathComponentKeywords() {
  return kPathComponents;
}

}  // namespace xff::fields
