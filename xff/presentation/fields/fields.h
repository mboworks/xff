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

#ifndef XFF_PRESENTATION_FIELDS_FIELDS_H_
#define XFF_PRESENTATION_FIELDS_FIELDS_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "mbo/types/optional_ref.h"
#include "mbo/types/string_or_view.h"
#include "xff/datetime/datetime.h"
#include "xff/hash/hash.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::fields {

// The per-entry inputs a field renderer reads. Bundled into a context so new
// fields can draw on more inputs without changing every renderer's signature.
struct RenderContext {
  std::string_view path;          // path as traversed
  std::string_view root;          // command-line search root it was reached from (find %H); may be empty
  std::string_view link_target;   // {target}: a symlink's target (find %l); empty for non-symlinks
  const vfs::Metadata& metadata;  // the entry's metadata
  int depth = 0;                  // 0 for a root operand, +1 per directory level
  // The filesystem the entry came FROM, for the fields that READ it ({hash}, {lines}). Inside a
  // mounted container that is the container's, so a member renders its own bytes rather than nothing
  // (`a.tar!x` is not a path the real filesystem has). Null means "read by path", which is what a
  // caller with no walk behind it (a bare Render) wants.
  mbo::types::OptionalRef<const vfs::FileSystem> fs;
  absl::TimeZone tz = absl::LocalTimeZone();  // zone for {atime}/{mtime}/{ctime}/{btime} formatting; --timezone
  std::string_view time_format;               // default format for a time field with no {:qualifier}; --time-format
  // --time-zone-suffix: whether a named preset renders its zone suffix (kAuto keeps the
  // preset default, kNever suppresses the optional offset, kAlways forces one).
  datetime::ZoneSuffix zone_suffix = datetime::ZoneSuffix::kAuto;
  // --hash-algorithm / --hash-encoding: defaults for a bare {hash} (empty -> sha256 / hex); a
  // {hash:ALGO[/ENCODING]} qualifier overrides per use. Only {hash} reads them.
  std::string_view hash_algorithm;
  std::string_view hash_encoding;
  // Optional borrowed data. OptionalRef makes the non-owning lifetime and absence explicit without
  // exposing nullable pointer operations to field renderers.
  mbo::types::OptionalRef<const std::vector<std::string>> captures;           // -regex groups: [0] whole match
  mbo::types::OptionalRef<const std::map<std::string, std::string>> defines;  // --define values
  mbo::types::OptionalRef<const std::map<std::string, std::string>> outputs;  // -capture results
  // Per-line match context for -grep:FORMAT: the 1-based number and text of the
  // matching line. `{line}` renders the number, `{text}` the line; both are empty
  // outside a -grep line (line_number unset), so they no-op in --template/-printf.
  std::optional<std::size_t> line_number;
  std::string_view line_text;
  // The matched span within the line (grep -o): `{match}` is the matched substring,
  // `{column}` its 1-based byte start. Empty/unset unless match_column is set (only
  // -grep:FORMAT computes it), so they no-op elsewhere.
  std::string_view match_text;
  std::optional<std::size_t> match_column;
  // --shards: when this render represents a collapsed shard set, `{shard}` is the number of shards
  // in the set and size-like fields aggregate across them (the RenderContext's metadata.size is the
  // set total). Unset for a normal entry, so `{shard}` no-ops there.
  std::optional<std::int64_t> shard_count;
  // {fuzzy}: normalized quality composed across successful fuzzy predicates. Unset when no fuzzy
  // test ran, so it no-ops in a -printf / --template that has none.
  std::optional<int> fuzzy_score;
};

// A resolved field renderer: produces one field's value for an entry. `key` is
// the bound argument for dynamic/namespaced fields (a capture index, an
// {env.NAME} variable, ...), empty for builtins. Compile resolves each {field}
// to one of these once, so Render is a direct call per entry, not name matching.
using FieldFn = mbo::StringOrView (*)(std::string_view key, std::string_view qualifier, const RenderContext& context);

// Compile parses the template once into literal/field segments; the resulting
// Template renders against many entries without re-scanning -- the hot path for
// --template (and -exec), which render every match.
class Template {
 public:
  static Template Compile(std::string_view tmpl);

  // Static syntax, field-name, and qualifier diagnostics, independent of per-entry values.
  // Consumers must validate before traversal or actions; absent runtime values are valid.
  absl::Status Validate() const;

  std::string Render(const RenderContext& context) const;

  // The value stream when this template is a single `{field:m<delim>PAT<delim>REPL<delim>flags}`
  // extraction with NO terminal reducer: the field's multi-line value split into lines, each matching
  // line rewritten by REPL, non-matching lines dropped. nullopt when the template is not exactly one
  // unreduced m// extraction (a literal, several segments, a non-m field, or an m// ending in a
  // reducer such as `;join(...)`, which is scalar-valued) -- i.e. it is an ordinary scalar template.
  // Backs the value-stream aggregation key (--summary of a per-line extraction) without a "list"
  // concept in the reduction: it just folds the returned values.
  std::optional<std::vector<std::string>> AsExtraction(const RenderContext& context) const;

  // True when the template is exactly one unreduced `{field:m/.../.../}` extraction -- the
  // context-free shape AsExtraction requires (so a caller can branch stream-vs-scalar before it has a
  // context). False once a terminal reducer (`;join(...)`) collapses it to a scalar.
  bool IsExtraction() const;

  // True when ANY segment is an UNREDUCED m// extraction (a value stream, including mid-template). A
  // scalar-output context (-printf / --format / --template / --columns / -exec) uses this to reject a
  // list-valued template up front. An m// pipeline that ends in a reducer (`;join(...)`) is scalar-
  // valued and does NOT trip this, so it is allowed in a scalar context.
  bool HasUnreducedExtraction() const;

  // Whether a parsed field reads this command capture; escaped literal braces do not count.
  bool ReferencesCapture(std::string_view name) const;

  // Inspect parsed hash fields, including post-processing, without reading any entry content.
  hash::DefaultUsage HashDefaultsUsed() const;

  // Whether a compiled field consumes birth time, including transformed fields.
  bool NeedsBirthTime() const;

  // Number of compiled hash/line-count fields that can read content when rendered.
  // This describes template structure, not observed reads or bytes.
  std::size_t ContentFieldCount() const;

 private:
  // A literal run (fn == nullptr -> emit `literal`) or a field reference: fn is
  // the renderer and `key` its bound argument (capture index, {env.NAME} var, ...).
  struct Segment {
    // How the qualifier transforms the rendered value: none (the qualifier is the
    // field's own format argument), a sed-style s/PAT/REPL/ rewrite, a path-component
    // extraction ({field:stem} etc.) that treats the value as a path, or a per-line
    // m/PAT/REPL/ extraction that yields a value stream (see AsExtraction; in a scalar
    // Render it is the matches newline-joined). All but kNone are post-render transforms;
    // the field then renders with no qualifier.
    enum class PostProcess { kNone, kRewrite, kComponent, kExtract };
    std::string literal;
    FieldFn fn = nullptr;
    std::string key;
    std::string qualifier;
    PostProcess post = PostProcess::kNone;
  };

  std::vector<Segment> segments_;
  absl::Status validation_;
};

// Length of one well-formed placeholder at the start of text, including its braces.
// Quoted qualifiers may contain closing braces. Malformed placeholders return nullopt.
std::optional<std::size_t> PlaceholderSize(std::string_view text);

// Compile only printf's %{...} field escapes; ordinary braces and escaped percent signs
// are literal. Malformed field escapes retain their template validation diagnostic.
std::vector<Template> PrintfTemplates(std::string_view format);

// One documented named field, for the `--help=fields` reference. The vocabulary's
// documentation source: FieldDocs() below is asserted (fields_test) to cover exactly
// the renderable field names, so the help topic cannot drift from the renderers.
struct FieldDoc {
  std::string_view name;                       // canonical placeholder name ({name})
  absl::Span<const std::string_view> aliases;  // alternate names, or empty
  std::string_view group;                      // short group key (path, type, owner, time, grep)
  std::string_view header;                     // display heading for the group (shown at its first field)
  std::string_view summary;                    // one-line description
};

// One row in the structured syntax reference owned by the field implementation.
struct FieldHelpRow {
  std::string term;
  std::string description;
};

// Output-independent documentation for field syntax beyond the named vocabulary.
// The help builder translates this model; it does not author field behavior itself.
struct FieldHelpDocs {
  std::vector<std::string> brace_rules;
  std::vector<FieldHelpRow> dynamic_namespaces;
  std::vector<FieldHelpRow> qualifiers;
  std::string qualifier_pipeline;
  std::string qualifier_example;
  std::string printf_note;
};

// The named {field} vocabulary, grouped for display (rows are pre-ordered by group).
// Covers exactly the renderable field names; the dynamic namespaces ({env.NAME},
// {def.NAME}, {capture.NAME}, {0}..{N}) and the qualifiers are prose in the renderer.
[[nodiscard]] absl::Span<const FieldDoc> FieldDocs();

// The canonical brace, namespace, qualifier, and pipeline documentation. Stable for
// the process lifetime so every output backend consumes the same field semantics.
[[nodiscard]] const FieldHelpDocs& FieldSyntaxDocs();

// Every renderable field name (the {field} table keys), including the empty name that
// backs {} (find's full-path placeholder). Powers the FieldDocs() coverage test.
[[nodiscard]] absl::Span<const std::string_view> FieldNames();

// Whether `spec` (a --columns entry / field name, optionally with a `:qualifier`) names a
// renderable field: a builtin (FieldNames), a {0}..{N} capture, or an {env./def./capture.}
// namespace. Powers --columns validation, so an unknown name is a usage error rather than
// a silently-empty column.
bool IsKnownField(std::string_view spec);

// The path-component qualifier keywords ({field:KEYWORD}: dir, name, stem, ...), so
// the help topic lists exactly the keywords the renderer accepts.
[[nodiscard]] absl::Span<const std::string_view> PathComponentKeywords();

// Convenience wrapper: Compile(tmpl).Render({path, metadata, depth}) with an
// empty root. Prefer Compile once + Render per entry on hot paths.
std::string Render(std::string_view tmpl, std::string_view path, const vfs::Metadata& metadata, int depth);

// Convenience wrapper rendering against a full context (so {root} resolves).
// Like the overload above, compiles per call -- prefer Compile once on hot paths.
std::string Render(std::string_view tmpl, const RenderContext& context);

}  // namespace xff::fields

#endif  // XFF_PRESENTATION_FIELDS_FIELDS_H_
