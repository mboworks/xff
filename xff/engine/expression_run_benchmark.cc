// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "benchmark/benchmark.h"
#include "xff/engine/expression_qualification.h"
#include "xff/engine/run.h"
#include "xff/parser/parser.h"
#include "xff/vfs/filesystem.h"

namespace {

using ::xff::engine::ExpressionExecutor;

enum class ReferenceFixture { kNone, kShared, kDistinct };

class RunFs final : public xff::vfs::FileSystem {
 public:
  explicit RunFs(std::size_t count, std::size_t content_bytes = 0, ReferenceFixture reference = ReferenceFixture::kNone)
      : content_(content_bytes, 'x'), reference_fixture_(reference) {
    if (reference != ReferenceFixture::kNone) {
      content_.clear();
      content_.reserve(content_bytes);
      for (std::size_t index = 0; content_.size() < content_bytes; ++index) {
        absl::StrAppend(&content_, "token", index % 257, " ");
      }
      content_.resize(content_bytes);
      reference_ = content_;
      for (char& byte : reference_) {
        if (byte >= 'a' && byte <= 'z') {
          byte = static_cast<char>(byte - 'a' + 'A');
        }
      }
    }
    entries_.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      std::string name = "file" + std::to_string(index) + (index % 2 == 0 ? ".txt" : ".cc");
      entries_.push_back({.path = "root/" + name, .name = std::move(name), .type = xff::vfs::FileType::kRegular});
    }
  }

  absl::StatusOr<std::vector<xff::vfs::Entry>> ReadDir(std::string_view path) const override {
    if (path == "root") {
      return entries_;
    }
    if (path == "peer") {
      auto entries = entries_;
      for (auto& entry : entries) {
        entry.path = "peer/" + entry.name;
      }
      return entries;
    }
    return absl::NotFoundError("unknown benchmark directory");
  }

  absl::StatusOr<xff::vfs::Metadata> Stat(std::string_view path, bool) const override {
    return xff::vfs::Metadata{
        .type = path == "root" || path == "peer" ? xff::vfs::FileType::kDirectory : xff::vfs::FileType::kRegular,
        .size = content_.empty() ? 7 : content_.size(),
        .mode = 0644,
    };
  }

  absl::StatusOr<std::string> ReadContent(std::string_view path) const override {
    if (reference_fixture_ != ReferenceFixture::kNone && path.starts_with("peer/")) {
      std::string bytes = reference_;
      if (reference_fixture_ == ReferenceFixture::kDistinct) {
        // A unique punctuation-only suffix changes bytes without changing the word-shingle set.
        for (const unsigned char byte : path) {
          for (unsigned bit = 0; bit < 8; ++bit) {
            bytes.push_back((byte & (1U << bit)) != 0 ? '.' : ',');
          }
        }
      }
      return bytes;
    }
    if (!content_.empty()) {
      return content_;
    }
    if (path == "peer/diff-control") {
      return "reference\n";
    }
    return path.ends_with(".txt") ? "needle\n" : "absent\n";
  }

  absl::Status Remove(std::string_view) const override { return absl::PermissionDeniedError("read-only benchmark"); }

  bool Access(std::string_view, xff::vfs::AccessMode) const override { return true; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override { return absl::NotFoundError("not a link"); }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return "memory"; }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

 private:
  std::vector<xff::vfs::Entry> entries_;
  std::string content_;
  std::string reference_;
  ReferenceFixture reference_fixture_;
};

struct Scenario {
  std::string_view name;
  std::vector<std::string> arguments;
  std::size_t content_bytes = 0;
  ReferenceFixture reference = ReferenceFixture::kNone;
};

struct Executor {
  std::string_view name;
  ExpressionExecutor executor;
};

constexpr auto kExecutors = std::to_array<Executor>({
    {.name = "tree", .executor = ExpressionExecutor::kTree},
    {.name = "production", .executor = ExpressionExecutor::kProduction},
    {.name = "bound", .executor = ExpressionExecutor::kBound},
    {.name = "prepared", .executor = ExpressionExecutor::kPrepared},
    {.name = "prepared-eager", .executor = ExpressionExecutor::kPreparedEager},
    {.name = "program-switch", .executor = ExpressionExecutor::kProgramSwitch},
    {.name = "program-functions", .executor = ExpressionExecutor::kProgramFunctions},
    {.name = "program-optimized", .executor = ExpressionExecutor::kProgramOptimized},
});

double FastestSeven(const std::vector<double>& times) {
  auto sorted = times;
  std::ranges::sort(sorted);
  const auto count = std::min<std::size_t>(7, sorted.size());
  return std::accumulate(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(count), 0.0)
         / static_cast<double>(count);
}

void Measure(benchmark::State& state, const Scenario& scenario, ExpressionExecutor executor) {
  const auto factory = xff::engine::QualifiedExpressionFactory(executor);
  const RunFs fs(static_cast<std::size_t>(state.range(0)), scenario.content_bytes, scenario.reference);
  std::vector<std::string> arguments{
      "--exact", "--color=never", "--sort=dir", "--jobs=" + std::to_string(state.range(1)), "root",
  };
  arguments.insert(arguments.end(), scenario.arguments.begin(), scenario.arguments.end());
  auto command = xff::parser::Parse(arguments);
  if (!command.ok()) {
    state.SkipWithError(command.status().ToString());
    return;
  }
  xff::parser::BindMatchers(
      *command, xff::parser::GrammarFromGlobals(command->globals),
      xff::parser::ResolveCaseMode(command->globals, xff::registry::Style::kXff));
  std::string expected;
  std::string actual;
  bool error = false;
  const auto on_error = [&](std::string_view, absl::Status) { error = true; };
  const auto reference = xff::engine::RunFind(
      *command, fs, [&](std::string_view text) { expected.append(text); }, on_error, std::nullopt, 0, nullptr);
  const auto candidate = xff::engine::RunFind(
      *command, fs, [&](std::string_view text) { actual.append(text); }, on_error, std::nullopt, 0, factory);
  if (error || reference.errors != 0 || candidate.errors != 0 || expected != actual
      || reference.any_match != candidate.any_match) {
    state.SkipWithError("whole-engine expression output differs from reference");
    return;
  }
  std::size_t bytes = 0;
  const auto emit = [&](std::string_view text) { bytes += text.size(); };
  // Parsing and fixture construction are excluded. Every iteration includes run preflight,
  // expression preparation, worker startup, traversal, metadata/content, sinks and teardown.
  for (auto iteration : state) {
    benchmark::DoNotOptimize(iteration);
    auto result = xff::engine::RunFind(*command, fs, emit, on_error, std::nullopt, 0, factory);
    benchmark::DoNotOptimize(result);
  }
  if (error) {
    state.SkipWithError("whole-engine expression measurement failed");
  }
  benchmark::DoNotOptimize(bytes);
  state.counters["files"] = static_cast<double>(state.range(0));
  state.counters["workers"] = static_cast<double>(state.range(1));
}

void RegisterEncodedOutput() {
  const auto profiles = std::to_array<Scenario>({
      {
          .name = "plain",
          .arguments = {"-type", "f"},
      },
      {
          .name = "escaped",
          .arguments = {"--path-encoding=escape", "-type", "f"},
      },
      {
          .name = "nul",
          .arguments = {"--format=nul", "-type", "f"},
      },
      {
          .name = "csv",
          .arguments = {"--format=csv", "--no-header", "-type", "f"},
      },
      {
          .name = "tsv",
          .arguments = {"--format=tsv", "--no-header", "-type", "f"},
      },
      {
          .name = "jsonl",
          .arguments = {"--format=jsonl", "-type", "f"},
      },
      {
          .name = "csv-columns",
          .arguments = {"--format=csv", "--no-header", "--columns=path", "-type", "f"},
      },
  });
  for (const auto& profile : profiles) {
    const std::string name = "engine/encoded-output/" + std::string(profile.name);
    benchmark::RegisterBenchmark(
        name, [profile](benchmark::State& state) { Measure(state, profile, ExpressionExecutor::kProduction); })
        ->ArgsProduct({{0, 10, 1'000, 10'000}, {1, 3}})
        ->UseRealTime()
        ->Unit(benchmark::kMicrosecond)
        ->Repetitions(9)
        ->MinTime(0.02)
        ->MinWarmUpTime(0.01);
  }
}

void RegisterColumnRows() {
  struct Columns {
    std::string_view name;
    std::string_view format;
    std::string_view columns;
  };

  static constexpr auto kColumns = std::to_array<Columns>({
      {
          .name = "csv1",
          .format = "csv",
          .columns = "path",
      },
      {
          .name = "csv4",
          .format = "csv",
          .columns = "path,name,stem,ext",
      },
      {
          .name = "csv16",
          .format = "csv",
          .columns = "path,path,path,path,path,path,path,path,path,path,path,path,path,path,path,path",
      },
      {
          .name = "tsv1",
          .format = "tsv",
          .columns = "path",
      },
      {
          .name = "tsv4",
          .format = "tsv",
          .columns = "path,name,stem,ext",
      },
      {
          .name = "tsv16",
          .format = "tsv",
          .columns = "path,path,path,path,path,path,path,path,path,path,path,path,path,path,path,path",
      },
      {
          .name = "aligned4",
          .format = "aligned",
          .columns = "path,name,stem,ext",
      },
      {
          .name = "markdown4",
          .format = "markdown",
          .columns = "path,name,stem,ext",
      },
  });
  for (const auto& columns : kColumns) {
    const Scenario scenario{
        .name = columns.name,
        .arguments =
            {
                "--format=" + std::string(columns.format),
                "--columns=" + std::string(columns.columns),
                "--no-header",
                "-type",
                "f",
            },
    };
    benchmark::RegisterBenchmark(
        "engine/column-rows/" + std::string(columns.name),
        [scenario](benchmark::State& state) { Measure(state, scenario, ExpressionExecutor::kProduction); })
        ->ArgsProduct({{0, 10, 1'000, 10'000}, {1, 3}})
        ->UseRealTime()
        ->Unit(benchmark::kMicrosecond)
        ->Repetitions(9)
        ->MinTime(0.02)
        ->MinWarmUpTime(0.01);
  }
}

void RegisterComparisonSummaries() {
  static constexpr auto kFormats =
      std::to_array<std::string_view>({"plain", "aligned", "markdown", "csv", "tsv", "jsonl"});

  struct SummaryProfile {
    std::string_view name;
    std::string_view grouping;
    std::string_view scopes;
  };

  static constexpr auto kProfiles = std::to_array<SummaryProfile>({
      {
          .name = "two-columns-names",
          .grouping = "{name}",
          .scopes = "compare",
      },
      {
          .name = "six-columns-names",
          .grouping = "{name}",
          .scopes = "left-total,right-total,left-only,right-only,different,identical",
      },
      {
          .name = "two-columns-ext-control",
          .grouping = "ext",
          .scopes = "compare",
      },
  });
  for (const auto& profile : kProfiles) {
    for (const auto format : kFormats) {
      const Scenario scenario{
          .name = profile.name,
          .arguments =
              {
                  "peer",
                  "--compare=summary",
                  "--summary=" + std::string(profile.grouping),
                  "--summary-scope=" + std::string(profile.scopes),
                  "--format=" + std::string(format),
                  "--width=1000",
                  "-type",
                  "f",
              },
      };
      benchmark::RegisterBenchmark(
          "engine/comparison-summary/" + std::string(profile.name) + "/" + std::string(format),
          [scenario](benchmark::State& state) { Measure(state, scenario, ExpressionExecutor::kProduction); })
          ->ArgsProduct({{0, 10, 1'000, 10'000}, {1, 3}})
          ->UseRealTime()
          ->Unit(benchmark::kMicrosecond)
          ->Repetitions(9)
          ->MinTime(0.02)
          ->MinWarmUpTime(0.01);
    }
  }
}

void RegisterNumericSummaries() {
  struct Profile {
    std::string_view name;
    std::string_view grouping;
  };

  static constexpr auto kProfiles = std::to_array<Profile>({
      {.name = "names", .grouping = "{name}"},
      {.name = "ext-control", .grouping = "ext"},
  });
  static constexpr auto kFormats =
      std::to_array<std::string_view>({"plain", "aligned", "markdown", "csv", "tsv", "jsonl"});
  for (const auto& profile : kProfiles) {
    for (const auto format : kFormats) {
      const Scenario scenario{
          .name = profile.name,
          .arguments =
              {
                  "--summary=" + std::string(profile.grouping),
                  "--format=" + std::string(format),
                  "--width=110",
                  "-type",
                  "f",
              },
      };
      benchmark::RegisterBenchmark(
          "engine/metric-summary/" + std::string(profile.name) + "/" + std::string(format),
          [scenario](benchmark::State& state) { Measure(state, scenario, ExpressionExecutor::kProduction); })
          ->ArgsProduct({{0, 10, 1'000, 10'000}, {1, 3}})
          ->UseRealTime()
          ->Unit(benchmark::kMicrosecond)
          ->Repetitions(9)
          ->MinTime(0.02)
          ->MinWarmUpTime(0.01);
    }
  }
}

}  // namespace

// XFF_ABI_POINTER: Google Benchmark's process argument interface.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  const auto
      scenarios =
          std::to_array<Scenario>(
              {
                  {
                      .name = "similar-shared-64",
                      .arguments = {"-type", "f", "-similar:3:100%", "peer/reference"},
                      .content_bytes = 64,
                      .reference = ReferenceFixture::kShared,
                  },
                  {
                      .name = "similar-distinct-64",
                      .arguments = {"-type", "f", "-similar:3:100%", "peer/{name}"},
                      .content_bytes = 64,
                      .reference = ReferenceFixture::kDistinct,
                  },
                  {
                      .name = "similar-shared-4096",
                      .arguments = {"-type", "f", "-similar:3:100%", "peer/reference"},
                      .content_bytes = 4'096,
                      .reference = ReferenceFixture::kShared,
                  },
                  {
                      .name = "similar-distinct-4096",
                      .arguments = {"-type", "f", "-similar:3:100%", "peer/{name}"},
                      .content_bytes = 4'096,
                      .reference = ReferenceFixture::kDistinct,
                  },
                  {
                      .name = "similar-shared-16384",
                      .arguments = {"-type", "f", "-similar:3:100%", "peer/reference"},
                      .content_bytes = 16'384,
                      .reference = ReferenceFixture::kShared,
                  },
                  {
                      .name = "similar-distinct-16384",
                      .arguments = {"-type", "f", "-similar:3:100%", "peer/{name}"},
                      .content_bytes = 16'384,
                      .reference = ReferenceFixture::kDistinct,
                  },
                  {
                      .name = "similar-shared-131072",
                      .arguments = {"-type", "f", "-similar:3:100%", "peer/reference"},
                      .content_bytes = 131'072,
                      .reference = ReferenceFixture::kShared,
                  },
                  {
                      .name = "similar-distinct-131072",
                      .arguments = {"-type", "f", "-similar:3:100%", "peer/{name}"},
                      .content_bytes = 131'072,
                      .reference = ReferenceFixture::kDistinct,
                  },
                  {
                      .name = "similar-shared-w1-4096",
                      .arguments = {"-type", "f", "-similar:1:100%", "peer/reference"},
                      .content_bytes = 4'096,
                      .reference = ReferenceFixture::kShared,
                  },
                  {
                      .name = "similar-distinct-w1-4096",
                      .arguments = {"-type", "f", "-similar:1:100%", "peer/{name}"},
                      .content_bytes = 4'096,
                      .reference = ReferenceFixture::kDistinct,
                  },
                  {
                      .name = "similar-shared-w5-4096",
                      .arguments = {"-type", "f", "-similar:5:100%", "peer/reference"},
                      .content_bytes = 4'096,
                      .reference = ReferenceFixture::kShared,
                  },
                  {
                      .name = "similar-distinct-w5-4096",
                      .arguments = {"-type", "f", "-similar:5:100%", "peer/{name}"},
                      .content_bytes = 4'096,
                      .reference = ReferenceFixture::kDistinct,
                  },
                  {
                      .name = "similar-equal-8000",
                      .arguments = {"-type", "f", "-similar:3:100%", "{path}"},
                      .content_bytes = 8'000,
                  },
                  {
                      .name = "similar-equal-65536",
                      .arguments = {"-type", "f", "-similar:3:100%", "{path}"},
                      .content_bytes = 65'536,
                  },
                  {
                      .name = "similar-equal",
                      .arguments = {"-type", "f", "-similar:3:100%", "{path}"},
                  },
                  {
                      .name = "similar-disjoint",
                      .arguments =
                          {
                              "-type",
                              "f",
                              "-similar:3:50%",
                              "peer/diff-control",
                          },
                  },
                  {
                      .name = "similar-zero",
                      .arguments =
                          {
                              "-type",
                              "f",
                              "-similar:3:0%",
                              "peer/diff-control",
                          },
                  },
                  {
                      .name = "compare-template",
                      .arguments = {"-type", "f", "-cmp", "peer/{relpath:s/file/copy/}"},
                  },
                  {
                      .name = "diff-normalized",
                      .arguments =
                          {
                              "--diff-ignore-matching=^(needle|absent|reference)$",
                              "-type",
                              "f",
                              "-diff:none",
                              "peer/diff-control",
                              "-print",
                          },
                  },
                  {
                      .name = "diff-plain",
                      .arguments =
                          {
                              "-type",
                              "f",
                              "-diff",
                              "peer/diff-control",
                          },
                  },
                  {
                      .name = "diff-skipped",
                      .arguments =
                          {
                              "--diff-ignore-matching=^(needle|absent|reference)$",
                              "-false",
                              "-diff",
                              "peer/diff-control",
                          },
                  },
                  {
                      .name = "diff-template",
                      .arguments = {"-type", "f", "-diff", "peer/{relpath}"},
                  },
                  {
                      .name = "hash-explicit",
                      .arguments = {"-type", "f", "-hash:md5/base64"},
                  },
                  {
                      .name = "hash-default",
                      .arguments = {"-type", "f", "-hash"},
                  },
                  {
                      .name = "hash-repeat",
                      .arguments = {"-type", "f", "-hash", "-hash", "-hash"},
                  },
                  {
                      .name = "hash-fields-repeat",
                      .arguments = {"-type", "f", "--template={hash}|{hash}|{hash:md5/base64}|{hash:sha256/base64}"},
                  },
                  {
                      .name = "hash-fields-transform",
                      .arguments = {"-type", "f", "--template={hash:s/^/SUM-/}|{hash}|{hash:md5/base64}"},
                  },
                  {
                      .name = "hasheq-explicit",
                      .arguments = {"-type", "f", "-hasheq:md5/base64", "V2pFZbcPWkwaCSXKvbWHpg=="},
                  },
                  {
                      .name = "hasheq-default",
                      .arguments = {"-type", "f", "-hasheq", "{hash}"},
                  },
                  {
                      .name = "hash-template",
                      .arguments = {"-type", "f", "-hasheq", "{hash}"},
                  },
                  {
                      .name = "first-limit",
                      .arguments = {"-type", "f", "-first", "3", "-print"},
                  },
                  {
                      .name = "mime",
                      .arguments = {"-type", "f", "-mime", "TEXT/*"},
                  },
                  {
                      .name = "name",
                      .arguments = {"-name", "*.txt"},
                  },
                  {
                      .name = "scalar",
                      .arguments = {"-type", "f", "-size", "+2c", "-perm", "0644", "-name", "*.txt"},
                  },
                  {
                      .name = "age",
                      .arguments = {"-type", "f", "-mtime", "+2d", "-mmin", "+1", "-used", "0", "-name", "*.txt"},
                  },
                  {
                      .name = "regex-output",
                      .arguments = {"-type", "f", "-rxc", "needle", "-print"},
                  },
                  {
                      .name = "regex-unreached",
                      .arguments = {"-type", "f", "-name", "*.missing", "-rxc", "needle"},
                  },
                  {
                      .name = "summary",
                      .arguments = {"-type", "f", "-name", "*.txt", "--summary=ext"},
                  },
                  {
                      .name = "scored-replay",
                      .arguments = {"-type", "f", "-fuzzy", "file", "-top", "3", "-print"},
                  },
              });
  for (const auto& scenario : scenarios) {
    for (const auto& executor : kExecutors) {
      auto& registration = *benchmark::RegisterBenchmark(
                                "engine/" + std::string(executor.name) + "/" + std::string(scenario.name), Measure,
                                scenario, executor.executor)
                                ->UseRealTime()
                                ->Unit(benchmark::kMicrosecond)
                                ->Repetitions(9)
                                ->MinTime(0.01)
                                ->MinWarmUpTime(0.01)
                                ->ComputeStatistics("fastest7of9", FastestSeven);
      // Keep the complete executor/worker matrix, but avoid multiplying the largest
      // content fixtures by 10,000 entries. Each case still has its own full oracle,
      // warmup and nine measured rounds. Metadata-sized workloads retain every scale.
      constexpr auto kCounts = std::to_array<std::size_t>({0, 1, 10, 1'000, 10'000});
      constexpr std::size_t kContentBytesPerRun = 4UZ << 20U;
      for (const auto count : kCounts) {
        if (scenario.content_bytes != 0 && count > kContentBytesPerRun / scenario.content_bytes) {
          continue;
        }
        registration.Args({static_cast<std::int64_t>(count), 1});
        registration.Args({static_cast<std::int64_t>(count), 3});
      }
    }
  }
  RegisterEncodedOutput();
  RegisterColumnRows();
  RegisterComparisonSummaries();
  RegisterNumericSummaries();
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
