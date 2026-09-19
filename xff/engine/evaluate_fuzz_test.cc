// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "xff/engine/evaluate.h"
#include "xff/parser/ast.h"
#include "xff/parser/parser.h"
#include "xff/registry/descriptor.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace {

constexpr std::size_t kMaxInputBytes = 16UZ * 1'024;
constexpr auto kFileTypes = std::to_array<xff::vfs::FileType>({
    xff::vfs::FileType::kRegular,
    xff::vfs::FileType::kDirectory,
    xff::vfs::FileType::kSymlink,
});

std::vector<std::string> DecodeArgs(std::string_view input) {
  std::vector<std::string> args;
  while (!input.empty()) {
    const std::size_t separator = input.find('\n');
    if (separator == std::string_view::npos) {
      args.emplace_back(input);
      break;
    }
    args.emplace_back(input.substr(0, separator));
    input.remove_prefix(separator + 1);
  }
  return args;
}

bool IsIsolated(const xff::parser::Expr& expr) {
  if (expr.kind == xff::parser::Expr::Kind::kPredicate
      && (!expr.descriptor.has_value() || expr.descriptor->safety != xff::registry::Safety::kNone
          || expr.descriptor->writes_file)) {
    return false;
  }
  return (expr.lhs == nullptr || IsIsolated(*expr.lhs)) && (expr.rhs == nullptr || IsIsolated(*expr.rhs));
}

class ReadOnlyFuzzFs final : public xff::vfs::FileSystem {
 public:
  ReadOnlyFuzzFs(std::string content, const xff::vfs::Metadata& metadata)
      : content_(std::move(content)), metadata_(metadata) {}

  absl::StatusOr<std::vector<xff::vfs::Entry>> ReadDir(std::string_view) const override {
    return std::vector<xff::vfs::Entry>{};
  }

  absl::StatusOr<xff::vfs::Metadata> Stat(std::string_view, bool) const override { return metadata_; }

  absl::Status Remove(std::string_view) const override {
    // A safety-classification regression must fail the harness rather than mutate any filesystem.
    std::abort();
  }

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

  absl::StatusOr<std::string> FsType(std::string_view) const override { return std::string("fuzzfs"); }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

  absl::StatusOr<std::string> ReadContent(std::string_view) const override { return content_; }

 private:
  std::string content_;
  xff::vfs::Metadata metadata_;
};

void EvaluateForType(const xff::parser::Expr& expression, std::string_view input, xff::vfs::FileType type) {
  const absl::Time now = absl::UnixEpoch() + absl::Hours(24 * 20'000);
  const xff::vfs::Metadata metadata{
      .type = type,
      .size = input.size(),
      .blocks = (input.size() + 511) / 512,
      .mode = 0644,
      .nlink = 1,
      .uid = 1'000,
      .gid = 1'000,
      .ino = 42,
      .dev = 7,
      .atime = now - absl::Hours(1),
      .mtime = now - absl::Hours(2),
      .ctime = now - absl::Hours(3),
      .btime = now - absl::Hours(4),
  };
  const ReadOnlyFuzzFs fs(std::string(input), metadata);
  const xff::engine::Visit visit{
      .path = "root/sample.txt",
      .name = "sample.txt",
      .root = "root",
      .depth = 1,
      .metadata = metadata,
      .fs = fs,
  };
  xff::engine::Control control;
  const auto discard = [](std::string_view) {};
  xff::engine::EvalContext context{
      .visit = visit,
      .emit = discard,
      .fs = fs,
      .now = now,
      .control = control,
  };
  static_cast<void>(xff::engine::EvaluateDeferred(expression, context));
}

}  // namespace

// XFF_ABI_POINTER: rules_fuzzing requires libFuzzer's C entry-point signature.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size > kMaxInputBytes) {
    return 0;
  }
  // libFuzzer exposes object-representation bytes; the parser and synthetic file consume the same
  // bounded byte sequence without borrowing host paths or resources.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const std::string_view input(reinterpret_cast<const char*>(data), size);
  const auto command = xff::parser::Parse(DecodeArgs(input));
  if (!command.ok() || command->expression == nullptr || !IsIsolated(*command->expression)) {
    return 0;
  }
  for (const xff::vfs::FileType type : kFileTypes) {
    EvaluateForType(*command->expression, input, type);
  }
  return 0;
}
