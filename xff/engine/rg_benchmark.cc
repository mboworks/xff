// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "benchmark/benchmark.h"
#include "xff/engine/run.h"
#include "xff/parser/parser.h"
#include "xff/vfs/filesystem.h"
#include "xff/vfs/read_source.h"

namespace {
class SearchTree final : public xff::vfs::FileSystem {
 public:
  explicit SearchTree(std::size_t count) : count_(count), text_("alpha123 needle\n") {
    for (std::size_t index = 0; index < 128; ++index) {
      text_ += "unmatched content for line selection\n";
    }
    source_ = xff::vfs::MemoryReadSource(text_);
  }

  absl::StatusOr<std::vector<xff::vfs::Entry>> ReadDir(std::string_view path) const override {
    if (path != "tree") {
      return absl::NotFoundError("not a directory");
    }
    std::vector<xff::vfs::Entry> entries;
    entries.reserve(count_);
    for (std::size_t index = 0; index < count_; ++index) {
      const auto name = std::to_string(index);
      entries.push_back({.path = "tree/" + name, .name = name, .type = xff::vfs::FileType::kRegular});
    }
    return entries;
  }

  absl::StatusOr<xff::vfs::Metadata> Stat(std::string_view path, bool) const override {
    return xff::vfs::Metadata{
        .type = path == "tree" ? xff::vfs::FileType::kDirectory : xff::vfs::FileType::kRegular,
        .size = path == "tree" ? 0 : text_.size(),
    };
  }

  absl::StatusOr<std::string> ReadContent(std::string_view) const override { return text_; }

  absl::StatusOr<xff::vfs::SharedReadSource> ContentSource(std::string_view) const override { return source_; }

  absl::Status Remove(std::string_view) const override { return absl::PermissionDeniedError("read-only"); }

  bool Access(std::string_view, xff::vfs::AccessMode mode) const override {
    return mode == xff::vfs::AccessMode::kRead;
  }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override {
    return absl::InvalidArgumentError("not a link");
  }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return "memory"; }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

 private:
  std::size_t count_;
  std::string text_;
  xff::vfs::SharedReadSource source_;
};

enum class Mode { kFilter, kNativeLines, kRgLines, kRgFiles, kRgCounts, kRgQuiet };

void Search(benchmark::State& state, Mode mode, std::string_view grammar) {
  const auto count = static_cast<std::size_t>(state.range(0));
  const SearchTree tree(count);
  std::vector<std::string> arguments{
      "--jobs=" + std::to_string(state.range(1)),
      "--archive=none",
      "--sort=none",
      "--regextype=" + std::string(grammar),
  };
  constexpr std::string_view kPattern = "(alpha|beta)[0-9]{3}.*needle";
  if (mode == Mode::kFilter || mode == Mode::kNativeLines) {
    if (mode == Mode::kNativeLines) {
      arguments.emplace_back("-M");
    }
    arguments.insert(arguments.end(), {"tree", "-rxc", std::string(kPattern)});
  } else {
    arguments.insert(arguments.begin(), "--rg");
    if (mode == Mode::kRgFiles) {
      arguments.emplace_back("-l");
    } else if (mode == Mode::kRgCounts) {
      arguments.emplace_back("-c");
    } else if (mode == Mode::kRgQuiet) {
      arguments.emplace_back("-q");
    }
    arguments.insert(arguments.end(), {std::string(kPattern), "tree"});
  }
  auto command = xff::parser::Parse(arguments);
  if (!command.ok()) {
    state.SkipWithError(command.status().ToString());
    return;
  }
  xff::parser::BindMatchers(
      *command, xff::parser::GrammarFromGlobals(command->globals),
      xff::parser::ResolveCaseMode(command->globals, xff::registry::Style::kXff));
  std::size_t records = 0;
  const auto emit = [&](std::string_view text) { records += std::ranges::count(text, '\n'); };
  const auto error = [](std::string_view, absl::Status) {};
  const auto initial = xff::engine::RunFind(*command, tree, emit, error);
  if (initial.errors != 0 || !initial.any_match || (mode != Mode::kRgQuiet && records != count)) {
    state.SkipWithError("incorrect selected results or output records");
    return;
  }
  for (auto iteration : state) {
    benchmark::DoNotOptimize(iteration);
    records = 0;
    const auto result = xff::engine::RunFind(*command, tree, emit, error);
    if (result.errors != 0 || !result.any_match) {
      state.SkipWithError("search failed");
      break;
    }
    benchmark::DoNotOptimize(records);
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
}
}  // namespace

// XFF_ABI_POINTER: Google Benchmark's process argument interface.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  constexpr auto kModes = std::to_array<std::pair<std::string_view, Mode>>({
      {"filter", Mode::kFilter},
      {"native-lines", Mode::kNativeLines},
      {"rg-lines", Mode::kRgLines},
      {"rg-files", Mode::kRgFiles},
      {"rg-counts", Mode::kRgCounts},
      {"rg-quiet", Mode::kRgQuiet},
  });
  for (const std::string_view grammar : {"RE2", "PCRE2"}) {
    for (const auto& [name, mode] : kModes) {
      const auto label = std::string(grammar) + "/" + std::string(name);
      benchmark::RegisterBenchmark(label, Search, mode, grammar)
          ->ArgsProduct({{10, 100, 1'000, 10'000}, {1, 4}})
          ->UseRealTime()
          ->Unit(benchmark::kMicrosecond);
    }
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
