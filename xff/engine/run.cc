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

#include "xff/engine/run.h"

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "mbo/container/limited_map.h"
#include "mbo/diff/diff.h"
#include "mbo/diff/diff_options.h"
#include "mbo/file/artefact.h"
#include "mbo/status/status_macros.h"
#include "nlohmann/json.hpp"
#include "xff/archive/archive_backend.h"
#include "xff/archive/member_path.h"
#include "xff/content/line_match.h"
#include "xff/datetime/datetime.h"
#include "xff/engine/collect.h"
#include "xff/engine/evaluate.h"
#include "xff/engine/extract.h"
#include "xff/engine/mount.h"
#include "xff/engine/walk.h"
#include "xff/env/env.h"
#include "xff/exec/exec.h"
#include "xff/filesystem/ignore/ignore.h"
#include "xff/filesystem/repo/repo.h"
#include "xff/hash/hash.h"
#include "xff/matching/language/language.h"
#include "xff/matching/mime/mime.h"
#include "xff/matching/regex/regex.h"
#include "xff/parser/ast.h"
#include "xff/parser/parser.h"
#include "xff/presentation/color/color.h"
#include "xff/presentation/fields/fields.h"
#include "xff/presentation/format/format.h"
#include "xff/presentation/render/render.h"
#include "xff/registry/descriptor.h"
#include "xff/shard/group.h"
#include "xff/shard/shard.h"
#include "xff/values/values.h"
#include "xff/vfs/filesystem.h"
#include "xff/vfs/local_fs.h"

namespace xff::engine {
namespace {

// Parses a non-negative decimal integer (find depth arguments).
std::optional<int> ParseNonNegInt(std::string_view text) {
  if (text.empty()) {
    return std::nullopt;
  }
  int value = 0;
  for (const char digit : text) {
    if (digit < '0' || digit > '9') {
      return std::nullopt;
    }
    value = (value * 10) + (digit - '0');
  }
  return value;
}

// find treats -maxdepth/-mindepth/-depth/-xdev as global positional options
// (they apply regardless of where they sit in the expression). The scan returns
// only the fields it sees; right-hand values override left-hand values, matching
// expression order and find's last-occurrence rule.
struct DepthOptions {
  std::optional<int> min_depth;
  std::optional<int> max_depth;
  bool post_order = false;
  bool single_filesystem = false;
  std::optional<bool> ignore_readdir_race;
};

DepthOptions ResolveDepthPredicate(const parser::Expr& expr) {
  if (expr.descriptor->name == "-depth" || expr.descriptor->name == "-d" || expr.descriptor->name == "-delete") {
    return {.post_order = true};  // -delete implies -depth; -d is the BSD/GNU short spelling
  }
  if (expr.descriptor->name == "-xdev" || expr.descriptor->name == "-mount" || expr.descriptor->name == "-x") {
    return {.single_filesystem = true};  // -mount (GNU/BSD) and -x (BSD) are synonyms for -xdev
  }
  if (expr.descriptor->name == "-ignore_readdir_race") {
    return {.ignore_readdir_race = true};
  }
  if (expr.descriptor->name == "-noignore_readdir_race") {
    return {.ignore_readdir_race = false};  // last occurrence wins, as in find
  }
  if (expr.args.empty()) {
    return {};
  }
  const std::optional<int> value = ParseNonNegInt(expr.args.front());
  if (expr.descriptor->name == "-maxdepth") {
    return {.max_depth = value};
  }
  if (expr.descriptor->name == "-mindepth") {
    return {.min_depth = value};
  }
  return {};
}

DepthOptions CombineDepthOptions(DepthOptions lhs, const DepthOptions& rhs) {
  return {
      .min_depth = rhs.min_depth.has_value() ? rhs.min_depth : lhs.min_depth,
      .max_depth = rhs.max_depth.has_value() ? rhs.max_depth : lhs.max_depth,
      .post_order = lhs.post_order || rhs.post_order,
      .single_filesystem = lhs.single_filesystem || rhs.single_filesystem,
      .ignore_readdir_race = rhs.ignore_readdir_race.has_value() ? rhs.ignore_readdir_race : lhs.ignore_readdir_race,
  };
}

DepthOptions ResolveDepthOptions(const parser::Expr& expr) {
  switch (expr.kind) {
    case parser::Expr::Kind::kPredicate: return ResolveDepthPredicate(expr);
    case parser::Expr::Kind::kNot: return ResolveDepthOptions(*expr.lhs);
    case parser::Expr::Kind::kAnd:
    case parser::Expr::Kind::kOr:
    case parser::Expr::Kind::kNand:
    case parser::Expr::Kind::kNor:
    case parser::Expr::Kind::kXor:
    case parser::Expr::Kind::kXnor:
    case parser::Expr::Kind::kComma:
      return CombineDepthOptions(ResolveDepthOptions(*expr.lhs), ResolveDepthOptions(*expr.rhs));
  }
  return {};
}

// find's -H/-L/-P select symlink handling; they are leading global options and
// the last one wins (default -P). The parser collects them in command.globals.
SymlinkMode ResolveSymlinkMode(const std::vector<std::string>& globals) {
  SymlinkMode mode = SymlinkMode::kNever;
  for (const std::string& global : globals) {
    if (global == "-P") {
      mode = SymlinkMode::kNever;
    } else if (global == "-L") {
      mode = SymlinkMode::kAll;
    } else if (global == "-H") {
      mode = SymlinkMode::kRoots;
    }
  }
  return mode;
}

// The mode-scoped default worker count when `-j` is absent (docs/design-parallel.md
// "Parallelism control"): modern (kXff) leaves a core for the consumer and caps at
// 15 to avoid oversubscription; find/fd/rg saturate cores; an unset style stays
// sequential (the conservative in-process / test default).
std::size_t DefaultWorkers(std::optional<registry::Style> style) {
  if (!style.has_value()) {
    return 1;
  }
  const unsigned detected = std::thread::hardware_concurrency();
  const std::size_t cores = detected == 0 ? 1 : detected;
  if (*style == registry::Style::kXff) {
    return std::max<std::size_t>(1, std::min<std::size_t>(cores - 1, 15));
  }
  return cores;
}

// xff --sort=none|dir|subtree|tree|roots|global: root and sibling ordering for the walk
// (see docs/design-parallel.md). `none` keeps readdir order (find's default);
// `dir` sorts each directory's listing; `subtree` adds contiguous subtrees;
// `tree` is depth-first path order within each root; `roots` sorts only roots;
// `global` combines sorted roots with tree order. Bare --sort and the legacy `name` mean `dir`. The
// default is mode-scoped: modern (kXff) sorts each directory, find stays unordered.
// Leading global, last occurrence wins.
SortOrder ResolveSort(const std::vector<std::string>& globals, std::optional<registry::Style> style) {
  SortOrder sort = style == registry::Style::kXff ? SortOrder::kDir : SortOrder::kNone;
  for (const std::string& global : globals) {
    if (global == "--sort" || global == "--sort=dir" || global == "--sort=name") {
      sort = SortOrder::kDir;
    } else if (global == "--sort=subtree") {
      sort = SortOrder::kSubtree;
    } else if (global == "--sort=tree") {
      sort = SortOrder::kTree;
    } else if (global == "--sort=roots") {
      sort = SortOrder::kRoots;
    } else if (global == "--sort=global") {
      sort = SortOrder::kGlobal;
    } else if (global == "--sort=none") {
      sort = SortOrder::kNone;
    }
  }
  return sort;
}

// Emits the buffered listing best-score-first. STABLE so equal scores keep the walk's order, which
// the style's --sort already made deterministic - a plain sort would make ties depend on the
// algorithm instead.
void EmitRanked(std::vector<std::pair<int, std::string>>& ranked, const EmitFn& emit) {
  std::stable_sort(
      ranked.begin(), ranked.end(), [](const auto& lhs, const auto& rhs) { return lhs.first > rhs.first; });
  for (const auto& [score, text] : ranked) {
    emit(text);
  }
}

// --sort=score: rank the printed listing by the -fuzzy score instead of a traversal order. It is
// deliberately NOT a SortOrder: every other mode orders the WALK, which streams, while a score only
// exists once an entry has been evaluated - so the walk keeps its style default (which decides ties)
// and the results are ranked afterwards. Leading global, last occurrence wins; a later --sort=MODE
// turns ranking back off, so the flag reads left to right like every other last-wins global.
bool ResolveRankByScore(const std::vector<std::string>& globals) {
  bool rank = false;
  for (const std::string& global : globals) {
    if (global == "--sort=score") {
      rank = true;
    } else if (absl::StartsWith(global, "--sort")) {
      rank = false;
    }
  }
  return rank;
}

// xff -j N / -j=N / --jobs=N: worker threads for the parallel directory read-ahead (see
// docs/design-parallel.md). When absent, the count is mode-scoped (DefaultWorkers).
// Leading global, last occurrence wins; a non-positive or unparseable value is a
// usage error.
absl::StatusOr<std::size_t> ResolveJobs(const std::vector<std::string>& globals, std::optional<registry::Style> style) {
  std::size_t jobs = DefaultWorkers(style);
  for (const std::string& global : globals) {
    std::string_view value;
    if (global.starts_with("--jobs=")) {
      value = std::string_view(global).substr(7);
    } else {
      continue;
    }
    if (value == "all") {  // every detected core, regardless of mode
      const unsigned detected = std::thread::hardware_concurrency();
      jobs = detected == 0 ? 1 : detected;
      continue;
    }
    std::size_t parsed = 0;
    if (!absl::SimpleAtoi(value, &parsed) || parsed < 1) {
      return absl::InvalidArgumentError(absl::StrCat("'", value, "': expected a positive integer or 'all'"));
    }
    jobs = parsed;
  }
  return jobs;
}

// xff --summary[=overall|type|ext|lang|mime|user|group]: reduce the matches to a count +
// total size table instead of printing each one. Bare --summary / =overall is one total row;
// =type groups by file type, =ext by filename extension, =lang by programming language, =mime by
// media type, =user (alias =owner) by owner name, =group by owning group (each a files-per-key
// histogram); =none / absent is off. The mime/user/group keys reuse the field vocabulary (the
// {mime}/{user}/{group} renderers), so they cannot drift from the field values.
enum class SummaryMode : std::uint8_t {
  kOff,
  kOverall,
  kType,
  kExt,
  kLanguage,
  kMime,
  kUser,
  kGroup,
  kHash,
  kHashVerification,
  kTemplate
};

// One --summary sink: a group-by mode plus the {template} string (mode == kTemplate only). --summary
// is repeatable, like --histogram, so a run can carry several independent summary tables.
struct SummarySpec {
  SummaryMode mode = SummaryMode::kOverall;
  std::string key_template;
};

// Parses every --summary[=X] into an ordered list of sinks (each occurrence appends one); the value
// selects the mode, `--summary=none` clears the list (turns every summary off). No last-wins
// collapse: two distinct --summary flags produce two tables.
std::vector<SummarySpec> ResolveSummaries(const std::vector<std::string>& globals) {
  using ModePair = std::pair<std::string_view, SummaryMode>;
  static constexpr auto kModes = mbo::container::MakeLimitedMap(
      ModePair{"--summary", SummaryMode::kOverall}, ModePair{"--summary=ext", SummaryMode::kExt},
      ModePair{"--summary=group", SummaryMode::kGroup}, ModePair{"--summary=hash", SummaryMode::kHash},
      ModePair{"--summary=lang", SummaryMode::kLanguage}, ModePair{"--summary=mime", SummaryMode::kMime},
      ModePair{"--summary=overall", SummaryMode::kOverall}, ModePair{"--summary=owner", SummaryMode::kUser},
      ModePair{"--summary=type", SummaryMode::kType}, ModePair{"--summary=user", SummaryMode::kUser},
      ModePair{"--summary=hash-verification", SummaryMode::kHashVerification});
  constexpr std::string_view kPrefix = "--summary=";
  std::vector<SummarySpec> specs;
  for (const std::string& global : globals) {
    if (global == "--summary=none") {
      specs.clear();
    } else if (global.starts_with("--summary={")) {
      specs.push_back({.mode = SummaryMode::kTemplate, .key_template = global.substr(kPrefix.size())});
    } else if (const auto it = kModes.find(global); it != kModes.end()) {
      specs.push_back({.mode = it->second, .key_template = ""});
    }
  }
  return specs;
}

// The concatenation of every --summary={template} string, for the -capture reference check (a
// {capture.NAME} used in any summary key counts as referenced).
std::string AllSummaryTemplates(const std::vector<std::string>& globals) {
  std::string out;
  for (const std::string& global : globals) {
    if (global.starts_with("--summary={")) {
      absl::StrAppend(&out, global.substr(std::string_view("--summary=").size()));
    }
  }
  return out;
}

// The readable file-type word used as a --summary=type group key, keyed by file
// type (kUnknown and any unmapped value fall through to "unknown"). A constexpr map,
// per the style's preference for a uniform key -> value mapping over a switch.
using TypeNamePair = std::pair<vfs::FileType, std::string_view>;
constexpr auto kTypeNames = mbo::container::MakeLimitedMap(
    TypeNamePair{vfs::FileType::kBlockDevice, "block-device"},
    TypeNamePair{vfs::FileType::kCharDevice, "char-device"},
    TypeNamePair{vfs::FileType::kDirectory, "directory"},
    TypeNamePair{vfs::FileType::kFifo, "fifo"},
    TypeNamePair{vfs::FileType::kRegular, "file"},
    TypeNamePair{vfs::FileType::kSocket, "socket"},
    TypeNamePair{vfs::FileType::kSymlink, "symlink"});

std::string_view TypeName(vfs::FileType type) {
  const auto it = kTypeNames.find(type);
  return it == kTypeNames.end() ? "unknown" : it->second;  // kUnknown / unmapped -> "unknown"
}

// The filename extension used as a --summary=ext group key: the part after the
// last '.', or "(none)" when there is none (including a leading-dot dotfile).
std::string SummaryExtension(std::string_view name) {
  const std::string_view::size_type dot = name.rfind('.');
  if (dot == std::string_view::npos || dot == 0) {
    return "(none)";
  }
  return std::string(name.substr(dot + 1));
}

// The group key for one matched entry under `mode` (kOff never reaches here). The mime/user/group
// keys render the matching field ({mime}/{user}/{group}) so the reduction reuses the field
// vocabulary rather than re-deriving the value; the field renderers never return empty (owner /
// group fall back to the numeric id, mime to application/octet-stream), so no "(none)" bucket.
std::string SummaryKey(SummaryMode mode, const Visit& visit) {
  switch (mode) {
    case SummaryMode::kExt: return SummaryExtension(visit.name);
    case SummaryMode::kType: return std::string(TypeName(visit.metadata.type));
    case SummaryMode::kLanguage: {
      const std::string_view lang = language::LanguageForName(visit.name);
      return lang.empty() ? "(none)" : std::string(lang);  // Unrecognized names share one bucket.
    }
    case SummaryMode::kMime: return fields::Render("{mime}", visit.path, visit.metadata, visit.depth);
    case SummaryMode::kUser: return fields::Render("{user}", visit.path, visit.metadata, visit.depth);
    case SummaryMode::kGroup: return fields::Render("{group}", visit.path, visit.metadata, visit.depth);
    // Digest of the whole file (default sha256/hex): identical files land in one bucket, so the
    // count column reads as a dedup histogram. Reuses the {hash} field renderer, so it cannot drift.
    case SummaryMode::kHash: return fields::Render("{hash}", visit.path, visit.metadata, visit.depth);
    default: return "total";  // kOverall: a single bucket
  }
}

// xff --histogram=BUCKET[:MEASURE] (repeatable): reduce matches to a bar chart instead of (or
// alongside) the --summary table. BUCKET groups the matches - categorical (overall / type / ext /
// lang / mime / user / group) or a numeric-range field (size / lines by order of magnitude, depth
// per level). The optional MEASURE is the bar's value (see HistAgg). Independent of and combinable
// with --summary; both are terminal reductions fed by one walk.
enum class HistBucket : std::uint8_t {
  kOverall,
  kType,
  kExt,
  kLang,
  kMime,
  kUser,
  kGroup,
  kSizeRange,
  kLinesRange,
  kDepthRange,
};

// A numeric-range bucket (size / lines / depth) draws its bars in ascending range order; a
// categorical bucket sorts by bar height and honors --top.
constexpr bool IsNumericBucket(HistBucket bucket) {
  return bucket == HistBucket::kSizeRange || bucket == HistBucket::kLinesRange || bucket == HistBucket::kDepthRange;
}

// A histogram's measure: the count of matched entries (the default), or an aggregate of a numeric
// field. kCount ignores `metric`; the aggregators reduce the metric field per bucket.
enum class HistAgg : std::uint8_t { kCount, kSum, kMean, kMin, kMax };
enum class HistMetric : std::uint8_t { kSize, kLines };  // the numeric field an aggregate reduces

struct HistogramSpec {
  HistBucket bucket = HistBucket::kOverall;
  HistAgg agg = HistAgg::kCount;
  HistMetric metric = HistMetric::kSize;  // only when agg != kCount
  std::string label;                      // the spec as written, for the jsonl tag + text heading
};

// The running per-bucket aggregate. `label` is the bucket's display text (the map key itself for a
// categorical bucket; a range like "10-99" for a numeric one). min/max are valid iff count>0.
struct HistCell {
  std::string label;
  std::uint64_t count = 0;
  std::uint64_t sum = 0;
  std::uint64_t min = 0;
  std::uint64_t max = 0;
};

// Parses a `--histogram` MEASURE (the part after `BUCKET:`) into (aggregator, metric). "count" is
// the aggregator-free measure; every other form must be AGG(FIELD) with AGG in sum/mean/min/max and
// FIELD in size/lines. A bare field with no aggregator (e.g. `lines`) is a usage error, per design.
absl::StatusOr<std::pair<HistAgg, HistMetric>> ParseHistMeasure(std::string_view measure) {
  const std::string_view::size_type open = measure.find('(');
  if (open == std::string_view::npos || measure.empty() || measure.back() != ')') {
    return absl::InvalidArgumentError(
        absl::StrCat(
            "histogram metric '", measure, "' needs an aggregator: sum(size), mean(lines), min(...), or max(...)"));
  }
  const std::string_view agg_name = measure.substr(0, open);
  const std::string_view field = measure.substr(open + 1, measure.size() - open - 2);
  HistAgg agg = HistAgg::kSum;
  if (agg_name == "sum") {
    agg = HistAgg::kSum;
  } else if (agg_name == "mean") {
    agg = HistAgg::kMean;
  } else if (agg_name == "min") {
    agg = HistAgg::kMin;
  } else if (agg_name == "max") {
    agg = HistAgg::kMax;
  } else {
    return absl::InvalidArgumentError(
        absl::StrCat("unknown histogram aggregator '", agg_name, "'; use sum, mean, min, or max"));
  }
  HistMetric metric = HistMetric::kSize;
  if (field == "size") {
    metric = HistMetric::kSize;
  } else if (field == "lines") {
    metric = HistMetric::kLines;
  } else {
    return absl::InvalidArgumentError(absl::StrCat("unknown histogram metric field '", field, "'; use size or lines"));
  }
  return std::make_pair(agg, metric);
}

// An order-of-magnitude range bucket for `value`: {order-preserving map key, human range label}.
// "0" for zero, then "1-9", "10-99", "100-999", ...; the map key is the zero-padded magnitude so
// buckets sort ascending. Exact integer math (no log/pow rounding).
std::pair<std::string, std::string> MagnitudeBucket(std::uint64_t value) {
  if (value == 0) {
    return {"00", "0"};
  }
  int magnitude = 0;
  std::uint64_t low = 1;
  for (std::uint64_t rest = value; rest >= 10; rest /= 10) {
    ++magnitude;
    low *= 10;
  }
  return {absl::StrFormat("%02d", magnitude + 1), absl::StrCat(low, "-", (low * 10) - 1)};
}

// A per-value depth bucket: one bucket per level. The map key is zero-padded so 2 sorts before 10.
std::pair<std::string, std::string> DepthBucket(int depth) {
  return {absl::StrFormat("%06d", depth), absl::StrCat(depth)};
}

// The {order-preserving map key, display label} for `visit` under `spec`, or nullopt when the bucket
// field is unavailable (a numeric lines bucket for a non-regular or binary file). Categorical
// buckets reuse SummaryKey; numeric buckets range-bucket a field.
std::optional<std::pair<std::string, std::string>> HistBucketKey(
    const HistogramSpec& spec,
    const Visit& visit,
    const vfs::FileSystem& fs) {
  switch (spec.bucket) {
    case HistBucket::kOverall:
    case HistBucket::kType:
    case HistBucket::kExt:
    case HistBucket::kLang:
    case HistBucket::kMime:
    case HistBucket::kUser:
    case HistBucket::kGroup: {
      // Categorical buckets reuse SummaryKey; the map converts the bucket to its summary mode
      // (kOverall - and any unmapped value - is the single "total" key).
      using BucketModePair = std::pair<HistBucket, SummaryMode>;
      constexpr auto kBucketModes = mbo::container::MakeLimitedMap(
          BucketModePair{HistBucket::kType, SummaryMode::kType}, BucketModePair{HistBucket::kExt, SummaryMode::kExt},
          BucketModePair{HistBucket::kLang, SummaryMode::kLanguage},
          BucketModePair{HistBucket::kMime, SummaryMode::kMime}, BucketModePair{HistBucket::kUser, SummaryMode::kUser},
          BucketModePair{HistBucket::kGroup, SummaryMode::kGroup});
      const auto it = kBucketModes.find(spec.bucket);
      const SummaryMode mode = it == kBucketModes.end() ? SummaryMode::kOverall : it->second;
      const std::string key = SummaryKey(mode, visit);
      return std::make_pair(key, key);
    }
    case HistBucket::kSizeRange: return MagnitudeBucket(visit.metadata.size);
    case HistBucket::kLinesRange: {
      if (visit.metadata.type != vfs::FileType::kRegular) {
        return std::nullopt;
      }
      const absl::StatusOr<std::string> content = fs.ReadContent(visit.path);
      if (!content.ok()) {
        return std::nullopt;
      }
      const std::optional<std::size_t> lines = content::ContentLineCount(*content);
      return lines.has_value() ? std::optional(MagnitudeBucket(*lines)) : std::nullopt;
    }
    case HistBucket::kDepthRange: return DepthBucket(visit.depth);
  }
  return std::nullopt;
}

absl::StatusOr<std::vector<HistogramSpec>> ResolveHistograms(const std::vector<std::string>& globals) {
  constexpr std::string_view kFlag = "--histogram";
  std::vector<HistogramSpec> specs;
  for (const std::string_view global : globals) {
    if (global != kFlag && !absl::StartsWith(global, absl::StrCat(kFlag, "="))) {
      continue;
    }
    const std::string_view value = global == kFlag ? "overall" : global.substr(kFlag.size() + 1);
    const std::string_view::size_type colon = value.find(':');
    const std::string_view bucket_name = value.substr(0, colon);
    HistogramSpec spec;
    spec.label = std::string(value);
    if (bucket_name == "overall") {
      spec.bucket = HistBucket::kOverall;
    } else if (bucket_name == "type") {
      spec.bucket = HistBucket::kType;
    } else if (bucket_name == "ext") {
      spec.bucket = HistBucket::kExt;
    } else if (bucket_name == "lang") {
      spec.bucket = HistBucket::kLang;
    } else if (bucket_name == "mime") {
      spec.bucket = HistBucket::kMime;
    } else if (bucket_name == "user" || bucket_name == "owner") {
      spec.bucket = HistBucket::kUser;
    } else if (bucket_name == "group") {
      spec.bucket = HistBucket::kGroup;
    } else if (bucket_name == "size") {
      spec.bucket = HistBucket::kSizeRange;
    } else if (bucket_name == "lines") {
      spec.bucket = HistBucket::kLinesRange;
    } else if (bucket_name == "depth") {
      spec.bucket = HistBucket::kDepthRange;
    } else {
      return absl::InvalidArgumentError(
          absl::StrCat(
              "unknown --histogram bucket '", bucket_name,
              "'; use overall, type, ext, lang, mime, user, group, size, lines, or depth"));
    }
    if (colon != std::string_view::npos && value.substr(colon + 1) != "count") {
      MBO_MOVE_TO_OR_RETURN(ParseHistMeasure(value.substr(colon + 1)), const auto [agg, metric]);
      spec.agg = agg;
      spec.metric = metric;
    }
    specs.push_back(std::move(spec));
  }
  return specs;
}

// A horizontal bar `fraction` (0..1) of `width` cells wide. With unicode, the Unicode block
// elements give eighth-of-a-cell precision (full block plus a partial); otherwise ASCII '#'
// rounded to whole cells. Empty for fraction 0.
std::string HistogramBar(double fraction, std::size_t width, bool unicode) {
  fraction = std::clamp(fraction, 0.0, 1.0);
  if (!unicode) {
    // NOLINTNEXTLINE(modernize-return-braced-init-list): braces would narrow
    return std::string(static_cast<std::size_t>(std::llround(fraction * static_cast<double>(width))), '#');
  }
  constexpr std::array<std::string_view, 8> kPartials = {"", "▏", "▎", "▍", "▌", "▋", "▊", "▉"};
  const auto eighths = static_cast<std::size_t>(std::llround(fraction * static_cast<double>(width) * 8.0));
  std::string bar;
  for (std::size_t full = eighths / 8; full > 0; --full) {
    bar += "█";  // full block
  }
  bar += kPartials.at(eighths % 8);
  return bar;
}

// The rendered aggregate for one bucket: a double for bar-scaling and sorting, an aligned text
// form, and the JSON number. mean is fixed to `precision` decimals; the integer aggregators
// (count / sum / min / max) group digits for the text form and emit a bare integer for JSON.
struct HistValue {
  double scale = 0;
  std::string text;
  std::string json;
};

HistValue HistMeasureValue(HistAgg agg, const HistCell& cell, unsigned precision) {
  const auto integer = [](std::uint64_t value) {
    return HistValue{.scale = static_cast<double>(value), .text = format::Int(value, ','), .json = absl::StrCat(value)};
  };
  switch (agg) {
    case HistAgg::kSum: return integer(cell.sum);
    case HistAgg::kMin: return integer(cell.min);
    case HistAgg::kMax: return integer(cell.max);
    case HistAgg::kMean: {
      const double mean = cell.count == 0 ? 0.0 : static_cast<double>(cell.sum) / static_cast<double>(cell.count);
      std::string formatted;
      if (!absl::FormatUntyped(
              &formatted, absl::UntypedFormatSpec(absl::StrCat("%.", precision, "f")), {absl::FormatArg(mean)})) {
        formatted = absl::StrCat(mean);  // defensive: the format is always valid
      }
      return HistValue{.scale = mean, .text = formatted, .json = formatted};
    }
    case HistAgg::kCount: break;
  }
  return integer(cell.count);
}

// Parses one --context SPEC: a bare non-negative integer sets both sides, else comma-separated
// A:N / B:N / C:N tokens set after / before / both (last value per side wins). A side absent from
// the result was not mentioned and therefore does not override an earlier global.
struct ContextSides {
  std::optional<std::size_t> before;
  std::optional<std::size_t> after;
};

absl::StatusOr<ContextSides> ParseContextSpec(std::string_view spec) {
  if (std::size_t both = 0; absl::SimpleAtoi(spec, &both)) {
    return ContextSides{.before = both, .after = both};
  }
  ContextSides result;
  for (const std::string_view token : absl::StrSplit(spec, ',', absl::SkipEmpty())) {
    const std::size_t colon = token.find(':');
    std::size_t value = 0;
    if (colon == std::string_view::npos || !absl::SimpleAtoi(token.substr(colon + 1), &value)) {
      return absl::InvalidArgumentError(absl::StrCat("bad --context token '", token, "' (use N, or A:N / B:N / C:N)"));
    }
    const std::string_view side = token.substr(0, colon);
    if (side == "A" || side == "a") {
      result.after = value;
    } else if (side == "B" || side == "b") {
      result.before = value;
    } else if (side == "C" || side == "c") {
      result.before = value;
      result.after = value;
    } else {
      return absl::InvalidArgumentError(absl::StrCat("bad --context side '", side, "' (use A, B, or C)"));
    }
  }
  return result;
}

// --context=SPEC / --before-context=N / --after-context=N (grep -C/-B/-A): the lines of context
// -grep prints before/after each match. Processed in order, last value per side wins; `specified`
// distinguishes a deliberate `--context=0` from no context flag. A malformed value is a usage error.
struct GrepContext {
  std::size_t before = 0;
  std::size_t after = 0;
  bool specified = false;
};

absl::StatusOr<GrepContext> ResolveGrepContext(const std::vector<std::string>& globals) {
  constexpr std::string_view kContext = "--context=";
  constexpr std::string_view kBefore = "--before-context=";
  constexpr std::string_view kAfter = "--after-context=";
  GrepContext result;
  for (const std::string& global : globals) {
    if (global.starts_with(kContext)) {
      result.specified = true;
      MBO_ASSIGN_OR_RETURN(
          const ContextSides sides, ParseContextSpec(std::string_view(global).substr(kContext.size())));
      result.before = sides.before.value_or(result.before);
      result.after = sides.after.value_or(result.after);
    } else if (global.starts_with(kBefore)) {
      result.specified = true;
      if (const std::string_view value = std::string_view(global).substr(kBefore.size());
          !absl::SimpleAtoi(value, &result.before)) {
        return absl::InvalidArgumentError(absl::StrCat("bad --before-context value '", value, "'"));
      }
    } else if (global.starts_with(kAfter)) {
      result.specified = true;
      if (const std::string_view value = std::string_view(global).substr(kAfter.size());
          !absl::SimpleAtoi(value, &result.after)) {
        return absl::InvalidArgumentError(absl::StrCat("bad --after-context value '", value, "'"));
      }
    }
  }
  return result;
}

// xff's modern output selector (leading globals, last wins, default plain):
// --format=plain|nul|jsonl, with -0 a shorthand for NUL. find's -print/-print0
// keep their fixed formats; this drives only the implicit (default) print.
render::Format ResolveFormat(const std::vector<std::string>& globals) {
  render::Format format = render::Format::kPlain;
  for (const std::string& global : globals) {
    if (global == "-0" || global == "--format=nul") {
      format = render::Format::kNul;
    } else if (global == "--format=plain") {
      format = render::Format::kPlain;
    } else if (global == "--format=jsonl") {
      format = render::Format::kJsonl;
    } else if (global == "--format=csv") {
      format = render::Format::kCsv;
    } else if (global == "--format=tsv") {
      format = render::Format::kTsv;
    } else if (global == "--format=aligned") {
      format = render::Format::kAligned;
    } else if (global == "--format=markdown" || global == "--format=md") {
      format = render::Format::kMarkdown;  // `md` is the short alias of the canonical `markdown`
    } else if (global == "--format=tree") {
      format = render::Format::kTree;
    }
  }
  return format;
}

// --unicode=auto|always|never: whether --format=tree draws Unicode box-drawing connectors (else
// ASCII). auto (the default) uses Unicode when the locale is UTF-8 (LC_ALL / LC_CTYPE / LANG,
// first set wins), the way --color=auto probes the tty. Last occurrence wins; bare --unicode ==
// --unicode=always.
bool ResolveUnicode(const std::vector<std::string>& globals) {
  constexpr std::string_view kPrefix = "--unicode=";
  std::optional<bool> forced;  // an explicit non-auto --unicode value
  for (const std::string& global : globals) {
    if (global == "--unicode") {
      forced = true;  // a bare --unicode forces Unicode on
    } else if (global.starts_with(kPrefix)) {
      // The shared vocabulary (auto / always / never plus yes / no / 1 / true / 0 /
      // false); an unrecognized value is ignored, leaving the prior resolution.
      if (const std::optional<values::Tristate> tri =
              values::ParseTristate(std::string_view(global).substr(kPrefix.size()));
          tri.has_value()) {
        if (*tri == values::Tristate::kAuto) {
          forced.reset();
        } else {
          forced = *tri == values::Tristate::kOn;
        }
      }
    }
  }
  if (forced.has_value()) {
    return *forced;
  }
  // POSIX precedence: LC_ALL overrides LC_CTYPE, which overrides LANG.
  static constexpr std::array kLocaleVars = std::to_array<std::string_view>({
      "LC_ALL",
      "LC_CTYPE",
      "LANG",
  });
  for (const std::string_view var : kLocaleVars) {
    if (const std::optional<std::string> value = env::Get(var); value.has_value() && !value->empty()) {
      return absl::StrContains(absl::AsciiStrToUpper(*value), "UTF");  // en_US.UTF-8, C.UTF-8, ...
    }
  }
  return false;  // no locale set -> ASCII is the safe default
}

// --columns=FIELD,FIELD,... : the tabular column set for --format=csv / tsv, drawn from
// the {field} vocabulary (a column may carry a :qualifier, like {field}). Last occurrence
// wins; empty (absent) keeps the single default `path` column.
std::vector<std::string> ResolveColumns(const std::vector<std::string>& globals) {
  constexpr std::string_view kPrefix = "--columns=";
  std::vector<std::string> columns;
  for (const std::string& global : globals) {
    if (global.starts_with(kPrefix)) {
      columns = absl::StrSplit(std::string_view(global).substr(kPrefix.size()), ',');  // last --columns wins
    }
  }
  return columns;
}

// --path-encoding=raw|escape: how the plain renderer emits path bytes (see
// render::PathEncoding). Mirrors ResolveFormat -- last occurrence wins, the
// find-compatible kRaw default; applies only to the default/plain output.
render::PathEncoding ResolvePathEncoding(const std::vector<std::string>& globals) {
  render::PathEncoding encoding = render::PathEncoding::kRaw;
  for (const std::string& global : globals) {
    if (global == "--path-encoding=raw") {
      encoding = render::PathEncoding::kRaw;
    } else if (global == "--path-encoding=escape") {
      encoding = render::PathEncoding::kEscape;
    }
  }
  return encoding;
}

// --template=TMPL renders each match through the field vocabulary (xff-native),
// overriding --format for the implicit print. Last occurrence wins; nullopt when
// absent.
std::optional<std::string> ResolveTemplate(const std::vector<std::string>& globals) {
  constexpr std::string_view kPrefix = "--template=";
  std::optional<std::string> tmpl;
  for (const std::string& global : globals) {
    if (global.starts_with(kPrefix)) {
      tmpl = global.substr(kPrefix.size());
    }
  }
  return tmpl;
}

// --timezone=ZONE (short alias --tz=ZONE) overrides the detected local zone used to interpret
// time-string arguments (-newerXt). Last occurrence of either spelling wins. An unknown zone is a
// usage error; absent the flag resolves to the local-zone default.
absl::StatusOr<absl::TimeZone> ResolveTimeZone(const std::vector<std::string>& globals) {
  std::optional<std::string> spec;
  for (const std::string& global : globals) {
    static constexpr std::array kTimezonePrefixes = std::to_array<std::string_view>({
        "--timezone=",
        "--tz=",
    });
    for (const std::string_view prefix : kTimezonePrefixes) {
      if (global.starts_with(prefix)) {
        spec = global.substr(prefix.size());  // last occurrence of either spelling wins
      }
    }
  }
  if (!spec.has_value()) {
    return absl::LocalTimeZone();
  }
  if (const std::optional<absl::TimeZone> zone = datetime::ParseTimeZone(*spec); zone.has_value()) {
    return *zone;
  }
  return absl::InvalidArgumentError(absl::StrCat("unknown time zone: '", *spec, "'"));
}

// --block-size=SIZE sets the bytes-per-block for a bare `-size N` and `-size Nb`
// (find's historical default is 512). Last occurrence wins.
constexpr std::uint64_t kDefaultFindBlockSizeBytes = 512;

absl::StatusOr<std::uint64_t> ResolveBlockSize(const std::vector<std::string>& globals) {
  constexpr std::string_view kPrefix = "--block-size=";
  std::optional<std::string> spec;
  for (const std::string& global : globals) {
    if (global.starts_with(kPrefix)) {
      spec = global.substr(kPrefix.size());  // last occurrence wins
    }
  }
  if (!spec.has_value()) {
    return kDefaultFindBlockSizeBytes;
  }
  return ParseBlockSize(*spec);
}

// --regextype=RE2|EXACT|PCRE2: validates the grammar selector for the whole run. The grammar itself
// is resolved by the parser (parser::GrammarFromGlobals) and pre-compiled into each matcher; this is
// the single validating reader, called unconditionally so it guards every pattern predicate
// (-regex/-rxc/-grep). RE2 (default) and EXACT (literal) are core engines, always available. PCRE2
// is a build-time extra: when its backend is not linked it is a usage error here, never a silent RE2
// fallback. MATCH is still reserved. An unknown value is a usage error. All are refused before the
// walk (exit 2). Last occurrence wins (the parser agrees).
absl::Status ValidateRegextype(const std::vector<std::string>& globals) {
  constexpr std::string_view kPrefix = "--regextype=";
  std::optional<std::string_view> selected;
  for (const std::string& global : globals) {
    if (global.starts_with(kPrefix)) {
      selected = std::string_view(global).substr(kPrefix.size());
    }
  }
  if (selected.has_value()) {
    const std::string_view value = *selected;
    if (value == "RE2" || value == "EXACT" || value == "FNMATCH" || value == "GLOB" || value == "SHGLOB") {
      return absl::OkStatus();  // core engines, always linked
    }
    if (value == "PCRE2") {
      if (!regex::Pcre2Available()) {
        return absl::InvalidArgumentError(
            "--regextype=PCRE2 is not built into this binary (the PCRE2 backend is a build extra)");
      }
    } else if (value == "MATCH") {
      return absl::InvalidArgumentError(
          absl::StrCat(
              "--regextype=", value,
              " is reserved and not supported yet; use RE2, EXACT, FNMATCH, GLOB, SHGLOB or PCRE2"));
    } else {
      return absl::InvalidArgumentError(
          absl::StrCat("unknown --regextype '", value, "'; expected RE2, EXACT, FNMATCH, GLOB, SHGLOB or PCRE2"));
    }
  }
  return absl::OkStatus();
}

// --time-format=NAME sets the default format for a time field rendered without an
// explicit {:qualifier} (a datetime preset name or a custom absl::FormatTime
// pattern). Last occurrence wins; empty (absent) keeps the built-in "space"
// default. Any value is accepted verbatim (an unknown name renders literally,
// like printf), so there is nothing to reject.
std::string ResolveTimeFormat(const std::vector<std::string>& globals) {
  constexpr std::string_view kPrefix = "--time-format=";
  std::string format;
  for (const std::string& global : globals) {
    if (global.starts_with(kPrefix)) {
      format = global.substr(kPrefix.size());  // last occurrence wins
    }
  }
  return format;
}

// --time-zone-suffix=auto|always|never controls whether a time field's named preset
// renders its zone suffix. Uses the shared tri-state parser, so true/yes/on == always
// and false/no/off == never; last occurrence wins, and an absent or unrecognized value
// keeps kAuto (each preset's built-in default).
datetime::ZoneSuffix ResolveZoneSuffix(const std::vector<std::string>& globals) {
  constexpr std::string_view kPrefix = "--time-zone-suffix=";
  datetime::ZoneSuffix suffix = datetime::ZoneSuffix::kAuto;
  for (const std::string& global : globals) {
    if (!global.starts_with(kPrefix)) {
      continue;
    }
    if (const std::optional<values::Tristate> tri = values::ParseTristate(global.substr(kPrefix.size()))) {
      suffix = *tri == values::Tristate::kOn    ? datetime::ZoneSuffix::kAlways
               : *tri == values::Tristate::kOff ? datetime::ZoneSuffix::kNever
                                                : datetime::ZoneSuffix::kAuto;
    }
  }
  return suffix;
}

// Wraps a FileSystem so Remove previews (emits the path) instead of deleting:
// backs --dry-run for -delete. ReadDir/Stat pass through unchanged.
class DryRunFileSystem : public vfs::FileSystem {
 public:
  DryRunFileSystem(const vfs::FileSystem& fs, EmitFn preview) : fs_(fs), preview_(preview) {}

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view dir) const override { return fs_.ReadDir(dir); }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool follow) const override {
    return fs_.Stat(path, follow);
  }

  bool Access(std::string_view path, vfs::AccessMode mode) const override { return fs_.Access(path, mode); }

  absl::StatusOr<std::string> ReadLink(std::string_view path) const override { return fs_.ReadLink(path); }

  absl::StatusOr<std::string> FsType(std::string_view path) const override { return fs_.FsType(path); }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view path) const override { return fs_.IsCaseSensitive(path); }

  absl::StatusOr<std::string> ReadContent(std::string_view path) const override { return fs_.ReadContent(path); }

  absl::Status Remove(std::string_view path) const override {
    preview_(absl::StrCat(path, "\n"));  // would-delete preview; nothing is removed
    return absl::OkStatus();
  }

 private:
  const vfs::FileSystem& fs_;
  EmitFn preview_;
};

// True if the expression contains an armed (effectful) action -- -delete or
// -exec. --safe refuses these. (-delete additionally implies -depth, applied
// by ResolveDepthOptions.)
bool ContainsArmedAction(const parser::Expr& expr) {
  switch (expr.kind) {
    case parser::Expr::Kind::kPredicate: return expr.descriptor->name == "-delete" || expr.descriptor->name == "-exec";
    case parser::Expr::Kind::kNot: return ContainsArmedAction(*expr.lhs);
    case parser::Expr::Kind::kAnd:
    case parser::Expr::Kind::kOr:
    case parser::Expr::Kind::kNand:
    case parser::Expr::Kind::kNor:
    case parser::Expr::Kind::kXor:
    case parser::Expr::Kind::kXnor:
    case parser::Expr::Kind::kComma: return ContainsArmedAction(*expr.lhs) || ContainsArmedAction(*expr.rhs);
  }
  return false;
}

// True if the expression mentions the primary `name` anywhere. Used for the
// positional options that take effect run-wide regardless of position (-daystart).
bool ContainsPrimary(const parser::Expr& expr, std::string_view name) {
  switch (expr.kind) {
    case parser::Expr::Kind::kPredicate: return expr.descriptor->name == name;
    case parser::Expr::Kind::kNot: return ContainsPrimary(*expr.lhs, name);
    case parser::Expr::Kind::kAnd:
    case parser::Expr::Kind::kOr:
    case parser::Expr::Kind::kNand:
    case parser::Expr::Kind::kNor:
    case parser::Expr::Kind::kXor:
    case parser::Expr::Kind::kXnor:
    case parser::Expr::Kind::kComma: return ContainsPrimary(*expr.lhs, name) || ContainsPrimary(*expr.rhs, name);
  }
  return false;
}

// Number of occurrences of one primary in the expression. Verification summaries require exactly
// one -hasheq because combining independent verdicts would otherwise hide which check failed.
std::size_t CountPrimary(const parser::Expr& expr, std::string_view name) {
  switch (expr.kind) {
    case parser::Expr::Kind::kPredicate: return expr.descriptor->name == name ? 1 : 0;
    case parser::Expr::Kind::kNot: return CountPrimary(*expr.lhs, name);
    case parser::Expr::Kind::kAnd:
    case parser::Expr::Kind::kOr:
    case parser::Expr::Kind::kNand:
    case parser::Expr::Kind::kNor:
    case parser::Expr::Kind::kXor:
    case parser::Expr::Kind::kXnor:
    case parser::Expr::Kind::kComma: return CountPrimary(*expr.lhs, name) + CountPrimary(*expr.rhs, name);
  }
  std::unreachable();
}

// Every `-first N` in the expression must carry a number. An unparseable or negative N is a usage
// error before the walk, NOT a filter that quietly admits nothing: `-first nope` is a typo, and a
// typo that silently returns an empty result set is indistinguishable from a tree with no matches.
// `-first 0` IS valid and means zero results - unambiguous, and the one case where "admit nothing"
// is what was asked for.
absl::Status ValidateFirstLimits(const parser::Expr& expr) {
  switch (expr.kind) {
    case parser::Expr::Kind::kPredicate: {
      if (!expr.descriptor.has_value() || expr.descriptor->name != "-first") {
        return absl::OkStatus();
      }
      int limit = 0;
      if (expr.args.empty() || !absl::SimpleAtoi(expr.args.front(), &limit)) {
        return absl::InvalidArgumentError(
            absl::StrCat("expects a count, got '", expr.args.empty() ? "" : expr.args.front(), "'"));
      }
      if (limit < 0) {
        return absl::InvalidArgumentError(absl::StrCat("count cannot be negative, got '", expr.args.front(), "'"));
      }
      return absl::OkStatus();
    }
    case parser::Expr::Kind::kNot: return ValidateFirstLimits(*expr.lhs);
    case parser::Expr::Kind::kAnd:
    case parser::Expr::Kind::kOr:
    case parser::Expr::Kind::kNand:
    case parser::Expr::Kind::kNor:
    case parser::Expr::Kind::kXor:
    case parser::Expr::Kind::kXnor:
    case parser::Expr::Kind::kComma: {
      MBO_RETURN_IF_ERROR(ValidateFirstLimits(*expr.lhs));
      return ValidateFirstLimits(*expr.rhs);
    }
  }
  return absl::OkStatus();
}

absl::Status ValidateTopLimits(const parser::Expr& expr) {
  switch (expr.kind) {
    case parser::Expr::Kind::kPredicate: {
      if (!expr.descriptor.has_value() || expr.descriptor->name != "-top") {
        return absl::OkStatus();
      }
      int limit = 0;
      if (expr.args.empty() || !absl::SimpleAtoi(expr.args.front(), &limit)) {
        return absl::InvalidArgumentError(
            absl::StrCat("expects a count, got '", expr.args.empty() ? "" : expr.args.front(), "'"));
      }
      if (limit < 0) {
        return absl::InvalidArgumentError(absl::StrCat("count cannot be negative, got '", expr.args.front(), "'"));
      }
      return absl::OkStatus();
    }
    case parser::Expr::Kind::kNot: return ValidateTopLimits(*expr.lhs);
    case parser::Expr::Kind::kAnd:
    case parser::Expr::Kind::kOr:
    case parser::Expr::Kind::kNand:
    case parser::Expr::Kind::kNor:
    case parser::Expr::Kind::kXor:
    case parser::Expr::Kind::kXnor:
    case parser::Expr::Kind::kComma:
      MBO_RETURN_IF_ERROR(ValidateTopLimits(*expr.lhs));
      return ValidateTopLimits(*expr.rhs);
  }
  return absl::OkStatus();
}

// Every primary that SETS the score, so adding one cannot leave ranking silently refusing it.
constexpr std::array kScoringPrimaries =
    std::to_array<std::string_view>({"-fuzzy", "-fuzzypath", "-ifuzzy", "-ifuzzypath"});

struct ScoreDomain {
  std::optional<int> threshold;
  std::optional<parser::FuzzyModel> model;
  bool mixed_thresholds = false;
  bool mixed_models = false;
};

void InspectScoreDomain(const parser::Expr& expr, ScoreDomain& domain) {
  switch (expr.kind) {
    case parser::Expr::Kind::kPredicate:
      if (absl::c_linear_search(kScoringPrimaries, expr.descriptor->name)) {
        const int threshold = expr.fuzzy_threshold.value_or(0);
        domain.mixed_thresholds |= domain.threshold.value_or(threshold) != threshold;
        domain.threshold = threshold;
        domain.mixed_models |= domain.model.value_or(expr.fuzzy_model) != expr.fuzzy_model;
        domain.model = expr.fuzzy_model;
      }
      return;
    case parser::Expr::Kind::kNot: InspectScoreDomain(*expr.lhs, domain); return;
    case parser::Expr::Kind::kAnd:
    case parser::Expr::Kind::kOr:
    case parser::Expr::Kind::kNand:
    case parser::Expr::Kind::kNor:
    case parser::Expr::Kind::kXor:
    case parser::Expr::Kind::kXnor:
    case parser::Expr::Kind::kComma:
      InspectScoreDomain(*expr.lhs, domain);
      InspectScoreDomain(*expr.rhs, domain);
      return;
  }
}

// Whether --sort=score can do what it says, checked before the walk. Both refusals are usage
// errors rather than a silent no-op: ordering by a value nothing produced is a mistake, and a
// format that cannot be reordered would drop the ranking without saying so.
absl::Status ValidateScoreRanking(
    bool rank_by_score,
    mbo::types::OptionalRef<const parser::Expr> expression,
    bool is_tree,
    bool buffered) {
  if (!rank_by_score) {
    return absl::OkStatus();
  }
  const bool has_fuzzy =
      expression.has_value() && absl::c_any_of(kScoringPrimaries, [expression](std::string_view name) {
        return ContainsPrimary(*expression, name);
      });
  if (!has_fuzzy) {
    return absl::InvalidArgumentError(
        absl::StrCat("needs one of ", absl::StrJoin(kScoringPrimaries, ", "), " in the expression"));
  }
  ScoreDomain domain;
  InspectScoreDomain(*expression, domain);
  if (domain.mixed_models) {
    return absl::InvalidArgumentError(
        "cannot compare fuzzy matches from different models; use the same fzf / sequence / levenshtein / "
        "shingles model on every -fuzzy/-fuzzypath test");
  }
  if (domain.mixed_thresholds) {
    return absl::InvalidArgumentError(
        "cannot compare fuzzy matches with different quality thresholds; use the same PCT% on every "
        "-fuzzy/-fuzzypath test (a bare fuzzy test has a 0% threshold)");
  }
  if (is_tree || buffered) {
    // --format=tree nests by path and the aligned/markdown writers stream rows through a width
    // buffer; either would silently ignore the ranking, which is worse than refusing it.
    return absl::InvalidArgumentError(
        absl::StrCat(
            "cannot rank a --format=", is_tree ? "tree" : "aligned/markdown",
            " listing; use a streaming format (the default, or --columns with csv/tsv)"));
  }
  return absl::OkStatus();
}

struct TopFlow {
  bool score_on_success = false;
  bool has_top = false;
  bool top_without_score = false;
};

TopFlow InspectTopFlow(const parser::Expr& expr, bool incoming_score) {
  switch (expr.kind) {
    case parser::Expr::Kind::kPredicate: {
      const bool scoring = absl::c_linear_search(kScoringPrimaries, expr.descriptor->name);
      const bool top = expr.descriptor->name == "-top";
      return {
          .score_on_success = incoming_score || scoring,
          .has_top = top,
          .top_without_score = top && !incoming_score,
      };
    }
    case parser::Expr::Kind::kNot: {
      TopFlow value = InspectTopFlow(*expr.lhs, incoming_score);
      value.score_on_success = incoming_score;
      return value;
    }
    case parser::Expr::Kind::kAnd: {
      const TopFlow lhs = InspectTopFlow(*expr.lhs, incoming_score);
      const TopFlow rhs = InspectTopFlow(*expr.rhs, lhs.score_on_success);
      return {
          .score_on_success = rhs.score_on_success,
          .has_top = lhs.has_top || rhs.has_top,
          .top_without_score = lhs.top_without_score || rhs.top_without_score,
      };
    }
    case parser::Expr::Kind::kComma: {
      const TopFlow lhs = InspectTopFlow(*expr.lhs, incoming_score);
      const TopFlow rhs = InspectTopFlow(*expr.rhs, incoming_score);
      return {
          .score_on_success = rhs.score_on_success,
          .has_top = lhs.has_top || rhs.has_top,
          .top_without_score = lhs.top_without_score || rhs.top_without_score,
      };
    }
    case parser::Expr::Kind::kOr:
    case parser::Expr::Kind::kXor:
    case parser::Expr::Kind::kNand:
    case parser::Expr::Kind::kNor:
    case parser::Expr::Kind::kXnor: {
      const TopFlow lhs = InspectTopFlow(*expr.lhs, incoming_score);
      const TopFlow rhs = InspectTopFlow(*expr.rhs, incoming_score);
      return {
          // A later -top may only rely on a score when every successful branch supplies one.
          .score_on_success = lhs.score_on_success && rhs.score_on_success,
          .has_top = lhs.has_top || rhs.has_top,
          .top_without_score = lhs.top_without_score || rhs.top_without_score,
      };
    }
  }
  return {};
}

absl::Status ValidateTopRanking(mbo::types::OptionalRef<const parser::Expr> expression) {
  if (!expression.has_value()) {
    return absl::OkStatus();
  }
  MBO_RETURN_IF_ERROR(ValidateTopLimits(*expression));
  const TopFlow flow = InspectTopFlow(*expression, false);
  if (!flow.has_top) {
    return absl::OkStatus();
  }
  if (flow.top_without_score) {
    return absl::InvalidArgumentError("must follow a successful -fuzzy/-fuzzypath matcher on every path");
  }
  ScoreDomain domain;
  InspectScoreDomain(*expression, domain);
  if (domain.mixed_models) {
    return absl::InvalidArgumentError(
        "cannot compare fuzzy matches from different models; use one model for every contributing matcher");
  }
  if (domain.mixed_thresholds) {
    return absl::InvalidArgumentError(
        "cannot compare fuzzy matches with different quality thresholds; use the same PCT% on every contributing "
        "matcher (a bare fuzzy test has a 0% threshold)");
  }
  return absl::OkStatus();
}

absl::Status ValidateShardStatuses(const parser::Expr& expr) {
  if (expr.kind == parser::Expr::Kind::kPredicate) {
    if (expr.descriptor->name != "-shard-status") {
      return absl::OkStatus();
    }
    if (expr.args.size() != 1
        || (expr.args.front() != "complete" && expr.args.front() != "incomplete"
            && expr.args.front() != "superfluous")) {
      return absl::InvalidArgumentError("expects complete, incomplete, or superfluous");
    }
    return absl::OkStatus();
  }
  if (expr.lhs != nullptr) {
    MBO_RETURN_IF_ERROR(ValidateShardStatuses(*expr.lhs));
  }
  if (expr.rhs != nullptr) {
    MBO_RETURN_IF_ERROR(ValidateShardStatuses(*expr.rhs));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::optional<std::size_t>> ResolveMaxResults(const std::vector<std::string>& globals) {
  std::optional<std::size_t> limit;
  for (const std::string& global : globals) {
    if (global == "--max-results") {
      return absl::InvalidArgumentError("expects '=N'");
    }
    constexpr std::string_view kPrefix = "--max-results=";
    if (!global.starts_with(kPrefix)) {
      continue;
    }
    std::int64_t parsed = 0;
    const std::string_view value = std::string_view(global).substr(kPrefix.size());
    if (!absl::SimpleAtoi(value, &parsed)) {
      return absl::InvalidArgumentError(absl::StrCat("expects a count, got '", value, "'"));
    }
    if (parsed < 0) {
      return absl::InvalidArgumentError(absl::StrCat("count cannot be negative, got '", value, "'"));
    }
    limit = static_cast<std::size_t>(parsed);
  }
  return limit;
}

// The pre-walk gate for result-set shaping: every rule that must hold before a single entry is
// visited. Reports through `on_error` and answers whether to proceed, so RunFind carries ONE gate
// however many rules there are - a new rule is added here instead of by growing RunFind, which is
// already at the function-size limit.
bool ResultShapingIsValid(
    mbo::types::OptionalRef<const parser::Expr> expression,
    bool rank_by_score,
    bool is_tree,
    bool buffered,
    WalkErrorFn on_error) {
  if (expression.has_value()) {
    if (const absl::Status first = ValidateFirstLimits(*expression); !first.ok()) {
      on_error("-first", first);
      return false;
    }
    if (const absl::Status shard_status = ValidateShardStatuses(*expression); !shard_status.ok()) {
      on_error("-shard-status", shard_status);
      return false;
    }
  }
  if (const absl::Status top = ValidateTopRanking(expression); !top.ok()) {
    on_error("-top", top);
    return false;
  }
  if (const absl::Status ranking = ValidateScoreRanking(rank_by_score, expression, is_tree, buffered); !ranking.ok()) {
    on_error("--sort=score", ranking);
    return false;
  }
  return true;
}

struct DeferredCandidate {
  CollectedEntry entry;
  std::vector<std::string> captures;
  std::map<std::string, std::string> outputs;
  EvaluationMemo memo;
  DeferredDecisions decisions;
  std::optional<bool> hash_verification;
  ExprIdentity waiting_at;
  int score = 0;
  std::size_t order = 0;
};

CollectedEntry OwnVisit(const Visit& visit) {
  return CollectedEntry{
      .path = std::string(visit.path),
      .name = std::string(visit.name),
      .root = std::string(visit.root),
      .depth = visit.depth,
      .metadata = visit.metadata,
      .fs = visit.fs,
      .fs_owner = visit.fs_owner,
  };
}

void AppendDeferredNodes(const parser::Expr& expr, std::vector<ExprIdentity>& nodes) {
  if (expr.kind == parser::Expr::Kind::kPredicate) {
    if (expr.descriptor->name == "-top" || expr.descriptor->name == "-shard-status") {
      nodes.emplace_back(expr);
    }
    return;
  }
  if (expr.lhs != nullptr) {
    AppendDeferredNodes(*expr.lhs, nodes);
  }
  if (expr.rhs != nullptr) {
    AppendDeferredNodes(*expr.rhs, nodes);
  }
}

int ReportShardDuplicateErrors(const shard::ShardSet& set, std::string_view prefix) {
  int errors = 0;
  for (const shard::ShardMember& member : set.members) {
    if (member.duplicates.empty()) {
      continue;
    }
    std::cerr << absl::StreamFormat(
        "xff: --shards-dedup=error: shard set '%s%s' has duplicate copies of shard %d: %s, %s\n", prefix, set.wildcard,
        member.index, member.path, absl::StrJoin(member.duplicates, ", "));
    ++errors;
  }
  return errors;
}

using CandidateIndexes = std::vector<std::size_t>;
using CandidatesByName = std::map<std::string_view, CandidateIndexes>;

using DeferredCandidateRefs = std::vector<std::reference_wrapper<DeferredCandidate>>;

std::map<std::string, CandidateIndexes> IndexCandidatesByDirectory(const DeferredCandidateRefs& entries) {
  std::map<std::string, CandidateIndexes> result;
  for (std::size_t index = 0; index < entries.size(); ++index) {
    const std::string_view path = entries[index].get().entry.path;
    const std::string_view::size_type slash = path.rfind('/');
    const std::string_view directory = slash == std::string_view::npos ? std::string_view() : path.substr(0, slash);
    result[std::string(directory)].push_back(index);
  }
  return result;
}

struct ShardStatusCohort {
  std::vector<shard::ShardFile> files;
  CandidatesByName by_name;
};

template<typename SchemeAllowed>
ShardStatusCohort MakeShardStatusCohort(
    const DeferredCandidateRefs& entries,
    absl::Span<const std::size_t> indexes,
    const shard::Matcher& shard_matcher,
    const SchemeAllowed& scheme_allowed) {
  ShardStatusCohort cohort;
  for (const std::size_t index : indexes) {
    const DeferredCandidate& candidate = entries[index];
    if (candidate.entry.metadata.type != vfs::FileType::kRegular) {
      continue;
    }
    const std::optional<shard::Match> decoded = shard_matcher.Decode(candidate.entry.name);
    if (!decoded.has_value() || !scheme_allowed(decoded->scheme)) {
      continue;
    }
    CandidateIndexes& visits = cohort.by_name[candidate.entry.name];
    if (visits.empty()) {
      cohort.files.push_back(
          {.name = candidate.entry.name,
           .size = candidate.entry.metadata.size,
           .mode = candidate.entry.metadata.mode,
           .mtime = absl::ToUnixNanos(candidate.entry.metadata.mtime)});
    }
    visits.push_back(index);
  }
  return cohort;
}

void SelectShardPath(
    std::string_view path,
    const parser::Expr& node,
    const DeferredCandidateRefs& entries,
    const CandidatesByName& by_name) {
  for (const std::size_t index : by_name.at(path)) {
    entries[index].get().decisions[ExprIdentity{node}] = true;
  }
}

void ApplyShardStatus(
    const shard::ShardSet& set,
    std::string_view wanted,
    const parser::Expr& node,
    const DeferredCandidateRefs& entries,
    const CandidatesByName& by_name) {
  if (wanted == "superfluous") {
    for (const shard::SuperfluousShard& extra : set.superfluous) {
      SelectShardPath(extra.path, node, entries, by_name);
    }
    return;
  }
  if (wanted != (set.complete ? "complete" : "incomplete")) {
    return;
  }
  for (const shard::ShardMember& member : set.members) {
    SelectShardPath(member.path, node, entries, by_name);
  }
}

template<typename SchemeAllowed>
int ResolveShardStatusRound(
    const parser::Expr& node,
    const DeferredCandidateRefs& entries,
    const shard::Matcher& shard_matcher,
    shard::Dedup shard_dedup,
    const SchemeAllowed& scheme_allowed,
    bool report_dedup_errors) {
  for (DeferredCandidate& candidate : entries) {
    candidate.decisions[ExprIdentity{node}] = false;
  }
  int errors = 0;
  for (const auto& [directory, indexes] : IndexCandidatesByDirectory(entries)) {
    const ShardStatusCohort cohort = MakeShardStatusCohort(entries, indexes, shard_matcher, scheme_allowed);
    const std::string prefix = directory.empty() ? std::string() : absl::StrCat(directory, "/");
    for (const shard::ShardSet& set : shard::GroupShards(cohort.files, shard_matcher, shard_dedup)) {
      if (report_dedup_errors && shard_dedup == shard::Dedup::kError) {
        errors += ReportShardDuplicateErrors(set, prefix);
      }
      ApplyShardStatus(set, node.args.front(), node, entries, cohort.by_name);
    }
  }
  return errors;
}

template<typename SchemeAllowed>
int ResolveDeferredRound(
    std::vector<DeferredCandidate>& candidates,
    const std::map<ExprIdentity, std::size_t>& node_order,
    const shard::Matcher& shard_matcher,
    shard::Dedup shard_dedup,
    const SchemeAllowed& scheme_allowed,
    bool report_dedup_errors) {
  // The caller enters only with a non-empty deferred frontier, and every candidate carries the
  // required node at which evaluation stopped.
  ExprIdentity next_node = candidates.front().waiting_at;
  std::size_t next_order = node_order.at(next_node);
  for (const DeferredCandidate& candidate : candidates) {
    const std::size_t order = node_order.at(candidate.waiting_at);
    if (order < next_order) {
      next_order = order;
      next_node = candidate.waiting_at;
    }
  }
  DeferredCandidateRefs entries;
  for (DeferredCandidate& candidate : candidates) {
    if (next_node == candidate.waiting_at) {
      entries.emplace_back(candidate);
    }
  }
  if (next_node.Get().descriptor->name == "-shard-status") {
    return ResolveShardStatusRound(
        next_node.Get(), entries, shard_matcher, shard_dedup, scheme_allowed, report_dedup_errors);
  }
  int limit = 0;
  // `-top` arity and its positive integer argument were validated before traversal.
  static_cast<void>(absl::SimpleAtoi(next_node.Get().args.front(), &limit));
  absl::c_stable_sort(
      entries, [](const DeferredCandidate& lhs, const DeferredCandidate& rhs) { return lhs.score > rhs.score; });
  for (std::size_t index = 0; index < entries.size(); ++index) {
    entries[index].get().decisions[next_node] = std::cmp_less(index, limit);
  }
  return 0;
}

// The path of `path` relative to the search `root` it was reached from, '/'-
// separated with no leading '/' -- what the ignore patterns match against. The
// walk only yields the root itself or paths beneath it, so stripping the root
// prefix (and any separator) is exact; the root entry maps to "" (never ignored).
std::string_view RelativeTo(std::string_view path, std::string_view root) {
  if (path == root || root.empty()) {
    return path == root ? std::string_view() : path;
  }
  if (path.size() > root.size() && path.starts_with(root)) {
    std::string_view rest = path.substr(root.size());
    while (!rest.empty() && rest.front() == '/') {
      rest.remove_prefix(1);
    }
    return rest;
  }
  return path;  // not root-prefixed (should not happen from the walk); match whole
}

// Builds the ignore/filter set from the run's `--exclude=GLOB` / `--include=GLOB`
// globals, in command-line order so the gitignore last-match-wins rule holds across
// them: `--exclude` adds a plain pattern, `--include` a negation (re-include).
ignore::PatternList BuildIgnorePatterns(const std::vector<std::string>& globals) {
  constexpr std::string_view kExclude = "--exclude=";
  constexpr std::string_view kInclude = "--include=";
  ignore::PatternList patterns;
  for (const std::string& global : globals) {
    if (global.starts_with(kExclude)) {
      patterns.Add(global.substr(kExclude.size()), /*negate=*/false);
    } else if (global.starts_with(kInclude)) {
      patterns.Add(global.substr(kInclude.size()), /*negate=*/true);
    }
  }
  return patterns;
}

// Resolves a search root to an absolute, normalized path for repo discovery (which
// walks the path string upward, so it needs an absolute path). Prepends the process
// cwd for a relative root; falls back to the raw path if that fails.
std::string AbsoluteDir(std::string_view root) {
  std::error_code ec;
  const std::filesystem::path abs = std::filesystem::absolute(std::filesystem::path(root), ec);
  return ec ? std::string(root) : abs.lexically_normal().string();
}

// Per-directory ignore-file lookup for --ignore-files / -g: reads and caches each
// directory's combined ignore PatternList (.gitignore < .ignore < .xffignore, so the
// later file wins) lazily, and answers the ignore Decision for a path from the ignore
// files in its ancestor directories -- deepest first, so a deeper file overrides a
// shallower one (gitignore precedence).
//
// When gitignore is on and a search root is inside a git repo, the ancestor walk is
// rebased on the REPO ROOT (git/rg/fd behavior): every directory from the entry up to
// the repo root is consulted -- including directories ABOVE the search root -- and the
// repo's `.git/info/exclude` is applied at the bottom (below every `.gitignore`). Off
// a repo (or gitignore off) the walk stops at the search root, unchanged.
//
// Single-threaded: the walk visitor runs on one coordinator thread, so the cache needs
// no lock. Inactive (no filenames) is a no-op.
class IgnoreFileCache {
 public:
  IgnoreFileCache(
      const vfs::FileSystem& fs,
      std::vector<std::string> filenames,
      bool gitignore_on,
      ignore::PatternList global_excludes)
      : fs_(fs),
        filenames_(std::move(filenames)),
        gitignore_on_(gitignore_on),
        global_excludes_(std::move(global_excludes)) {}

  bool Active() const { return !filenames_.empty(); }

  ignore::Decision Decide(std::string_view path, std::string_view root, bool is_dir) {
    const Scope& scope = ScopeFor(root);
    // In repo scope the walk is done in absolute paths anchored at the repo root, so a
    // relative entry (e.g. under root ".") is first rebased onto the search root's
    // absolute form. Off a repo, `base` is the search root as given and no rebase is
    // needed. `owner` keeps the rebased string alive for the string_views below.
    std::string owner;
    std::string_view rel;
    if (scope.in_repo) {
      const std::string_view rel_to_root = RelativeTo(path, root);
      owner = rel_to_root.empty() ? scope.abs_root : absl::StrCat(scope.abs_root, "/", rel_to_root);
      rel = RelativeTo(owner, scope.base);
    } else {
      rel = RelativeTo(path, root);
    }
    // A `.gitkeep` intentionally keeps its (otherwise-empty) directory in the repo, so the gitignore
    // layers never ignore it - it is always kept, as if by a top-precedence `!.gitkeep`. Explicit
    // --exclude / --include (global_excludes_) still decide, so a user can still override it.
    if (Active() && rel.substr(rel.rfind('/') + 1) == ".gitkeep") {
      return global_excludes_.Match(rel, is_dir);
    }
    // Walk the ancestor directories of the entry, deepest first: for rel "a/b/c" that is
    // "a/b", then "a", then "" (the base). Each directory's ignore file matches the entry
    // relative to THAT directory.
    std::string_view ancestor = rel;
    for (;;) {
      const std::string_view::size_type slash = ancestor.rfind('/');
      ancestor = slash == std::string_view::npos ? std::string_view() : ancestor.substr(0, slash);
      const std::string dir = ancestor.empty() ? scope.base : absl::StrCat(scope.base, "/", ancestor);
      const std::string_view sub = ancestor.empty() ? rel : rel.substr(ancestor.size() + 1);
      const ignore::Decision decision = ListFor(dir).Match(sub, is_dir);
      if (decision != ignore::Decision::kDefault) {
        return decision;
      }
      if (ancestor.empty()) {
        // At the repo root, `.git/info/exclude` applies below every `.gitignore`, and
        // git's global excludes (`core.excludesFile`) below that -- both repo-root-
        // anchored, so they match the entry relative to the repo root: `rel`.
        if (scope.in_repo) {
          const ignore::Decision decision = RepoExcludeFor(scope.base).Match(rel, is_dir);
          if (decision != ignore::Decision::kDefault) {
            return decision;
          }
        }
        return global_excludes_.Match(rel, is_dir);  // lowest layer; empty (a no-op) when off / none
      }
    }
  }

 private:
  // Where a search root's ignore walk is anchored: `base` is the repo root (absolute)
  // when gitignore is on and the root is in a repo (`in_repo`), else the search root as
  // given. `abs_root` is the root's absolute form, used to rebase relative entries.
  struct Scope {
    std::string base;
    std::string abs_root;
    bool in_repo = false;
  };

  const Scope& ScopeFor(std::string_view root) {
    const auto it = scope_cache_.find(std::string(root));
    if (it != scope_cache_.end()) {
      return it->second;
    }
    Scope scope;
    if (gitignore_on_) {
      scope.abs_root = AbsoluteDir(root);
      if (const std::optional<std::string> repo_root = repo::FindRepoRoot(fs_, scope.abs_root)) {
        scope.base = *repo_root;
        scope.in_repo = true;
      }
    }
    if (!scope.in_repo) {
      scope.base = std::string(root);
    }
    return scope_cache_.emplace(std::string(root), std::move(scope)).first->second;
  }

  const ignore::PatternList& ListFor(const std::string& dir) {
    const auto it = cache_.find(dir);
    if (it != cache_.end()) {
      return it->second;
    }
    ignore::PatternList list;
    for (const std::string& name : filenames_) {
      if (const absl::StatusOr<std::string> content = fs_.ReadContent(absl::StrCat(dir, "/", name)); content.ok()) {
        list.AddPatterns(*content);
      }
    }
    return cache_.emplace(dir, std::move(list)).first->second;
  }

  // The repo's `.git/info/exclude` (gitignore-format), matched relative to the repo
  // root; cached per repo root, empty when the file is absent.
  const ignore::PatternList& RepoExcludeFor(const std::string& repo_root) {
    const auto it = repo_exclude_cache_.find(repo_root);
    if (it != repo_exclude_cache_.end()) {
      return it->second;
    }
    ignore::PatternList list;
    if (const absl::StatusOr<std::string> content = fs_.ReadContent(absl::StrCat(repo_root, "/.git/info/exclude"));
        content.ok()) {
      list.AddPatterns(*content);
    }
    return repo_exclude_cache_.emplace(repo_root, std::move(list)).first->second;
  }

  const vfs::FileSystem& fs_;
  std::vector<std::string> filenames_;
  bool gitignore_on_;
  ignore::PatternList global_excludes_;  // git core.excludesFile, applied below .git/info/exclude
  std::map<std::string, Scope> scope_cache_;
  std::map<std::string, ignore::PatternList> cache_;
  std::map<std::string, ignore::PatternList> repo_exclude_cache_;
};

// --ignore-file=PATH sources: each an explicitly named ignore file whose gitignore-format
// patterns are rooted at the file's OWN directory (its absolute, normalized parent), not the
// search root. An entry matches a source only when it lies within that root; its path relative
// to the root is tested. Pointing the flag at the file both selects the patterns and anchors
// them, so no separate --ignore-file-root flag is needed. Repeatable: sources are consulted in
// command-line order and the last non-silent one wins (later --ignore-file overrides earlier,
// like the gitignore last-match convention across files). Read best-effort -- an unreadable file
// contributes nothing, matching how a missing .gitignore is handled. Applied whole-run, so it is
// independent of -g / --gitignore (which drive the auto per-directory stack).
class RootedIgnoreFiles {
 public:
  RootedIgnoreFiles() = default;

  // Builds the sources from the --ignore-file=PATH globals (command-line order), reading each
  // through `fs`. A source's root is AbsoluteDir(dirname(PATH)); a bare filename roots at cwd.
  static RootedIgnoreFiles FromGlobals(const vfs::FileSystem& fs, const std::vector<std::string>& globals) {
    constexpr std::string_view kIgnoreFile = "--ignore-file=";
    RootedIgnoreFiles out;
    for (const std::string& global : globals) {
      if (!global.starts_with(kIgnoreFile)) {
        continue;
      }
      const std::string_view path = std::string_view(global).substr(kIgnoreFile.size());
      if (path.empty()) {
        continue;
      }
      const std::string_view::size_type slash = path.rfind('/');
      const std::string_view dir = slash == std::string_view::npos ? std::string_view(".") : path.substr(0, slash);
      Source source{.root = AbsoluteDir(dir), .patterns = {}};
      if (const absl::StatusOr<std::string> content = fs.ReadContent(path); content.ok()) {
        source.patterns = ignore::PatternList::Parse(*content);
      }
      out.sources_.push_back(std::move(source));
    }
    return out;
  }

  bool Active() const { return !sources_.empty(); }

  // The decision for an entry given its absolute, normalized path. Each source whose root
  // contains the entry contributes; the last non-default decision wins (later --ignore-file
  // overrides earlier). kDefault when no source's root contains the entry, or all are silent.
  ignore::Decision Decide(std::string_view abs_path, bool is_dir) const {
    ignore::Decision result = ignore::Decision::kDefault;
    for (const Source& source : sources_) {
      const std::optional<std::string_view> rel = Under(abs_path, source.root);
      if (!rel.has_value()) {
        continue;  // the entry is outside this source's root subtree
      }
      if (const ignore::Decision decision = source.patterns.Match(*rel, is_dir);
          decision != ignore::Decision::kDefault) {
        result = decision;
      }
    }
    return result;
  }

 private:
  struct Source {
    std::string root;  // absolute, normalized directory the patterns are relative to
    ignore::PatternList patterns;
  };

  // `abs_path` relative to `root` ('/'-separated, no leading '/'), or nullopt when `abs_path` is
  // neither `root` nor beneath it. The root directory itself maps to "" (matched against, but a
  // pattern rarely matches "", and the walk never filters the named search root anyway).
  static std::optional<std::string_view> Under(std::string_view abs_path, std::string_view root) {
    if (abs_path == root) {
      return std::string_view();
    }
    if (abs_path.size() > root.size() && abs_path.starts_with(root) && abs_path[root.size()] == '/') {
      return abs_path.substr(root.size() + 1);
    }
    return std::nullopt;
  }

  std::vector<Source> sources_;
};

bool HasGlobal(const std::vector<std::string>& globals, std::string_view flag) {
  return absl::c_any_of(globals, [flag](std::string_view global) { return global == flag; });
}

// --gitignore / -g ternary. Bare `-g` / `--gitignore` selects AUTO (respect .gitignore
// only when the traversal is inside a git repo, matching git's own behavior);
// `--gitignore=on` (or the short `-g+`) forces it on regardless, `--gitignore=off`
// (short `-g-`) forces it off. The rg-style `--ignore-vcs` / `--no-ignore-vcs` are synonyms
// for the VCS ignore-file layer: `--ignore-vcs` == AUTO (respect it, like bare -g) and
// `--no-ignore-vcs` == OFF (drop it) -- today git is xff's only VCS ignore file, so
// --no-ignore-vcs is nearly --gitignore=off. All of these are one last-occurrence-wins scan;
// `.ignore` / `.xffignore` are a separate axis (--ignore-files), untouched here. Off by
// default (find-compatible). -u / --no-ignore overrules them all: the master switch over every
// ignore source is position-independent, not a participant in the last-wins scan.
enum class GitignoreMode { kOff, kOn, kAuto };

GitignoreMode ResolveGitignoreMode(const std::vector<std::string>& globals, std::optional<registry::Style> style) {
  if (HasGlobal(globals, "--no-ignore") || HasGlobal(globals, "-u")) {
    return GitignoreMode::kOff;
  }
  // The opinionated style (rg) respect ignore files by default (their headline
  // behavior); find/xff start off (find-compatible). An explicit -g / --gitignore flag
  // still overrides.
  const bool opinionated = style == registry::Style::kRg;
  GitignoreMode mode = opinionated ? GitignoreMode::kOn : GitignoreMode::kOff;
  constexpr std::string_view kPrefix = "--gitignore=";
  for (const std::string& global : globals) {
    if (global == "-g" || global == "--gitignore" || global == "--ignore-vcs") {
      mode = GitignoreMode::kAuto;
    } else if (global == "-g+") {
      mode = GitignoreMode::kOn;
    } else if (global == "-g-" || global == "--no-ignore-vcs") {
      mode = GitignoreMode::kOff;
    } else if (global.starts_with(kPrefix)) {
      // The shared vocabulary (on / off / auto plus yes / no / true / false / 1 / 0), so this flag
      // spells its values the way every other tri-state does; an unrecognized value is ignored,
      // leaving the prior resolution.
      if (const std::optional<values::Tristate> tri = values::ParseTristate(global.substr(kPrefix.size()))) {
        mode = *tri == values::Tristate::kOn    ? GitignoreMode::kOn
               : *tri == values::Tristate::kOff ? GitignoreMode::kOff
                                                : GitignoreMode::kAuto;
      }
    }
  }
  return mode;
}

// --archive / -z: how far archive diving descends. The three modes are NESTED
// (none subset roots subset all), so one ordered enum expresses two separately-wanted
// behaviors: diving into an archive named AS A ROOT, and diving into archives met
// mid-walk. `none` keeps find's behavior (an archive is one plain file), `roots` dives
// only when a search root is itself an archive (pointing xff AT an archive implies
// looking inside), `all` also dives archives discovered during the walk. Bare --archive
// is `all`; the short form takes chmod-style suffix signs like the -g gitignore trio
// (-z- none, -z roots, -z+ all). Last occurrence wins. The default is style-scoped: the
// find style stays at `none` for drop-in fidelity, every xff-family style starts at
// `roots`.
enum class ArchiveMode : std::uint8_t { kNone, kRoots, kAll, kAny };

ArchiveMode ResolveArchiveMode(const std::vector<std::string>& globals, std::optional<registry::Style> style) {
  // find keeps archives opaque; the xff family looks inside one it was pointed at.
  ArchiveMode mode = style == registry::Style::kFind ? ArchiveMode::kNone : ArchiveMode::kRoots;
  for (const std::string& global : globals) {
    // The short forms come in a lower-case (read) and an upper-case (read + write) family whose
    // RUNGS are identical, so both spellings of a rung are read here and only ResolveArchiveWrite
    // cares about the case. `-Z-` is the none rung in both families, and additionally disarms
    // writing (see ResolveArchiveWrite).
    if (global == "--archive=any" || global == "--archive-any" || global == "-z++" || global == "-Z++") {
      mode = ArchiveMode::kAny;
    } else if (global == "--archive" || global == "--archive=all" || global == "-z+" || global == "-Z+") {
      mode = ArchiveMode::kAll;
    } else if (global == "--archive=roots" || global == "-z" || global == "-Z") {
      mode = ArchiveMode::kRoots;
    } else if (global == "--archive=none" || global == "-z-" || global == "-Z-") {
      mode = ArchiveMode::kNone;
    }
  }
  return mode;
}

// Whether the run armed the archive WRITE surface: the two flags that let an action touch a member
// (`--archive-extract`, `--archive-delete`), or the umbrella that arms both.
//
// The umbrella is a spelling of the two flags rather than a third mechanism, so `-Z++ file.tar` and
// `--archive=any --archive-extract --archive-delete` are the same run. It is a separate question from
// the dive MODE, which is why the check is its own function: `--archive-write` arms writing without
// touching how far the walk dives, lowercase `-z++` changes only the read rung, and uppercase `-Z++`
// does both.
struct ArchiveWrite {
  bool extract = false;
  bool remove = false;
};

ArchiveWrite ResolveArchiveWrite(const std::vector<std::string>& globals) {
  ArchiveWrite write;
  for (const std::string& global : globals) {
    // The UPPER-case short family is the write one, at every rung: `-Z` is `-z` plus the write
    // flags, `-Z+` is `-z+` plus them, and so on. Case carries the capability, the signs carry the
    // level, so neither axis can be reached by accident while aiming at the other.
    //
    // The axes stay INDEPENDENT even where one looks pointless: `-Z++ -z-` arms member-writing with
    // reading off, so it has no observable effect unless the run also names a write sink. `--pack`
    // is the create-without-harvesting shape, although creating a new ordinary file does not itself
    // REQUIRE member-write permission; with reading enabled, -delete / the exec family are the
    // operations that consume the permission. A lower-case form never disarms; only `-Z-` does.
    if (global == "-Z-") {
      // The reset: `-Z-` is `-z-` said out loud about writing, so it clears what any earlier
      // spelling armed (a config file's `-Z+`, or an earlier flag on the same line).
      write = ArchiveWrite{};
    } else if (global == "--archive-write" || global == "-Z" || global == "-Z+" || global == "-Z++") {
      write = ArchiveWrite{.extract = true, .remove = true};
    } else if (global == "--archive-extract") {
      write.extract = true;
    } else if (global == "--archive-delete") {
      write.remove = true;
    }
  }
  return write;
}

// True when the run EXPLICITLY asked for archive handling (any spelling), as opposed to
// inheriting a style default. The not-yet-implemented guard fires only on an explicit
// request, so the xff family's `roots` default cannot break an ordinary walk.
bool HasArchiveFlag(const std::vector<std::string>& globals) {
  return absl::c_any_of(globals, [](std::string_view global) {
    return global == "--archive" || global.starts_with("--archive=") || global == "-z" || global == "-z+"
           || global == "-z++" || global == "-z-" || global == "-Z" || global == "-Z+" || global == "-Z++";
  });
}

// The walk's spelling of the same three modes. Two enums exist because the walk knows nothing about
// flags and the CLI knows nothing about traversal; this is the one place they meet.
ArchiveDive ArchiveDiveOf(ArchiveMode mode) {
  switch (mode) {
    // `any` is `all` on the DIVE axis: the extra it adds is dropping the name gate on detection,
    // which the walk reads separately (see the sniff-everything option below).
    case ArchiveMode::kAll:
    case ArchiveMode::kAny: return ArchiveDive::kAll;
    case ArchiveMode::kNone: return ArchiveDive::kNone;
    case ArchiveMode::kRoots: return ArchiveDive::kRoots;
  }
  return ArchiveDive::kNone;
}

// --archive-separator=STRING / --archive-prefix=[URI|STRING]: how a mounted container spells its
// member paths. Any string is accepted for either (see globals.cc), so there is nothing to validate;
// last occurrence wins, and an absent flag keeps the library default. The returned views point into
// `globals`, which must outlive the result.
archive::MemberPathOptions ReadMemberPathOptions(const std::vector<std::string>& globals) {
  archive::MemberPathOptions options;
  constexpr std::string_view kSeparator = "--archive-separator=";
  constexpr std::string_view kPrefix = "--archive-prefix=";
  for (const std::string& global : globals) {
    if (global.starts_with(kSeparator)) {
      options.separator = std::string_view(global).substr(kSeparator.size());
    } else if (global.starts_with(kPrefix)) {
      options.prefix = std::string_view(global).substr(kPrefix.size());
    }
  }
  return options;
}

// --pack=FILE: where the walk's matches are written instead of listed. Last occurrence wins, like
// every other valued global; absent means no packing at all.
std::optional<std::string> ReadPackTarget(const std::vector<std::string>& globals) {
  constexpr std::string_view kPrefix = "--pack=";
  // Last occurrence wins, so scan from the back and stop at the first hit: a forward loop that
  // overwrites its way to the same answer says the rule backwards and keeps looking after it knows.
  for (const std::string& global : std::ranges::reverse_view(globals)) {
    if (global.starts_with(kPrefix)) {
      return global.substr(kPrefix.size());
    }
  }
  return std::nullopt;
}

using Json = ::nlohmann::ordered_json;

absl::StatusOr<std::vector<archive::PackOption>> ReadPackOptionFile(std::string_view path) {
  if (path.empty()) {
    return absl::InvalidArgumentError("expected a file after --pack-option=@");
  }
  const vfs::LocalFs fs;
  const absl::StatusOr<std::string> content = fs.ReadContent(path);
  if (!content.ok()) {
    return absl::NotFoundError(absl::StrCat("cannot read pack-option file '", path, "'"));
  }
  const Json root = Json::parse(*content, nullptr, /*allow_exceptions=*/false);
  if (root.is_discarded()) {
    return absl::InvalidArgumentError(absl::StrCat("pack-option file '", path, "' is not valid JSON"));
  }
  if (!root.is_object()) {
    return absl::InvalidArgumentError(absl::StrCat("pack-option file '", path, "' must contain one JSON object"));
  }
  std::vector<archive::PackOption> options;
  options.reserve(root.size());
  for (const auto& [name, value] : root.items()) {
    if (value.is_string()) {
      options.push_back({.name = name, .value = value.get<std::string>()});
    } else if (value.is_boolean()) {
      options.push_back({.name = name, .value = value.get<bool>() ? "yes" : "no"});
    } else if (value.is_number_integer() || value.is_number_unsigned()) {
      options.push_back({.name = name, .value = value.dump()});
    } else {
      return absl::InvalidArgumentError(
          absl::StrCat("pack-option file '", path, "' value for '", name, "' must be a string, integer, or boolean"));
    }
  }
  return options;
}

// --pack-option=NAME=VALUE or @FILE.json (repeatable) and its one-knob spelling --pack-level=N, collected in the
// order given so the writer's "last value for a NAME wins" rule falls out of the order alone. Only
// the SHAPE is checked here; what the names mean belongs to the backend, which owns the vocabulary.
absl::StatusOr<std::vector<archive::PackOption>> ReadPackOptions(const std::vector<std::string>& globals) {
  constexpr std::string_view kOption = "--pack-option=";
  constexpr std::string_view kLevel = "--pack-level=";
  std::vector<archive::PackOption> options;
  for (const std::string& global : globals) {
    if (global.starts_with(kLevel)) {
      options.push_back({.name = "level", .value = global.substr(kLevel.size())});
      continue;
    }
    if (!global.starts_with(kOption)) {
      continue;
    }
    const std::string_view spec = std::string_view(global).substr(kOption.size());
    if (spec.starts_with('@')) {
      MBO_ASSIGN_OR_RETURN(std::vector<archive::PackOption> file_options, ReadPackOptionFile(spec.substr(1)));
      for (archive::PackOption& option : file_options) {
        options.push_back(std::move(option));
      }
      continue;
    }
    const std::string_view::size_type equals = spec.find('=');
    if (equals == std::string_view::npos || equals == 0) {
      return absl::InvalidArgumentError(
          absl::StrCat("expected NAME=VALUE, got '", spec, "'; see --help=archive for the names"));
    }
    options.push_back({.name = std::string(spec.substr(0, equals)), .value = std::string(spec.substr(equals + 1))});
  }
  return options;
}

// Whether every option NAME is one the linked writer knows. Done before the walk because the writer
// only sees them once there is something to write, and a typo should not cost a traversal. What an
// option MEANS (which formats take it, what values are legal) stays the writer's call.
absl::Status CheckPackOptionNames(const std::vector<archive::PackOption>& options) {
  const std::vector<archive::PackOptionInfo> vocabulary = archive::ContainerPackVocabulary();
  for (const archive::PackOption& option : options) {
    const bool known = absl::c_any_of(vocabulary, [&option](const archive::PackOptionInfo& known_option) {
      return known_option.name == option.name;
    });
    if (known) {
      continue;
    }
    // Joined with a projection rather than copied into a vector first: the names are only needed to
    // build this one message.
    const std::string known_names = absl::StrJoin(
        vocabulary, ", ",
        [](std::string* out, const archive::PackOptionInfo& known_option) { absl::StrAppend(out, known_option.name); });
    return absl::InvalidArgumentError(
        absl::StrCat("unknown pack option '", option.name, "'; known options are ", known_names));
  }
  return absl::OkStatus();
}

// The name a packed entry gets INSIDE the archive: its path relative to the search root it was found
// under, so `xff src -name '*.cc' --pack=x.tar` stores `sub/a.cc` and not `src/sub/a.cc`. An entry
// that IS its root (a file named on the command line) keeps its basename, since a full path stored as
// a member name would unpack into an absolute or dot-prefixed place nobody asked for.
std::string PackMemberName(std::string_view path, std::string_view root) {
  if (path.size() > root.size() + 1 && path.starts_with(root) && path[root.size()] == '/') {
    return std::string(path.substr(root.size() + 1));
  }
  const std::string_view::size_type slash = path.rfind('/');
  return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

// A path in a form two spellings of the same file compare equal in, used only to keep the archive
// being written out of itself. Weakly canonical because the output does not exist yet; on failure the
// path is returned unchanged, which still catches the ordinary `--pack=out.tar` case.
std::string PackIdentity(std::string_view path) {
  std::error_code error;
  const std::filesystem::path canonical = std::filesystem::weakly_canonical(std::filesystem::path(path), error);
  return error ? std::string(path) : canonical.string();
}

std::string_view ArchiveModeName(ArchiveMode mode) {
  switch (mode) {
    case ArchiveMode::kAll: return "all";
    case ArchiveMode::kAny: return "any";
    case ArchiveMode::kRoots: return "roots";
    case ArchiveMode::kNone: return "none";
  }
  return "none";
}

// What a reduction (--summary / --histogram) counts when the walk dives into containers. Diving makes
// one byte visible twice - once as the container's own size, once as its members' - so a total that
// adds both describes no filesystem that exists.
enum class ArchiveAggregate {
  kBoth,       // count the container AND its members: the archive plus what unpacking it would give
  kContainer,  // count containers, never their members: sizes as they sit on disk
  kMembers,    // the default: count the members of a dived container instead of the container
};

// --archive-aggregate=both|container|members (last occurrence wins), the reduction's view of a dived
// container. Only reductions are affected: what gets PRINTED is the walk's business, so a member is
// still listed under `container` and the container still listed under `members`.
absl::StatusOr<ArchiveAggregate> ResolveArchiveAggregate(const std::vector<std::string>& globals) {
  using ModePair = std::pair<std::string_view, ArchiveAggregate>;
  static constexpr auto kModes = mbo::container::MakeLimitedMap(
      ModePair{"both", ArchiveAggregate::kBoth}, ModePair{"container", ArchiveAggregate::kContainer},
      ModePair{"members", ArchiveAggregate::kMembers});
  ArchiveAggregate result = ArchiveAggregate::kMembers;
  constexpr std::string_view kFlag = "--archive-aggregate=";
  for (const std::string& global : globals) {
    if (!global.starts_with(kFlag)) {
      continue;
    }
    const std::string_view value = std::string_view(global).substr(kFlag.size());
    const auto it = kModes.find(value);
    if (it == kModes.end()) {
      return absl::InvalidArgumentError(
          absl::StrCat("bad --archive-aggregate value '", value, "': expected both, container, or members"));
    }
    result = it->second;
  }
  return result;
}

// The whole `--archive` family: how far/deep to dive and how a mounted container renders member paths.
//
// Diving needs the archive extra, which a lean build does not link, so an EXPLICIT request there is
// an error rather than a silent no-op - "xff cannot see into this archive" is exactly the wrong
// impression to leave. A STYLE default (`roots` for the xff family) must never break an ordinary
// run, so it degrades quietly to walking archives as the plain files they are.
//
// The returned member-path views point into `globals`, which outlives the walk.
struct ResolvedArchiveOptions {
  ArchiveDive archive_dive = ArchiveDive::kNone;
  int archive_depth = 1;
  archive::MemberPathOptions member_paths;
};

absl::StatusOr<ResolvedArchiveOptions> ResolveArchiveOptions(
    const std::vector<std::string>& globals,
    std::optional<registry::Style> style) {
  ResolvedArchiveOptions result;
  const ArchiveMode archive_mode = ResolveArchiveMode(globals, style);
  if (archive_mode != ArchiveMode::kNone && !archive::ContainerSupportAvailable()) {
    if (HasArchiveFlag(globals)) {
      return absl::UnimplementedError(
          absl::StrCat(
              "archive diving (requested mode '", ArchiveModeName(archive_mode),
              "') is not built into this binary; use --archive=none / -z- to walk archives as plain files"));
    }
  } else {
    result.archive_dive = ArchiveDiveOf(archive_mode);
  }
  // --archive-depth=N: how many containers deep diving goes (see WalkOptions::archive_depth). A bad
  // or zero value is a usage error rather than a silent clamp - "0" most likely means "off", which
  // --archive=none spells, and guessing which was meant would be worse than saying so.
  for (const std::string& global : globals) {
    constexpr std::string_view kArchiveDepth = "--archive-depth=";
    if (!global.starts_with(kArchiveDepth)) {
      continue;
    }
    const std::string_view value = std::string_view(global).substr(kArchiveDepth.size());
    int parsed = 0;
    if (!absl::SimpleAtoi(value, &parsed) || parsed < 1) {
      return absl::InvalidArgumentError(
          absl::StrCat("bad --archive-depth value '", value, "': expected a whole number of 1 or more"));
    }
    result.archive_depth = parsed;
  }
  result.member_paths = ReadMemberPathOptions(globals);
  return result;
}

// The walk's whole view of archives: hand it a container path and the filesystem that container was
// found on, and get back a filesystem over its members (or the InvalidArgument that means "an
// ordinary file after all"). Returned as a value the caller keeps alive across the walk; the walk
// only calls it when `options.archive` allows diving.
// The final path component, for the name gate. A member path's separator does not matter here: the
// gate only applies mid-walk on the real filesystem, where components are slash-separated.
std::string_view BasenameOf(std::string_view path) {
  const std::string_view::size_type slash = path.rfind('/');
  return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

auto MakeContainerMounter(
    const vfs::FileSystem& walk_fs,
    const archive::MemberPathOptions& member_path_options,
    bool sniff_any) {
  return [&member_path_options, &walk_fs, sniff_any](
             std::string_view container, const vfs::FileSystem& source,
             int depth) -> absl::StatusOr<std::unique_ptr<const vfs::FileSystem>> {
    // `--archive=all` offers every regular file in the tree, and asking the reader means opening one
    // and letting libarchive bid every format on it. So a file the walk MET (depth > 0) has to look
    // like a container by NAME first; a file the user NAMED (depth 0) is always opened, because
    // pointing xff at it is the request. `--archive-any` drops the gate, which is how an archive whose
    // name says nothing is still found.
    if (depth > 0 && !sniff_any && !archive::LooksLikeContainerName(BasenameOf(container))) {
      return absl::InvalidArgumentError(absl::StrCat("not a container by name: ", container));
    }
    if (&source != &walk_fs) {
      // A container inside a container: it has no path of its own, so its bytes come out of its
      // parent first and the mounted filesystem keeps them. How deep this goes is --archive-depth.
      MBO_ASSIGN_OR_RETURN(const std::string bytes, source.ReadContent(container));
      MBO_ASSIGN_OR_RETURN(
          std::unique_ptr<vfs::FileSystem> nested, archive::OpenContainerBytes(container, bytes, member_path_options));
      return nested;
    }
    MBO_ASSIGN_OR_RETURN(
        std::unique_ptr<vfs::FileSystem> mounted, archive::OpenContainer(container, member_path_options));
    return mounted;
  };
}

// --hash-algorithm=ALGO / --hash-encoding=hex|base64: the defaults a bare -hash action and a
// bare {hash} field use. Last occurrence wins; an empty value means "unset" and the reader
// downstream falls back to sha256 / hex. Only READS them - the caller validates, so each bad
// value can name its own flag in the usage error.
struct HashDefaults {
  std::string algorithm;
  std::string encoding;
};

HashDefaults ReadHashDefaults(const std::vector<std::string>& globals) {
  constexpr std::string_view kHashAlgo = "--hash-algorithm=";
  constexpr std::string_view kHashEncoding = "--hash-encoding=";
  HashDefaults defaults;
  for (const std::string& global : globals) {
    if (global.starts_with(kHashAlgo)) {
      defaults.algorithm = global.substr(kHashAlgo.size());
    } else if (global.starts_with(kHashEncoding)) {
      defaults.encoding = global.substr(kHashEncoding.size());
    }
  }
  return defaults;
}

// --hidden / --no-hidden: whether to skip hidden dotfiles (a path component starting with
// '.'). Default is style-scoped: find and the conservative xff style show them
// (find-compatible), the opinionated style (rg) skips them (fd-like, less dotclutter).
// --hidden forces show, --no-hidden forces skip; last occurrence wins. An explicitly named
// search root is always entered regardless (handled at the walk by depth).
bool ResolveSkipHidden(const std::vector<std::string>& globals, std::optional<registry::Style> style) {
  bool skip = style == registry::Style::kRg;
  for (const std::string& global : globals) {
    if (global == "--hidden") {
      skip = false;
    } else if (global == "--no-hidden") {
      skip = true;
    }
  }
  return skip;
}

// The version-control systems whose metadata --skip-vcs can prune: a token (the --skip-vcs= value)
// mapped to the directory / gitlink-file name to drop. Order matches the --help / display order.
constexpr std::array<std::pair<std::string_view, std::string_view>, 7> kVcsMetadata = {{
    {"git", ".git"},
    {"hg", ".hg"},
    {"svn", ".svn"},
    {"jj", ".jj"},
    {"bzr", ".bzr"},
    {"darcs", "_darcs"},
    {"cvs", "CVS"},
}};

// Resolves the VCS metadata NAMES (`.git`, `.hg`, ...) --skip-vcs should prune from the walk.
// Explicit --skip-vcs / --no-skip-vcs win, last occurrence: bare or `=all` selects every VCS, `=none`
// (or --no-skip-vcs) selects none, a comma list selects exactly those tokens (a frozen subset). With
// no such flag, gitignore mode (`-g`) implies just `.git` (the shipped default), else nothing. An
// unknown token is an InvalidArgument usage error, refused before the walk.
// NOLINTNEXTLINE(readability-function-cognitive-complexity): cohesive dispatch
absl::StatusOr<absl::flat_hash_set<std::string>> ResolveSkipVcs(
    const std::vector<std::string>& globals,
    bool gitignore_on) {
  constexpr std::string_view kPrefix = "--skip-vcs=";
  const auto dir_for = [](std::string_view token) -> std::optional<std::string_view> {
    for (const auto& [tok, dir] : kVcsMetadata) {
      if (tok == token) {
        return dir;
      }
    }
    return std::nullopt;
  };
  const auto add_all = [](absl::flat_hash_set<std::string>& names) {
    for (const auto& [tok, dir] : kVcsMetadata) {
      names.emplace(dir);
    }
  };
  bool saw_flag = false;
  absl::flat_hash_set<std::string> names;
  for (const std::string& global : globals) {
    if (global == "--no-skip-vcs") {
      saw_flag = true;
      names.clear();
    } else if (global == "--skip-vcs") {
      saw_flag = true;
      names.clear();
      add_all(names);
    } else if (global.starts_with(kPrefix)) {
      saw_flag = true;
      names.clear();
      const std::string_view value = std::string_view(global).substr(kPrefix.size());
      if (value == "all") {
        add_all(names);
      } else if (value != "none") {
        for (const std::string_view token : absl::StrSplit(value, ',', absl::SkipEmpty())) {
          const std::optional<std::string_view> dir = dir_for(token);
          if (!dir.has_value()) {
            return absl::InvalidArgumentError(
                absl::StrCat(
                    "unknown --skip-vcs value '", token,
                    "'; expected a comma list of git,hg,svn,jj,bzr,darcs,cvs (or all/none)"));
          }
          names.emplace(*dir);
        }
      }
    }
  }
  if (!saw_flag && gitignore_on) {
    names.emplace(".git");  // -g implies --skip-vcs=git (the shipped default)
  }
  return names;
}

// -g auto: whether any search root is inside a git working tree, so .gitignore applies.
bool AnyRootInRepo(const vfs::FileSystem& fs, absl::Span<const std::string> roots) {
  return absl::c_any_of(
      roots, [&fs](std::string_view root) { return repo::FindRepoRoot(fs, AbsoluteDir(root)).has_value(); });
}

// The per-directory ignore filenames in effect, lowest precedence first (a directory's
// files accumulate into one list, so a later name wins): .gitignore (-g) < .ignore <
// .xffignore (--ignore-files). Empty when ignore-file processing is off -- it is
// find-compatibly off by default, and --no-ignore / -u is the master switch that
// force-disables every source.
std::vector<std::string> ResolveIgnoreFileNames(
    const std::vector<std::string>& globals,
    bool gitignore_on,
    std::optional<registry::Style> style) {
  if (HasGlobal(globals, "--no-ignore") || HasGlobal(globals, "-u")) {
    return {};
  }
  std::vector<std::string> names;
  if (gitignore_on) {
    names.emplace_back(".gitignore");
  }
  // The opinionated style (rg) also honor .ignore / .xffignore by default (like
  // ripgrep / fd); other styles need --ignore-files. -u / --no-ignore above still
  // force-disables all.
  const bool opinionated = style == registry::Style::kRg;
  if (HasGlobal(globals, "--ignore-files") || opinionated) {
    names.emplace_back(".ignore");
    names.emplace_back(".xffignore");
  }
  return names;
}

// --implicit-print=yes|no forces the default (implicit) print on or off,
// overriding find's "an action suppresses it" rule. Last occurrence wins;
// nullopt means no override (use the find default). Bare --implicit-print == =yes.
std::optional<bool> ResolveImplicitPrint(const std::vector<std::string>& globals) {
  constexpr std::string_view kPrefix = "--implicit-print=";
  std::optional<bool> result;
  for (const std::string& global : globals) {
    if (global == "--implicit-print") {
      result = true;  // a bare --implicit-print forces the default print on
    } else if (global.starts_with(kPrefix)) {
      // The shared bool vocabulary (yes / no / 1 / true / 0 / false); an
      // unrecognized value is ignored, leaving the prior resolution.
      if (const std::optional<bool> parsed = values::ParseBool(std::string_view(global).substr(kPrefix.size()));
          parsed.has_value()) {
        result = parsed;
      }
    }
  }
  return result;
}

// Collects --define=NAME=VALUE globals into a name->value map (last wins). The
// text after the prefix is NAME=VALUE; NAME runs to the first '=', VALUE (which
// may itself contain '=') is the rest. --define=NAME with no '=' binds empty.
std::map<std::string, std::string> ResolveDefines(const std::vector<std::string>& globals) {
  constexpr std::string_view kPrefix = "--define=";
  std::map<std::string, std::string> defines;
  for (const std::string& global : globals) {
    if (!global.starts_with(kPrefix)) {
      continue;
    }
    const std::string spec = global.substr(kPrefix.size());
    const std::string::size_type eq = spec.find('=');
    if (eq == std::string::npos) {
      defines[spec] = "";
    } else {
      defines[spec.substr(0, eq)] = spec.substr(eq + 1);
    }
  }
  return defines;
}

// Appends the NAME of every -capture action in the expression (its args[0]).
void AppendCaptureNames(const parser::Expr& expr, std::vector<std::string>& names) {
  switch (expr.kind) {
    case parser::Expr::Kind::kPredicate:
      // A node with `!` (label_override) is ALLOWED to re-bind, so it is not reported as a duplicate.
      if (expr.descriptor->name == "-capture" && !expr.args.empty() && !expr.label_override) {
        names.push_back(expr.args.front());
      }
      break;
    case parser::Expr::Kind::kNot: AppendCaptureNames(*expr.lhs, names); break;
    case parser::Expr::Kind::kAnd:
    case parser::Expr::Kind::kOr:
    case parser::Expr::Kind::kNand:
    case parser::Expr::Kind::kNor:
    case parser::Expr::Kind::kXor:
    case parser::Expr::Kind::kXnor:
    case parser::Expr::Kind::kComma:
      AppendCaptureNames(*expr.lhs, names);
      AppendCaptureNames(*expr.rhs, names);
      break;
  }
}

std::vector<std::string> CollectCaptureNames(const parser::Expr& expr) {
  std::vector<std::string> names;
  AppendCaptureNames(expr, names);
  return names;
}

// Returns a -capture NAME bound more than once, or nullopt when all are unique.
std::optional<std::string> DuplicateCaptureName(const parser::Expr& expr) {
  std::vector<std::string> names = CollectCaptureNames(expr);
  absl::c_sort(names);
  const auto dup = absl::c_adjacent_find(names);
  return dup == names.end() ? std::nullopt : std::optional<std::string>(*dup);
}

// Returns the -collect NAME a node REUSES without saying so, or nullopt when every reuse is marked.
// A site carrying `!` (label_override) has declared the reuse deliberate, so it never reports; an
// unmarked site whose name an earlier site already used does.
std::optional<std::string_view> DuplicateCollectionName(const parser::Expr& expr) {
  absl::flat_hash_set<std::string_view> seen;
  for (const CollectSite& site : CollectSites(expr)) {
    if (site.override_name) {
      continue;  // `!` says the reuse is deliberate, so it neither reports nor blocks a later one
    }
    if (!seen.insert(site.name).second) {
      return site.name;
    }
  }
  return std::nullopt;
}

// Reports a -capture NAME bound twice, returning true when it did (so the caller stops before
// traversing). -capture BINDS a name to a value, so a second binding silently clobbers the first and
// every later {capture.NAME} renders wrong data; -collect APPENDS, so a shared name silently doubles
// what the summary reduces. One mechanism for both: the `!` modifier on the node that reuses the name
// (`-capture:!NAME`, `-collect:!NAME`). It is per INSTANCE, which the whole-run --capture-override it
// replaced could not be - that flag loosened every -capture in the command, including the ones the
// author never thought about.
bool ReportDuplicateBindingName(const parser::Expr& expr, WalkErrorFn on_error) {
  if (const std::optional<std::string> dup = DuplicateCaptureName(expr); dup.has_value()) {
    on_error(
        "-capture",
        absl::FailedPreconditionError(
            absl::StrCat(
                "duplicate -capture name '", *dup, "'; write '-capture:!", *dup, "' if the re-bind is meant")));
    return true;
  }
  if (const std::optional<std::string_view> dup = DuplicateCollectionName(expr); dup.has_value()) {
    on_error(
        "-collect", absl::FailedPreconditionError(
                        absl::StrCat(
                            "duplicate -collect name '", *dup, "'; write '-collect:!", *dup,
                            "' on the later one if sharing the collection is meant")));
    return true;
  }
  return false;
}

// Appends strings that may reference {capture.NAME}: the command tokens of every
// -exec and -capture action (a later command can use an earlier capture). The
// --template global is added by the caller.
void AppendCaptureRefs(const parser::Expr& expr, std::vector<std::string>& refs) {
  switch (expr.kind) {
    case parser::Expr::Kind::kPredicate:
      if (expr.descriptor->name == "-exec") {
        refs.insert(refs.end(), expr.args.begin(), expr.args.end());
      } else if (expr.descriptor->name == "-capture" && expr.args.size() > 2) {
        refs.insert(refs.end(), expr.args.begin() + 2, expr.args.end());  // skip [NAME, REGEX]
      }
      break;
    case parser::Expr::Kind::kNot: AppendCaptureRefs(*expr.lhs, refs); break;
    case parser::Expr::Kind::kAnd:
    case parser::Expr::Kind::kOr:
    case parser::Expr::Kind::kNand:
    case parser::Expr::Kind::kNor:
    case parser::Expr::Kind::kXor:
    case parser::Expr::Kind::kXnor:
    case parser::Expr::Kind::kComma:
      AppendCaptureRefs(*expr.lhs, refs);
      AppendCaptureRefs(*expr.rhs, refs);
      break;
  }
}

std::vector<std::string> CollectCaptureRefs(const parser::Expr& expr) {
  std::vector<std::string> refs;
  AppendCaptureRefs(expr, refs);
  return refs;
}

// The first argument anywhere in `expr` that compiles to a field template carrying an UNREDUCED m//
// extraction (a value stream), or nullopt. An unreduced extraction is only meaningful as a --summary
// key; in any per-entry scalar render context (-exec/-printf/-grep/... command and format args) a
// value stream has no single value, so it is a usage error. A reducer-terminated extraction
// (`;join(...)`) is scalar-valued and allowed, so it does NOT trip this. Checking EVERY arg is safe:
// HasUnreducedExtraction is true only for a known field with a well-formed unreduced m// qualifier,
// which a user writes solely to extract -- a -name glob / -regex / -size value never trips it.
std::optional<std::string> FindScalarExtraction(const parser::Expr& expr) {
  switch (expr.kind) {
    case parser::Expr::Kind::kPredicate:
      for (const std::string& arg : expr.args) {
        if (fields::Template::Compile(arg).HasUnreducedExtraction()) {
          return arg;
        }
      }
      return std::nullopt;
    case parser::Expr::Kind::kNot: return FindScalarExtraction(*expr.lhs);
    case parser::Expr::Kind::kAnd:
    case parser::Expr::Kind::kOr:
    case parser::Expr::Kind::kNand:
    case parser::Expr::Kind::kNor:
    case parser::Expr::Kind::kXor:
    case parser::Expr::Kind::kXnor:
    case parser::Expr::Kind::kComma:
      if (const std::optional<std::string> lhs = FindScalarExtraction(*expr.lhs); lhs.has_value()) {
        return lhs;
      }
      return FindScalarExtraction(*expr.rhs);
  }
  return std::nullopt;
}

// Returns a -capture NAME whose {capture.NAME} placeholder appears nowhere (no
// -exec/-capture command, not the --template, and not a --summary={...} key), or nullopt when all
// are used.
std::optional<std::string> UnusedCaptureName(
    const parser::Expr& expr,
    const std::optional<std::string>& tmpl,
    std::string_view summary_key) {
  const std::vector<std::string> names = CollectCaptureNames(expr);
  if (names.empty()) {
    return std::nullopt;
  }
  std::vector<std::string> refs = CollectCaptureRefs(expr);
  if (tmpl.has_value()) {
    refs.push_back(*tmpl);
  }
  if (!summary_key.empty()) {
    refs.emplace_back(summary_key);  // --summary={...} references captures too
  }
  for (const std::string& name : names) {
    const std::string closed = absl::StrCat("{capture.", name, "}");
    const std::string qualified = absl::StrCat("{capture.", name, ":");
    const bool used = std::any_of(refs.begin(), refs.end(), [&](const std::string& ref) {
      return ref.contains(closed) || ref.contains(qualified);
    });
    if (!used) {
      return name;
    }
  }
  return std::nullopt;
}

}  // namespace

// Internal helpers for RunFind's global-flag resolution + JSON key quoting (TU-local).
namespace {

// --human[=iec|si|off] (and the --si alias): how sizes render in -ls and --summary. si = decimal
// (kB/MB, 1000^N) - the default, since it reads most human; iec = binary (KiB/MiB, 1024^N); off =
// raw bytes. Numeric synonyms: 1000 = si, 1024 = iec. Bare --human and --si both select si. The
// default when unset is style-scoped: the modern styles (xff, rg) show human (si), the find style
// shows raw bytes (find -ls compatibility). Last occurrence wins; nullopt means raw bytes.
std::optional<format::SizeUnits> ResolveHuman(
    const std::vector<std::string>& globals,
    std::optional<registry::Style> style) {
  std::optional<format::SizeUnits> units;
  if (style.has_value() && *style != registry::Style::kFind) {
    units = format::SizeUnits::kSi;  // the modern styles (xff, rg) default to human sizes (SI)
  }
  for (const std::string_view global : globals) {
    if (global == "--human" || global == "--human=si" || global == "--human=1000" || global == "--si") {
      units = format::SizeUnits::kSi;  // bare --human defaults to SI (KB/MB); --si is its alias
    } else if (global == "--human=iec" || global == "--human=1024") {
      units = format::SizeUnits::kIec;
    } else if (global == "--human=off") {
      units = std::nullopt;  // force raw bytes, even in the modern styles
    }
  }
  return units;
}

// The resolved --buffer cap for a column buffer: a row `window` and/or a `byte_budget` (0 =
// no byte cap). A byte-budget value clears the row cap to kAll so only the bytes bound.
struct BufferBound {
  std::size_t window;
  std::size_t byte_budget;
};

// --buffer=auto|off|all|N[k/M/G]|N<byte-unit>: how much to buffer to align columns (-ls, and
// the buffered --format=aligned/markdown tables). auto (=100) buffers the first 100 rows to
// compute widths then streams the rest at them; off / 0 disables buffering; all buffers the
// whole run; a row count N (optional decimal SI multiplier k/M/G/T) buffers N rows; a byte
// budget (N with a byte unit, e.g. 10MB / 10MiB) buffers until that many cell bytes, then
// streams. Last occurrence wins; an unrecognized value is ignored. `default_window` applies
// when no --buffer flag is present (-ls passes 100 = auto; the tables pass kAll = full align).
BufferBound ResolveBufferBound(const std::vector<std::string>& globals, std::size_t default_window) {
  BufferBound bound{.window = default_window, .byte_budget = 0};
  for (const std::string& global : globals) {
    if (global == "--buffer") {
      bound = {.window = 100, .byte_budget = 0};  // bare --buffer == --buffer=auto
    } else if (global.starts_with("--buffer=")) {
      const std::string_view value = std::string_view(global).substr(9);
      if (const std::optional<std::size_t> rows = format::ParseBufferWindow(value)) {
        bound = {.window = *rows, .byte_budget = 0};
      } else if (const std::optional<std::size_t> bytes = format::ParseByteBudget(value)) {
        bound = {.window = format::ColumnBuffer::kAll, .byte_budget = *bytes};  // bytes-only cap
      }
      // else: unrecognized value -> keep the previous bound
    }
  }
  return bound;
}

// --top=N: with --summary, keep only the N largest groups by size. Last occurrence
// wins; nullopt (absent, or a non-positive / malformed N) means no limit -- all
// groups in the default alphabetical order.
std::optional<std::size_t> ResolveTop(const std::vector<std::string>& globals) {
  constexpr std::string_view kPrefix = "--top=";
  std::optional<std::size_t> top;
  for (const std::string& global : globals) {
    if (!global.starts_with(kPrefix)) {
      continue;
    }
    if (std::size_t value = 0; absl::SimpleAtoi(std::string_view(global).substr(kPrefix.size()), &value) && value > 0) {
      top = value;
    }
  }
  return top;
}

// --histogram-width=N: the cell width the tallest histogram bar fills (default 40). A non-positive
// or malformed value is ignored (keeps the default); the last valid one wins.
std::size_t ResolveHistogramWidth(const std::vector<std::string>& globals) {
  constexpr std::string_view kPrefix = "--histogram-width=";
  std::size_t width = 40;
  for (const std::string& global : globals) {
    if (!global.starts_with(kPrefix)) {
      continue;
    }
    if (std::size_t value = 0; absl::SimpleAtoi(std::string_view(global).substr(kPrefix.size()), &value) && value > 0) {
      width = value;
    }
  }
  return width;
}

// --summary-precision=N: fraction digits for the --summary human size column (default 2,
// e.g. "12.34 MiB"). A malformed value keeps the default; the count is capped at 9 so the
// column stays readable. Bytes stay integer regardless (12 B), with the fraction columns
// blanked so points line up (see format::SizeColumns).
unsigned ResolveSummaryPrecision(const std::vector<std::string>& globals) {
  constexpr std::string_view kPrefix = "--summary-precision=";
  unsigned precision = 2;
  for (const std::string& global : globals) {
    if (!global.starts_with(kPrefix)) {
      continue;
    }
    if (std::uint32_t value = 0;
        absl::SimpleAtoi(std::string_view(global).substr(kPrefix.size()), &value) && value <= 9) {
      precision = value;
    }
  }
  return precision;
}

// A JSON string literal for `text` (quotes included): escapes the JSON-significant
// characters and any control byte as \uXXXX. Used for the --summary=jsonl group key
// (type/extension names, which are normally plain but may carry odd bytes).
// --shards[=auto|SCHEME,...]: enable sharded-file collapsing and (optionally) restrict the schemes.
// Off unless a --shards appears; bare --shards / =auto recognizes every scheme (empty `schemes`);
// a comma list selects a subset. Last occurrence wins. An unknown scheme name is a usage error.
struct ShardsConfig {
  bool enabled = false;
  std::vector<shard::Scheme> schemes;  // empty = all schemes (auto)
};

absl::StatusOr<ShardsConfig> ResolveShards(const std::vector<std::string>& globals) {
  ShardsConfig cfg;
  for (const std::string& global : globals) {
    constexpr std::string_view kPrefix = "--shards";
    if (global != kPrefix && !global.starts_with("--shards=")) {
      continue;
    }
    cfg.enabled = true;
    cfg.schemes.clear();  // last --shards wins, and re-selects the scheme set
    if (global == kPrefix) {
      continue;  // bare --shards == auto (all schemes)
    }
    const std::string_view value = std::string_view(global).substr(std::string_view("--shards=").size());
    if (value == "auto") {
      continue;
    }
    for (const std::string_view name : absl::StrSplit(value, ',')) {
      if (name == "of") {
        cfg.schemes.push_back(shard::Scheme::kOf);
      } else if (name == "dotnum") {
        cfg.schemes.push_back(shard::Scheme::kDotNum);
      } else if (name == "underscore") {
        cfg.schemes.push_back(shard::Scheme::kUnderscore);
      } else {
        return absl::InvalidArgumentError(
            absl::StrCat("unknown --shards scheme '", name, "' (want auto, of, dotnum, or underscore)"));
      }
    }
  }
  return cfg;
}

// --shards-show=first|wildcard|count: how a collapsed shard set's one line reads. `first` (default)
// shows the representative (lowest-index) shard's path; `wildcard` shows the masked-index name
// (`f-???-of-003`); `count` shows the wildcard plus the shard count. Last occurrence wins; an
// unknown value is a usage error.
enum class ShardShow : std::uint8_t { kFirst, kWildcard, kCount };

absl::StatusOr<ShardShow> ResolveShardShow(const std::vector<std::string>& globals) {
  ShardShow show = ShardShow::kFirst;
  for (const std::string& global : globals) {
    constexpr std::string_view kPrefix = "--shards-show=";
    if (!global.starts_with(kPrefix)) {
      continue;
    }
    const std::string_view value = std::string_view(global).substr(kPrefix.size());
    if (value == "first") {
      show = ShardShow::kFirst;
    } else if (value == "wildcard") {
      show = ShardShow::kWildcard;
    } else if (value == "count") {
      show = ShardShow::kCount;
    } else {
      return absl::InvalidArgumentError(
          absl::StrCat("unknown --shards-show value '", value, "' (want first, wildcard, or count)"));
    }
  }
  return show;
}

// --shards-dedup=first|mtime|error: how same-index duplicates (redundant regenerations differing
// only by tail) are resolved. first (default) keeps the lexicographically-first name; mtime keeps
// the newest; error fails the run when any set has duplicates. Last occurrence wins; an unknown
// value is a usage error.
absl::StatusOr<shard::Dedup> ResolveShardDedup(const std::vector<std::string>& globals) {
  shard::Dedup dedup = shard::Dedup::kFirst;
  for (const std::string& global : globals) {
    constexpr std::string_view kPrefix = "--shards-dedup=";
    if (!global.starts_with(kPrefix)) {
      continue;
    }
    const std::string_view value = std::string_view(global).substr(kPrefix.size());
    if (value == "first") {
      dedup = shard::Dedup::kFirst;
    } else if (value == "mtime") {
      dedup = shard::Dedup::kMtime;
    } else if (value == "error") {
      dedup = shard::Dedup::kError;
    } else {
      return absl::InvalidArgumentError(
          absl::StrCat("unknown --shards-dedup value '", value, "' (want first, mtime, or error)"));
    }
  }
  return dedup;
}

// One output line for a collapsed shard set: the `prefix` (its directory) + the chosen display body,
// then a completeness / count annotation. An incomplete set always shows `(present/expected -
// INCOMPLETE)`; a complete set adds `(N shards)` only under `count`.
std::string_view ShardRepresentativePath(const shard::ShardSet& set) {
  return !set.members.empty() ? std::string_view(set.members.front().path)
                              : std::string_view(set.superfluous.front().path);
}

std::string RenderShardSet(const shard::ShardSet& set, std::string_view prefix, ShardShow show) {
  const std::string_view display =
      show == ShardShow::kFirst ? ShardRepresentativePath(set) : std::string_view(set.wildcard);
  std::string line = absl::StrCat(prefix, display);
  const std::size_t present = set.members.size();
  if (!set.complete) {
    absl::StrAppend(&line, " (", present, "/", present + set.missing.size(), " - INCOMPLETE)");
  } else if (show == ShardShow::kCount) {
    absl::StrAppend(&line, " (", present, " shards)");
  }
  return line;
}

std::string JsonQuote(std::string_view text) {
  std::string out = "\"";
  for (const char ch : text) {
    switch (ch) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          absl::StrAppend(&out, "\\u", absl::Hex(static_cast<unsigned char>(ch), absl::kZeroPad4));
        } else {
          out.push_back(ch);
        }
    }
  }
  out.push_back('"');
  return out;
}

// Rewrites each container without the members `-delete` matched (--archive-delete), returning the
// number of containers that could not be done - the driver's error count, so a failed rewrite is an
// exit code rather than a silent no-op.
//
// Grouped by container and applied once per container: a rewrite reads the whole archive and writes a
// new one, so doing it per member would rewrite the same file N times, each rewrite another window in
// which an interrupt leaves work half done.
//
// A member of a NESTED container is refused: its container is itself bytes inside another one, so
// rewriting it would mean rewriting the outer container around the new inner bytes - a different
// operation, and not one to do implicitly.
int FlushArchiveDeletions(
    const std::vector<std::string>& members,
    const archive::MemberPathOptions& member_path_options,
    bool dry_run,
    EmitFn emit,
    WalkErrorFn on_error) {
  std::map<std::string, std::vector<std::string>> by_container;
  int errors = 0;
  for (const std::string& path : members) {
    const std::optional<archive::MemberPathParts> parts = archive::SplitMemberPath(path, member_path_options);
    if (!parts.has_value()) {
      ++errors;
      on_error(path, absl::InternalError("not a member path, so no container to rewrite"));
      continue;
    }
    if (archive::IsMemberPath(parts->container, member_path_options)) {
      ++errors;
      on_error(path, absl::UnimplementedError("a member of a container inside another container cannot be removed"));
      continue;
    }
    by_container[std::string(parts->container)].emplace_back(parts->member);
  }
  for (const auto& [container, names] : by_container) {
    if (dry_run) {
      // The same shape --dry-run uses for an ordinary -delete: say what would go, touch nothing.
      for (const std::string& name : names) {
        emit(absl::StrCat(container, member_path_options.separator, name, "\n"));
      }
      continue;
    }
    if (const absl::Status status = archive::RemoveContainerMembers(container, names); !status.ok()) {
      ++errors;
      on_error(container, status);
    }
  }
  return errors;
}

// One accumulator per --summary sink: {group key -> {count, total size}}.
using SummaryCells = std::map<std::string, std::pair<std::uint64_t, std::uint64_t>>;

// Accumulates one matched unit into every --summary sink. A {template} key that is an m// EXTRACTION
// contributes a value stream (one count per extracted line, size not attributed - a per-line key
// would double-count the file's size); every other key contributes one entry, whose size is
// meaningful. Shared by the two feeds a run has: the per-entry one during the walk, and the
// post-walk --shards one, where a whole logical set arrives as a single unit.
// The run-wide rendering settings a collected entry needs to build its field-render context. Bundled
// because they are resolved once before the walk and never vary per entry, so passing them one by one
// would make FeedCollections a parameter list nobody can read.
struct CollectionRenderDefaults {
  const vfs::FileSystem& fs;
  absl::TimeZone tz;
  std::string_view time_format;
  datetime::ZoneSuffix zone_suffix;
  std::string_view hash_algorithm;
  std::string_view hash_encoding;
  const std::map<std::string, std::string>& defines;
};

void FeedSummaries(
    const std::vector<SummarySpec>& specs,
    const std::vector<std::optional<fields::Template>>& templates,
    std::vector<SummaryCells>& cells_per_sink,
    const fields::RenderContext& key_ctx,
    const Visit& visit) {
  for (std::size_t i = 0; i < specs.size(); ++i) {
    if (specs[i].mode == SummaryMode::kHashVerification) {
      continue;  // fed from the -hasheq verdict, not from the expression's matched result
    }
    SummaryCells& cells = cells_per_sink[i];
    if (specs[i].mode != SummaryMode::kTemplate) {
      std::pair<std::uint64_t, std::uint64_t>& agg = cells[SummaryKey(specs[i].mode, visit)];
      agg.first += 1;
      agg.second += visit.metadata.size;
      continue;
    }
    const std::optional<fields::Template>& tmpl = templates[i];
    if (!tmpl.has_value()) {
      continue;  // a kTemplate sink always carries its compiled template, but the type allows the gap
    }
    const std::optional<std::vector<std::string>> stream = tmpl->AsExtraction(key_ctx);
    if (!stream.has_value()) {
      std::pair<std::uint64_t, std::uint64_t>& agg = cells[tmpl->Render(key_ctx)];
      agg.first += 1;
      agg.second += visit.metadata.size;
      continue;
    }
    for (const std::string& key : *stream) {
      cells[key].first += 1;
    }
  }
}

// Feeds the verdict of the single -hasheq into each verification sink. This is deliberately
// separate from FeedSummaries: a failed verification normally makes the expression false, but it
// is precisely the result this reduction must count. No verdict means short-circuiting never
// reached -hasheq and therefore contributes nothing.
void FeedVerificationSummaries(
    const std::vector<SummarySpec>& specs,
    std::vector<SummaryCells>& cells_per_sink,
    const Visit& visit,
    std::optional<bool> verification) {
  if (!verification.has_value()) {
    return;
  }
  for (std::size_t i = 0; i < specs.size(); ++i) {
    if (specs[i].mode != SummaryMode::kHashVerification) {
      continue;
    }
    auto& aggregate = cells_per_sink[i][*verification ? "verified" : "failed"];
    aggregate.first += 1;
    aggregate.second += visit.metadata.size;
  }
}

std::optional<std::uint64_t> HistogramValue(const HistogramSpec& spec, const Visit& visit, const vfs::FileSystem& fs) {
  if (spec.agg == HistAgg::kCount) {
    return 1;
  }
  if (spec.metric == HistMetric::kSize) {
    return visit.metadata.size;
  }
  if (visit.metadata.type != vfs::FileType::kRegular) {
    return std::nullopt;
  }
  const absl::StatusOr<std::string> content = fs.ReadContent(visit.path);
  return content.ok() ? content::ContentLineCount(*content) : std::nullopt;
}

// Accumulates one matched unit into every --histogram sink. An entry with no value for a spec is
// skipped rather than bucketed as zero: the bucket field may be unavailable (a lines bucket for a
// binary file), and so may the metric.
void FeedHistograms(
    const std::vector<HistogramSpec>& specs,
    std::vector<std::map<std::string, HistCell>>& cells_per_sink,
    const Visit& visit,
    const vfs::FileSystem& fs) {
  for (std::size_t i = 0; i < specs.size(); ++i) {
    const HistogramSpec& spec = specs[i];
    const std::optional<std::pair<std::string, std::string>> bucket = HistBucketKey(spec, visit, fs);
    if (!bucket.has_value()) {
      continue;
    }
    const std::optional<std::uint64_t> value = HistogramValue(spec, visit, fs);
    if (!value.has_value()) {
      continue;
    }
    HistCell& cell = cells_per_sink[i][bucket->first];
    if (cell.count == 0) {
      cell.label = bucket->second;  // display text, set once when the bucket is first seen
    }
    cell.min = cell.count == 0 ? *value : std::min(cell.min, *value);
    cell.max = cell.count == 0 ? *value : std::max(cell.max, *value);
    cell.sum += *value;
    cell.count += 1;
  }
}

// The run's collection store, budgeted from --buffer and marked active when the expression collects
// at all. --buffer bounds it the same way it bounds a column buffer: a row window or a byte budget.
// The default is NO cap, because a number picked here would be a guess; the budget exists so that a
// run which would exhaust memory says so instead of dying.
Collections MakeCollections(
    mbo::types::OptionalRef<const parser::Expr> expression,
    const std::vector<std::string>& globals) {
  Collections collections;
  if (!expression.has_value() || CollectSites(*expression).empty()) {
    return collections;
  }
  collections.SetActive(true);
  const BufferBound bound = ResolveBufferBound(globals, format::ColumnBuffer::kAll);
  collections.SetBudget(
      Collections::Budget{
          .rows = bound.window == format::ColumnBuffer::kAll ? 0 : bound.window,
          .bytes = bound.byte_budget,
      });
  return collections;
}

// The -collect post-walk pass: feeds every collected entry into the reduction sinks, in collection
// name order then walk order. `-collect` exists so that a truncating test can narrow the LISTING
// without also narrowing what is summarised, which is only possible if the sinks read a set the walk
// held back rather than the stream. BOTH sinks switch together: a run where --summary reduced the
// collection while --histogram reduced the matches would report two different totals for one walk.
// The Visit is rebuilt per entry because a collected entry owns its storage (see collect.h).
void FeedCollections(
    const Collections& collections,
    const CollectionRenderDefaults& defaults,
    const std::vector<SummarySpec>& summaries,
    const std::vector<std::optional<fields::Template>>& summary_templates,
    std::vector<SummaryCells>& summary_cells,
    const std::vector<HistogramSpec>& histograms,
    std::vector<std::map<std::string, HistCell>>& histogram_cells) {
  for (const std::string_view name : collections.Names()) {
    for (const CollectedEntry& collected : collections.Entries(name)) {
      const Visit visit = collected.AsVisit();
      const std::string link;  // {target} is not resolved for a collected entry
      const fields::RenderContext key_ctx{
          .path = visit.path,
          .root = visit.root,
          .link_target = link,
          .metadata = visit.metadata,
          .depth = visit.depth,
          .fs = visit.fs.has_value() ? *visit.fs : defaults.fs,
          .tz = defaults.tz,
          .time_format = defaults.time_format,
          .zone_suffix = defaults.zone_suffix,
          .hash_algorithm = defaults.hash_algorithm,
          .hash_encoding = defaults.hash_encoding,
          .defines = defaults.defines,
      };
      FeedSummaries(summaries, summary_templates, summary_cells, key_ctx, visit);
      FeedHistograms(histograms, histogram_cells, visit, *visit.fs);
    }
  }
}

// The collection's post-walk step: refuse an INCOMPLETE collection, otherwise feed the reduction
// sinks from it. Returns 0 to carry on, or the exit code the driver must return.
//
// Overflow is a hard stop rather than a truncation because every sink downstream would otherwise
// report a plausible number computed over part of the walk, which is indistinguishable from a
// correct one - the same reason --max-results caps output without stopping the walk.
int FinishCollections(
    const Collections& collections,
    WalkErrorFn on_error,
    const std::vector<SummarySpec>& summaries,
    const std::vector<std::optional<fields::Template>>& summary_templates,
    std::vector<SummaryCells>& summary_cells,
    const std::vector<HistogramSpec>& histograms,
    std::vector<std::map<std::string, HistCell>>& histogram_cells,
    const CollectionRenderDefaults& defaults) {
  if (collections.Overflowed()) {
    const Collections::Budget budget = collections.CurrentBudget();
    on_error(
        "-collect",
        absl::ResourceExhaustedError(
            absl::StrCat(
                "the collection exceeded --buffer (",
                budget.rows != 0 ? absl::StrCat(budget.rows, " rows") : absl::StrCat(budget.bytes, " bytes"),
                "); raise --buffer or narrow the expression")));
    return 2;
  }
  if (collections.Active() && (!summaries.empty() || !histograms.empty())) {
    FeedCollections(collections, defaults, summaries, summary_templates, summary_cells, histograms, histogram_cells);
  }
  return 0;
}

}  // namespace

namespace {

struct TreeCompareEntry {
  std::string path;
  vfs::Metadata metadata;
  mbo::types::OptionalRef<const vfs::FileSystem> fs;
  std::shared_ptr<const vfs::FileSystem> fs_owner;
};

using TreeCompareEntries = std::map<std::string, TreeCompareEntry>;

enum class TreeCompareOutput { kStatus, kDiff };

struct TreeCompareSelection {
  bool left_only = true;
  bool right_only = true;
  bool identical = false;
  bool different = true;
};

TreeCompareOutput ResolveTreeCompareOutput(const std::vector<std::string>& globals) {
  constexpr std::string_view kPrefix = "--compare=";
  TreeCompareOutput output = TreeCompareOutput::kStatus;
  for (const std::string& global : globals) {
    if (global.starts_with(kPrefix)) {
      output = global.substr(kPrefix.size()) == "diff" ? TreeCompareOutput::kDiff : TreeCompareOutput::kStatus;
    }
  }
  return output;
}

absl::StatusOr<TreeCompareSelection> ResolveTreeCompareSelection(const std::vector<std::string>& globals) {
  constexpr std::string_view kPrefix = "--compare-select=";
  TreeCompareSelection selection;
  for (const std::string& global : globals) {
    if (!global.starts_with(kPrefix)) {
      continue;
    }
    selection = {.left_only = false, .right_only = false, .identical = false, .different = false};
    for (const std::string_view kind : absl::StrSplit(global.substr(kPrefix.size()), ',')) {
      if (kind == "all") {
        selection = {.left_only = true, .right_only = true, .identical = true, .different = true};
      } else if (kind == "left-only") {
        selection.left_only = true;
      } else if (kind == "right-only") {
        selection.right_only = true;
      } else if (kind == "identical") {
        selection.identical = true;
      } else if (kind == "different") {
        selection.different = true;
      } else {
        return absl::InvalidArgumentError(
            absl::StrCat(
                "unknown comparison result '", kind, "' (use left-only, right-only, identical, different, or all)"));
      }
    }
  }
  return selection;
}

absl::StatusOr<bool> SameTreeEntry(const TreeCompareEntry& left, const TreeCompareEntry& right) {
  if (left.metadata.type != right.metadata.type) {
    return false;
  }
  if (left.metadata.type == vfs::FileType::kDirectory) {
    return true;
  }
  if (left.metadata.type == vfs::FileType::kSymlink) {
    MBO_ASSIGN_OR_RETURN(const std::string left_target, left.fs->ReadLink(left.path));
    MBO_ASSIGN_OR_RETURN(const std::string right_target, right.fs->ReadLink(right.path));
    return left_target == right_target;
  }
  if (left.metadata.type != vfs::FileType::kRegular) {
    return true;
  }
  if (left.metadata.size != right.metadata.size) {
    return false;
  }
  constexpr std::size_t kChunkSize = std::size_t{64} * 1'024;
  for (std::uint64_t offset = 0; offset < left.metadata.size; offset += kChunkSize) {
    const auto length = static_cast<std::size_t>(std::min<std::uint64_t>(kChunkSize, left.metadata.size - offset));
    MBO_ASSIGN_OR_RETURN(const std::string left_chunk, left.fs->ReadContentRange(left.path, offset, length));
    MBO_ASSIGN_OR_RETURN(const std::string right_chunk, right.fs->ReadContentRange(right.path, offset, length));
    if (left_chunk != right_chunk) {
      return false;
    }
  }
  return true;
}

absl::StatusOr<std::string> TreeEntryPatch(
    const std::optional<TreeCompareEntry>& left,
    const std::optional<TreeCompareEntry>& right,
    std::string_view relative_path,
    const mbo::diff::DiffOptions& options) {
  const std::string left_name = left.has_value() ? absl::StrCat("a/", relative_path) : "/dev/null";
  const std::string right_name = right.has_value() ? absl::StrCat("b/", relative_path) : "/dev/null";
  const bool regular = (!left.has_value() || left->metadata.type == vfs::FileType::kRegular)
                       && (!right.has_value() || right->metadata.type == vfs::FileType::kRegular);
  if (!regular) {
    return absl::StrCat("Files ", left_name, " and ", right_name, " differ\n");
  }
  std::string left_data;
  std::string right_data;
  if (left.has_value()) {
    MBO_ASSIGN_OR_RETURN(left_data, left->fs->ReadContent(left->path));
  }
  if (right.has_value()) {
    MBO_ASSIGN_OR_RETURN(right_data, right->fs->ReadContent(right->path));
  }
  if (absl::StrContains(left_data.substr(0, content::kBinaryNulSniffBytes), '\0')
      || absl::StrContains(right_data.substr(0, content::kBinaryNulSniffBytes), '\0')) {
    return absl::StrCat("Binary files ", left_name, " and ", right_name, " differ\n");
  }
  return mbo::diff::Diff::FileDiff(
      mbo::file::Artefact{.data = left_data, .name = left_name},
      mbo::file::Artefact{.data = right_data, .name = right_name}, options);
}

using MatchedEntryFn = absl::FunctionRef<void(const Visit&)>;

RunResult RunFindCore(
    const parser::Command& command,
    absl::Span<const std::string> roots,
    const vfs::FileSystem& fs,
    EmitFn emit,
    WalkErrorFn on_error,
    std::optional<registry::Style> style,
    mbo::types::OptionalRef<const MatchedEntryFn> matched_entry,
    bool compare_listing);

// NOLINTNEXTLINE(readability-function-cognitive-complexity): cohesive two-tree merge dispatch
RunResult RunTreeCompare(
    const parser::Command& command,
    const vfs::FileSystem& fs,
    EmitFn emit,
    WalkErrorFn on_error,
    std::optional<registry::Style> style) {
  if (command.roots.size() != 2) {
    on_error("--compare", absl::InvalidArgumentError("requires exactly two roots"));
    return RunResult{.errors = 2};
  }
  const TreeCompareOutput output = ResolveTreeCompareOutput(command.globals);
  const absl::StatusOr<TreeCompareSelection> selection_result = ResolveTreeCompareSelection(command.globals);
  if (!selection_result.ok()) {
    on_error("--compare-select", selection_result.status());
    return RunResult{.errors = 2};
  }
  const TreeCompareSelection selection = *selection_result;
  if (output == TreeCompareOutput::kDiff && selection.identical) {
    on_error("--compare-select", absl::InvalidArgumentError("identical entries have no patch representation"));
    return RunResult{.errors = 2};
  }
  mbo::diff::DiffOptions diff_options;
  diff_options.output_format = mbo::diff::DiffOptions::OutputFormat::kUnified;
  diff_options.time_format = "";
  for (const std::string& global : command.globals) {
    constexpr std::string_view kAlgorithm = "--diff-algorithm=";
    constexpr std::string_view kContext = "--diff-context=";
    if (global.starts_with(kAlgorithm)) {
      const std::string_view value = std::string_view(global).substr(kAlgorithm.size());
      const std::optional<mbo::diff::DiffOptions::Algorithm> algorithm =
          mbo::diff::DiffOptions::ParseAlgorithmFlag(value);
      if (!algorithm.has_value()) {
        on_error("--diff-algorithm", absl::InvalidArgumentError(absl::StrCat("unknown diff algorithm '", value, "'")));
        return RunResult{.errors = 2};
      }
      diff_options.algorithm = *algorithm;
    } else if (global.starts_with(kContext)) {
      const std::string_view value = std::string_view(global).substr(kContext.size());
      if (!absl::SimpleAtoi(value, &diff_options.context_size)) {
        on_error("--diff-context", absl::InvalidArgumentError(absl::StrCat("invalid context size '", value, "'")));
        return RunResult{.errors = 2};
      }
    }
  }

  std::array<TreeCompareEntries, 2> entries;
  std::mutex callback_mutex;
  std::set<std::string> reported_errors;
  const auto run_side = [&](std::size_t side) {
    const auto collect_callback = [&](const Visit& visit) {
      const std::string_view relative = RelativeTo(visit.path, visit.root);
      entries.at(side).emplace(
          relative.empty() ? "." : std::string(relative),
          TreeCompareEntry{
              .path = std::string(visit.path), .metadata = visit.metadata, .fs = visit.fs, .fs_owner = visit.fs_owner});
    };
    const auto emit_callback = [&](std::string_view text) {
      const std::scoped_lock lock(callback_mutex);
      emit(text);
    };
    const auto error_callback = [&](std::string_view path, absl::Status status) {
      const std::scoped_lock lock(callback_mutex);
      if (reported_errors.emplace(absl::StrCat(path, "\n", status.ToString())).second) {
        on_error(path, std::move(status));
      }
    };
    const MatchedEntryFn collect = collect_callback;
    const EmitFn synchronized_emit = emit_callback;
    const WalkErrorFn synchronized_error = error_callback;
    return RunFindCore(
        command, absl::MakeConstSpan(command.roots).subspan(side, 1), fs, synchronized_emit, synchronized_error, style,
        collect, /*compare_listing=*/true);
  };
  std::future<RunResult> left_result = std::async(std::launch::async, run_side, 0);
  const RunResult right_result = run_side(1);
  const RunResult left_run_result = left_result.get();
  if (left_run_result.errors != 0 || right_result.errors != 0) {
    return RunResult{.errors = std::max(left_run_result.errors, right_result.errors)};
  }

  bool different = false;
  auto left = entries[0].begin();
  auto right = entries[1].begin();
  while (left != entries[0].end() || right != entries[1].end()) {
    if (right == entries[1].end() || (left != entries[0].end() && left->first < right->first)) {
      if (left->second.metadata.type != vfs::FileType::kDirectory && selection.left_only) {
        if (output == TreeCompareOutput::kStatus) {
          emit(absl::StrCat("left-only\t", left->first, "\n"));
        } else {
          const absl::StatusOr<std::string> patch =
              TreeEntryPatch(left->second, std::nullopt, left->first, diff_options);
          if (!patch.ok()) {
            on_error(left->first, patch.status());
            return RunResult{.errors = 1};
          }
          emit(*patch);
        }
      }
      different = different || left->second.metadata.type != vfs::FileType::kDirectory;
      ++left;
    } else if (left == entries[0].end() || right->first < left->first) {
      if (right->second.metadata.type != vfs::FileType::kDirectory && selection.right_only) {
        if (output == TreeCompareOutput::kStatus) {
          emit(absl::StrCat("right-only\t", right->first, "\n"));
        } else {
          const absl::StatusOr<std::string> patch =
              TreeEntryPatch(std::nullopt, right->second, right->first, diff_options);
          if (!patch.ok()) {
            on_error(right->first, patch.status());
            return RunResult{.errors = 1};
          }
          emit(*patch);
        }
      }
      different = different || right->second.metadata.type != vfs::FileType::kDirectory;
      ++right;
    } else {
      const absl::StatusOr<bool> same = SameTreeEntry(left->second, right->second);
      if (!same.ok()) {
        on_error(left->first, same.status());
        return RunResult{.errors = 1};
      }
      if (*same && left->second.metadata.type != vfs::FileType::kDirectory && selection.identical) {
        emit(absl::StrCat("identical\t", left->first, "\n"));
      } else if (!*same && selection.different) {
        if (output == TreeCompareOutput::kStatus) {
          emit(absl::StrCat("different\t", left->first, "\n"));
        } else {
          const absl::StatusOr<std::string> patch =
              TreeEntryPatch(left->second, right->second, left->first, diff_options);
          if (!patch.ok()) {
            on_error(left->first, patch.status());
            return RunResult{.errors = 1};
          }
          emit(*patch);
        }
      }
      different = different || !*same;
      ++left;
      ++right;
    }
  }
  return RunResult{.errors = 0, .any_match = different};
}

}  // namespace

namespace {

// Cohesive run dispatch; the visitor and post-walk sinks intentionally share this state.
// NOLINTNEXTLINE(readability-function-cognitive-complexity,readability-function-size,hicpp-function-size,google-readability-function-size)
RunResult RunFindCore(
    const parser::Command& command,
    absl::Span<const std::string> roots,
    const vfs::FileSystem& fs,
    EmitFn emit,
    WalkErrorFn on_error,
    std::optional<registry::Style> style,
    mbo::types::OptionalRef<const MatchedEntryFn> matched_entry,
    bool compare_listing) {
  bool any_match = false;
  std::vector<std::string> mime_vocabulary_files;
  mime::ConflictPolicy mime_conflicts = mime::ConflictPolicy::kError;
  std::vector<std::string> language_db_files;
  language::ConflictPolicy language_conflicts = language::ConflictPolicy::kError;
  for (const std::string& global : command.globals) {
    constexpr std::string_view kVocabulary = "--mime-vocabulary=";
    constexpr std::string_view kConflicts = "--mime-conflicts=";
    constexpr std::string_view kLanguageDbPrefix = "--lang-db=";
    constexpr std::string_view kLanguageConflicts = "--lang-conflicts=";
    if (global.starts_with(kVocabulary)) {
      mime_vocabulary_files.emplace_back(std::string_view(global).substr(kVocabulary.size()));
    } else if (global.starts_with(kConflicts)) {
      const std::string_view value = std::string_view(global).substr(kConflicts.size());
      if (value == "first") {
        mime_conflicts = mime::ConflictPolicy::kFirst;
      }
      if (value == "last") {
        mime_conflicts = mime::ConflictPolicy::kLast;
      }
      if (value == "error") {
        mime_conflicts = mime::ConflictPolicy::kError;
      }
    } else if (global.starts_with(kLanguageDbPrefix)) {
      language_db_files.emplace_back(std::string_view(global).substr(kLanguageDbPrefix.size()));
    } else if (global.starts_with(kLanguageConflicts)) {
      const std::string_view value = std::string_view(global).substr(kLanguageConflicts.size());
      if (value == "first") {
        language_conflicts = language::ConflictPolicy::kFirst;
      }
      if (value == "last") {
        language_conflicts = language::ConflictPolicy::kLast;
      }
      if (value == "error") {
        language_conflicts = language::ConflictPolicy::kError;
      }
    }
  }
  if (const absl::Status status = mime::Configure(mime_vocabulary_files, mime_conflicts); !status.ok()) {
    on_error("--mime-vocabulary", status);
    return RunResult{.errors = 2};
  }
  if (const absl::Status status = language::Configure(language_db_files, language_conflicts); !status.ok()) {
    on_error("--lang-db", status);
    return RunResult{.errors = 2};
  }
  const mbo::types::OptionalRef<const parser::Expr> expression = parser::AsConstOptionalExpr(command.expression);
  const bool has_action = expression.has_value() && ContainsAction(*expression);
  // --implicit-print=yes|no overrides find's default-print rule (otherwise !has_action).
  const bool implicit_print = ResolveImplicitPrint(command.globals).value_or(!has_action && !compare_listing);
  if (HasGlobal(command.globals, "--safe") && expression.has_value() && ContainsArmedAction(*expression)) {
    on_error("-delete", absl::FailedPreconditionError("refused: --safe forbids destructive actions"));
    return RunResult{.errors = 2};  // do not traverse
  }
  // A NAME bound twice by -capture or -collect is a usage error before the walk (see
  // ReportDuplicateBindingName for why each one fails closed).
  if (expression.has_value() && ReportDuplicateBindingName(*expression, on_error)) {
    return RunResult{.errors = 2};  // do not traverse
  }
  WalkOptions options;
  options.symlinks = ResolveSymlinkMode(command.globals);
  options.sort = ResolveSort(command.globals, style);
  const absl::StatusOr<std::size_t> workers = ResolveJobs(command.globals, style);
  if (!workers.ok()) {
    on_error("--jobs", workers.status());
    return RunResult{.errors = 2};
  }
  options.workers = *workers;
  const render::Format format = ResolveFormat(command.globals);
  const render::PathEncoding path_encoding = ResolvePathEncoding(command.globals);
  // --color=auto|always|never: colorize the plain listing by file type. auto (the
  // default) colors only a real terminal with NO_COLOR unset; a pipe (or a test's
  // captured stdout) is not a tty, so it stays plain unless --color=always forces it.
  const bool colorize =
      format == render::Format::kPlain
      && color::Enabled(color::ResolveWhen(command.globals), ::isatty(STDOUT_FILENO) != 0, env::Has("NO_COLOR"));
  // Colouring can classify every visited regular file. Pin the configured immutable snapshot once
  // so that hot-path lookups neither take the registry mutex nor observe a later embedder/test run's
  // reconfiguration. Runs that do not emit colour retain the language database's lazy loading.
  const std::optional<language::LanguageSnapshot> language_snapshot =
      colorize ? std::optional(language::ActiveSnapshot()) : std::nullopt;
  // --color-scheme: the palette every colourised surface uses, resolved ONCE because colour is a
  // whole-run choice. The default (`auto`, i.e. ls OR xff) takes the terminal's own theme when there
  // is one - $LS_COLORS, or $LSCOLORS where only BSD's variable is set, which is the macOS case - and
  // xff's built-in scheme when there is not; see color::Scheme.
  const color::Palette palette = color::PaletteFor(
      color::ResolveScheme(command.globals), env::Get("LS_COLORS").value_or(""), env::Get("LSCOLORS").value_or(""));
  const std::optional<std::string> tmpl = ResolveTemplate(command.globals);
  // A -capture whose {capture.NAME} is never referenced ran a subprocess for
  // nothing (use -exec for pure side effects); flag it before traversing.
  if (expression.has_value()) {
    if (const std::optional<std::string> unused =
            UnusedCaptureName(*expression, tmpl, AllSummaryTemplates(command.globals));
        unused.has_value()) {
      on_error(
          "-capture", absl::FailedPreconditionError(
                          absl::StrCat("-capture '", *unused, "' is never referenced as {capture.", *unused, "}")));
      return RunResult{.errors = 2};  // do not traverse
    }
  }
  // Precompile the --template once; rendering each match then skips re-scanning.
  const std::optional<fields::Template> compiled_tmpl =
      tmpl.has_value() ? std::optional<fields::Template>(fields::Template::Compile(*tmpl)) : std::nullopt;
  // --columns=FIELD,... : the tabular column set (--format=csv/tsv/aligned/markdown). Validate
  // before the walk -- an unknown column, a non-tabular format, or a suppressed default listing
  // (an action / --implicit-print=no) is a usage error, not a silently-empty or moot table.
  // aligned/markdown are `buffered` (the whole table renders after the walk, once every column
  // width is known); csv/tsv stream a row at a time. All four support --columns + a header.
  const std::vector<std::string> columns = ResolveColumns(command.globals);
  // (i): an m// extraction yields a value stream, valid ONLY as a --summary key. In any per-entry
  // scalar render context -- an -exec/-printf/-grep command or format arg, --template, or a
  // --columns field -- a stream has no single value, so reject it up front (exit 2) rather than
  // silently newline-joining it. --summary handles the extraction key separately (kTemplate).
  {
    std::optional<std::string> extraction;
    if (expression.has_value()) {
      extraction = FindScalarExtraction(*expression);
    }
    if (!extraction.has_value() && tmpl.has_value() && fields::Template::Compile(*tmpl).HasUnreducedExtraction()) {
      extraction = *tmpl;
    }
    for (const std::string& col : columns) {
      if (extraction.has_value()) {
        break;
      }
      if (fields::Template::Compile(absl::StrCat("{", col, "}")).HasUnreducedExtraction()) {
        extraction = col;
      }
    }
    if (extraction.has_value()) {
      on_error(
          "field template", absl::InvalidArgumentError(
                                absl::StrCat(
                                    "an m// extraction ('", *extraction,
                                    "') is only valid as a --summary key, not in a per-entry render context")));
      return RunResult{.errors = 2};
    }
  }
  const bool buffered = format == render::Format::kAligned || format == render::Format::kMarkdown;
  const bool is_tree = format == render::Format::kTree;
  const bool tabular = format == render::Format::kCsv || format == render::Format::kTsv || buffered;
  if ((tabular || is_tree || !columns.empty()) && !implicit_print) {
    on_error(
        "--format", absl::FailedPreconditionError(
                        "tabular/tree output (--format=csv/tsv/aligned/markdown/tree) and --columns format the "
                        "default listing; an action like -ls / -printf / -exec produces its own output -- drop "
                        "the action, or drop --format / --columns"));
    return RunResult{.errors = 2};
  }
  if (!columns.empty() && !tabular) {
    on_error(
        "--columns",
        absl::FailedPreconditionError("--columns needs a tabular --format (csv, tsv, aligned, or markdown)"));
    return RunResult{.errors = 2};
  }
  for (const std::string& col : columns) {
    if (col.empty() || !fields::IsKnownField(col)) {
      on_error("--columns", absl::InvalidArgumentError(absl::StrCat("unknown column '", col, "'")));
      return RunResult{.errors = 2};
    }
  }
  // Precompile one field Template per column ({col}); each match renders them into a row.
  std::vector<fields::Template> column_templates;
  column_templates.reserve(columns.size());
  for (const std::string& col : columns) {
    column_templates.push_back(fields::Template::Compile(absl::StrCat("{", col, "}")));
  }
  const bool exec_fields = HasGlobal(command.globals, "--exec-fields");  // route -exec through the vocabulary
  const std::map<std::string, std::string> defines = ResolveDefines(command.globals);  // {def.NAME} values
  // --exclude=GLOB / --include=GLOB: a run-level gitignore-style filter. An ignored
  // entry is dropped before evaluation (a matched directory is pruned, not descended);
  // --include re-includes. Empty when neither flag is present (zero overhead).
  const ignore::PatternList ignore_patterns = BuildIgnorePatterns(command.globals);
  if (expression.has_value()) {
    const DepthOptions depth = ResolveDepthOptions(*expression);
    options.min_depth = depth.min_depth.value_or(options.min_depth);
    options.max_depth = depth.max_depth.value_or(options.max_depth);
    options.post_order = options.post_order || depth.post_order;
    options.single_filesystem = options.single_filesystem || depth.single_filesystem;
    options.ignore_readdir_race = depth.ignore_readdir_race.value_or(options.ignore_readdir_race);
  }
  // A malformed -size / -blocks value (unknown unit, an over-64-bit unit like Z/Y,
  // or a non-numeric count) is a usage error refused before the walk -- find rejects
  // bad -size at parse time too, rather than silently matching nothing.
  if (expression.has_value()) {
    if (const absl::Status size_status = ValidateSizeArgs(*expression); !size_status.ok()) {
      on_error("-size/-blocks", size_status);
      return RunResult{.errors = 2};  // do not traverse
    }
  }
  // --timezone=ZONE overrides the local zone for interpreting time-string args
  // (-newerXt) and -daystart's midnight. Resolved first (both need it); an unknown
  // zone is a usage error, refused before traversal.
  const absl::StatusOr<absl::TimeZone> tz_result = ResolveTimeZone(command.globals);
  if (!tz_result.ok()) {
    on_error("--timezone", tz_result.status());
    return RunResult{.errors = 2};  // do not traverse
  }
  const absl::TimeZone tz = *tz_result;
  // Capture one reference instant so every entry's age test (-mtime/-mmin) is
  // measured against the same clock. -daystart measures from today's local
  // midnight (in tz) instead of find's start time (the run's start).
  const bool daystart = expression.has_value() && ContainsPrimary(*expression, "-daystart");
  const absl::Time now = daystart ? datetime::StartOfDay(absl::Now(), tz) : absl::Now();
  // --time-format=NAME: default spec for a time field with no {:qualifier}.
  const std::string time_format = ResolveTimeFormat(command.globals);
  const datetime::ZoneSuffix zone_suffix = ResolveZoneSuffix(command.globals);
  // --block-size=SIZE: bytes per -size block (a bare value / the 'b' suffix); find's
  // historical default is 512. A malformed SIZE is a usage error, refused here.
  const absl::StatusOr<std::uint64_t> block_size_result = ResolveBlockSize(command.globals);
  if (!block_size_result.ok()) {
    on_error("--block-size", block_size_result.status());
    return RunResult{.errors = 2};  // do not traverse
  }
  const std::uint64_t block_size = *block_size_result;
  // --sort=score ranks the listing by the -fuzzy score, so it needs a score to rank by and a
  // listing to reorder. Both are usage errors before the walk rather than a silent no-op: ordering
  // by a value nothing produced is a mistake, not an empty ordering.
  const bool rank_by_score = ResolveRankByScore(command.globals);
  if (!ResultShapingIsValid(expression, rank_by_score, is_tree, buffered, on_error)) {
    return RunResult{.errors = 2};  // do not traverse; the helper reported which rule failed
  }

  // --regextype=RE2|EXACT|PCRE2: the grammar is resolved by the parser and pre-compiled into each
  // matcher; here we only validate the selector for the whole run. PCRE2 when not built into this
  // binary, MATCH (reserved), and unknown values are usage errors, refused before the walk.
  if (const absl::Status regextype = ValidateRegextype(command.globals); !regextype.ok()) {
    on_error("--regextype", regextype);
    return RunResult{.errors = 2};  // do not traverse
  }
  // --count / -c: -grep emits a per-file matching-line count instead of the lines.
  const bool grep_count = HasGlobal(command.globals, "--count") || HasGlobal(command.globals, "-c");
  // --context / --before-context / --after-context (grep -C/-B/-A): -grep context lines. Validated
  // here so a bad value is a usage error (exit 2) before the walk.
  const absl::StatusOr<GrepContext> grep_context_result = ResolveGrepContext(command.globals);
  if (!grep_context_result.ok()) {
    on_error("--context", grep_context_result.status());
    return RunResult{.errors = 2};
  }
  const GrepContext grep_context = *grep_context_result;
  const std::size_t grep_before = grep_context.before;
  const std::size_t grep_after = grep_context.after;
  // --diff-algorithm=naive|direct|myers: the engine -diff uses (mbo::diff). Last occurrence
  // wins; empty -> myers (the default). Validated here so a bad value is a usage error (exit 2)
  // before the walk rather than a silent fallback.
  std::string diff_algorithm;
  for (const std::string& global : command.globals) {
    constexpr std::string_view kDiffAlgo = "--diff-algorithm=";
    if (global.starts_with(kDiffAlgo)) {
      diff_algorithm = global.substr(kDiffAlgo.size());
    }
  }
  if (!diff_algorithm.empty() && !mbo::diff::DiffOptions::ParseAlgorithmFlag(diff_algorithm).has_value()) {
    on_error(
        "--diff-algorithm",
        absl::InvalidArgumentError(
            absl::StrCat("unknown diff algorithm '", diff_algorithm, "' (use naive, direct, or myers)")));
    return RunResult{.errors = 2};
  }
  // --diff-ignore=<tokens> / --diff-ignore-matching=REGEX: -diff normalization (mbo::diff). Last
  // occurrence of each wins; empty -> exact. Validated here (shared with the apply path) so a bad
  // token or regex is a usage error (exit 2) before the walk.
  std::string diff_ignore;
  std::string diff_ignore_matching;
  for (const std::string& global : command.globals) {
    constexpr std::string_view kDiffIgnore = "--diff-ignore=";
    constexpr std::string_view kDiffIgnoreMatching = "--diff-ignore-matching=";
    if (global.starts_with(kDiffIgnoreMatching)) {
      diff_ignore_matching = global.substr(kDiffIgnoreMatching.size());
    } else if (global.starts_with(kDiffIgnore)) {
      diff_ignore = global.substr(kDiffIgnore.size());
    }
  }
  if (const absl::Status status = ValidateDiffIgnore(diff_ignore, diff_ignore_matching); !status.ok()) {
    on_error("--diff-ignore", status);
    return RunResult{.errors = 2};
  }
  // --diff-format=u|c|n|y|unified|context|normal|side-by-side: the default -diff output format
  // (last occurrence wins; unset -> unified). A per-action -diff:STYLE letter still overrides it.
  // Validated here so a bad value is a usage error (exit 2) before the walk.
  mbo::diff::DiffOptions::OutputFormat diff_format = mbo::diff::DiffOptions::OutputFormat::kUnified;
  std::string diff_format_flag;
  for (const std::string& global : command.globals) {
    constexpr std::string_view kDiffFormat = "--diff-format=";
    if (global.starts_with(kDiffFormat)) {
      diff_format_flag = global.substr(kDiffFormat.size());
    }
  }
  if (!diff_format_flag.empty()) {
    const std::optional<mbo::diff::DiffOptions::OutputFormat> parsed = ParseDiffFormatFlag(diff_format_flag);
    if (!parsed.has_value()) {
      on_error(
          "--diff-format", absl::InvalidArgumentError(
                               absl::StrCat(
                                   "unknown diff format '", diff_format_flag,
                                   "' (use u/unified, c/context, n/normal, or y/side-by-side)")));
      return RunResult{.errors = 2};
    }
    diff_format = *parsed;
  }
  // --diff-context=N (and --context=N when symmetric): the default -diff context size (built-in 3).
  // --context feeds diff only when before==after (a single symmetric value a diff can represent);
  // --diff-context overrides --context regardless of order; a per-action -diff:uN overrides both.
  std::size_t diff_context = 3;
  if (grep_context.specified && grep_before == grep_after) {
    diff_context = grep_before;
  }
  for (const std::string& global : command.globals) {
    constexpr std::string_view kDiffContext = "--diff-context=";
    if (global.starts_with(kDiffContext)) {
      const std::string_view value = std::string_view(global).substr(kDiffContext.size());
      if (std::size_t parsed = 0; absl::SimpleAtoi(value, &parsed)) {
        diff_context = parsed;
      } else {
        on_error("--diff-context", absl::InvalidArgumentError(absl::StrCat("bad --diff-context value '", value, "'")));
        return RunResult{.errors = 2};
      }
    }
  }
  // The whole --archive surface, resolved as one value before the walk consumes it.
  const absl::StatusOr<ResolvedArchiveOptions> archive_options = ResolveArchiveOptions(command.globals, style);
  if (!archive_options.ok()) {
    on_error("--archive", archive_options.status());
    return RunResult{.errors = 2};
  }
  options.archive = archive_options->archive_dive;
  options.archive_depth = archive_options->archive_depth;
  const archive::MemberPathOptions member_path_options = archive_options->member_paths;
  // --archive-any: offer every file to the reader instead of only those whose name looks like a
  // container. Expensive by design (every file is opened and format-bid), so it is opt-in.
  // `--archive=any` / `-z++` / `-Z++` is the top rung: dive like `all` AND drop the name gate.
  // `--archive-any` is the older spelling of the same thing.
  const bool archive_any = absl::c_contains(command.globals, "--archive-any")
                           || absl::c_contains(command.globals, "--archive=any")
                           || absl::c_contains(command.globals, "-z++") || absl::c_contains(command.globals, "-Z++");
  // --hash-algorithm=ALGO / --hash-encoding=hex|base64: defaults for a bare -hash action and a
  // bare {hash} field (last occurrence wins; empty -> sha256 / hex). Validated here so a bad value
  // is a usage error (exit 2) before the walk; the explicit -hash:ALGO[/ENCODING] specs in the
  // expression are validated by ValidateHashArgs below.
  const HashDefaults hash_defaults = ReadHashDefaults(command.globals);
  const std::string& hash_algorithm = hash_defaults.algorithm;
  const std::string& hash_encoding = hash_defaults.encoding;
  if (!hash_algorithm.empty() && !hash::IsAlgorithm(hash_algorithm)) {
    on_error(
        "--hash-algorithm", absl::InvalidArgumentError(
                                absl::StrCat(
                                    "unknown hash algorithm '", hash_algorithm,
                                    "' (one of: ", absl::StrJoin(hash::AlgorithmNames(), ", "), ")")));
    return RunResult{.errors = 2};
  }
  if (!hash_encoding.empty() && !hash::ParseEncoding(hash_encoding).has_value()) {
    on_error(
        "--hash-encoding",
        absl::InvalidArgumentError(absl::StrCat("unknown hash encoding '", hash_encoding, "' (use hex or base64)")));
    return RunResult{.errors = 2};
  }
  if (expression.has_value()) {
    if (const absl::Status status = ValidateHashArgs(*expression); !status.ok()) {
      on_error("-hash", status);
      return RunResult{.errors = 2};
    }
  }
  // --summary (repeatable): reduce matches to a {count, total size} per group instead of printing
  // each one; each --summary is an independent table emitted after the walk. --summary={template}
  // keys group by a field template, compiled once per sink: an unreduced m// extraction key groups
  // per extracted line (a value stream folded into the counts); any other template (including a
  // reducer-terminated m// like `;join(...)`, which is scalar) one key per matched entry. A template
  // mixing an UNREDUCED extraction with other text has no single key and is a usage error refused
  // before the walk; a reduced extraction is scalar and mixes fine.
  const std::vector<SummarySpec> summaries = ResolveSummaries(command.globals);
  const bool hash_verification_summary =
      absl::c_any_of(summaries, [](const SummarySpec& spec) { return spec.mode == SummaryMode::kHashVerification; });
  if (hash_verification_summary) {
    const std::size_t checks = expression.has_value() ? CountPrimary(*expression, "-hasheq") : 0;
    if (checks != 1) {
      on_error(
          "--summary=hash-verification",
          absl::InvalidArgumentError(absl::StrCat("requires exactly one -hasheq expression, found ", checks)));
      return RunResult{.errors = 2};
    }
  }
  std::vector<std::optional<fields::Template>> summary_templates(summaries.size());  // compiled, kTemplate only
  for (std::size_t i = 0; i < summaries.size(); ++i) {
    if (summaries[i].mode != SummaryMode::kTemplate) {
      continue;
    }
    fields::Template tmpl = fields::Template::Compile(summaries[i].key_template);
    if (tmpl.HasUnreducedExtraction() && !tmpl.IsExtraction()) {
      on_error(
          "--summary", absl::InvalidArgumentError(
                           "a --summary key template must be a plain field or exactly one m// extraction, not a mix"));
      return RunResult{.errors = 2};
    }
    summary_templates[i] = std::move(tmpl);
  }
  const std::optional<format::SizeUnits> human =
      ResolveHuman(command.globals, style);  // --human: size units for --summary and -ls (xff -> human)
  // One {group -> {count, total size}} accumulator per --summary sink.
  std::vector<SummaryCells> summary_cells(summaries.size());
  // --histogram (repeatable): a bar chart of the count per bucket, alongside or instead of
  // --summary. Both are reductions fed by one walk; a run with either suppresses the listing.
  absl::StatusOr<std::vector<HistogramSpec>> histograms_or = ResolveHistograms(command.globals);
  if (!histograms_or.ok()) {
    on_error("--histogram", histograms_or.status());
    return RunResult{.errors = 2};
  }
  const std::vector<HistogramSpec> histograms = *std::move(histograms_or);
  std::vector<std::map<std::string, HistCell>> histogram_cells(histograms.size());  // one per spec
  // --shards: collapse each sharded-file set to one line. Like --summary it defers the listing
  // (buffered per directory, grouped after the walk), so it joins `any_reduction`.
  absl::StatusOr<ShardsConfig> shards_or = ResolveShards(command.globals);
  if (!shards_or.ok()) {
    on_error("--shards", shards_or.status());
    return RunResult{.errors = 2};
  }
  const ShardsConfig shards = *std::move(shards_or);
  absl::StatusOr<ShardShow> shard_show_or = ResolveShardShow(command.globals);
  if (!shard_show_or.ok()) {
    on_error("--shards-show", shard_show_or.status());
    return RunResult{.errors = 2};
  }
  const ShardShow shard_show = *shard_show_or;
  absl::StatusOr<shard::Dedup> shard_dedup_or = ResolveShardDedup(command.globals);
  if (!shard_dedup_or.ok()) {
    on_error("--shards-dedup", shard_dedup_or.status());
    return RunResult{.errors = 2};
  }
  const shard::Dedup shard_dedup = *shard_dedup_or;
  // --shard-pattern=REGEX (repeatable): user custom schemes, tried before the built-ins. Collected
  // in order so the first-listed pattern wins on overlap.
  std::vector<std::string> shard_patterns;
  for (const std::string& global : command.globals) {
    constexpr std::string_view kPrefix = "--shard-pattern=";
    if (global.starts_with(kPrefix)) {
      shard_patterns.emplace_back(std::string_view(global).substr(kPrefix.size()));
    }
  }
  // Matcher over the custom patterns plus all built-in schemes (scheme restriction is applied per set
  // below). Make() can fail on a bad custom pattern; surface it as a usage error.
  const bool shard_status_enabled = expression.has_value() && ContainsPrimary(*expression, "-shard-status");
  std::optional<shard::Matcher> shard_matcher;
  if (shards.enabled || shard_status_enabled) {
    absl::StatusOr<shard::Matcher> matcher_or = shard::Matcher::Make({}, shard_patterns);
    if (!matcher_or.ok()) {
      on_error("--shard-pattern", matcher_or.status());
      return RunResult{.errors = 2};
    }
    shard_matcher = *std::move(matcher_or);
  } else {
    shard_matcher = *shard::Matcher::Make();
  }

  // A matched file buffered for shard grouping, bucketed by directory (grouping is per-directory).
  // `name` owns the basename so a ShardFile view into it stays valid post-walk; `root` / `depth` /
  // `metadata` are the entry's, kept so a per-set reduction (--summary / --histogram in shard mode)
  // can synthesize a Visit from the representative shard.
  struct ShardBufFile {
    std::string name;
    std::uint64_t size = 0;
    std::uint32_t mode = 0;
    std::int64_t mtime = 0;  // Unix nanos, for --shards-dedup=mtime
    std::string root;
    int depth = 0;
    vfs::Metadata metadata;
  };

  std::map<std::string, std::vector<ShardBufFile>> shard_buckets;  // dir -> its matched files
  // --archive-extract: an exec-family action on an archive member writes the member to a temporary
  // file and hands the child that. Lives for the whole run because a `-exec ... +` batch and a -j
  // child both outlive the entry, and removes everything it made when the run ends.
  const ArchiveWrite archive_write = ResolveArchiveWrite(command.globals);
  const bool archive_extract = archive_write.extract;
  ExtractedMembers extracted_members;
  // --archive-mount: serve a member from a read-only MOUNT of its container instead of a copy.
  // Preferred over extraction where the machine can mount; where it cannot, the provider answers
  // nothing, extraction takes over, and the reason is reported once after the walk.
  MountedContainers mounted_containers(absl::c_contains(command.globals, "--archive-mount"));
  // --archive-delete: `-delete` on a member records it here instead of refusing; the containers are
  // rewritten after the walk (see the flush below), because a member cannot be removed from a
  // container the walk is reading at that moment.
  const bool archive_delete = archive_write.remove;
  std::vector<std::string> archive_deletions;
  // --pack=FILE: the matches become a NEW archive rather than a listing, so it is a sink like
  // --summary. Everything that can be checked without walking is checked here: a missing extra and an
  // output name that carries no writable format both cost a whole traversal if found out afterwards.
  const std::optional<std::string> pack_target = ReadPackTarget(command.globals);
  const absl::StatusOr<std::vector<archive::PackOption>> pack_options = ReadPackOptions(command.globals);
  if (!pack_options.ok()) {
    on_error("--pack-option", pack_options.status());
    return RunResult{.errors = 2};
  }
  std::vector<archive::PackFile> pack_files;
  // The output's own identity, so the walk never packs the archive into itself. The basename is kept
  // beside the resolved path as a cheap gate: canonicalizing every match would put a syscall on the
  // hot path to answer a question almost every entry answers "no" to by name alone.
  std::string pack_identity;
  std::string_view pack_basename;
  bool pack_saw_member = false;
  if (pack_target.has_value()) {
    if (!archive::ContainerPackingAvailable()) {
      on_error("--pack", absl::UnimplementedError("this binary was built without archive support"));
      return RunResult{.errors = 2};
    }
    if (archive::ContainerPackFormatFor(*pack_target).empty()) {
      on_error(
          "--pack", absl::InvalidArgumentError(
                        absl::StrCat(
                            "cannot tell the archive format from '", *pack_target, "'; expected a name ending in .",
                            absl::StrJoin(archive::ContainerPackFormats(), ", ."))));
      return RunResult{.errors = 2};
    }
    if (const absl::Status names = CheckPackOptionNames(*pack_options); !names.ok()) {
      on_error("--pack-option", names);
      return RunResult{.errors = 2};
    }
    pack_identity = PackIdentity(*pack_target);
    const std::string_view::size_type slash = pack_identity.rfind('/');
    pack_basename = slash == std::string_view::npos ? std::string_view(pack_identity)
                                                    : std::string_view(pack_identity).substr(slash + 1);
  }
  const bool any_reduction = !summaries.empty() || !histograms.empty() || shards.enabled || pack_target.has_value();
  const absl::StatusOr<std::optional<std::size_t>> max_results = ResolveMaxResults(command.globals);
  if (!max_results.ok()) {
    on_error("--max-results", max_results.status());
    return RunResult{.errors = 2};
  }
  // --archive-aggregate: what a reduction counts when the walk dives. Only `members` needs the walk to
  // open a container before its own entry is visited (see WalkOptions::mount_before_visit), and only
  // when there is a reduction to feed, so an ordinary run never pays for it.
  const absl::StatusOr<ArchiveAggregate> archive_aggregate = ResolveArchiveAggregate(command.globals);
  if (!archive_aggregate.ok()) {
    on_error("--archive-aggregate", archive_aggregate.status());
    return RunResult{.errors = 2};
  }
  if (any_reduction && options.archive != ArchiveDive::kNone && *archive_aggregate == ArchiveAggregate::kMembers) {
    options.mount_before_visit = true;
  }
  int errors = 0;
  // The normalized fuzzy quality composed by the evaluator and read by {fuzzy} / --sort=score.
  // Run-scoped rather than per-entry so the phases can share it; cleared before each evaluation.
  std::optional<int> fuzzy_score;
  // The -hasheq verdict for the entry currently being evaluated. Kept independently from matched:
  // a failed check must still reach --summary=hash-verification.
  std::optional<bool> hash_verification;
  // -first N budgets, one per instance, for the whole run (see EvalContext::first_counts).
  FirstCounts first_counts;
  // -collect[:NAME]: the entries held back for the post-walk reduction. `collect_names` is read from
  // the AST (presence is SYNTACTIC, like find's implicit -print: a -collect in a branch that never
  // runs still switches the summary's source, and the summary is then legitimately empty).
  Collections collections = MakeCollections(expression, command.globals);

  // --dry-run: route deletions through a previewing wrapper, so -delete reports
  // what it would remove without touching the filesystem.
  const DryRunFileSystem dry_run_fs(fs, emit);
  const bool dry_run = HasGlobal(command.globals, "--dry-run");
  const vfs::FileSystem& walk_fs = dry_run ? dry_run_fs : fs;
  // --ignore-files: honor per-directory .ignore / .xffignore files (off by default,
  // find-compatible; -u / --no-ignore forces it off). Reads through walk_fs, so a
  // --dry-run still consults them. Inactive is zero overhead.
  // -g / --gitignore: on forces .gitignore, auto enables it only when a search root
  // is inside a git repo (probe once, before the walk). -u / --no-ignore still wins:
  // ResolveGitignoreMode returns kOff then, so the repo probe and the global-excludes
  // read below are skipped too.
  const GitignoreMode gitignore_mode = ResolveGitignoreMode(command.globals, style);
  const bool gitignore_on =
      gitignore_mode == GitignoreMode::kOn || (gitignore_mode == GitignoreMode::kAuto && AnyRootInRepo(walk_fs, roots));
  // --skip-vcs[=LIST] / --no-skip-vcs: the VCS metadata dir names to prune. Resolved once (needs
  // gitignore_on for the -g -> .git default) and validated here, so a bad token is a usage error
  // (exit 2) refused before the walk.
  const absl::StatusOr<absl::flat_hash_set<std::string>> skip_vcs = ResolveSkipVcs(command.globals, gitignore_on);
  if (!skip_vcs.ok()) {
    on_error("--skip-vcs", skip_vcs.status());
    return RunResult{.errors = 2};
  }
  const absl::flat_hash_set<std::string>& skip_vcs_names = *skip_vcs;
  // git's global excludes (core.excludesFile, else ~/.config/git/ignore): the lowest
  // ignore layer, resolved once when gitignore is on. Read through walk_fs so --dry-run
  // still consults it; empty (a no-op) otherwise.
  ignore::PatternList global_excludes;
  if (gitignore_on) {
    const repo::GitConfigEnv git_env{
        .home = env::Get("HOME").value_or(""),
        .xdg_config_home = env::Get("XDG_CONFIG_HOME").value_or(""),
    };
    if (const std::optional<std::string> path = repo::GlobalExcludesPath(walk_fs, git_env)) {
      if (const absl::StatusOr<std::string> content = walk_fs.ReadContent(*path); content.ok()) {
        global_excludes = ignore::PatternList::Parse(*content);
      }
    }
  }
  IgnoreFileCache ignore_files(
      walk_fs, ResolveIgnoreFileNames(command.globals, gitignore_on, style), gitignore_on, std::move(global_excludes));
  // --ignore-file=PATH: explicit ignore files, each rooted at its own directory. Independent of
  // -g / --no-ignore (the user named these), consulted below CLI --exclude but above the auto
  // .gitignore stack (an explicitly named file outranks auto-discovery, below a direct glob).
  const RootedIgnoreFiles rooted_ignore_files = RootedIgnoreFiles::FromGlobals(walk_fs, command.globals);

  // -ok confirmation: prompt to stderr, read a line from stdin, affirmative on y/Y (like find).
  const auto confirm = [](std::string_view prompt) -> bool {
    std::cerr << prompt;
    std::string line;
    if (!std::getline(std::cin, line)) {
      return false;  // EOF or closed stdin -> decline
    }
    return !line.empty() && (line[0] == 'y' || line[0] == 'Y');
  };

  // File-output actions (-fprint/-fprint0/-fprintf/-fls) append to a named file,
  // opened once (truncating) on first write and held open for the whole walk. The
  // visitor is single-threaded, so the sink map needs no synchronisation. Streams
  // close (flushing) when `file_sinks` goes out of scope after the walk.
  // XFF_HOST_IO: -fprint-family actions intentionally write user-selected host output files.
  std::map<std::string, std::ofstream> file_sinks;
  const auto emit_file = [&file_sinks](std::string_view file, std::string_view record) {
    const std::string name(file);
    auto it = file_sinks.find(name);
    if (it == file_sinks.end()) {
      // XFF_HOST_IO: open the explicit output path selected by the -fprint-family action.
      it = file_sinks.emplace(name, std::ofstream(name, std::ios::binary | std::ios::trunc)).first;
    }
    it->second.write(record.data(), static_cast<std::streamsize>(record.size()));
  };

  // -ls aligned output: each -ls row's cells feed a ColumnBuffer (per --buffer), whose
  // ready output is emitted as it forms and whose remainder is flushed after the walk.
  // Built once here so the computed column widths span the whole run. The visitor is
  // single-threaded, so the buffer needs no synchronisation.
  std::vector<format::Align> ls_aligns;
  std::vector<std::size_t> ls_mins;
  for (const LsColumn& column : LsColumns()) {
    ls_aligns.push_back(column.align);
    ls_mins.push_back(column.min_width);
  }
  const BufferBound ls_bound = ResolveBufferBound(command.globals, 100);  // -ls default: auto=100
  format::ColumnBuffer ls_buffer(std::move(ls_aligns), std::move(ls_mins), ls_bound.window, ls_bound.byte_budget);
  const auto emit_ls_row = [&ls_buffer, &emit](std::vector<std::string> cells) {
    if (const std::string ready = ls_buffer.Add(std::move(cells)); !ready.empty()) {
      emit(ready);
    }
  };

  // `-exec/-execdir ... +`: each batch node's matched items accrue here during the
  // walk and run at the end. Outer key the Expr node; inner key the directory ("" =
  // -exec's single global batch, the entry's dir = -execdir's per-dir batches). The
  // visitor is single-threaded, so no synchronisation is needed.
  ExecBatches exec_batches;

  // -j>1: `-exec/-execdir ... ;` children run concurrently on this bounded runner.
  // Its numeric cap matches the directory-read worker count, but the two pools
  // are independent. It is wired into the context only when workers > 1; at -j 1
  // (and the in-process default) the actions stay synchronous and this stays idle.
  exec::ParallelExec parallel_exec(options.workers);

  // Impossible-task policy (design.md "Exit-code model"): a predicate that cannot be
  // evaluated correctly on an entry's filesystem (e.g. -Btime where birth time is
  // unrecorded) signals via control.unsupported. By default that is a hard error
  // (exit 2); --skip-unsupported downgrades it to a warning and skips the entry.
  // Reported once per run (a representative path) rather than once per entry, so a
  // whole btime-less tree does not flood stderr.
  const bool skip_unsupported = HasGlobal(command.globals, "--skip-unsupported");
  bool unsupported_reported = false;

  // FS-native name matching (design.md "macOS / cross-platform correctness"; #45):
  // the xff style matches -name/-path the way the entry's own volume resolves
  // names, so a lookup the OS would satisfy case-insensitively (APFS/HFS+ default,
  // NTFS) also matches here. --exact opts out (verbatim byte-exact), and the find
  // style is always byte-exact (drop-in faithful). When active, each entry's volume
  // is probed once (cached by device id; the visitor is single-threaded, so the
  // cache needs no synchronisation), and a case-folding volume sets fold_name_case.
  const bool fs_native_case = style == registry::Style::kXff && !HasGlobal(command.globals, "--exact");
  absl::flat_hash_map<std::uint64_t, bool> case_sensitive_by_dev;
  // --hidden / --no-hidden (style-scoped default): whether to drop hidden dotfiles.
  const bool skip_hidden = ResolveSkipHidden(command.globals, style);

  // The header row (the column names, or the single default `path`) prints unless --no-header,
  // and only for the implicit path listing (not a --summary or explicit-action stream).
  const bool table_header_shown = implicit_print && !any_reduction && !HasGlobal(command.globals, "--no-header");

  // Buffered tabular output (--format=aligned/markdown) streams each match's cells through a
  // TableStream: it buffers up to the --buffer window (default all = full alignment) to size the
  // columns, then emits the header + rule + rows and streams the rest at the locked widths. The
  // visitor is single-threaded, so no lock.
  // --sort=score: the ranked listing. Only ENTRY output goes here - headers, dry-run previews,
  // -exec output and the summaries keep streaming, because they are not the listing being ranked
  // (and -exec has already run its command by then, so buffering its output would misreport when
  // it happened). Pairs are {score, rendered text}; entries the fuzzy matcher never scored sort
  // last, keeping them visible rather than dropping them.
  std::vector<std::pair<int, std::string>> ranked;
  // Entry output goes through here so ranking is one decision in one place rather than a condition
  // repeated at every print branch. Without --sort=score it is exactly `emit`.
  const auto emit_entry = [&ranked, &emit, rank_by_score, &fuzzy_score](std::string_view text) {
    if (rank_by_score) {
      ranked.emplace_back(fuzzy_score.value_or(std::numeric_limits<int>::min()), std::string(text));
    } else {
      emit(text);
    }
  };
  std::optional<render::TableStream> table_stream;
  if (buffered) {
    std::vector<std::string> table_columns = columns.empty() ? std::vector<std::string>{"path"} : columns;
    const BufferBound bound = ResolveBufferBound(command.globals, render::TableStream::kAll);  // tables: all
    table_stream.emplace(format, std::move(table_columns), table_header_shown, bound.window, bound.byte_budget);
  }

  // --format=tree splices each matched path into a shared-prefix structure (its ancestors become
  // branch nodes), rendered depth-first after the walk. --unicode picks box-drawing vs ASCII.
  std::optional<render::Tree> tree;
  if (is_tree) {
    tree.emplace(ResolveUnicode(command.globals));
  }

  // Streaming tabular (csv/tsv) emits its one-time header row here; the buffered formats emit
  // theirs inside TableStream, so Header() / EncodeTabularRow() return "" and this is a no-op.
  if (table_header_shown) {
    const std::string header = columns.empty() ? render::Renderer(format, path_encoding).Header()
                                               : render::EncodeTabularRow(format, columns);  // the column names
    if (!header.empty()) {
      emit(header);
    }
  }

  // The walk's whole view of archives: hand it a container path, get a filesystem over the members
  // or the InvalidArgument that means "an ordinary file after all". Passed unconditionally because
  // `options.archive` decides whether it is ever called.
  const auto mount_container = MakeContainerMounter(walk_fs, member_path_options, archive_any);
  std::size_t listed_results = 0;

  // Completes the run-level consequences of one fully evaluated entry. Deferred result-set
  // predicates (-top / -shard-status) call this after selection; ordinary entries call it from the walk.
  // Keeping it outside the visitor is what makes replay use precisely the same listing/reduction
  // path rather than a second, subtly different renderer.
  // NOLINTNEXTLINE(readability-function-cognitive-complexity): one extracted sink dispatch shared by walk and replay
  const auto finish_entry = [&](const Visit& visit, std::map<std::string, std::string>& outputs, bool matched,
                                std::optional<bool> verification) {
    if (matched) {
      any_match = true;
      if (matched_entry.has_value()) {
        (*matched_entry)(visit);
      }
    }
    const bool counted =
        *archive_aggregate == ArchiveAggregate::kBoth
        || (*archive_aggregate == ArchiveAggregate::kMembers && !visit.dived)
        || (*archive_aggregate == ArchiveAggregate::kContainer && visit.metadata.source != vfs::Source::kArchiveMember);
    if (counted) {
      FeedVerificationSummaries(summaries, summary_cells, visit, verification);
    }
    if (matched && any_reduction) {
      if (pack_target.has_value()) {
        if (visit.metadata.source == vfs::Source::kArchiveMember) {
          pack_saw_member = true;
        } else if (visit.path == visit.root && visit.metadata.type == vfs::FileType::kDirectory) {
          // The root directory itself is not an archive member.
        } else if (visit.name != pack_basename || PackIdentity(visit.path) != pack_identity) {
          pack_files.push_back({.source = std::string(visit.path), .name = PackMemberName(visit.path, visit.root)});
        }
      }
      if (shards.enabled) {
        const std::string_view path = visit.path;
        const std::string_view::size_type slash = path.rfind('/');
        const std::string_view dir = slash == std::string_view::npos ? std::string_view() : path.substr(0, slash);
        const std::string_view base = slash == std::string_view::npos ? path : path.substr(slash + 1);
        shard_buckets[std::string(dir)].push_back(
            {.name = std::string(base),
             .size = visit.metadata.size,
             .mode = visit.metadata.mode,
             .mtime = absl::ToUnixNanos(visit.metadata.mtime),
             .root = std::string(visit.root),
             .depth = visit.depth,
             .metadata = visit.metadata});
      }
      if (!shards.enabled && counted && !collections.Active()) {
        const std::string link;
        const fields::RenderContext key_ctx{
            .path = visit.path,
            .root = visit.root,
            .link_target = link,
            .metadata = visit.metadata,
            .depth = visit.depth,
            .fs = visit.fs.has_value() ? *visit.fs : walk_fs,
            .tz = tz,
            .time_format = time_format,
            .zone_suffix = zone_suffix,
            .hash_algorithm = hash_algorithm,
            .hash_encoding = hash_encoding,
            .defines = defines,
            .outputs = outputs,
            .fuzzy_score = fuzzy_score};
        FeedSummaries(summaries, summary_templates, summary_cells, key_ctx, visit);
        FeedHistograms(histograms, histogram_cells, visit, *visit.fs);
      }
    } else if (matched && implicit_print && (!max_results->has_value() || listed_results < **max_results)) {
      ++listed_results;
      const std::string_view entry_color = colorize ? palette.CodeFor(
                                                          visit.name, visit.metadata.type, visit.metadata.mode,
                                                          language_snapshot->TerminalColorForName(visit.name))
                                                    : std::string_view();
      if (is_tree) {
        tree->Add(visit.path);
      } else if (buffered && column_templates.empty() && !compiled_tmpl.has_value()) {
        emit(table_stream->Add({std::string(visit.path)}));
      } else if (!column_templates.empty() || compiled_tmpl.has_value()) {
        std::string link;
        if (visit.metadata.type == vfs::FileType::kSymlink) {
          if (const absl::StatusOr<std::string> target = walk_fs.ReadLink(visit.path); target.ok()) {
            link = *target;
          }
        }
        const fields::RenderContext ctx{
            .path = visit.path,
            .root = visit.root,
            .link_target = link,
            .metadata = visit.metadata,
            .depth = visit.depth,
            .fs = visit.fs.has_value() ? *visit.fs : walk_fs,
            .tz = tz,
            .time_format = time_format,
            .zone_suffix = zone_suffix,
            .hash_algorithm = hash_algorithm,
            .hash_encoding = hash_encoding,
            .defines = defines,
            .outputs = outputs,
            .fuzzy_score = fuzzy_score};
        if (!column_templates.empty()) {
          std::vector<std::string> cells;
          cells.reserve(column_templates.size());
          for (const fields::Template& column : column_templates) {
            cells.push_back(column.Render(ctx));
          }
          if (buffered) {
            emit(table_stream->Add(cells));
          } else {
            emit_entry(render::EncodeTabularRow(format, cells));
          }
        } else {
          emit_entry(compiled_tmpl->Render(ctx) + "\n");
        }
      } else {
        emit_entry(render::Renderer(format, path_encoding).Record(visit.path, entry_color));
      }
    }
  };

  std::vector<DeferredCandidate> deferred_candidates;
  std::size_t deferred_order = 0;
  std::vector<ExprIdentity> deferred_nodes;
  if (expression.has_value()) {
    AppendDeferredNodes(*expression, deferred_nodes);
  }
  std::map<ExprIdentity, std::size_t> deferred_node_order;
  for (std::size_t index = 0; index < deferred_nodes.size(); ++index) {
    deferred_node_order.emplace(deferred_nodes[index], index);
  }
  const absl::Status status = Walk(
      walk_fs, roots, options,
      // NOLINTNEXTLINE(readability-function-cognitive-complexity): cohesive dispatch
      [&](const Visit& visit) {
        // Hidden filter: unless hidden files are included, drop a dotfile (basename
        // starting with '.') before any evaluation or output. A hidden directory is
        // pruned (its whole subtree skipped); a hidden file is skipped. Depth 0 is an
        // explicitly named search root, always entered -- so `xff .git` still descends.
        if (skip_hidden && visit.depth > 0 && !visit.name.empty() && visit.name.front() == '.') {
          return visit.metadata.type == vfs::FileType::kDirectory ? WalkAction::kPrune : WalkAction::kContinue;
        }
        // VCS metadata filter: prune version-control plumbing directories (--skip-vcs; `-g` implies
        // `.git`), like ripgrep / fd. Git never lists `.git` in a .gitignore -- it excludes its own
        // plumbing implicitly -- so the ignore rules alone never drop it. At any depth and
        // deliberately independent of the hidden filter, so hidden files the user keeps (`.bazelrc`,
        // `.gitignore`, ...) still show; only VCS plumbing is dropped. Depth 0 is an explicitly named
        // root, always entered, so `xff .git` still descends. `skip_vcs_names` holds the metadata
        // names (`.git`, `.hg`, ...); it is empty (this filter off) unless --skip-vcs or -g is active.
        if (!skip_vcs_names.empty() && visit.depth > 0 && skip_vcs_names.contains(visit.name)) {
          return visit.metadata.type == vfs::FileType::kDirectory ? WalkAction::kPrune : WalkAction::kContinue;
        }
        // Ignore filter: drop an ignored entry before any evaluation or output. A
        // matched directory is pruned (its subtree is never walked, so this is also
        // the fast path); a matched file is simply skipped. The search root itself
        // (empty relative path) is never filtered -- the user named it. CLI
        // --exclude/--include has highest precedence, then explicit --ignore-file sources
        // (rooted at the file's own dir), then the auto per-directory ignore-file stack
        // (--ignore-files / -g); each later layer decides only where the earlier ones are silent.
        if (!ignore_patterns.empty() || rooted_ignore_files.Active() || ignore_files.Active()) {
          const bool is_dir = visit.metadata.type == vfs::FileType::kDirectory;
          const std::string_view rel = RelativeTo(visit.path, visit.root);
          if (!rel.empty()) {
            ignore::Decision decision = ignore_patterns.Match(rel, is_dir);
            if (decision == ignore::Decision::kDefault && rooted_ignore_files.Active()) {
              decision = rooted_ignore_files.Decide(AbsoluteDir(visit.path), is_dir);
            }
            if (decision == ignore::Decision::kDefault && ignore_files.Active()) {
              decision = ignore_files.Decide(visit.path, visit.root, is_dir);
            }
            if (decision == ignore::Decision::kIgnore) {
              return is_dir ? WalkAction::kPrune : WalkAction::kContinue;
            }
          }
        }
        Control control;
        std::vector<std::string> captures;           // -regex groups for this entry; consumed by gated -exec {0}..{N}
        std::map<std::string, std::string> outputs;  // -capture results for this entry; read by {capture.NAME}
        // FS-native name matching: fold -name/-path case on a case-folding volume
        // (xff style, no --exact). Probe each device once, defaulting to
        // case-sensitive (byte-exact) on the miss and on any probe error.
        bool fold_name_case = false;
        if (fs_native_case) {
          auto [it, inserted] = case_sensitive_by_dev.try_emplace(visit.metadata.dev, true);
          if (inserted) {
            it->second = walk_fs.IsCaseSensitive(visit.path).value_or(true);
          }
          fold_name_case = !it->second;
        }
        // The entry's colour, from the one palette this run resolved: used by the plain listing below
        // and by -ls's name column, so the two cannot disagree about what a file looks like.
        const std::string_view entry_color = colorize ? palette.CodeFor(
                                                            visit.name, visit.metadata.type, visit.metadata.mode,
                                                            language_snapshot->TerminalColorForName(visit.name))
                                                      : std::string_view();
        // -fuzzy / -ifuzzy leave their score here for {fuzzy}; cleared per entry so a name that runs
        // no fuzzy test renders empty rather than inheriting the previous entry's score.
        fuzzy_score.reset();
        hash_verification.reset();
        EvaluationMemo evaluation_memo;
        const DeferredDecisions deferred_results;
        DeferredEvaluation deferred{.decisions = deferred_results, .memo = evaluation_memo};
        EvalContext eval_context{
            .visit = visit,
            .emit = emit,
            .emit_file = emit_file,
            .emit_ls_row = emit_ls_row,
            .ls_color = entry_color,
            .ls_size_units = human,
            // The entry's OWN filesystem, so a predicate that reads a member reads it out of the
            // container rather than looking for `a.tar!x` on disk and finding nothing.
            .fs = visit.fs.has_value() ? *visit.fs : walk_fs,
            .now = now,
            .tz = tz,
            .time_format = time_format,
            .zone_suffix = zone_suffix,
            .block_size = block_size,
            .fold_name_case = fold_name_case,
            .fuzzy_score = fuzzy_score,
            .hash_verification = hash_verification_summary ? mbo::types::OptionalRef{hash_verification}
                                                           : mbo::types::OptionalRef<std::optional<bool>>{},
            .deferred = deferred,
            .grep_count = grep_count,
            .grep_before = grep_before,
            .grep_after = grep_after,
            .diff_algorithm = diff_algorithm,
            .diff_ignore = diff_ignore,
            .diff_ignore_matching = diff_ignore_matching,
            .diff_format = diff_format,
            .diff_context = diff_context,
            .hash_algorithm = hash_algorithm,
            .hash_encoding = hash_encoding,
            .control = control,
            .exec_fields = exec_fields,
            .captures =
                exec_fields ? mbo::types::OptionalRef{captures} : mbo::types::OptionalRef<std::vector<std::string>>{},
            .defines = defines,
            .outputs = outputs,
            .first_counts = first_counts,
            .collections = collections,
            .confirm = confirm,
            .exec_batches = exec_batches,
            .parallel_exec = options.workers > 1 ? mbo::types::OptionalRef{parallel_exec}
                                                 : mbo::types::OptionalRef<exec::ParallelExec>{},
            .extract = archive_extract ? mbo::types::OptionalRef{extracted_members}
                                       : mbo::types::OptionalRef<ExtractedMembers>{},
            .mounts = mounted_containers,
            .archive_deletions = archive_delete ? mbo::types::OptionalRef{archive_deletions}
                                                : mbo::types::OptionalRef<std::vector<std::string>>{},
        };
        const EvaluationResult evaluated =
            !expression.has_value() ? EvaluationResult{.matched = true} : EvaluateDeferred(*expression, eval_context);
        if (evaluated.deferred) {
          deferred_candidates.push_back(
              {.entry = OwnVisit(visit),
               .captures = std::move(captures),
               .outputs = std::move(outputs),
               .memo = std::move(evaluation_memo),
               .decisions = {},
               .hash_verification = hash_verification,
               .waiting_at = evaluated.waiting_at.value(),
               .score = evaluated.fuzzy.value_or(0),
               .order = deferred_order++});
        } else {
          finish_entry(visit, outputs, evaluated.matched, hash_verification);
        }
        if (!control.unsupported.empty() && !unsupported_reported) {
          unsupported_reported = true;  // once per run, not per entry
          if (skip_unsupported) {
            on_error(visit.path, absl::FailedPreconditionError(absl::StrCat(control.unsupported, " (skipped)")));
          } else {
            on_error(
                visit.path, absl::FailedPreconditionError(
                                absl::StrCat(control.unsupported, "; use --skip-unsupported to skip such entries")));
            ++errors;  // impossible task -> hard error (exit 2)
          }
        }
        if (control.quit) {
          return WalkAction::kStop;
        }
        if (control.prune) {
          return WalkAction::kPrune;
        }
        return WalkAction::kContinue;
      },
      [&](std::string_view path, absl::Status error_status) {
        ++errors;
        on_error(path, error_status);
      },
      mount_container);
  if (!status.ok()) {
    ++errors;  // Fatal traversal error (none today; per-path errors handled above).
  }

  // Resolve one deferred frontier at a time. A decision may expose another result-set predicate
  // farther right; replay stops there and the next round resolves that node's independent cohort.
  // Completed-prefix memo entries keep stateful tests and actions before each frontier single-shot.
  while (!deferred_candidates.empty()) {
    const auto scheme_allowed = [&](shard::Scheme scheme) {
      return scheme == shard::Scheme::kCustom || shards.schemes.empty()
             || absl::c_linear_search(shards.schemes, scheme);
    };
    errors += ResolveDeferredRound(
        deferred_candidates, deferred_node_order, *shard_matcher, shard_dedup, scheme_allowed,
        /*report_dedup_errors=*/!shards.enabled);
    std::vector<DeferredCandidate> next_round;
    next_round.reserve(deferred_candidates.size());
    for (DeferredCandidate& candidate : deferred_candidates) {
      const Visit visit = candidate.entry.AsVisit();
      Control control;
      bool fold_name_case = false;
      if (fs_native_case) {
        auto [it, inserted] = case_sensitive_by_dev.try_emplace(visit.metadata.dev, true);
        if (inserted) {
          it->second = walk_fs.IsCaseSensitive(visit.path).value_or(true);
        }
        fold_name_case = !it->second;
      }
      const std::string_view entry_color = colorize ? palette.CodeFor(
                                                          visit.name, visit.metadata.type, visit.metadata.mode,
                                                          language_snapshot->TerminalColorForName(visit.name))
                                                    : std::string_view();
      fuzzy_score.reset();
      DeferredEvaluation deferred{.decisions = candidate.decisions, .memo = candidate.memo};
      EvalContext eval_context{
          .visit = visit,
          .emit = emit,
          .emit_file = emit_file,
          .emit_ls_row = emit_ls_row,
          .ls_color = entry_color,
          .ls_size_units = human,
          .fs = visit.fs.has_value() ? *visit.fs : walk_fs,
          .now = now,
          .tz = tz,
          .time_format = time_format,
          .zone_suffix = zone_suffix,
          .block_size = block_size,
          .fold_name_case = fold_name_case,
          .fuzzy_score = fuzzy_score,
          .hash_verification = hash_verification_summary ? mbo::types::OptionalRef{candidate.hash_verification}
                                                         : mbo::types::OptionalRef<std::optional<bool>>{},
          .deferred = deferred,
          .grep_count = grep_count,
          .grep_before = grep_before,
          .grep_after = grep_after,
          .diff_algorithm = diff_algorithm,
          .diff_ignore = diff_ignore,
          .diff_ignore_matching = diff_ignore_matching,
          .diff_format = diff_format,
          .diff_context = diff_context,
          .hash_algorithm = hash_algorithm,
          .hash_encoding = hash_encoding,
          .control = control,
          .exec_fields = exec_fields,
          .captures = exec_fields ? mbo::types::OptionalRef{candidate.captures}
                                  : mbo::types::OptionalRef<std::vector<std::string>>{},
          .defines = defines,
          .outputs = candidate.outputs,
          .first_counts = first_counts,
          .collections = collections,
          .confirm = confirm,
          .exec_batches = exec_batches,
          .parallel_exec = options.workers > 1 ? mbo::types::OptionalRef{parallel_exec}
                                               : mbo::types::OptionalRef<exec::ParallelExec>{},
          .extract = archive_extract ? mbo::types::OptionalRef{extracted_members}
                                     : mbo::types::OptionalRef<ExtractedMembers>{},
          .mounts = mounted_containers,
          .archive_deletions = archive_delete ? mbo::types::OptionalRef{archive_deletions}
                                              : mbo::types::OptionalRef<std::vector<std::string>>{},
      };
      const EvaluationResult evaluated = EvaluateDeferred(*expression, eval_context);
      if (evaluated.deferred) {
        candidate.waiting_at = evaluated.waiting_at.value();
        candidate.score = evaluated.fuzzy.value_or(0);
        next_round.push_back(std::move(candidate));
      } else {
        finish_entry(visit, candidate.outputs, evaluated.matched, candidate.hash_verification);
      }
      if (!control.unsupported.empty() && !unsupported_reported) {
        unsupported_reported = true;
        if (skip_unsupported) {
          on_error(visit.path, absl::FailedPreconditionError(absl::StrCat(control.unsupported, " (skipped)")));
        } else {
          on_error(
              visit.path, absl::FailedPreconditionError(
                              absl::StrCat(control.unsupported, "; use --skip-unsupported to skip such entries")));
          ++errors;
        }
      }
    }
    deferred_candidates = std::move(next_round);
  }

  // Flush any -ls rows still buffered for alignment (a run shorter than the --buffer
  // window, or --buffer=all). No-op when -ls was not used or nothing remains.
  if (const std::string ls_tail = ls_buffer.Flush(); !ls_tail.empty()) {
    emit(ls_tail);
  }

  // Flush the buffered tabular formats (--format=aligned/markdown): emit whatever the
  // TableStream still holds (a run shorter than the --buffer window, or --buffer=all), plus a
  // header-only table when nothing matched. A no-op once the window already streamed everything.
  if (rank_by_score) {
    EmitRanked(ranked, emit);
  }
  if (table_stream.has_value()) {
    if (const std::string table = table_stream->Flush(); !table.empty()) {
      emit(table);
    }
  }

  // Render the tree (--format=tree) now that every matched path has been spliced in.
  if (tree.has_value()) {
    if (const std::string rendered = tree->Render(); !rendered.empty()) {
      emit(rendered);
    }
  }

  // -j>1: reap every concurrent `-exec/-execdir ... ;` child still running. find's
  // `;` form is a predicate -- a nonzero exit makes only the action false, it does
  // NOT affect find's exit status (verified against BSD/GNU find) -- so the drained
  // failure count is intentionally discarded here, keeping -j N identical to the
  // synchronous -j 1 path. The `+` batch form is the one that does count failures,
  // and it runs through exec_batches just below. A no-op when nothing was launched.
  parallel_exec.Drain();

  // `-exec/-execdir ... +`: now that the walk is done, run each batch node's
  // accumulated items in ARG_MAX chunks -- -exec once over the global ("") bucket,
  // -execdir once per directory bucket (cwd = that dir). A nonzero exit is a
  // per-command error, as for `;`.
  for (const auto& [node, by_dir] : exec_batches) {
    const parser::Expr& expr = node.Get();
    const bool execdir = expr.descriptor->name == "-execdir";
    for (const auto& [dir, items] : by_dir) {
      const bool ok = execdir ? exec::ExecuteBatchInDir(expr.args, items, dir) : exec::ExecuteBatch(expr.args, items);
      if (!ok) {
        ++errors;
        on_error(expr.descriptor->name, absl::UnknownError("batched command exited non-zero"));
      }
    }
  }

  // --archive-mount: say ONCE that mounting could not happen, so a run that silently extracted
  // instead is not mistaken for one that mounted. Not an error: falling back is the flag's
  // documented behaviour, which is what makes it safe to keep in a config file.
  if (!mounted_containers.DegradeReason().empty()) {
    std::cerr << absl::StreamFormat("xff: --archive-mount: %s\n", mounted_containers.DegradeReason());
  }

  // --archive-delete: rewrite each container without the members `-delete` matched. Grouped by
  // container and applied once per container, so an archive is rewritten a single time however many
  // of its members matched - and only now, with the walk (and its open readers) finished.
  if (!archive_deletions.empty()) {
    errors += FlushArchiveDeletions(archive_deletions, member_path_options, dry_run, emit, on_error);
  }

  // --pack: write the archive, now that the walk has produced every member and released the readers
  // it held. All or nothing by construction (the writer renames into place), so an error here leaves
  // no file behind and an existing one untouched.
  if (pack_target.has_value()) {
    if (pack_saw_member) {
      ++errors;
      on_error(
          "--pack", absl::UnimplementedError(
                        absl::StrCat(
                            "refusing to write ", *pack_target,
                            ": the expression matched a member of another archive, and re-packing members is not"
                            " supported yet; narrow the expression or turn diving off with -z-")));
    } else if (dry_run) {
      emit(absl::StrCat("would pack ", pack_files.size(), " entries into ", *pack_target, "\n"));
    } else {
      // Kept out of the `else if` condition: an init-statement with an initializer this long is the
      // one construct the pinned and the hermetic clang-format lay out differently, so each undoes
      // the other's work.
      const absl::Status packed =
          archive::PackContainer(*pack_target, pack_files, archive::PackOptions{.options = *pack_options});
      if (!packed.ok()) {
        ++errors;
        on_error("--pack", packed);
      }
    }
  }

  // --shards: group each directory's matches into logical sets once (reused by the listing below).
  // When --summary / --histogram are also active, each set feeds them as ONE aggregated unit (its
  // size is the set total, {shard} its shard count) and each non-shard file as itself, so the
  // reductions aggregate per logical set. The walk skipped the per-file feed in shard mode.
  struct GroupedDir {
    std::string prefix;
    std::vector<shard::ShardSet> sets;
    std::vector<std::string> passthrough;  // non-shard basenames (owned), sorted
  };

  std::vector<GroupedDir> shard_groups;
  if (shards.enabled) {
    const auto scheme_allowed = [&](shard::Scheme scheme) {
      return scheme == shard::Scheme::kCustom || shards.schemes.empty()
             || absl::c_linear_search(shards.schemes, scheme);
    };
    // Accumulate one logical unit into the summary / histogram sinks, mirroring the per-file feed the
    // walk skips in shard mode. `visit` carries the unit's size (a set's total); `shard_count`
    // populates {shard} (nullopt for a non-shard file). A kLines histogram reads the representative
    // only (per-set line summing is a v2 concern with the reassembled view).
    const auto feed_summaries = [&](const fields::RenderContext& key_ctx, const Visit& visit) {
      FeedSummaries(summaries, summary_templates, summary_cells, key_ctx, visit);
    };
    const auto feed_histograms = [&](const Visit& visit) {
      FeedHistograms(histograms, histogram_cells, visit, *visit.fs);
    };
    // Feed one logical unit (a set, or a non-shard file) into both sinks; `shard_count` -> {shard}.
    const auto feed_unit = [&](const Visit& visit, std::optional<std::int64_t> shard_count) {
      const std::string link;
      const fields::RenderContext key_ctx{
          .path = visit.path,
          .root = visit.root,
          .link_target = link,
          .metadata = visit.metadata,
          .depth = visit.depth,
          .fs = visit.fs.has_value() ? *visit.fs : walk_fs,
          .tz = tz,
          .time_format = time_format,
          .zone_suffix = zone_suffix,
          .hash_algorithm = hash_algorithm,
          .hash_encoding = hash_encoding,
          .defines = defines,
          .shard_count = shard_count};
      feed_summaries(key_ctx, visit);
      feed_histograms(visit);
    };
    const bool feed = !summaries.empty() || !histograms.empty();
    for (const auto& [dir, files] : shard_buckets) {
      GroupedDir group;
      group.prefix = dir.empty() ? std::string() : absl::StrCat(dir, "/");
      std::vector<shard::ShardFile> shard_files;
      std::map<std::string_view, const ShardBufFile*> by_name;
      for (const ShardBufFile& file : files) {
        by_name.emplace(file.name, &file);
        const std::optional<shard::Match> match = shard_matcher->Decode(file.name);
        if (file.metadata.type == vfs::FileType::kRegular && match.has_value() && scheme_allowed(match->scheme)) {
          shard_files.push_back({.name = file.name, .size = file.size, .mode = file.mode, .mtime = file.mtime});
        } else {
          group.passthrough.push_back(file.name);
        }
      }
      group.sets = shard::GroupShards(shard_files, *shard_matcher, shard_dedup);
      const auto synth = [&walk_fs](const ShardBufFile& rec, std::string_view path, const vfs::Metadata& md) {
        return Visit{
            .path = path, .name = rec.name, .root = rec.root, .depth = rec.depth, .metadata = md, .fs = walk_fs};
      };
      for (const shard::ShardSet& set : group.sets) {
        // --shards-dedup=error: a same-index duplicate is ambiguous, so report it and fail the run.
        if (shard_dedup == shard::Dedup::kError) {
          errors += ReportShardDuplicateErrors(set, group.prefix);
        }
        if (feed) {
          const ShardBufFile& rec = *by_name.at(ShardRepresentativePath(set));
          vfs::Metadata md = rec.metadata;
          md.size = set.total_size;  // {size} and size-based buckets aggregate across the set
          const std::string path = absl::StrCat(group.prefix, rec.name);
          feed_unit(synth(rec, path, md), static_cast<std::int64_t>(set.members.size()));
        }
      }
      if (feed) {
        for (const std::string& name : group.passthrough) {
          const ShardBufFile& rec = *by_name.at(name);
          const std::string path = absl::StrCat(group.prefix, rec.name);
          feed_unit(synth(rec, path, rec.metadata), std::nullopt);
        }
      }
      absl::c_sort(group.passthrough);
      shard_groups.push_back(std::move(group));
    }
  }

  if (const int collect_status = FinishCollections(
          collections, on_error, summaries, summary_templates, summary_cells, histograms, histogram_cells,
          CollectionRenderDefaults{
              .fs = walk_fs,
              .tz = tz,
              .time_format = time_format,
              .zone_suffix = zone_suffix,
              .hash_algorithm = hash_algorithm,
              .hash_encoding = hash_encoding,
              .defines = defines,
          });
      collect_status != 0) {
    return RunResult{.errors = collect_status, .any_match = any_match};
  }

  // --summary: emit one accumulated table per sink -- a row per group (the map is ordered) plus a
  // `total` row (the overall mode is already a single "total" group). Default is a right-aligned
  // human table (grouped digits); --format=jsonl emits one machine object per row. Multiple --summary
  // flags print in order, separated by a blank line. --top / --summary-precision are global and apply
  // to every table.
  if (!summaries.empty()) {
    const std::optional<std::size_t> summary_top = ResolveTop(command.globals);
    const unsigned precision = ResolveSummaryPrecision(command.globals);

    struct Row {
      std::string key;
      std::uint64_t count = 0;
      std::uint64_t size = 0;
    };

    // The human size table: label left, grouped count, then the size as a right-aligned number and a
    // left-aligned unit (so decimal points line up). Extracted so emit_summary just dispatches.
    const auto emit_sized_table = [&](const std::vector<Row>& rows) {
      std::vector<format::SizeParts> sizes;
      sizes.reserve(rows.size());
      std::size_t number_width = 0;
      for (const Row& row : rows) {
        format::SizeParts parts = format::SizeColumns(row.size, *human, precision);
        number_width = std::max(number_width, parts.number.size());
        sizes.push_back(std::move(parts));
      }
      format::Table table({format::Align::kLeft, format::Align::kRight, format::Align::kLeft});
      for (std::size_t i = 0; i < rows.size(); ++i) {
        table.AddRow(
            {rows[i].key, format::Int(rows[i].count, ','),
             absl::StrCat(format::PadLeft(sizes[i].number, number_width), " ", sizes[i].suffix)});
      }
      emit(table.Render());
    };
    const auto emit_summary = [&](SummaryMode mode,
                                  const std::map<std::string, std::pair<std::uint64_t, std::uint64_t>>& cells) {
      std::vector<Row> rows;
      std::uint64_t total_count = 0;
      std::uint64_t total_size = 0;
      for (const auto& [key, agg] : cells) {
        rows.push_back(Row{.key = key, .count = agg.first, .size = agg.second});
        total_count += agg.first;
        total_size += agg.second;
      }
      // Some summaries carry no size dimension (an m// extraction tallies keys, never bytes); a
      // total of zero means there is nothing size-worthy to show, so drop the size column / field
      // rather than print a spurious `0 B` (or a `bytes:0` jsonl field).
      const bool has_size = total_size > 0;
      // --top=N: keep the N largest groups by size (count, then key, break ties); the total row
      // still reflects every matched group. Absent => all groups in the map's alphabetical order.
      if (summary_top.has_value() && mode != SummaryMode::kOverall) {
        absl::c_sort(rows, [](const Row& lhs, const Row& rhs) {
          if (lhs.size != rhs.size) {
            return lhs.size > rhs.size;
          }
          if (lhs.count != rhs.count) {
            return lhs.count > rhs.count;
          }
          return lhs.key < rhs.key;
        });
        if (rows.size() > *summary_top) {
          rows.resize(*summary_top);
        }
      }
      if (mode != SummaryMode::kOverall) {
        rows.push_back(Row{.key = "total", .count = total_count, .size = total_size});
      }
      if (format == render::Format::kJsonl) {
        for (const Row& row : rows) {
          std::string obj = absl::StrCat("{\"group\":", JsonQuote(row.key), ",\"count\":", row.count);
          if (has_size) {
            absl::StrAppend(&obj, ",\"bytes\":", row.size);
          }
          absl::StrAppend(&obj, "}\n");
          emit(obj);
        }
      } else if (!has_size) {
        // Count-only: a two-column label / count table (no size dimension to report).
        format::Table table({format::Align::kLeft, format::Align::kRight});
        for (const Row& row : rows) {
          table.AddRow({row.key, format::Int(row.count, ',')});
        }
        emit(table.Render());
      } else if (human.has_value()) {
        emit_sized_table(rows);
      } else {
        format::Table table({format::Align::kLeft, format::Align::kRight, format::Align::kRight});
        for (const Row& row : rows) {
          table.AddRow({row.key, format::Int(row.count, ','), format::Int(row.size, ',')});
        }
        emit(table.Render());
      }
    };
    for (std::size_t i = 0; i < summaries.size(); ++i) {
      if (i > 0 && format != render::Format::kJsonl) {
        emit("\n");  // blank line between consecutive tables (jsonl streams objects, no separator)
      }
      emit_summary(summaries[i].mode, summary_cells[i]);
    }
  }

  // --histogram: after any --summary table, emit each histogram's bars (or jsonl rows), in the
  // order the flags were given. Bars scale to the tallest bucket; --top keeps the N tallest.
  // Unicode block bars on a UTF-8 locale (--unicode), ASCII '#' otherwise.
  if (!histograms.empty()) {
    const bool unicode = ResolveUnicode(command.globals);
    const std::optional<std::size_t> top = ResolveTop(command.globals);
    const unsigned precision = ResolveSummaryPrecision(command.globals);
    const std::size_t bar_width = ResolveHistogramWidth(command.globals);

    struct Bar {
      std::string label;
      HistValue value;
    };

    for (std::size_t i = 0; i < histograms.size(); ++i) {
      const bool numeric = IsNumericBucket(histograms[i].bucket);
      std::vector<Bar> bars;
      double max_scale = 0;
      // The map iterates in key order, i.e. the ascending range order for a numeric bucket.
      for (const auto& [key, cell] : histogram_cells[i]) {
        HistValue value = HistMeasureValue(histograms[i].agg, cell, precision);
        max_scale = std::max(max_scale, value.scale);
        bars.push_back(Bar{.label = cell.label, .value = std::move(value)});
      }
      // A categorical bucket sorts by bar height (tallest first) and honors --top; a numeric-range
      // bucket keeps the ascending range order (a distribution) and shows every range.
      if (!numeric) {
        absl::c_sort(bars, [](const Bar& lhs, const Bar& rhs) {
          if (lhs.value.scale > rhs.value.scale) {
            return true;
          }
          if (lhs.value.scale < rhs.value.scale) {
            return false;
          }
          return lhs.label < rhs.label;
        });
        if (top.has_value() && bars.size() > *top) {
          bars.resize(*top);
        }
      }
      if (format == render::Format::kJsonl) {
        for (const Bar& bar : bars) {
          emit(
              absl::StrCat(
                  "{\"histogram\":", JsonQuote(histograms[i].label), ",\"bucket\":", JsonQuote(bar.label),
                  ",\"value\":", bar.value.json, "}\n"));
        }
        continue;
      }
      // Text bars: the label left-padded to the widest, the value right-aligned, then the bar. The
      // bar is last so its Unicode width never disturbs the aligned columns.
      std::size_t label_width = 0;
      std::size_t value_width = 0;
      for (const Bar& bar : bars) {
        label_width = std::max(label_width, bar.label.size());
        value_width = std::max(value_width, bar.value.text.size());
      }
      for (const Bar& bar : bars) {
        const double fraction = max_scale == 0 ? 0.0 : bar.value.scale / max_scale;
        emit(
            absl::StrCat(
                bar.label, std::string(label_width - bar.label.size(), ' '), "  ",
                format::PadLeft(bar.value.text, value_width), "  ", HistogramBar(fraction, bar_width, unicode), "\n"));
      }
    }
  }

  // --shards: collapse each directory's matched files into logical shard sets (one line per set) and
  // list the non-shard matches unchanged. A file whose scheme is not selected is treated as non-shard.
  // --shards-show picks each set's line (representative path / wildcard / wildcard + count), and an
  // incomplete set is annotated `(present/expected - INCOMPLETE)`; see RenderShardSet.
  if (!shards.enabled) {
    return RunResult{.errors = errors, .any_match = any_match};
  }
  // The one-line listing prints only when no --summary / --histogram is active; those aggregate the
  // sets and are the terminal output (like --summary replacing the plain listing). The sets were
  // grouped once above (shard_groups), so this just renders them per --shards-show and lists the
  // non-shard matches unchanged.
  if (!summaries.empty() || !histograms.empty()) {
    return RunResult{.errors = errors, .any_match = any_match};
  }
  for (const GroupedDir& group : shard_groups) {
    for (const shard::ShardSet& set : group.sets) {
      emit(absl::StrCat(RenderShardSet(set, group.prefix, shard_show), "\n"));
    }
    for (const std::string& name : group.passthrough) {  // already sorted
      emit(absl::StrCat(group.prefix, name, "\n"));
    }
  }
  return RunResult{.errors = errors, .any_match = any_match};
}

}  // namespace

RunResult RunFind(
    const parser::Command& command,
    const vfs::FileSystem& fs,
    EmitFn emit,
    WalkErrorFn on_error,
    std::optional<registry::Style> style) {
  if (absl::c_any_of(command.globals, [](std::string_view global) {
        return global == "--compare" || global.starts_with("--compare=");
      })) {
    return RunTreeCompare(command, fs, emit, on_error, style);
  }
  return RunFindCore(
      command, command.roots, fs, emit, on_error, style, mbo::types::OptionalRef<const MatchedEntryFn>{},
      /*compare_listing=*/false);
}

namespace {

// Display strings for the flavor feature-map, one per resolver's value type.
std::string GitignoreName(GitignoreMode mode) {
  switch (mode) {
    case GitignoreMode::kAuto: return "auto";
    case GitignoreMode::kOff: return "off";
    case GitignoreMode::kOn: return "on";
  }
  return "off";
}

std::string HiddenName(bool skip) {
  return skip ? "skip" : "show";
}

std::string HumanName(std::optional<format::SizeUnits> units) {
  if (!units.has_value()) {
    return "bytes";
  }
  return *units == format::SizeUnits::kSi ? "si" : "iec";
}

std::string SortName(SortOrder order) {
  switch (order) {
    case SortOrder::kNone: return "none";
    case SortOrder::kDir: return "per-dir";
    case SortOrder::kSubtree: return "subtree";
    case SortOrder::kTree: return "tree";
    case SortOrder::kRoots: return "roots";
    case SortOrder::kGlobal: return "global";
  }
  return "none";
}

std::string CaseName(parser::CaseMode mode) {
  switch (mode) {
    case parser::CaseMode::kInsensitive: return "insensitive";
    case parser::CaseMode::kSensitive: return "sensitive";
    case parser::CaseMode::kSmart: return "smart";
  }
  return "sensitive";
}

constexpr auto kFlavorFacets = std::to_array<FlavorFacet>({
    {.behavior = "ignore files (.gitignore/.ignore)",
     .flag = "-g / --gitignore, --no-ignore",
     .value = [](const std::vector<std::string>& globals,
                 registry::Style style) { return GitignoreName(ResolveGitignoreMode(globals, style)); }},
    {.behavior = "hidden dotfiles",
     .flag = "--hidden / --no-hidden",
     .value = [](const std::vector<std::string>& globals,
                 registry::Style style) { return HiddenName(ResolveSkipHidden(globals, style)); }},
    {.behavior = "sizes",
     .flag = "--human",
     .value = [](const std::vector<std::string>& globals,
                 registry::Style style) { return HumanName(ResolveHuman(globals, style)); }},
    {.behavior = "traversal order",
     .flag = "--sort",
     .value = [](const std::vector<std::string>& globals,
                 registry::Style style) { return SortName(ResolveSort(globals, style)); }},
    {.behavior = "letter case",
     .flag = "--case, -i, -s[+|-]",
     .value = [](const std::vector<std::string>& globals,
                 registry::Style style) { return CaseName(parser::ResolveCaseMode(globals, style)); }},
});

}  // namespace

absl::Span<const FlavorFacet> FlavorFacets() {
  return kFlavorFacets;
}

}  // namespace xff::engine
