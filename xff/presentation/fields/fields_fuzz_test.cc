// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "xff/hash/hash.h"
#include "xff/presentation/fields/fields.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace {

// Every path, including absolute and traversal-shaped fuzz input, resolves only
// to this synthetic content. An accidental mutation is a harness failure.
class FieldFuzzFs final : public xff::vfs::FileSystem {
 public:
  FieldFuzzFs(std::string_view content, const xff::vfs::Metadata& metadata) : content_(content), metadata_(metadata) {}

  absl::StatusOr<std::vector<xff::vfs::Entry>> ReadDir(std::string_view) const override {
    return std::vector<xff::vfs::Entry>{};
  }

  absl::StatusOr<xff::vfs::Metadata> Stat(std::string_view, bool) const override { return metadata_; }

  absl::Status Remove(std::string_view) const override { std::abort(); }

  absl::Status WriteContent(std::string_view, std::string_view) const override { std::abort(); }

  absl::StatusOr<std::unique_ptr<xff::vfs::OutputFile>> OpenOutput(std::string_view, bool) const override {
    std::abort();
  }

  absl::Status RemoveControlled(std::string_view, const xff::vfs::MutationPolicy&) const override { std::abort(); }

  absl::StatusOr<std::unique_ptr<xff::vfs::OutputFile>> OpenControlledOutput(
      std::string_view,
      bool,
      const xff::vfs::MutationPolicy&) const override {
    std::abort();
  }

  bool Access(std::string_view, xff::vfs::AccessMode mode) const override {
    return mode == xff::vfs::AccessMode::kRead;
  }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override { return std::string("target"); }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return std::string("field-fuzz"); }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

  absl::StatusOr<std::string> ReadContent(std::string_view) const override { return std::string(content_); }

 private:
  std::string_view content_;
  const xff::vfs::Metadata& metadata_;
};

struct Input {
  std::string_view tmpl;
  std::string_view path;
  std::string_view captured;
};

Input DecodeInput(std::string_view input) {
  const std::size_t first = input.find('\n');
  if (first == std::string_view::npos) {
    return {.tmpl = input};
  }
  const std::size_t second = input.find('\n', first + 1);
  if (second == std::string_view::npos) {
    return {.tmpl = input.substr(0, first), .path = input.substr(first + 1)};
  }
  return {
      .tmpl = input.substr(0, first),
      .path = input.substr(first + 1, second - first - 1),
      .captured = input.substr(second + 1),
  };
}

}  // namespace

// XFF_ABI_POINTER: rules_fuzzing requires libFuzzer's C entry-point signature.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  // libFuzzer exposes object-representation bytes; template literals and paths may contain arbitrary
  // non-NUL bytes, while malformed field syntax must remain safe too.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const Input input = DecodeInput(std::string_view(reinterpret_cast<const char*>(data), size));

  xff::vfs::Metadata metadata;
  metadata.type = xff::vfs::FileType::kRegular;
  metadata.size = input.captured.size();
  metadata.atime = absl::UnixEpoch();
  metadata.mtime = absl::UnixEpoch();
  metadata.ctime = absl::UnixEpoch();
  metadata.btime = absl::UnixEpoch();
  const std::vector<std::string> captures = {std::string(input.captured)};
  const std::map<std::string, std::string> values = {{"value", std::string(input.captured)}};
  const std::string path(input.path);
  const FieldFuzzFs filesystem(input.captured, metadata);
  const xff::fields::RenderContext context{
      .path = path,
      .root = "/field-fuzz",
      .metadata = metadata,
      .depth = 1,
      .fs = filesystem,
      .tz = absl::UTCTimeZone(),
      .captures = captures,
      .defines = values,
      .outputs = values,
      .line_number = 1,
      .line_text = input.captured,
      .match_text = input.captured,
      .match_column = 1,
      .shard_count = 1,
      .fuzzy_score = 100,
  };

  const xff::fields::Template compiled = xff::fields::Template::Compile(input.tmpl);
  const std::string rendered = compiled.Render(context);
  // The corpus contains exact hash templates with host-shaped paths. Check
  // routing for those cases without adding two hashes to every fuzz iteration.
  if (input.tmpl == "{hash}") {
    const auto expected_hash = xff::hash::HashData("sha256", input.captured);
    if (!expected_hash.has_value() || rendered != *expected_hash) {
      std::abort();
    }
  }
  const std::optional<std::vector<std::string>> extraction = compiled.AsExtraction(context);
  if (compiled.IsExtraction() != extraction.has_value()) {
    std::abort();
  }
  if (compiled.IsExtraction() && !compiled.HasUnreducedExtraction()) {
    std::abort();
  }
  if (rendered != compiled.Render(context)) {
    std::abort();
  }
  return 0;
}
