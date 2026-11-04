// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"
#include "mbo/status/status_macros.h"
#include "xff/engine/run.h"
#include "xff/parser/parser.h"
#include "xff/vfs/filesystem.h"

namespace {

constexpr std::string_view kRoot = "root_with_a_prefix_longer_than_small_string_storage";

// The fixture owns names outside measurement. Both policies construct the same
// listing; only complete-path policy allocates an independent full path for
// each child.
class ListingFs final : public xff::vfs::FileSystem {
 public:
  ListingFs(std::size_t count, bool deep, bool names) : names_(names) {
    directories_.try_emplace(std::string(kRoot));
    for (std::size_t index = 0; index < count; ++index) {
      std::string directory(kRoot);
      if (deep) {
        const auto first = std::to_string(index % 8);
        AddDirectory(directory, first);
        absl::StrAppend(&directory, "/", first);
        const auto second = std::to_string(index % 31);
        AddDirectory(directory, second);
        absl::StrAppend(&directory, "/", second);
      }
      directories_.at(directory).push_back({
          .name = absl::StrCat("file", index, index % 10 == 0 ? ".txt" : ".dat"),
          .type = xff::vfs::FileType::kRegular,
      });
    }
  }

  bool SupportsNameOnlyListings() const override { return names_; }

  absl::StatusOr<std::vector<xff::vfs::Entry>> ReadDirNames(std::string_view path) const override {
    const auto found = directories_.find(path);
    if (found == directories_.end()) {
      return absl::NotFoundError("unknown memory directory");
    }
    return found->second;
  }

  absl::StatusOr<std::vector<xff::vfs::Entry>> ReadDir(std::string_view path) const override {
    MBO_ASSIGN_OR_RETURN(auto entries, ReadDirNames(path));
    for (auto& entry : entries) {
      entry.path = absl::StrCat(path, "/", entry.name);
    }
    return entries;
  }

  absl::StatusOr<xff::vfs::Metadata> Stat(std::string_view path, bool) const override {
    return xff::vfs::Metadata{
        .type = directories_.contains(path) ? xff::vfs::FileType::kDirectory : xff::vfs::FileType::kRegular,
        .size = 7,
        .ino = std::hash<std::string_view>{}(path),
    };
  }

  absl::Status Remove(std::string_view) const override { return absl::PermissionDeniedError("read-only fixture"); }

  bool Access(std::string_view, xff::vfs::AccessMode) const override { return true; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override { return absl::NotFoundError("not a link"); }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return "memory"; }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

  absl::StatusOr<std::string> ReadContent(std::string_view) const override { return ""; }

 private:
  void AddDirectory(const std::string& parent, const std::string& name) {
    if (directories_.try_emplace(absl::StrCat(parent, "/", name)).second) {
      directories_.at(parent).push_back({.name = name, .type = xff::vfs::FileType::kDirectory});
    }
  }

  const bool names_;
  std::map<std::string, std::vector<xff::vfs::Entry>, std::less<>> directories_;
};

void Measure(benchmark::State& state, bool deep, bool selected, bool names) {
  const auto count = static_cast<std::size_t>(state.range(0));
  const ListingFs baseline(count, deep, false);
  const ListingFs candidate(count, deep, names);
  std::vector<std::string> arguments{
      "--exact",          "--color=never", "--sort=dir", "--jobs=" + std::to_string(state.range(1)),
      std::string(kRoot), "-type",         "f",
  };
  if (selected) {
    arguments.append_range(std::to_array<std::string>({"-name", "*.txt"}));
  }
  auto command = xff::parser::Parse(arguments);
  if (!command.ok()) {
    state.SkipWithError(command.status().ToString());
    return;
  }
  xff::parser::BindMatchers(
      *command, xff::parser::GrammarFromGlobals(command->globals),
      xff::parser::ResolveCaseMode(command->globals, xff::registry::Style::kXff));
  bool failed = false;
  const auto on_error = [&](std::string_view, absl::Status) { failed = true; };
  std::string expected;
  std::string actual;
  const auto reference =
      xff::engine::RunFind(*command, baseline, [&](std::string_view text) { expected.append(text); }, on_error);
  const auto check =
      xff::engine::RunFind(*command, candidate, [&](std::string_view text) { actual.append(text); }, on_error);
  if (failed || reference.errors != 0 || check.errors != 0 || reference.any_match != check.any_match
      || expected != actual) {
    state.SkipWithError("complete-path and names-only results differ");
    return;
  }
  std::size_t bytes = 0;
  const auto emit = [&](std::string_view text) { bytes += text.size(); };
  for (auto iteration : state) {
    benchmark::DoNotOptimize(iteration);
    auto result = xff::engine::RunFind(*command, candidate, emit, on_error);
    benchmark::DoNotOptimize(result);
    if (result.errors != 0) {
      failed = true;
    }
  }
  if (failed) {
    state.SkipWithError("listing run failed");
  }
  benchmark::DoNotOptimize(bytes);
}

void Register() {
  for (const bool deep : {false, true}) {
    for (const bool selected : {false, true}) {
      for (const bool names : {false, true}) {
        const std::string name =
            absl::StrCat(deep ? "deep/" : "broad/", selected ? "name-txt/" : "files/", names ? "names" : "complete");
        auto& registration = *benchmark::RegisterBenchmark(name, Measure, deep, selected, names);
        registration.UseRealTime();
        for (const auto count : {10, 100, 1'000, 10'000, 100'000}) {
          for (const auto workers : {1, 3}) {
            registration.Args({count, workers});
          }
        }
      }
    }
  }
}

}  // namespace

int main(int argc,
         char** argv) {  // XFF_ABI_POINTER: benchmark command-line entry point.
  benchmark::Initialize(&argc, argv);
  Register();
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
