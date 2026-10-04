// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "benchmark/benchmark.h"
#include "mbo/status/status_macros.h"
#include "xff/engine/evaluate.h"
#include "xff/engine/walk.h"
#include "xff/parser/parser.h"
#include "xff/vfs/filesystem.h"

namespace {

// Immutable in-memory input. The execution kernel never visits the host filesystem.
class ExpressionFs final : public xff::vfs::FileSystem {
 public:
  absl::StatusOr<std::vector<xff::vfs::Entry>> ReadDir(std::string_view) const override {
    return std::vector<xff::vfs::Entry>{};
  }

  absl::StatusOr<xff::vfs::Metadata> Stat(std::string_view, bool) const override {
    return absl::FailedPreconditionError("kernel must use prepared entry metadata");
  }

  absl::Status Remove(std::string_view) const override { return absl::PermissionDeniedError("read-only fixture"); }

  bool Access(std::string_view, xff::vfs::AccessMode) const override { return true; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override { return std::string("target"); }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return std::string("memory"); }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

  absl::StatusOr<std::string> ReadContent(std::string_view) const override { return std::string("needle\n"); }
};

enum class Executor { kTree, kBound, kPrepared, kTreeWorker, kPreparedWorker };

struct PreparedWorkerBenchmark {
  xff::engine::PreparedExpression expression;
  xff::engine::PreparedExpression::Worker worker;

  static absl::StatusOr<PreparedWorkerBenchmark> Prepare(const xff::parser::Expr& source) {
    MBO_ASSIGN_OR_RETURN(auto expression, xff::engine::PreparedExpression::Prepare(source));
    auto worker = expression.MakeWorker();
    return PreparedWorkerBenchmark{.expression = std::move(expression), .worker = std::move(worker)};
  }

  xff::engine::EvaluationResult Evaluate(xff::engine::EvalContext& context) const { return worker.Evaluate(context); }

  std::size_t NodeCount() const { return expression.NodeCount(); }

  std::size_t OperandCount() const { return expression.OperandCount(); }

  std::size_t StorageBytes() const { return expression.StorageBytes() + worker.StorageBytes(); }
};

struct ExpressionCase {
  std::string_view name;
  std::string_view primary;
  std::string_view argument;
};

// The named core matrix is shared by preparation and execution. Long cheap chains expose dispatch
// and operand costs; costly content/scoring cases use shorter chains. Effects are controlled output.
constexpr auto kCases = std::to_array<ExpressionCase>({
    {
        .name = "type",
        .primary = "-type",
        .argument = "f",
    },
    {
        .name = "name",
        .primary = "-name",
        .argument = "*.txt",
    },
    {
        .name = "size",
        .primary = "-size",
        .argument = "+2c",
    },
    {
        .name = "permission",
        .primary = "-perm",
        .argument = "0644",
    },
    {
        .name = "permission-symbolic",
        .primary = "-perm",
        .argument = "u+rw,go=r",
    },
    {
        .name = "numeric",
        .primary = "-links",
        .argument = "+0",
    },
    {
        .name = "content",
        .primary = "-content",
        .argument = "needle",
    },
    {
        .name = "regex",
        .primary = "-rxc",
        .argument = "n[a-z]+le",
    },
    {
        .name = "path-regex",
        .primary = "-regex",
        .argument = "tree/file[.]txt",
    },
    {
        .name = "fuzzy",
        .primary = "-fuzzy",
        .argument = "file",
    },
    {
        .name = "output",
        .primary = "-printf",
        .argument = "%p",
    },
});

std::vector<std::string> Arguments(const ExpressionCase& example, std::int64_t length) {
  std::vector<std::string> arguments;
  arguments.reserve(1 + (2 * static_cast<std::size_t>(length)));
  arguments.emplace_back(".");
  for (std::int64_t index = 0; index < length; ++index) {
    arguments.emplace_back(example.primary);
    arguments.emplace_back(example.argument);
  }
  return arguments;
}

absl::StatusOr<xff::parser::Command> Parse(const std::vector<std::string>& arguments) {
  MBO_ASSIGN_OR_RETURN(auto command, xff::parser::Parse(arguments));
  xff::parser::BindMatchers(command, xff::regex::Grammar::kRe2, xff::parser::CaseMode::kSensitive);
  return command;
}

template<Executor Mode>
void Prepare(benchmark::State& state, const ExpressionCase& example) {
  const auto arguments = Arguments(example, state.range(0));
  for (auto iteration : state) {
    benchmark::DoNotOptimize(iteration);
    const auto command = Parse(arguments);
    if (!command.ok()) {
      state.SkipWithError(command.status().ToString());
      break;
    }
    if constexpr (Mode != Executor::kTree && Mode != Executor::kTreeWorker) {
      const auto program = [&] {
        if constexpr (Mode == Executor::kBound) {
          return xff::engine::BoundExpression::Prepare(*command->expression);
        } else if constexpr (Mode == Executor::kPreparedWorker) {
          return PreparedWorkerBenchmark::Prepare(*command->expression);
        } else {
          return xff::engine::PreparedExpression::Prepare(*command->expression);
        }
      }();
      if (!program.ok()) {
        state.SkipWithError(program.status().ToString());
        break;
      }
      benchmark::DoNotOptimize(program->NodeCount());
    } else {
      // The shipping tree executor has no additional preparation table.
      benchmark::DoNotOptimize(command->expression.get());
      if constexpr (Mode == Executor::kTreeWorker) {
        xff::engine::WorkerMatchers worker;
        worker.Bind(*command->expression);
        benchmark::DoNotOptimize(worker);
      }
    }
  }
}

template<Executor Mode>
void Kernel(benchmark::State& state, const ExpressionCase& example) {
  const auto command = Parse(Arguments(example, state.range(0)));
  if (!command.ok()) {
    state.SkipWithError(command.status().ToString());
    return;
  }
  const auto prepared = [&] {
    if constexpr (Mode == Executor::kBound) {
      return xff::engine::BoundExpression::Prepare(*command->expression);
    } else if constexpr (Mode == Executor::kPrepared) {
      return xff::engine::PreparedExpression::Prepare(*command->expression);
    } else if constexpr (Mode == Executor::kPreparedWorker) {
      return PreparedWorkerBenchmark::Prepare(*command->expression);
    } else {
      return absl::StatusOr<std::reference_wrapper<const xff::parser::Expr>>(std::cref(*command->expression));
    }
  }();
  if (!prepared.ok()) {
    state.SkipWithError(prepared.status().ToString());
    return;
  }
  const auto& program = *prepared;
  const ExpressionFs fs;
  const xff::vfs::Metadata metadata{.type = xff::vfs::FileType::kRegular, .size = 7, .mode = 0644, .nlink = 2};
  const xff::engine::Visit visit{.path = "tree/file.txt", .name = "file.txt", .metadata = metadata, .fs = fs};
  // NOLINTNEXTLINE(misc-const-correctness): EvalContext and evaluator callbacks mutate this control.
  xff::engine::Control control;
  std::uint64_t emitted = 0;
  const auto emit = [&emitted](std::string_view value) { emitted += value.size(); };
  xff::engine::EvalContext context{
      .visit = visit,
      .emit = emit,
      .fs = fs,
      .now = absl::UnixEpoch(),
      .tz = absl::UTCTimeZone(),
      .control = control,
  };
  xff::engine::WorkerMatchers worker;
  if constexpr (Mode == Executor::kTreeWorker) {
    worker.Bind(*command->expression);
    context.worker_matchers = worker;
  }
  // Check the result before timing; all core cases must reach their complete AND chain.
  const auto evaluate = [&] {
    if constexpr (Mode != Executor::kTree && Mode != Executor::kTreeWorker) {
      return program.Evaluate(context);
    } else {
      return xff::engine::EvaluateDeferred(program.get(), context);
    }
  };
  const auto check = evaluate();
  if (!check.matched || check.deferred || check.unknown || !control.metadata_error.ok()
      || !control.unsupported.empty()) {
    state.SkipWithError("expression baseline failed its untimed oracle check");
    return;
  }
  for (auto iteration : state) {
    benchmark::DoNotOptimize(iteration);
    for (std::int64_t entry = 0; entry < state.range(1); ++entry) {
      context.content.Invalidate();
      auto result = evaluate();
      benchmark::DoNotOptimize(result.matched);
    }
  }
  benchmark::DoNotOptimize(emitted);
  state.SetItemsProcessed(state.iterations() * state.range(1));
  state.counters["predicates_per_entry"] = static_cast<double>(state.range(0));
  if constexpr (Mode != Executor::kTree && Mode != Executor::kTreeWorker) {
    state.counters["extra_bytes"] = static_cast<double>(program.StorageBytes());
    state.counters["nodes"] = static_cast<double>(program.NodeCount());
  }
  if constexpr (Mode == Executor::kPrepared || Mode == Executor::kPreparedWorker) {
    state.counters["operands"] = static_cast<double>(program.OperandCount());
  }
}

double FastestSeven(const std::vector<double>& values) {
  auto sorted = values;
  std::ranges::sort(sorted);
  double sum = 0;
  std::size_t count = 0;
  for (const double value : sorted | std::views::take(7)) {
    sum += value;
    ++count;
  }
  return count == 0 ? std::numeric_limits<double>::quiet_NaN() : sum / static_cast<double>(count);
}

template<Executor Mode>
void Register(std::string_view name, const ExpressionCase& example) {
  benchmark::RegisterBenchmark("prepare/" + std::string(name) + "/" + std::string(example.name), Prepare<Mode>, example)
      ->Arg(1)
      ->Arg(16)
      ->Arg(64)
      ->UseRealTime()
      ->Unit(benchmark::kNanosecond)
      ->Repetitions(9)
      ->MinTime(0.01)
      ->MinWarmUpTime(0.01)
      ->ComputeStatistics("fastest7of9", FastestSeven);
  benchmark::RegisterBenchmark("kernel/" + std::string(name) + "/" + std::string(example.name), Kernel<Mode>, example)
      ->ArgsProduct({{1, 16, 64}, {10, 1'000}})
      ->UseRealTime()
      ->Unit(benchmark::kNanosecond)
      ->Repetitions(9)
      ->MinTime(0.01)
      ->MinWarmUpTime(0.01)
      ->ComputeStatistics("fastest7of9", FastestSeven);
}

}  // namespace

// XFF_ABI_POINTER: Google Benchmark's process argument interface.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  for (const auto& example : kCases) {
    Register<Executor::kTree>("tree", example);
    Register<Executor::kBound>("bound", example);
    Register<Executor::kPrepared>("prepared", example);
    if (example.name == "regex" || example.name == "path-regex") {
      Register<Executor::kTreeWorker>("tree-worker", example);
      Register<Executor::kPreparedWorker>("prepared-worker", example);
    }
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
