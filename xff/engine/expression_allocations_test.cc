// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/base/config.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/engine/evaluate.h"
#include "xff/engine/expression_execution.h"
#include "xff/engine/expression_qualification.h"
#include "xff/engine/walk.h"
#include "xff/parser/parser.h"
#include "xff/vfs/filesystem.h"

#if defined(ABSL_HAVE_ADDRESS_SANITIZER)
# include <sanitizer/allocator_interface.h>
#endif

namespace xff::engine {
namespace {

using ::testing::Eq;
using ::testing::Ge;
using ::testing::Gt;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::SizeIs;
using ::testing::ValuesIn;

struct AllocationCounts {
  std::size_t allocations = 0;
  std::size_t requested_bytes = 0;
  std::size_t frees = 0;
};

struct AllocationRecording {
  bool enabled = false;
  AllocationCounts counts;
};

// Only this diagnostic executable installs hooks. Thread-local POD counters neither allocate
// nor lock, and ignore allocations made by the test runner outside a measured operation.
AllocationRecording& Recording() {
  static constinit thread_local AllocationRecording recording;
  return recording;
}

#if defined(ABSL_HAVE_ADDRESS_SANITIZER)
// XFF_ABI_POINTER: sanitizer allocator-hook callback signature.
void RecordAllocation(const volatile void*, std::size_t bytes) {
  auto& recording = Recording();
  if (recording.enabled) {
    ++recording.counts.allocations;
    recording.counts.requested_bytes += bytes;
  }
}

// XFF_ABI_POINTER: sanitizer allocator-hook callback signature.
void RecordFree(const volatile void*) {
  auto& recording = Recording();
  if (recording.enabled) {
    ++recording.counts.frees;
  }
}
#endif

bool InstallHooks() {
#if defined(ABSL_HAVE_ADDRESS_SANITIZER)
  static const int kInstalled = __sanitizer_install_malloc_and_free_hooks(RecordAllocation, RecordFree);
  return kInstalled > 0;
#else
  return false;
#endif
}

class AllocationScope final {
 public:
  AllocationScope() : recording_(Recording()) {
    recording_.counts = {};
    recording_.enabled = true;
  }

  AllocationScope(const AllocationScope&) = delete;
  AllocationScope& operator=(const AllocationScope&) = delete;
  AllocationScope(AllocationScope&&) = delete;
  AllocationScope& operator=(AllocationScope&&) = delete;

  ~AllocationScope() { recording_.enabled = false; }

  AllocationCounts Finish() const {
    recording_.enabled = false;
    return recording_.counts;
  }

 private:
  AllocationRecording& recording_;
};

template<typename Value>
struct AllocationMeasurement {
  Value value;
  AllocationCounts counts;
};

// Deliberately not nested: every phase completes before starting the next one. The returned
// value keeps ownership alive across phases; its eventual destruction can be measured separately.
template<typename Action>
auto MeasureAllocations(Action&& action) {
  const AllocationScope scope;
  auto value = std::invoke(std::forward<Action>(action));
  const auto allocation_counts = scope.Finish();
  return AllocationMeasurement<decltype(value)>{.value = std::move(value), .counts = allocation_counts};
}

class AllocationFs final : public vfs::FileSystem {
 public:
  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view) const override {
    return absl::FailedPreconditionError("allocation fixture must not traverse");
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view, bool) const override {
    return absl::FailedPreconditionError("allocation fixture supplies metadata");
  }

  absl::Status Remove(std::string_view) const override {
    return absl::PermissionDeniedError("isolated read-only fixture");
  }

  bool Access(std::string_view, vfs::AccessMode) const override { return false; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override {
    return absl::FailedPreconditionError("allocation fixture has no links");
  }

  absl::StatusOr<std::string> ReadContent(std::string_view) const override {
    return absl::FailedPreconditionError("allocation fixture has no content");
  }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return std::string("memory"); }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }
};

struct AllocationTest : ::testing::Test {
  void SetUp() override {
    const bool installed = InstallHooks();
#if defined(ABSL_HAVE_ADDRESS_SANITIZER)
    ASSERT_THAT(installed, IsTrue());
#else
    ASSERT_THAT(installed, IsFalse());
    GTEST_SKIP() << "allocation diagnostics require --config=clang --config=asan";
#endif
  }

  static void RecordCounts(std::string_view phase, const AllocationCounts& measured) {
    RecordProperty(absl::StrCat(phase, "_allocations"), std::to_string(measured.allocations));
    RecordProperty(absl::StrCat(phase, "_requested_bytes"), std::to_string(measured.requested_bytes));
    RecordProperty(absl::StrCat(phase, "_frees"), std::to_string(measured.frees));
  }
};

TEST_F(AllocationTest, CounterObservesAllocationAndFreeWithoutCountingAssertions) {
  auto measured = MeasureAllocations([] { return std::vector<char>(4'096, 'x'); });
  EXPECT_THAT(measured.value, SizeIs(4'096));
  EXPECT_THAT(measured.counts.allocations, Ge(1));
  EXPECT_THAT(measured.counts.requested_bytes, Ge(4'096));
  EXPECT_THAT(measured.counts.frees, Eq(0));
  const auto released = MeasureAllocations([&measured] {
    std::vector<char>().swap(measured.value);
    return true;
  });
  EXPECT_THAT(released.counts.allocations, Eq(0));
  EXPECT_THAT(released.counts.frees, Ge(1));
}

struct AllocationCase {
  std::string name;
  std::vector<std::string> arguments;
  int predicates = 1;
  ExpressionExecutor executor = ExpressionExecutor::kProduction;
  ExpressionWorkerRole role = ExpressionWorkerRole::kCoordinator;
  bool expected = true;
  bool payload_may_allocate = false;
};

std::vector<AllocationCase> AllocationCases() {
  struct Family {
    std::string_view name;
    std::vector<std::string> arguments;
    bool expected = true;
    bool payload_may_allocate = false;
  };

  const auto families = std::to_array<Family>({
      {
          .name = "Boolean",
          .arguments = {"-true"},
      },
      {
          .name = "Type",
          .arguments = {"-type", "f"},
      },
      {
          .name = "Size",
          .arguments = {"-size", "+1c"},
      },
      {
          .name = "Permission",
          .arguments = {"-perm", "0644"},
      },
      {
          .name = "Age",
          .arguments = {"-mtime", "0"},
      },
      {
          .name = "First",
          .arguments = {"-first", "2147483647"},
      },
      {
          .name = "Mime",
          .arguments = {"-mime", "TEXT/*"},
          .payload_may_allocate = true,
      },
      {
          .name = "Regex",
          .arguments = {"-regex", ".*file[0-9]+\\.txt"},
          .payload_may_allocate = true,
      },
      {
          .name = "UnreachedRegex",
          .arguments = {"-false", "-a", "-regex", "unused"},
          .expected = false,
      },
  });
  constexpr auto kExecutors = std::to_array({
      std::pair{"Tree", ExpressionExecutor::kTree},
      std::pair{"Production", ExpressionExecutor::kProduction},
      std::pair{"Program", ExpressionExecutor::kProgramOptimized},
  });
  constexpr auto kRoles = std::to_array({
      std::pair{"Coordinator", ExpressionWorkerRole::kCoordinator},
      std::pair{"Concurrent", ExpressionWorkerRole::kConcurrent},
  });
  std::vector<AllocationCase> result;
  result.reserve(families.size() * kExecutors.size() * kRoles.size() * 3);
  for (const auto& family : families) {
    for (const auto& [executor_name, executor] : kExecutors) {
      for (const auto& [role_name, role] : kRoles) {
        for (const int predicates : {1, 16, 64}) {
          result.push_back({
              .name = absl::StrCat(family.name, executor_name, role_name, predicates),
              .arguments = family.arguments,
              .predicates = predicates,
              .executor = executor,
              .role = role,
              .expected = family.expected,
              .payload_may_allocate = family.payload_may_allocate,
          });
        }
      }
    }
  }
  return result;
}

struct ExpressionAllocationTest
    : AllocationTest
    , ::testing::WithParamInterface<AllocationCase> {};

TEST_P(ExpressionAllocationTest, RecordsPreparationWorkerFirstEvaluationSteadyStateAndTeardown) {
  const auto& scenario = GetParam();
  std::vector<std::string> arguments{"."};
  arguments.reserve(1 + scenario.arguments.size() * static_cast<std::size_t>(scenario.predicates));
  for ([[maybe_unused]] const int index : std::views::iota(0, scenario.predicates)) {
    arguments.insert(arguments.end(), scenario.arguments.begin(), scenario.arguments.end());
  }
  auto parsing = MeasureAllocations([&arguments] { return parser::Parse(arguments); });
  ASSERT_OK_AND_ASSIGN(auto command, std::move(parsing.value));
  RecordCounts("parse", parsing.counts);
  const auto binding = MeasureAllocations([&command] {
    parser::BindMatchers(command, regex::Grammar::kRe2, parser::CaseMode::kSensitive);
    return true;
  });
  RecordCounts("bind", binding.counts);
  auto preparation = MeasureAllocations(
      [&command, &scenario] { return PrepareQualifiedExpression(*command.expression, scenario.executor); });
  ASSERT_OK_AND_ASSIGN(auto prepared, std::move(preparation.value));
  RecordCounts("prepare", preparation.counts);
  std::optional<ExpressionExecution> execution(std::move(prepared));
  auto worker_creation =
      MeasureAllocations([&execution, &scenario] { return execution.value().MakeWorker(scenario.role); });
  RecordCounts("worker", worker_creation.counts);
  std::optional<ExpressionExecution::Worker> worker(std::move(worker_creation.value));
  const AllocationFs fs;
  const vfs::Metadata metadata{.type = vfs::FileType::kRegular, .size = 7, .mode = 0644};
  const Visit visit{.path = "tree/file123.txt", .name = "file123.txt", .metadata = metadata, .fs = fs};
  // NOLINTNEXTLINE(misc-const-correctness): Evaluator callbacks mutate the referenced control.
  Control control;
  FirstCounts first_counts;
  const auto emit = [](std::string_view) {};
  EvalContext context{
      .visit = visit,
      .emit = emit,
      .fs = fs,
      .now = absl::UnixEpoch(),
      .tz = absl::UTCTimeZone(),
      .control = control,
      .first_counts = first_counts,
  };
  const auto first = MeasureAllocations([&worker, &context] { return worker.value().Evaluate(context); });
  EXPECT_THAT(first.value.matched, Eq(scenario.expected));
  EXPECT_THAT(first.value.unknown, IsFalse());
  RecordCounts("first", first.counts);
  const auto steady = MeasureAllocations([&worker, &context, &scenario] {
    bool equivalent = true;
    for ([[maybe_unused]] const int entry : std::views::iota(0, 128)) {
      const auto observed = worker.value().Evaluate(context);
      equivalent = equivalent && observed.matched == scenario.expected && !observed.unknown;
    }
    return equivalent;
  });
  EXPECT_THAT(steady.value, IsTrue());
  RecordCounts("steady", steady.counts);
  RecordProperty("allocation_diagnostic", "1");
  RecordProperty("predicates", scenario.predicates);
  RecordProperty("worker_role", scenario.role == ExpressionWorkerRole::kCoordinator ? "coordinator" : "concurrent");
  RecordProperty("steady_entries", 128);
  if (!scenario.payload_may_allocate) {
    EXPECT_THAT(steady.counts.allocations, Eq(0));
    EXPECT_THAT(steady.counts.requested_bytes, Eq(0));
  }
  const auto worker_teardown = MeasureAllocations([&worker] {
    worker.reset();
    return true;
  });
  RecordCounts("worker_teardown", worker_teardown.counts);
  const auto plan_teardown = MeasureAllocations([&execution] {
    execution.reset();
    return true;
  });
  RecordCounts("plan_teardown", plan_teardown.counts);
  const auto source_teardown = MeasureAllocations([&command, &first_counts] {
    command.expression.reset();
    first_counts.clear();
    return true;
  });
  RecordCounts("source_teardown", source_teardown.counts);
  EXPECT_THAT(worker_teardown.counts.frees + plan_teardown.counts.frees, Gt(0));
}

INSTANTIATE_TEST_SUITE_P(
    Phases,
    ExpressionAllocationTest,
    ValuesIn(AllocationCases()),
    [](const ::testing::TestParamInfo<AllocationCase>& info) { return info.param.name; });

}  // namespace
}  // namespace xff::engine
