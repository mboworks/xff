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

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"
#include "xff/engine/run.h"
#include "xff/parser/parser.h"

namespace xff::engine {
namespace {
using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::Contains;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Not;

struct SearchFs final : vfs::FileSystem {
  std::map<std::string, std::string> files{{"tree/a", "hit\nmiss\n"}, {"tree/b", "miss\n"}, {"patterns", "hit\n"}};
  bool read_error = false;

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view path) const override {
    if (path != "tree") {
      return absl::NotFoundError("not a directory");
    }
    return std::vector<vfs::Entry>{
        {.path = "tree/a", .name = "a", .type = vfs::FileType::kRegular},
        {.path = "tree/b", .name = "b", .type = vfs::FileType::kRegular},
    };
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool) const override {
    if (path == "tree") {
      return vfs::Metadata{.type = vfs::FileType::kDirectory};
    }
    const auto found = files.find(std::string(path));
    if (found == files.end()) {
      return absl::NotFoundError("missing file");
    }
    return vfs::Metadata{.type = vfs::FileType::kRegular, .size = found->second.size()};
  }

  absl::Status Remove(std::string_view) const override {
    ADD_FAILURE() << "rg must never mutate the source";
    return absl::PermissionDeniedError("read-only");
  }

  bool Access(std::string_view, vfs::AccessMode mode) const override { return mode == vfs::AccessMode::kRead; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override {
    return absl::InvalidArgumentError("not a link");
  }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return "memory"; }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

  absl::StatusOr<std::string> ReadContent(std::string_view path) const override {
    if (read_error) {
      return absl::PermissionDeniedError("denied content");
    }
    const auto found = files.find(std::string(path));
    if (found == files.end()) {
      return absl::NotFoundError("missing content");
    }
    return found->second;
  }
};

struct RgEngineTest : ::testing::Test {
  SearchFs fs;
  std::string output;
  std::vector<std::string> errors;

  RunResult Run(std::vector<std::string> args) {
    args.insert(args.begin(), "--rg");
    auto parsed = parser::Parse(args);
    EXPECT_THAT(parsed, IsOk());
    if (!parsed.ok()) {
      return RunResult{.errors = 2};
    }
    auto command = *std::move(parsed);
    parser::BindMatchers(
        command, parser::GrammarFromGlobals(command.globals),
        parser::ResolveCaseMode(command.globals, registry::Style::kXff));
    output.clear();
    errors.clear();
    return RunFind(
        command, fs, [this](std::string_view text) { output += text; },
        [this](std::string_view, absl::Status status) { errors.emplace_back(status.message()); });
  }
};

TEST_F(RgEngineTest, PreparationRequiresAnRgSearch) {
  EXPECT_THAT(
      PrepareRgOutput({}, fs, false, false), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("not configured")));
}

TEST_F(RgEngineTest, VirtualPatternFilesAndEntriesUseTheSuppliedFilesystem) {
  const auto result = Run({"-f", "patterns", "tree", "--xff", "-name", "a"});
  EXPECT_THAT(result.errors, Eq(0));
  EXPECT_THAT(result.any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("tree/a:hit\n"));
}

TEST_F(RgEngineTest, ReadFailuresAreErrorsNotNoMatches) {
  fs.read_error = true;
  const auto result = Run({"hit", "tree"});
  EXPECT_THAT(result.errors, Eq(2));
  EXPECT_THAT(result.any_match, IsFalse());
  EXPECT_THAT(errors, Contains(HasSubstr("denied content")));
}

TEST_F(RgEngineTest, PatternReadFailuresStopBeforeTraversal) {
  fs.read_error = true;
  const auto result = Run({"-f", "patterns", "tree"});
  EXPECT_THAT(result.errors, Eq(2));
  EXPECT_THAT(result.any_match, IsFalse());
  EXPECT_THAT(errors.size(), Eq(1));
}

TEST_F(RgEngineTest, NonmatchingFilesAndDirectoriesDoNotSetSuccess) {
  const auto result = Run({"absent", "tree"});
  EXPECT_THAT(result.errors, Eq(0));
  EXPECT_THAT(result.any_match, IsFalse());
  EXPECT_THAT(output, IsEmpty());
}

TEST_F(RgEngineTest, EmptyPatternsAndInvertedPortionsFollowRg) {
  EXPECT_THAT(Run({"--count-matches", "^", "tree/a"}).any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("tree/a:2\n"));
  EXPECT_THAT(Run({"-vo", "hit", "tree/a"}).any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("tree/a:miss\n"));
}

TEST_F(RgEngineTest, SmartCaseAppliesToTheWholePatternUnion) {
  EXPECT_THAT(Run({"-S", "-e", "HIT", "-e", "MISS", "tree"}).any_match, IsFalse());
  EXPECT_THAT(Run({"-S", "-e", "HIT", "-e", "miss", "tree/a"}).any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("tree/a:miss\n"));
}

TEST_F(RgEngineTest, WordMatchingChecksAdjacentCharactersIncludingPunctuationPatterns) {
  fs.files.insert_or_assign("tree/a", "@\nword@word\n @ \n");
  EXPECT_THAT(Run({"-ow", "@", "tree/a"}).any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("tree/a:@\ntree/a:@\n"));
}

TEST_F(RgEngineTest, SummariesCountSearchSelectedFiles) {
  EXPECT_THAT(Run({"hit", "tree", "--summary"}).any_match, IsTrue());
  EXPECT_THAT(output, HasSubstr("1"));
  EXPECT_THAT(output, Not(HasSubstr("tree/a:hit")));
}

TEST_F(RgEngineTest, ListingModeStillFiltersByTheRgSearch) {
  EXPECT_THAT(Run({"hit", "tree", "--no-match-output"}).any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("tree/a\n"));
}

TEST_F(RgEngineTest, OutputLimitDoesNotLeakExtraContent) {
  fs.files.insert_or_assign("tree/b", "hit\n");
  EXPECT_THAT(Run({"hit", "tree", "--sort=global", "--max-results=1"}).any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("tree/a:hit\n"));
}

TEST_F(RgEngineTest, InvalidFormatsAndConflictingModesAreRejected) {
  EXPECT_THAT(Run({"hit\nmiss", "tree"}).errors, Eq(2));
  EXPECT_THAT(Run({"hit", "tree", "--format=csv"}).errors, Eq(2));
  EXPECT_THAT(Run({"hit", "tree", "--columns=path"}).errors, Eq(2));
  EXPECT_THAT(Run({"hit", "tree", "--compare"}).errors, Eq(2));
  EXPECT_THAT(Run({"hit", "tree", "--xff", "-delete"}).errors, Eq(2));
  EXPECT_THAT(Run({"hit", "tree", "--xff", "-exec", "echo", "{}", ";"}).errors, Eq(2));
}

TEST_F(RgEngineTest, OverlappingMatchesRespectInlineAndFilePatternOrder) {
  fs.files.insert_or_assign("patterns", "h\n");
  EXPECT_THAT(Run({"-o", "-f", "patterns", "-e", "hit", "tree/a"}).any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("tree/a:h\n"));
  EXPECT_THAT(Run({"-o", "-e", "hit", "-f", "patterns", "tree/a"}).any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("tree/a:hit\n"));
}

TEST_F(RgEngineTest, EmptyPatternFileJsonDoesNotInventAnEmptyPattern) {
  fs.files.insert_or_assign("patterns", "");
  EXPECT_THAT(Run({"-f", "patterns", "--files-without-match", "tree/a", "--format=jsonl"}).any_match, IsTrue());
  EXPECT_THAT(output, HasSubstr("\"patterns\":[]"));
}

TEST_F(RgEngineTest, ExplanationIdentifiesTheSeparateContentSearch) {
  ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"--rg", "hit", "tree"}));
  EXPECT_THAT(ExplainResources(command), IsOkAndHolds(HasSubstr("rg-search")));
}

TEST_F(RgEngineTest, JsonUsesTheExistingXffContentSchema) {
  EXPECT_THAT(Run({"hit", "tree", "--format=jsonl"}).any_match, IsTrue());
  EXPECT_THAT(output, HasSubstr("\"record\":\"grep\""));
  EXPECT_THAT(output, HasSubstr("\"path\":\"tree/a\""));
}
}  // namespace
}  // namespace xff::engine
