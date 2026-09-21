// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/parallel_match.h"

#include <cstddef>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/parser/parser.h"
#include "xff/vfs/local_fs.h"

namespace xff::engine {
namespace {

using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::SizeIs;

struct ParallelMatchTest : ::testing::Test {
  // Type-only expressions never call this backend. No real paths are visited.
  const vfs::LocalFs fs;

  std::vector<CollectedEntry> Entries(std::size_t count) const {
    std::vector<CollectedEntry> entries;
    entries.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      entries.push_back({
          .path = std::to_string(index),
          .metadata = {.type = index % 2 == 0 ? vfs::FileType::kRegular : vfs::FileType::kDirectory},
          .fs = fs,
      });
    }
    return entries;
  }
};

TEST_F(ParallelMatchTest, RepeatedBatchesRetainEntryOrderAndDoNotReuseOldResults) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"root", "-type", "f"}));
  EXPECT_THAT(CanParallelMatch(*command.expression), IsTrue());
  EXPECT_THAT(HasContentMatch(*command.expression), IsFalse());
  ParallelMatch matcher(*command.expression, 4, false);
  for (const std::size_t count : {10, 1'000, 100, 10, 10'000}) {
    const auto& results = matcher.Match(Entries(count));
    EXPECT_THAT(results, SizeIs(count));
    EXPECT_THAT(matcher.Entries(), SizeIs(count));
    for (std::size_t index = 0; index < count; ++index) {
      EXPECT_THAT(results.at(index).matched, Eq(index % 2 == 0));
      EXPECT_THAT(matcher.Entries().at(index).path, Eq(std::to_string(index)));
    }
  }
}

TEST_F(ParallelMatchTest, ZeroWorkersIsClampedAndUnusedPoolStartsNoWork) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"root", "-true"}));
  const ParallelMatch unused(*command.expression, 4, false);
  ParallelMatch matcher(*command.expression, 0, true);
  EXPECT_THAT(matcher.Match(Entries(100)), SizeIs(100));
  EXPECT_THAT(matcher.Match(Entries(0)), SizeIs(0));
}

TEST_F(ParallelMatchTest, StatefulMetadataAndActionExpressionsCannotEnterWorkers) {
  const std::vector<std::vector<std::string>> expressions{
      {"root", "-first", "1"},
      {"root", "-size", "1c"},
      {"root", "-delete"},
      {"root", "-exec", "echo", "{}", ";"},
      {"root", "-type", "f", "-print"},
      {"root", "-true", "-o", "-quit"},
  };
  for (const auto& arguments : expressions) {
    MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse(arguments));
    EXPECT_THAT(CanParallelMatch(*command.expression), IsFalse());
  }
  MBO_ASSERT_OK_AND_ASSIGN(const auto content, parser::Parse({"root", "-type", "f", "-content", "needle"}));
  EXPECT_THAT(CanParallelMatch(*content.expression), IsTrue());
  EXPECT_THAT(HasContentMatch(*content.expression), IsTrue());
}

}  // namespace
}  // namespace xff::engine
