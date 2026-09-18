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

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"
#include "nlohmann/json.hpp"
#include "xff/env/env.h"
#include "xff/parser/parser.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"
#include "xff/vfs/local_fs.h"

namespace xff::engine {
namespace {

namespace fs = ::std::filesystem;
using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::mbo::testing::WithDropIndent;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::MatchesRegex;
using ::testing::Ne;
using ::testing::Not;
using ::testing::PrintToString;
using ::testing::SizeIs;
using ::testing::StartsWith;
using ::testing::UnorderedElementsAre;

// Fixture tree:
//   <root>/a.txt
//   <root>/b.md
//   <root>/sub/c.txt
// Arbitrary path bytes must be tested without depending on a host filesystem's name rules.
struct BytePathFs final : vfs::FileSystem {
  const std::string name = std::string("bad") + static_cast<char>(0xff) + ".txt";
  const std::string path = "left/" + name;
  const std::string content = std::string("hit ") + static_cast<char>(0xff) + "\n";

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view dir) const override {
    if (dir == "left") {
      return std::vector<vfs::Entry>{{.path = path, .name = name, .type = vfs::FileType::kRegular}};
    }
    if (dir == "right") {
      return std::vector<vfs::Entry>{};
    }
    return absl::NotFoundError("unknown virtual directory");
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view requested, bool) const override {
    if (requested == "left" || requested == "right") {
      return vfs::Metadata{.type = vfs::FileType::kDirectory};
    }
    if (requested == path) {
      return vfs::Metadata{.type = vfs::FileType::kRegular, .size = content.size()};
    }
    return absl::NotFoundError("unknown virtual entry");
  }

  absl::Status Remove(std::string_view) const override {
    ADD_FAILURE() << "Structured-output tests must not mutate their input";
    return absl::PermissionDeniedError("read-only fixture");
  }

  bool Access(std::string_view requested, vfs::AccessMode mode) const override {
    return requested == path && mode == vfs::AccessMode::kRead;
  }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override { return absl::NotFoundError("not a link"); }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return "memory"; }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

  absl::StatusOr<std::string> ReadContent(std::string_view requested) const override {
    return requested == path ? absl::StatusOr<std::string>(content) : absl::NotFoundError("unknown virtual content");
  }
};

struct RunTest : ::testing::Test {
  void SetUp() override {
    root_ = fs::path(::testing::TempDir())
            / (std::string("xff_run_") + ::testing::UnitTest::GetInstance()->current_test_info()->name());
    std::error_code ec;
    fs::remove_all(root_, ec);
    ASSERT_THAT(fs::create_directories(root_ / "sub"), IsTrue());
    { std::ofstream(root_ / "a.txt") << "a"; }
    { std::ofstream(root_ / "b.md") << "b"; }
    { std::ofstream(root_ / "sub" / "c.txt") << "c"; }
  }

  void TearDown() override {
    env::ClearForTesting();
    std::error_code ec;
    fs::remove_all(root_, ec);
  }

  std::string Path(std::string_view child) const { return (root_ / child).string(); }

  // Parses `<root> <expr...>`, runs it, and returns the emitted records with a
  // single trailing terminator ('\n' or '\0') stripped.
  std::vector<std::string> RunExpr(const std::vector<std::string>& expr) {
    std::vector<std::string> argv = {root_.string()};
    argv.insert(argv.end(), expr.begin(), expr.end());
    auto command = parser::Parse(argv);
    EXPECT_THAT(command, IsOk());
    parser::BindMatchers(
        *command, parser::GrammarFromGlobals(command->globals),
        parser::ResolveCaseMode(command->globals, registry::Style::kXff));
    std::vector<std::string> records;
    last_errors_ = RunFind(
                       *command, fs_,
                       [&](std::string_view record) {
                         std::string text(record);
                         if (!text.empty() && (text.back() == '\n' || text.back() == '\0')) {
                           text.pop_back();
                         }
                         records.push_back(std::move(text));
                       },
                       [](std::string_view, absl::Status) {})
                       .errors;
    return records;
  }

  // Like RunExpr, but takes the whole argv (so leading globals such as --summary
  // can come before the root), returning the emitted records, terminator stripped.
  std::vector<std::string> RunArgvRecords(const std::vector<std::string>& argv) { return RunArgvRecords(argv, fs_); }

  std::vector<std::string> RunArgvRecords(const std::vector<std::string>& argv, const vfs::FileSystem& filesystem) {
    auto command = parser::Parse(argv);
    EXPECT_THAT(command, IsOk());
    std::vector<std::string> records;
    if (!command.ok()) {
      return records;
    }
    parser::BindMatchers(
        *command, parser::GrammarFromGlobals(command->globals),
        parser::ResolveCaseMode(command->globals, registry::Style::kXff));
    last_errors_ = RunFind(
                       *command, filesystem,
                       [&](std::string_view record) {
                         std::string text(record);
                         if (!text.empty() && (text.back() == '\n' || text.back() == '\0')) {
                           text.pop_back();
                         }
                         records.push_back(std::move(text));
                       },
                       [](std::string_view, absl::Status) {})
                       .errors;
    return records;
  }

  // Runs the bare root under `style` to exercise the style-scoped traversal
  // defaults (RunFind's `style`), returning records with the terminator stripped.
  std::vector<std::string> RunStyled(registry::Style style) const {
    const auto command = parser::Parse({root_.string()});
    EXPECT_THAT(command, IsOk());
    std::vector<std::string> records;
    if (!command.ok()) {
      return records;
    }
    RunFind(
        *command, fs_,
        [&](std::string_view record) {
          std::string text(record);
          if (!text.empty() && (text.back() == '\n' || text.back() == '\0')) {
            text.pop_back();
          }
          records.push_back(std::move(text));
        },
        [](std::string_view, absl::Status) {}, style);
    return records;
  }

  vfs::LocalFs fs_;
  fs::path root_;
  int last_errors_ = 0;
};

TEST_F(RunTest, SummaryJsonIdentifiesRepeatedAndTemplateRequests) {
  const auto records = RunArgvRecords(
      {root_.string(), "-type", "f", "--summary=ext", "--summary=ext", "--summary={name}", "--summary={ext}",
       "--format=jsonl"});
  EXPECT_THAT(last_errors_, Eq(0));
  std::map<std::size_t, std::string> identities;
  for (const auto& record : records) {
    const auto row = nlohmann::json::parse(record);
    EXPECT_THAT(row.at("record").get<std::string>(), Eq("summary"));
    EXPECT_THAT(row.at("scope").get<std::string>(), Eq("all"));
    const auto grouping = row.at("summary").get<std::string>();
    identities[row.at("request").get<std::size_t>()] =
        grouping == "template" ? row.at("template").get<std::string>() : grouping;
  }
  const std::map<std::size_t, std::string> expected = {{0, "ext"}, {1, "ext"}, {2, "{name}"}, {3, "{ext}"}};
  EXPECT_THAT(identities, Eq(expected));
}

TEST_F(RunTest, SummaryJsonCanonicalizesAliasesWithoutMergingRequests) {
  const auto records = RunArgvRecords(
      {root_.string(), "-type", "f", "--summary=owner", "--summary=user", "--summary=group", "--summary=hash",
       "--format=jsonl"});
  EXPECT_THAT(last_errors_, Eq(0));
  std::map<std::size_t, std::string> identities;
  for (const auto& record : records) {
    const auto row = nlohmann::json::parse(record);
    identities[row.at("request").get<std::size_t>()] = row.at("summary").get<std::string>();
  }
  const std::map<std::size_t, std::string> expected = {{0, "user"}, {1, "user"}, {2, "group"}, {3, "hash"}};
  EXPECT_THAT(identities, Eq(expected));
}

TEST_F(RunTest, ComparisonSummaryJsonPreservesRequestIndicesAndRoots) {
  const auto records = RunArgvRecords(
      {"--compare=summary", root_.string(), Path("sub"), "--summary=ext", "--summary=type", "--format=jsonl"});
  EXPECT_THAT(last_errors_, Eq(0));
  std::map<std::size_t, std::string> identities;
  for (const auto& record : records) {
    const auto row = nlohmann::json::parse(record);
    EXPECT_THAT(row.at("record").get<std::string>(), Eq("summary"));
    EXPECT_THAT(row.at("left_root").get<std::string>(), Eq(root_.string()));
    EXPECT_THAT(row.at("right_root").get<std::string>(), Eq(Path("sub")));
    identities[row.at("request").get<std::size_t>()] = row.at("summary").get<std::string>();
  }
  const std::map<std::size_t, std::string> expected = {{0, "compare"}, {1, "ext"}, {2, "type"}};
  EXPECT_THAT(identities, Eq(expected));
}

TEST_F(RunTest, SummaryJsonResetRestartsIdentityAndRootScopeNamesItsRoot) {
  const auto records = RunArgvRecords(
      {root_.string(), Path("sub"), "-type", "f", "--summary=ext", "--summary=none", "--summary=type",
       "--summary-scope=root", "--format=jsonl"});
  EXPECT_THAT(last_errors_, Eq(0));
  std::map<std::string, std::size_t> roots;
  for (const auto& record : records) {
    const auto row = nlohmann::json::parse(record);
    EXPECT_THAT(row.at("request").get<std::size_t>(), Eq(0));
    EXPECT_THAT(row.at("summary").get<std::string>(), Eq("type"));
    EXPECT_THAT(row.at("scope").get<std::string>(), Eq("root"));
    ++roots[row.at("root").get<std::string>()];
  }
  const std::map<std::string, std::size_t> expected = {{root_.string(), 2}, {Path("sub"), 2}};
  EXPECT_THAT(roots, Eq(expected));
}

TEST_F(RunTest, SummaryJsonIndicesFollowRequestsRatherThanEmissionOrder) {
  const auto records =
      RunArgvRecords({"--summary=ext", "--compare=summary", root_.string(), Path("sub"), "--format=jsonl"});
  ASSERT_THAT(records, Not(IsEmpty()));
  EXPECT_THAT(last_errors_, Eq(0));
  const auto first = nlohmann::json::parse(records.front());
  EXPECT_THAT(first.at("summary").get<std::string>(), Eq("compare"));
  EXPECT_THAT(first.at("request").get<std::size_t>(), Eq(1));
  const auto last = nlohmann::json::parse(records.back());
  EXPECT_THAT(last.at("summary").get<std::string>(), Eq("ext"));
  EXPECT_THAT(last.at("request").get<std::size_t>(), Eq(0));
}

TEST_F(RunTest, SummaryTotalMarkerDoesNotReserveGroupNames) {
  ASSERT_THAT(fs_.WriteContent(Path("total"), "abc"), IsOk());
  ASSERT_THAT(fs_.WriteContent(Path("(none)"), "x"), IsOk());
  const auto records =
      RunArgvRecords({root_.string(), "-type", "f", "--summary={name}", "--summary={def.MISSING}", "--format=jsonl"});
  EXPECT_THAT(last_errors_, Eq(0));
  std::size_t totals = 0;
  bool literal_total = false;
  bool literal_none = false;
  bool empty_key = false;
  for (const auto& record : records) {
    const auto row = nlohmann::json::parse(record);
    if (row.at("is_total").get<bool>()) {
      ++totals;
      EXPECT_THAT(row.at("group").get<std::string>(), Eq("total"));
      EXPECT_THAT(row.at("count").get<std::size_t>(), Eq(5));
    } else {
      const auto key = row.at("group").get<std::string>();
      literal_total |= key == "total";
      literal_none |= key == "(none)";
      empty_key |= key.empty();
    }
  }
  EXPECT_THAT(totals, Eq(2));
  EXPECT_THAT(literal_total, IsTrue());
  EXPECT_THAT(literal_none, IsTrue());
  EXPECT_THAT(empty_key, IsTrue());
}

TEST_F(RunTest, ComparisonScopeTotalMarkerDistinguishesLiteralTotal) {
  ASSERT_THAT(fs_.WriteContent(Path("total"), "abc"), IsOk());
  const auto records = RunArgvRecords(
      {"--compare=status", "--compare-select=none", root_.string(), Path("sub"), "-type", "f", "--summary={name}",
       "--format=jsonl"});
  EXPECT_THAT(last_errors_, Eq(0));
  std::size_t literal_rows = 0;
  std::size_t total_rows = 0;
  for (const auto& record : records) {
    const auto row = nlohmann::json::parse(record);
    if (row.at("group").get<std::string>() == "total") {
      if (row.at("is_total").get<bool>()) {
        ++total_rows;
      } else {
        ++literal_rows;
      }
    }
  }
  EXPECT_THAT(literal_rows, Eq(1));
  EXPECT_THAT(total_rows, Eq(1));
}

TEST_F(RunTest, SummaryDisplayQuotesAmbiguousDataLabels) {
  ASSERT_THAT(fs_.WriteContent(Path("total"), "abc"), IsOk());
  ASSERT_THAT(fs_.WriteContent(Path("\"total\""), "x"), IsOk());
  const auto formats = std::to_array<std::string>({"--format=plain", "--format=md"});
  for (const auto& format : formats) {
    const auto records =
        RunArgvRecords({root_.string(), "-name", "*total*", "--summary={name}", "--summary={def.MISSING}", format});
    EXPECT_THAT(last_errors_, Eq(0));
    EXPECT_THAT(records, Contains(HasSubstr("\"total\"")));
    EXPECT_THAT(records, Contains(HasSubstr("\"\"")));
    EXPECT_THAT(records, Contains(HasSubstr(R"json("\"total\"")json")));
  }
}

TEST_F(RunTest, ComparisonScopeDisplayQuotesAmbiguousDataLabels) {
  ASSERT_THAT(fs_.WriteContent(Path("total"), "abc"), IsOk());
  ASSERT_THAT(fs_.WriteContent(Path("\"total\""), "x"), IsOk());
  const auto formats = std::to_array<std::string>({"--format=plain", "--format=md"});
  for (const auto& format : formats) {
    const auto records = RunArgvRecords(
        {"--compare=status", "--compare-select=none", root_.string(), Path("sub"), "-name", "*total*",
         "--summary={name}", "--summary={def.MISSING}", format});
    EXPECT_THAT(last_errors_, Eq(0));
    EXPECT_THAT(records, Contains(HasSubstr("\"total\"")));
    EXPECT_THAT(records, Contains(HasSubstr("\"\"")));
    EXPECT_THAT(records, Contains(HasSubstr(R"json("\"total\"")json")));
  }
}

TEST_F(RunTest, StructuredProducersPreserveNonUtf8PathsAndGrepText) {
  const BytePathFs filesystem;
  const auto grep = RunArgvRecords({filesystem.path, "-grep", "hit", "--summary={name}", "--format=jsonl"}, filesystem);
  EXPECT_THAT(last_errors_, Eq(0));
  ASSERT_THAT(grep, SizeIs(3));
  const auto match = nlohmann::json::parse(grep.front());
  EXPECT_THAT(match.at("text").at("data").get<std::string>(), Eq("aGl0IP8="));
  EXPECT_THAT(match.at("path").at("encoding").get<std::string>(), Eq("base64"));
  const auto group = nlohmann::json::parse(grep.at(1));
  EXPECT_THAT(group.at("group").at("data").get<std::string>(), Eq("YmFk/y50eHQ="));
  const auto comparison = RunArgvRecords({"--compare", "left", "right", "-type", "f", "--format=jsonl"}, filesystem);
  EXPECT_THAT(last_errors_, Eq(0));
  ASSERT_THAT(comparison, SizeIs(1));
  const auto row = nlohmann::json::parse(comparison.front());
  EXPECT_THAT(row.at("path").at("data").get<std::string>(), Eq("YmFk/y50eHQ="));
}

TEST_F(RunTest, CompareJsonlStatusAndSummaryFormOneJsonStream) {
  ASSERT_THAT(fs_.WriteContent(Path("a\"name.txt"), "quoted"), IsOk());
  const auto records = RunArgvRecords(
      {"--compare", root_.string(), Path("sub"), "-type", "f", "--compare-select=all", "--summary=ext",
       "--format=jsonl"});
  EXPECT_THAT(last_errors_, Eq(0));
  std::size_t comparisons = 0;
  std::size_t summaries = 0;
  bool quoted_path = false;
  for (const auto& record : records) {
    const auto row = nlohmann::json::parse(record);
    if (row.contains("record") && row.at("record") == "comparison") {
      ++comparisons;
      EXPECT_THAT(row.at("left_root").get<std::string>(), Eq(root_.string()));
      EXPECT_THAT(row.at("right_root").get<std::string>(), Eq(Path("sub")));
      EXPECT_THAT(row.at("status").get<std::string>(), Ne(""));
      quoted_path |= row.at("path") == "a\"name.txt";
    } else {
      ++summaries;
      EXPECT_THAT(row.contains("group"), IsTrue());
    }
  }
  EXPECT_THAT(comparisons, Eq(5));
  EXPECT_THAT(summaries, Eq(3));
  EXPECT_THAT(quoted_path, IsTrue());
}

TEST_F(RunTest, UnsupportedComparisonFormatsFailBeforeActions) {
  constexpr auto kFormats = std::to_array<std::string_view>(
      {"--format=csv", "--format=tsv", "--format=md", "--format=aligned", "--format=nul", "--format=tree"});
  for (const auto format : kFormats) {
    EXPECT_THAT(
        RunArgvRecords({"--compare", root_.string(), Path("sub"), std::string(format), "-type", "f", "-delete"}),
        IsEmpty());
    EXPECT_THAT(last_errors_, Eq(2));
    EXPECT_THAT(fs_.ReadContent(Path("a.txt")), IsOkAndHolds(Eq("a")));
    EXPECT_THAT(fs_.ReadContent(Path("sub/c.txt")), IsOkAndHolds(Eq("c")));
  }
  EXPECT_THAT(
      RunArgvRecords({"--compare=diff", root_.string(), Path("sub"), "--format=jsonl", "-type", "f", "-delete"}),
      IsEmpty());
  EXPECT_THAT(last_errors_, Eq(2));
  EXPECT_THAT(fs_.ReadContent(Path("a.txt")), IsOkAndHolds(Eq("a")));
  EXPECT_THAT(fs_.ReadContent(Path("sub/c.txt")), IsOkAndHolds(Eq("c")));
}

TEST_F(RunTest, GrepJsonlContextAndSummaryFormOneJsonStream) {
  ASSERT_THAT(
      fs_.WriteContent(Path("grep.txt"), "before\nhit \"quoted\"\nafter\ngap\ngap\nbefore\nhit\nafter\n"), IsOk());
  const auto records =
      RunArgvRecords({Path("grep.txt"), "-grep", "hit", "--context=1", "--summary=ext", "--format=jsonl"});
  EXPECT_THAT(last_errors_, Eq(0));
  std::vector<std::size_t> numbers;
  std::vector<std::size_t> groups;
  std::vector<std::string> kinds;
  std::size_t summaries = 0;
  bool quoted_text = false;
  for (const auto& record : records) {
    const auto row = nlohmann::json::parse(record);
    if (row.contains("record") && row.at("record") == "grep") {
      numbers.push_back(row.at("line").get<std::size_t>());
      groups.push_back(row.at("group").get<std::size_t>());
      kinds.push_back(row.at("kind").get<std::string>());
      EXPECT_THAT(row.at("path").get<std::string>(), Eq(Path("grep.txt")));
      EXPECT_THAT(row.at("pattern").get<std::string>(), Eq("hit"));
      quoted_text |= row.at("text") == "hit \"quoted\"";
    } else {
      ++summaries;
      EXPECT_THAT(row.contains("group"), IsTrue());
    }
  }
  EXPECT_THAT(numbers, ElementsAre(1, 2, 3, 6, 7, 8));
  EXPECT_THAT(groups, ElementsAre(0, 0, 0, 1, 1, 1));
  EXPECT_THAT(kinds, ElementsAre("context", "match", "context", "context", "match", "context"));
  EXPECT_THAT(summaries, Eq(2));
  EXPECT_THAT(quoted_text, IsTrue());
}

TEST_F(RunTest, GrepJsonlCountAndExplicitTemplatesKeepTheirContracts) {
  ASSERT_THAT(fs_.WriteContent(Path("grep.txt"), "hit\nno\nhit\n"), IsOk());
  const auto records = RunArgvRecords({Path("grep.txt"), "-grep", "hit", "--count", "--context=1", "--format=jsonl"});
  ASSERT_THAT(records, SizeIs(1));
  const auto row = nlohmann::json::parse(records.front());
  EXPECT_THAT(row.at("record").get<std::string>(), Eq("grep"));
  EXPECT_THAT(row.at("kind").get<std::string>(), Eq("count"));
  EXPECT_THAT(row.at("count").get<std::size_t>(), Eq(2));
  EXPECT_THAT(row.at("path").get<std::string>(), Eq(Path("grep.txt")));
  EXPECT_THAT(last_errors_, Eq(0));
  EXPECT_THAT(RunArgvRecords({Path("grep.txt"), "-grep:{line}", "hit", "--format=jsonl"}), ElementsAre("1", "3"));
  EXPECT_THAT(last_errors_, Eq(0));
}

TEST_F(RunTest, GrepSummariesAndHistogramsKeepDistinctJsonSchemas) {
  const auto records =
      RunArgvRecords({Path("a.txt"), "-grep", "a", "--summary=ext", "--histogram=ext", "--format=jsonl"});
  EXPECT_THAT(last_errors_, Eq(0));
  ASSERT_THAT(records, SizeIs(4));
  const auto match = nlohmann::json::parse(records.front());
  EXPECT_THAT(match.at("record").get<std::string>(), Eq("grep"));
  const auto summary = nlohmann::json::parse(records.at(1));
  EXPECT_THAT(summary.at("record").get<std::string>(), Eq("summary"));
  EXPECT_THAT(summary.at("is_total").get<bool>(), IsFalse());
  const auto total = nlohmann::json::parse(records.at(2));
  EXPECT_THAT(total.at("is_total").get<bool>(), IsTrue());
  const auto histogram = nlohmann::json::parse(records.back());
  EXPECT_THAT(histogram.at("bucket").get<std::string>(), Eq("txt"));
  EXPECT_THAT(histogram.at("value").get<int>(), Eq(1));
}

TEST_F(RunTest, ExplicitPrintfRetainsAuthoredOutputBesideJsonSummaries) {
  const auto records = RunArgvRecords({Path("a.txt"), "-printf", "authored\n", "--summary=ext", "--format=jsonl"});
  EXPECT_THAT(last_errors_, Eq(0));
  ASSERT_THAT(records, SizeIs(3));
  EXPECT_THAT(records.front(), Eq("authored"));
  EXPECT_THAT(nlohmann::json::parse(records.at(1)).at("record").get<std::string>(), Eq("summary"));
}

TEST_F(RunTest, UnsupportedGrepFormatsFailBeforeActions) {
  constexpr auto kFormats = std::to_array<std::string_view>(
      {"--format=csv", "--format=tsv", "--format=md", "--format=aligned", "--format=nul", "--format=tree"});
  for (const auto format : kFormats) {
    EXPECT_THAT(
        RunArgvRecords({Path("a.txt"), "-delete", ",", "-grep", "a", std::string(format), "--summary=ext"}), IsEmpty());
    EXPECT_THAT(last_errors_, Eq(2));
    EXPECT_THAT(fs_.ReadContent(Path("a.txt")), IsOkAndHolds(Eq("a")));
  }
}

TEST_F(RunTest, NoExpressionPrintsEverything) {
  EXPECT_THAT(
      RunExpr({}), UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, CompareUsesTheRequestedIgnorePolicyIndependentlyOnEachSide) {
  const fs::path left = root_ / "left";
  const fs::path right = root_ / "right";
  ASSERT_THAT(fs::create_directories(left / "nested"), IsTrue());
  ASSERT_THAT(fs::create_directories(right / "nested"), IsTrue());
  { std::ofstream(left / ".gitignore") << "left-ignored\n"; }
  { std::ofstream(right / ".gitignore") << "right-ignored\n"; }
  { std::ofstream(left / "same") << "same"; }
  { std::ofstream(right / "same") << "same"; }
  { std::ofstream(left / "changed", std::ios::binary) << std::string("a\0b", 3); }
  { std::ofstream(right / "changed", std::ios::binary) << std::string("a\0c", 3); }
  { std::ofstream(left / "left-only") << "left"; }
  { std::ofstream(right / "right-only") << "right"; }
  { std::ofstream(left / "left-ignored") << "ignored"; }
  { std::ofstream(right / "right-ignored") << "ignored"; }

  EXPECT_THAT(
      RunArgvRecords({"--compare", left.string(), right.string()}),
      ElementsAre(
          "different\t.gitignore", "different\tchanged", "left-only\tleft-ignored", "left-only\tleft-only",
          "right-only\tright-ignored", "right-only\tright-only"));
  EXPECT_THAT(last_errors_, 0);

  EXPECT_THAT(
      RunArgvRecords({"--compare", "--gitignore=on", left.string(), right.string()}),
      ElementsAre("different\t.gitignore", "different\tchanged", "left-only\tleft-only", "right-only\tright-only"));
  EXPECT_THAT(last_errors_, 0);

  EXPECT_THAT(
      RunArgvRecords({"--compare=status", "--compare-select=identical", left.string(), right.string()}),
      ElementsAre("identical\t.", "identical\tnested", "identical\tsame"));
  EXPECT_THAT(last_errors_, 0);

  const std::vector<std::string> patch =
      RunArgvRecords({"--compare=diff", "--compare-select=different", left.string(), right.string()});
  EXPECT_THAT(patch, Contains(HasSubstr("--- a/.gitignore\n+++ b/.gitignore")));
  EXPECT_THAT(patch, Contains("Binary files a/changed and b/changed differ"));
  EXPECT_THAT(last_errors_, 0);

  const std::vector<std::string> complete_patch = RunArgvRecords({"--compare=diff", left.string(), right.string()});
  EXPECT_THAT(complete_patch, Contains(HasSubstr("--- a/left-only\n+++ /dev/null")));
  EXPECT_THAT(complete_patch, Contains(HasSubstr("--- /dev/null\n+++ b/right-only")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, CompareRequiresTwoRootsAndAppliesTheExpressionToBoth) {
  EXPECT_THAT(RunArgvRecords({"--compare", root_.string()}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
  const fs::path left = root_ / "expression-left";
  const fs::path right = root_ / "expression-right";
  ASSERT_THAT(fs::create_directories(left), IsTrue());
  ASSERT_THAT(fs::create_directories(right), IsTrue());
  { std::ofstream(left / "selected.txt") << "left"; }
  { std::ofstream(right / "selected.txt") << "right"; }
  { std::ofstream(left / "excluded.md") << "left"; }
  { std::ofstream(right / "excluded.md") << "right"; }
  EXPECT_THAT(
      RunArgvRecords({"--compare", left.string(), right.string(), "-name", "*.txt"}),
      ElementsAre("different\tselected.txt"));
  EXPECT_THAT(last_errors_, 0);
  EXPECT_THAT(
      RunArgvRecords({"--compare", left.string(), right.string(), "-name", "*.txt", "-printf", "%f\\n"}),
      UnorderedElementsAre("selected.txt", "selected.txt", "different\tselected.txt"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, CompareSelectsEveryResultKind) {
  const fs::path left = root_ / "select-left";
  const fs::path right = root_ / "select-right";
  ASSERT_THAT(fs::create_directories(left), IsTrue());
  ASSERT_THAT(fs::create_directories(right), IsTrue());
  { std::ofstream(left / "left") << "left"; }
  { std::ofstream(right / "right") << "right"; }
  { std::ofstream(left / "same") << "same"; }
  { std::ofstream(right / "same") << "same"; }
  { std::ofstream(left / "different") << "old"; }
  { std::ofstream(right / "different") << "newer"; }

  EXPECT_THAT(
      RunArgvRecords({"--compare", "--compare-select=all", left.string(), right.string()}),
      ElementsAre("identical\t.", "different\tdifferent", "left-only\tleft", "right-only\tright", "identical\tsame"));
  EXPECT_THAT(
      RunArgvRecords({"--compare", "--compare-select=left-only,right-only", left.string(), right.string()}),
      ElementsAre("left-only\tleft", "right-only\tright"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, CompareStatusHonorsPathEncoding) {
  const fs::path left = root_ / "encoding-left";
  const fs::path right = root_ / "encoding-right";
  ASSERT_THAT(fs::create_directories(left), IsTrue());
  ASSERT_THAT(fs::create_directories(right), IsTrue());
  { std::ofstream(left / "line\nbreak\tvalue") << "left"; }
  { std::ofstream(right / "same\npath") << "same"; }
  { std::ofstream(left / "same\npath") << "same"; }

  EXPECT_THAT(
      RunArgvRecords({"--compare", "--path-encoding=escape", left.string(), right.string()}),
      ElementsAre("left-only\tline\\nbreak\\tvalue"));
  EXPECT_THAT(
      RunArgvRecords(
          {"--compare", "--compare-select=identical", "--path-encoding=escape", left.string(), right.string()}),
      ElementsAre("identical\t.", "identical\tsame\\npath"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, CompareHandlesFileKindsAndTraversalOptions) {
  const fs::path left = root_ / "kinds-left";
  const fs::path right = root_ / "kinds-right";
  ASSERT_THAT(fs::create_directories(left / ".hidden-dir"), IsTrue());
  ASSERT_THAT(fs::create_directories(right / ".hidden-dir"), IsTrue());
  ASSERT_THAT(fs::create_directories(left / ".git"), IsTrue());
  ASSERT_THAT(fs::create_directories(right / ".git"), IsTrue());
  ASSERT_THAT(fs::create_directories(left / "type-change"), IsTrue());
  { std::ofstream(right / "type-change") << "file"; }
  { std::ofstream(left / "size-change") << "short"; }
  { std::ofstream(right / "size-change") << "considerably longer"; }
  { std::ofstream(left / ".hidden") << "left"; }
  { std::ofstream(right / ".hidden") << "right"; }
  { std::ofstream(left / ".hidden-dir/value") << "left"; }
  { std::ofstream(right / ".hidden-dir/value") << "right"; }
  { std::ofstream(left / ".git/value") << "left"; }
  { std::ofstream(right / ".git/value") << "right"; }
  { std::ofstream(left / ".hg") << "left"; }
  { std::ofstream(right / ".hg") << "right"; }
  fs::create_symlink("target-a", left / "link");
  fs::create_symlink("target-b", right / "link");

  EXPECT_THAT(
      RunArgvRecords({"--compare", "--no-hidden", left.string(), right.string()}),
      ElementsAre("different\tlink", "different\tsize-change", "different\ttype-change"));
  EXPECT_THAT(
      RunArgvRecords({"--compare=diff", "--compare-select=different", left.string(), right.string()}),
      Contains("Files a/link and b/link differ"));
  EXPECT_THAT(
      RunArgvRecords({"--compare", "--exclude=size-change", left.string(), right.string()}),
      Not(Contains("different\tsize-change")));
  EXPECT_THAT(
      RunArgvRecords({"--compare", "--skip-vcs=hg", left.string(), right.string()}), Not(Contains("different\t.hg")));
  EXPECT_THAT(
      RunArgvRecords({"--compare", "--no-ignore", left.string(), right.string()}), Contains("different\t.git/value"));
  EXPECT_THAT(RunArgvRecords({"-u", "--compare", left.string(), right.string()}), Contains("different\t.git/value"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, CompareRootSymlinksFollowTheRequestedTraversalMode) {
  const fs::path left_target = root_ / "symlink-left-target";
  const fs::path right_target = root_ / "symlink-right-target";
  ASSERT_THAT(fs::create_directories(left_target), IsTrue());
  ASSERT_THAT(fs::create_directories(right_target), IsTrue());
  { std::ofstream(left_target / "value") << "same"; }
  { std::ofstream(right_target / "value") << "same"; }
  const fs::path left = root_ / "symlink-left";
  const fs::path right = root_ / "symlink-right";
  fs::create_symlink(left_target.filename(), left);
  fs::create_symlink(right_target.filename(), right);

  EXPECT_THAT(RunArgvRecords({"--compare", left.string(), right.string()}), ElementsAre("different\t."));
  EXPECT_THAT(RunArgvRecords({"-H", "--compare", left.string(), right.string()}), IsEmpty());
  EXPECT_THAT(RunArgvRecords({"-L", "--compare", left.string(), right.string()}), IsEmpty());
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, CompareAppliesExplicitAndGlobalIgnoreFiles) {
  const fs::path left = root_ / "ignore-left";
  const fs::path right = root_ / "ignore-right";
  const fs::path config = root_ / "config";
  ASSERT_THAT(fs::create_directories(left), IsTrue());
  ASSERT_THAT(fs::create_directories(right), IsTrue());
  ASSERT_THAT(fs::create_directories(config / "git"), IsTrue());
  { std::ofstream(left / "global") << "left"; }
  { std::ofstream(right / "global") << "right"; }
  { std::ofstream(left / "explicit") << "left"; }
  { std::ofstream(right / "explicit") << "right"; }
  { std::ofstream(config / "git/ignore") << "global\n"; }
  const fs::path explicit_ignore = root_ / "extra.ignore";
  { std::ofstream(explicit_ignore) << "ignore-left/explicit\nignore-right/explicit\n"; }
  env::SetForTesting("XDG_CONFIG_HOME", config.string());

  EXPECT_THAT(
      RunArgvRecords(
          {"--compare", "--gitignore=on", "--ignore-file=" + explicit_ignore.string(), left.string(), right.string()}),
      IsEmpty());
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, CompareValidatesRootsSelectionsAndDiffOptions) {
  const fs::path left = root_ / "options-left";
  const fs::path right = root_ / "options-right";
  ASSERT_THAT(fs::create_directories(left), IsTrue());
  ASSERT_THAT(fs::create_directories(right), IsTrue());
  { std::ofstream(left / "value") << "old\n"; }
  { std::ofstream(right / "value") << "new\n"; }

  EXPECT_THAT(RunArgvRecords({"--compare", (root_ / "missing").string(), right.string()}), IsEmpty());
  EXPECT_THAT(last_errors_, 1);
  EXPECT_THAT(RunArgvRecords({"--compare", left.string(), (root_ / "missing").string()}), IsEmpty());
  EXPECT_THAT(last_errors_, 1);
  const fs::path missing = root_ / "missing-on-both-sides";
  EXPECT_THAT(RunArgvRecords({"--compare", missing.string(), missing.string()}), IsEmpty());
  EXPECT_THAT(last_errors_, 1);
  EXPECT_THAT(
      RunArgvRecords({"--compare", (root_ / "a.txt").string(), right.string()}),
      ElementsAre("different\t.", "right-only\tvalue"));
  EXPECT_THAT(last_errors_, 0);
  EXPECT_THAT(RunArgvRecords({"--compare", "--compare-select=unknown", left.string(), right.string()}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
  EXPECT_THAT(RunArgvRecords({"--compare", "--skip-vcs=unknown", left.string(), right.string()}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
  EXPECT_THAT(
      RunArgvRecords({"--compare=diff", "--compare-select=identical", left.string(), right.string()}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
  EXPECT_THAT(RunArgvRecords({"--compare=diff", "--diff-algorithm=unknown", left.string(), right.string()}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
  EXPECT_THAT(RunArgvRecords({"--compare=diff", "--diff-context=invalid", left.string(), right.string()}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
  EXPECT_THAT(
      RunArgvRecords({"--compare=diff", "--diff-algorithm=myers", "--diff-context=0", left.string(), right.string()}),
      Contains(HasSubstr("-old\n+new")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, FprintWritesMatchesToFileNotStdout) {
  // -fprint FILE redirects the matched paths into FILE; being an action, it also
  // suppresses the implicit -print, so stdout stays empty. The output file lives
  // outside the walked tree so it never appears in its own results.
  const std::string out = (fs::path(::testing::TempDir()) / "xff_fprint_out.lst").string();
  std::error_code ec;
  fs::remove(out, ec);
  EXPECT_THAT(RunExpr({"-name", "*.txt", "-fprint", out}), IsEmpty());
  EXPECT_THAT(last_errors_, 0);
  std::ifstream in(out, std::ios::binary);
  ASSERT_THAT(in.good(), IsTrue());
  std::vector<std::string> lines;
  for (std::string line; std::getline(in, line);) {
    lines.push_back(line);
  }
  EXPECT_THAT(lines, UnorderedElementsAre(Path("a.txt"), Path("sub/c.txt")));
  fs::remove(out, ec);
}

TEST_F(RunTest, FprintlnAndFprintflnWriteWithOsLineEndingToFile) {
  // xff: the file-writing forms of -println / -printfln, the counterparts of -fprint /
  // -fprintf. Each redirects into FILE (so, being an action, the implicit -print is
  // suppressed and stdout stays empty) and terminates the record with the OS line
  // ending (here "\n"). -fprintfln takes FILE then FORMAT, like -fprintf.
  const auto read_all = [](const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  };
  const std::string ln = (fs::path(::testing::TempDir()) / "xff_fprintln.out").string();
  const std::string fln = (fs::path(::testing::TempDir()) / "xff_fprintfln.out").string();
  std::error_code ec;
  fs::remove(ln, ec);
  fs::remove(fln, ec);

  EXPECT_THAT(RunExpr({"-name", "a.txt", "-fprintln", ln}), IsEmpty());
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-fprintfln", fln, "name %f"}), IsEmpty());
  EXPECT_THAT(last_errors_, 0);

  EXPECT_THAT(read_all(ln), Eq(Path("a.txt") + "\n"));
  EXPECT_THAT(read_all(fln), Eq(std::string("name a.txt\n")));

  fs::remove(ln, ec);
  fs::remove(fln, ec);
}

TEST_F(RunTest, PrintfPercentBraceEscapeExpandsXffFields) {
  // xff: `%{field}` in a -printf format reaches the brace field vocabulary (here
  // {relpath}); `%%` stays a literal percent, a bare `{..}` stays literal (printf formats
  // legitimately contain braces), and an unterminated `%{` is emitted literally. The whole
  // format renders as one record (it owns its terminator).
  EXPECT_THAT(
      RunExpr({"-name", "a.txt", "-printf", "rel=%{relpath} f=%f pct=%% bare={x} bad=%{oops\n"}),
      ElementsAre("rel=a.txt f=a.txt pct=% bare={x} bad=%{oops"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, DaystartFeedsTheTimeTests) {
  // Age a.txt to ~10 days ago, then select with -daystart -mtime +5 (older than
  // ~5 days, measured from today's local midnight). 10 days clears the boundary
  // with room to spare, so this exercises the daystart -> reference-instant ->
  // time-test wiring end to end without depending on the exact midnight cutoff.
  const auto ten_days_ago = fs::file_time_type::clock::now() - std::chrono::hours(24 * 10);
  std::error_code ec;
  fs::last_write_time(root_ / "a.txt", ten_days_ago, ec);
  ASSERT_THAT(ec, IsFalse());
  EXPECT_THAT(RunExpr({"-daystart", "-mtime", "+5", "-name", "a.txt"}), ElementsAre(Path("a.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, TraversalSynonymsAccepted) {
  // -mount/-x (= -xdev) and -d (= -depth) are accepted; on a single-device tree
  // -xdev prunes nothing, and -d only reorders (post-order), so the set is the same.
  EXPECT_THAT(
      RunExpr({"-mount"}),
      UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
  EXPECT_THAT(
      RunExpr({"-x"}),
      UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
  EXPECT_THAT(RunExpr({"-d", "-name", "*.txt"}), UnorderedElementsAre(Path("a.txt"), Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, IgnoreReaddirRaceAccepted) {
  // The option parses and walks normally (no races on this stable tree); the
  // ENOENT-suppression behaviour itself is covered at the walk level.
  EXPECT_THAT(
      RunExpr({"-ignore_readdir_race"}),
      UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
  EXPECT_THAT(
      RunExpr({"-noignore_readdir_race", "-name", "*.txt"}), UnorderedElementsAre(Path("a.txt"), Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ExecPlusBatchesAllMatchesIntoOneRun) {
  // Engine-level: RunFind accumulates the matches and flushes ONE batched command
  // at end-of-walk (the exec_batches map + post-walk flush), in-process. The shell
  // appends a RUN marker per invocation plus each path, so exactly one RUN line
  // proves a single batched run (per-entry would yield two). The full binary/CLI
  // path is covered at the system level in //xff/cli:exec_test (mboworks/bashtest).
  const std::string out = (fs::path(::testing::TempDir()) / "xff_execplus_out.lst").string();
  std::error_code ec;
  fs::remove(out, ec);
  const std::string script = "echo RUN >> '" + out + R"('; for p in "$@"; do echo "$p" >> ')" + out + "'; done";
  RunExpr({"-name", "*.txt", "-exec", "sh", "-c", script, "_", "{}", "+"});
  EXPECT_THAT(last_errors_, 0);
  std::ifstream in(out, std::ios::binary);
  ASSERT_THAT(in.good(), IsTrue());
  std::vector<std::string> lines;
  for (std::string line; std::getline(in, line);) {
    lines.push_back(line);
  }
  EXPECT_THAT(lines, UnorderedElementsAre("RUN", Path("a.txt"), Path("sub/c.txt")));
  fs::remove(out, ec);
}

TEST_F(RunTest, ExecdirPlusBatchesPerDirectory) {
  // *.txt are in two directories (root/a.txt, root/sub/c.txt), so -execdir ... +
  // runs ONCE PER DIRECTORY, passing the ./basename. Two dirs -> two RUN markers;
  // the items are basenames, not full paths (the cwd is each entry's directory).
  const std::string out = (fs::path(::testing::TempDir()) / "xff_execdirplus_out.lst").string();
  std::error_code ec;
  fs::remove(out, ec);
  const std::string script = "echo RUN >> '" + out + R"('; for p in "$@"; do echo "$p" >> ')" + out + "'; done";
  RunExpr({"-name", "*.txt", "-execdir", "sh", "-c", script, "_", "{}", "+"});
  EXPECT_THAT(last_errors_, 0);
  std::ifstream in(out, std::ios::binary);
  ASSERT_THAT(in.good(), IsTrue());
  std::vector<std::string> lines;
  for (std::string line; std::getline(in, line);) {
    lines.push_back(line);
  }
  EXPECT_THAT(lines, UnorderedElementsAre("RUN", "RUN", "./a.txt", "./c.txt"));
  fs::remove(out, ec);
}

TEST_F(RunTest, ExecSemicolonUnderParallelJobsRunsEveryMatch) {
  // -j>1 routes the serial `-exec ... ;` action through the bounded ParallelExec
  // runner. Each of the two *.txt matches must still run exactly once; the children
  // append their path (one short, O_APPEND-atomic line apiece) so order is
  // unspecified but the set is complete -- proving no match is dropped on launch.
  const std::string out = (fs::path(::testing::TempDir()) / "xff_execpar_out.lst").string();
  std::error_code ec;
  fs::remove(out, ec);
  const std::string script = "echo \"$1\" >> '" + out + "'";
  RunArgvRecords({"-j", "2", root_.string(), "-name", "*.txt", "-exec", "sh", "-c", script, "_", "{}", ";"});
  EXPECT_THAT(last_errors_, 0);
  std::ifstream in(out, std::ios::binary);
  ASSERT_THAT(in.good(), IsTrue());
  std::vector<std::string> lines;
  for (std::string line; std::getline(in, line);) {
    lines.push_back(line);
  }
  EXPECT_THAT(lines, UnorderedElementsAre(Path("a.txt"), Path("sub/c.txt")));
  fs::remove(out, ec);
}

TEST_F(RunTest, ExecSemicolonUnderParallelJobsLeavesExitStatusUnaffected) {
  // find's `-exec ... ;` is a predicate: a nonzero exit makes the action false but
  // does NOT raise find's exit status (unlike the `+` batch form). The parallel
  // runner preserves that -- both *.txt matches run `sh -c 'exit 1'`, yet the run
  // reports no error, identical to the synchronous -j 1 path.
  RunArgvRecords({"-j", "2", root_.string(), "-name", "*.txt", "-exec", "sh", "-c", "exit 1", ";"});
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, JobsAllParsesAndWalksEverything) {
  // --jobs=all resolves to every detected core; the parallel walk still visits the
  // whole tree. The set is complete (order unspecified). On a 1-core host it folds
  // to -j 1, which returns the same set, so the assertion holds regardless.
  EXPECT_THAT(
      RunArgvRecords({"--jobs=all", root_.string()}),
      UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, JobsAcceptsLongShortAndAllForms) {
  static const std::vector<std::vector<std::string>> kPrefixes = {
      {"--jobs=1"}, {"-j", "1"}, {"-j=1"}, {"-j1"}, {"-j", "all"}, {"-j=all"}, {"-jall"},
  };
  for (const std::vector<std::string>& prefix : kPrefixes) {
    SCOPED_TRACE(PrintToString(prefix));
    std::vector<std::string> argv = prefix;
    argv.push_back(root_.string());
    argv.insert(argv.end(), {"-name", "*.txt"});
    EXPECT_THAT(RunArgvRecords(argv), UnorderedElementsAre(Path("a.txt"), Path("sub/c.txt")));
  }
}

TEST_F(RunTest, JobsRejectsInvalidValuesBeforeWalking) {
  static constexpr auto kInvalidValues = std::to_array<std::string_view>({"--jobs=0", "--jobs=invalid"});
  for (const std::string_view value : kInvalidValues) {
    SCOPED_TRACE(value);
    RunArgvRecords({std::string(value), root_.string()});
    EXPECT_THAT(last_errors_, 2);
  }
}

TEST_F(RunTest, StyleScopedSortDefault) {
  // With no --sort, the active style picks the default: xff (kXff) sorts each
  // directory's listing, so the walk is deterministic (root, then a.txt < b.md <
  // sub as a block, then sub's contents). find leaves it unordered (same set).
  EXPECT_THAT(
      RunStyled(registry::Style::kXff),
      ElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
  EXPECT_THAT(
      RunStyled(registry::Style::kFind),
      UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
}

TEST_F(RunTest, SortNameVisitsSiblingsInDeterministicOrder) {
  // --sort=name orders each directory's entries by name, so the whole walk is
  // deterministic: root first, then a.txt < b.md < sub, then sub/c.txt. ElementsAre
  // (not UnorderedElementsAre) asserts the exact sequence.
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"--sort", root_.string()}));
  std::vector<std::string> records;
  RunFind(
      command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, ElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
}

TEST_F(RunTest, NameGlobImplicitPrint) {
  EXPECT_THAT(RunExpr({"-name", "*.txt"}), UnorderedElementsAre(Path("a.txt"), Path("sub/c.txt")));
}

TEST_F(RunTest, TypeDirectoryImplicitPrint) {
  EXPECT_THAT(RunExpr({"-type", "d"}), UnorderedElementsAre(root_.string(), Path("sub")));
}

TEST_F(RunTest, ExplicitPrintIsNotDoubled) {
  EXPECT_THAT(RunExpr({"-name", "*.txt", "-print"}), UnorderedElementsAre(Path("a.txt"), Path("sub/c.txt")));
}

TEST_F(RunTest, Print0EmitsNulTerminatedRecords) {
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-print0"}), UnorderedElementsAre(Path("a.txt")));
}

TEST_F(RunTest, MaxDepthLimitsDescent) {
  // -maxdepth 1: root + its direct children, but not sub/c.txt (depth 2).
  EXPECT_THAT(
      RunExpr({"-maxdepth", "1"}), UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub")));
}

TEST_F(RunTest, LastDepthOptionWinsAcrossTheExpressionTree) {
  EXPECT_THAT(
      RunExpr({"-maxdepth", "0", "-maxdepth", "1"}),
      UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub")));
  EXPECT_THAT(
      RunExpr({"-mindepth", "2", "-mindepth", "1"}),
      UnorderedElementsAre(Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
}

TEST_F(RunTest, MinDepthSkipsRoot) {
  // -mindepth 1: everything except the root operand itself.
  EXPECT_THAT(
      RunExpr({"-mindepth", "1"}), UnorderedElementsAre(Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
}

TEST_F(RunTest, EmptyMatchesEmptyFileAndDir) {
  std::error_code ec;
  { std::ofstream(root_ / "empty.txt"); }  // 0 bytes
  fs::create_directory(root_ / "emptydir", ec);
  // -empty: the zero-byte file and the childless directory only (a.txt/b.md/
  // sub/c.txt are non-empty; root and sub have children).
  EXPECT_THAT(RunExpr({"-empty"}), UnorderedElementsAre(Path("empty.txt"), Path("emptydir")));
}

TEST_F(RunTest, LinksOneMatchesRegularFiles) {
  // Regular files have one hard link; directories have >= 2.
  EXPECT_THAT(RunExpr({"-links", "1"}), UnorderedElementsAre(Path("a.txt"), Path("b.md"), Path("sub/c.txt")));
}

TEST_F(RunTest, MimeMatchesByExtensionDerivedType) {
  // Fixture: a.txt + sub/c.txt (text/plain) and b.md (text/markdown) are all text/*;
  // the directories (no extension -> octet-stream) are excluded.
  EXPECT_THAT(RunExpr({"-mime", "text/*"}), UnorderedElementsAre(Path("a.txt"), Path("b.md"), Path("sub/c.txt")));
}

TEST_F(RunTest, MimeVocabularyOverridesMatchingAndExposesMetadataFields) {
  const fs::path vocabulary = root_ / "mime.json";
  const fs::path binary = root_ / "b.md";
  const fs::path unspecified = root_ / "unspecified.mime-unknown";
  { const std::ofstream empty(unspecified); }
  std::ofstream(vocabulary) << R"({
    "application/x-note": {
      "description": "Note document",
      "source": "project",
      "charset": "UTF-8",
      "compressible": true,
      "extensions": ["txt"]
    },
    "application/x-binary": {"compressible": false, "extensions": ["md"]},
    "application/x-unspecified": {"extensions": ["mime-unknown"]}
  })";
  EXPECT_THAT(
      RunArgvRecords(
          {"--mime-vocabulary=" + vocabulary.string(), Path("a.txt"), binary.string(), unspecified.string(), "-printf",
           "%{mime}|%{mime-category}|%{mime-description}|%{mime-charset}|%{mime-compressible}|%{mime-source}\n"}),
      ElementsAre(
          "application/x-note|application|Note document|UTF-8|yes|project", "application/x-binary|application|||no|",
          "application/x-unspecified|application||||"));
  EXPECT_THAT(last_errors_, Eq(0));
}

TEST_F(RunTest, MissingMimeVocabularyFailsBeforeTraversal) {
  EXPECT_THAT(RunArgvRecords({"--mime-vocabulary=" + Path("absent.json"), root_.string(), "-print"}), IsEmpty());
  EXPECT_THAT(last_errors_, Eq(2));
}

TEST_F(RunTest, MimeConflictPolicyControlsAmbiguousImportedVocabulary) {
  const fs::path vocabulary = root_ / "mime-conflict.json";
  std::ofstream(vocabulary) << R"({
    "application/x-first": {"extensions": ["txt"]},
    "application/x-last": {"extensions": ["txt"]}
  })";
  const std::string flag = "--mime-vocabulary=" + vocabulary.string();
  EXPECT_THAT(RunArgvRecords({flag, root_.string(), "-name", "a.txt", "-printf", "%{mime}\n"}), IsEmpty());
  EXPECT_THAT(last_errors_, Eq(2));
  EXPECT_THAT(
      RunArgvRecords({flag, "--mime-conflicts=first", root_.string(), "-name", "a.txt", "-printf", "%{mime}\n"}),
      ElementsAre("application/x-first"));
  EXPECT_THAT(
      RunArgvRecords({flag, "--mime-conflicts=last", root_.string(), "-name", "a.txt", "-printf", "%{mime}\n"}),
      ElementsAre("application/x-last"));
}

TEST_F(RunTest, LangMatchesAndRendersTheLanguage) {
  { std::ofstream(root_ / "main.cc"); }
  { std::ofstream(root_ / "app.py"); }
  { std::ofstream(root_ / "Makefile"); }
  // -lang globs the language name case-insensitively (C++ from .cc; Python from .py).
  EXPECT_THAT(RunExpr({"-lang", "c++"}), ElementsAre(Path("main.cc")));
  EXPECT_THAT(RunExpr({"-lang", "python"}), ElementsAre(Path("app.py")));
  EXPECT_THAT(RunExpr({"-lang", "Makefile"}), ElementsAre(Path("Makefile")));
  // The {lang} field renders the canonical name, usable in -printf / --format.
  EXPECT_THAT(RunExpr({"-name", "main.cc", "-printf", "%{lang}\n"}), ElementsAre("C++"));
}

TEST_F(RunTest, LanguageDbOverridesMatchingAliasesAndMetadataFields) {
  { std::ofstream(root_ / "types.note"); }
  const fs::path vocabulary = root_ / "languages.json";
  std::ofstream(vocabulary) << R"({
    "NoteScript": {
      "type": "programming",
      "color": "#123456",
      "group": "Script",
      "source": "project",
      "aliases": ["ns"],
      "extensions": ["note"]
    }
  })";
  EXPECT_THAT(
      RunArgvRecords(
          {"--lang-db=" + vocabulary.string(), root_.string(), "-lang", "ns", "-printf",
           "%{lang}|%{lang-type}|%{lang-color}|%{lang-group}|%{lang-source}\n"}),
      ElementsAre("NoteScript|programming|#123456|Script|project"));
  EXPECT_THAT(last_errors_, Eq(0));
}

TEST_F(RunTest, MissingLanguageDbFailsBeforeTraversal) {
  EXPECT_THAT(RunArgvRecords({"--lang-db=" + Path("absent.json"), root_.string(), "-print"}), IsEmpty());
  EXPECT_THAT(last_errors_, Eq(2));
}

TEST_F(RunTest, LanguageConflictPolicyControlsAmbiguousImportedVocabulary) {
  { std::ofstream(root_ / "types.note"); }
  const fs::path vocabulary = root_ / "language-conflict.json";
  std::ofstream(vocabulary) << R"({
    "First": {"extensions": ["note"]},
    "Last": {"extensions": ["note"]}
  })";
  const std::string flag = "--lang-db=" + vocabulary.string();
  EXPECT_THAT(RunArgvRecords({flag, root_.string(), "-name", "types.note", "-printf", "%{lang}\n"}), IsEmpty());
  EXPECT_THAT(last_errors_, Eq(2));
  EXPECT_THAT(
      RunArgvRecords({flag, "--lang-conflicts=first", root_.string(), "-name", "types.note", "-printf", "%{lang}\n"}),
      ElementsAre("First"));
  EXPECT_THAT(
      RunArgvRecords({flag, "--lang-conflicts=last", root_.string(), "-name", "types.note", "-printf", "%{lang}\n"}),
      ElementsAre("Last"));
}

TEST_F(RunTest, MissingRootCountsError) {
  const std::vector<std::string> argv = {(root_ / "absent").string(), "-print"};
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse(argv));
  std::vector<std::string> records;
  const auto [errors, any_match] = RunFind(
      command, fs_, [&](std::string_view record) { records.emplace_back(record); },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, IsEmpty());
  EXPECT_THAT(errors, 1);
}

TEST_F(RunTest, PruneSkipsDirectoryDescent) {
  // `-name sub -prune -o -print`: prints everything except `sub` and its contents.
  EXPECT_THAT(
      RunExpr({"-name", "sub", "-prune", "-o", "-print"}),
      UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md")));
}

TEST_F(RunTest, QuitStopsTraversal) {
  // `-quit` is an action (so no implicit -print) that stops after the first entry.
  EXPECT_THAT(RunExpr({"-quit"}), IsEmpty());
}

TEST_F(RunTest, DepthVisitsPostOrder) {
  const std::vector<std::string> out = RunExpr({"-depth"});
  // -depth lists the same set but post-order, so the root operand prints last.
  EXPECT_THAT(out, UnorderedElementsAre(root_.string(), Path("a.txt"), Path("b.md"), Path("sub"), Path("sub/c.txt")));
  ASSERT_THAT(out, Not(IsEmpty()));
  EXPECT_THAT(out.back(), root_.string());
}

TEST_F(RunTest, SymlinkLModeFollowsDirectorySymlink) {
  std::error_code ec;
  fs::create_directory_symlink(root_ / "sub", root_ / "lnk", ec);
  ASSERT_THAT(ec, IsFalse());
  // `find -L <root> -name c.txt`: -L follows the directory symlink lnk -> sub, so
  // c.txt is reachable both directly (sub/c.txt) and through the link (lnk/c.txt).
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"-L", root_.string(), "-name", "c.txt"}));
  std::vector<std::string> out;
  RunFind(
      command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && (text.back() == '\n' || text.back() == '\0')) {
          text.pop_back();
        }
        out.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(out, UnorderedElementsAre(Path("sub/c.txt"), Path("lnk/c.txt")));
}

TEST_F(RunTest, SymlinkHModeFollowsOnlyACommandLineRoot) {
  std::error_code ec;
  fs::create_directory_symlink(root_ / "sub", root_ / "root-link", ec);
  ASSERT_THAT(ec, Eq(std::error_code{}));
  const std::string link = Path("root-link");

  EXPECT_THAT(RunArgvRecords({"-H", link, "-name", "c.txt"}), ElementsAre(link + "/c.txt"));
  EXPECT_THAT(RunArgvRecords({"-P", link, "-name", "c.txt"}), IsEmpty());
}

TEST_F(RunTest, FormatJsonlRendersImplicitPrintAsJson) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"--format=jsonl", root_.string(), "-name", "a.txt"}));
  std::vector<std::string> records;
  RunFind(
      command, fs_, [&](std::string_view record) { records.emplace_back(record); },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, UnorderedElementsAre(Eq(std::string("{\"path\":\"") + Path("a.txt") + "\"}\n")));
}

TEST_F(RunTest, FormatNulViaDashZero) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"-0", root_.string(), "-name", "a.txt"}));
  std::vector<std::string> records;
  RunFind(
      command, fs_, [&](std::string_view record) { records.emplace_back(record); },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, UnorderedElementsAre(Eq(Path("a.txt") + std::string("\0", 1))));
}

TEST_F(RunTest, ColorAlwaysWrapsDirectoriesButLeavesPlainFilesUncolored) {
  // --color=always forces ANSI even though the test's captured stdout is a pipe,
  // not a tty (so the default auto would stay plain). A directory gets bold blue
  // (1;34); a plain non-executable regular file is emitted with no escapes.
  EXPECT_THAT(
      RunArgvRecords({"--color=always", root_.string(), "-name", "sub"}),
      ElementsAre(absl::StrCat("\x1b[1;34m", Path("sub"), "\x1b[0m")));
  EXPECT_THAT(RunArgvRecords({"--color=always", root_.string(), "-name", "a.txt"}), ElementsAre(Path("a.txt")));
}

TEST_F(RunTest, LanguageDatabaseColorsRegularFilesInListingsAndLs) {
  const fs::path database = root_ / "languages.json";
  { std::ofstream(database) << R"({"Paint":{"color":"#3178c6","extensions":["colored"]}})"; }
  { std::ofstream(root_ / "paint.colored") << "paint"; }
  const std::string flag = absl::StrCat("--lang-db=", database.string());
  const std::string colored_path = absl::StrCat("\x1b[38;2;49;120;198m", Path("paint.colored"), "\x1b[0m");

  EXPECT_THAT(
      RunArgvRecords({"--color=always", "--color-scheme=xff", flag, root_.string(), "-name", "paint.colored"}),
      ElementsAre(colored_path));
  EXPECT_THAT(
      RunArgvRecords({"--color=always", "--color-scheme=xff", flag, root_.string(), "-name", "paint.colored", "-ls"}),
      ElementsAre(HasSubstr(colored_path)));
}

TEST_F(RunTest, TemplateRelpathIsRelativeToTheSearchRoot) {
  // {relpath} renders each entry's path relative to the search root (find %P), so the
  // walk's per-entry root wiring is exercised end-to-end.
  EXPECT_THAT(
      RunArgvRecords({"--template={relpath}", root_.string(), "-type", "f"}),
      UnorderedElementsAre("a.txt", "b.md", "sub/c.txt"));
}

TEST_F(RunTest, TemplateTargetRendersTheSymlinkTarget) {
  // {target} = the symlink's target (find %l), resolved via ReadLink at the render
  // context; empty for a non-symlink. Exercises the engine's link-target wiring e2e.
  std::error_code ec;
  fs::create_symlink("a.txt", root_ / "link.lnk", ec);
  ASSERT_THAT(ec, IsFalse());
  EXPECT_THAT(RunArgvRecords({"--template={target}", root_.string(), "-name", "link.lnk"}), ElementsAre("a.txt"));
  EXPECT_THAT(RunArgvRecords({"--template=[{target}]", root_.string(), "-name", "a.txt"}), ElementsAre("[]"));
}

TEST_F(RunTest, CmpMatchesByteIdenticalContent) {
  { std::ofstream(root_ / "twin.txt") << "a"; }  // byte-identical to a.txt (content "a")
  { std::ofstream(root_ / "diff.txt") << "X"; }  // differs
  // -cmp TARGET is TRUE (same) when byte-identical; TARGET is a field template (a bare
  // path is a literal). a.txt == twin.txt, a.txt != diff.txt.
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-cmp", Path("twin.txt")}), ElementsAre(Path("a.txt")));
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-cmp", Path("diff.txt")}), IsEmpty());
  // `! -cmp` selects files that differ (the "list changed files" idiom).
  EXPECT_THAT(RunExpr({"-name", "a.txt", "!", "-cmp", Path("diff.txt")}), ElementsAre(Path("a.txt")));
  // A missing / unreadable target counts as differing (not-same -> false).
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-cmp", Path("nope.txt")}), IsEmpty());
}

TEST_F(RunTest, CmpTargetIsAPerEntryTemplate) {
  // The target is rendered per entry, so {def.NAME} / {name} build it dynamically:
  // compare each file against a same-named file under a parallel directory.
  const std::string other = (fs::path(::testing::TempDir()) / "xff_cmp_other").string();
  std::error_code ec;
  fs::remove_all(other, ec);
  ASSERT_THAT(fs::create_directories(other), IsTrue());
  { std::ofstream(fs::path(other) / "a.txt") << "a"; }         // identical to <root>/a.txt
  { std::ofstream(fs::path(other) / "b.md") << "DIFFERENT"; }  // differs from <root>/b.md ("b")
  // ! -cmp '{def.OTHER}/{name}' -> files whose twin under OTHER differs (b.md; a.txt matches).
  const std::vector<std::string> changed =
      RunArgvRecords({"--define=OTHER=" + other, root_.string(), "-type", "f", "!", "-cmp", "{def.OTHER}/{name}"});
  fs::remove_all(other, ec);
  // sub/c.txt has no counterpart under OTHER (missing -> differs); b.md differs; a.txt matches.
  EXPECT_THAT(changed, UnorderedElementsAre(Path("b.md"), Path("sub/c.txt")));
}

TEST_F(RunTest, SimilarMatchesNearDuplicateTextAgainstAReference) {
  constexpr std::string_view reference =
      "one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen seventeen "
      "eighteen nineteen twenty";
  { std::ofstream(root_ / "reference.txt") << reference; }
  { std::ofstream(root_ / "candidate-close.txt") << reference.substr(0, reference.rfind(' ')) << " twentyone"; }
  {
    std::ofstream(root_ / "candidate-far.txt")
        << "one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen "
           "seventeen red blue green";
  }

  // One changed final word retains 15 of 17 union shingles (88%), so the default 80% admits it.
  // Four changed trailing words retain only 13 of 19 (68%), so the same matcher rejects it.
  EXPECT_THAT(
      RunExpr({"-name", "candidate-*.txt", "-similar", Path("reference.txt")}),
      ElementsAre(Path("candidate-close.txt")));
  EXPECT_THAT(
      RunExpr({"-name", "candidate-*.txt", "-similar:65%", Path("reference.txt")}),
      UnorderedElementsAre(Path("candidate-close.txt"), Path("candidate-far.txt")));
}

TEST_F(RunTest, SimilarWidthAndTokenNormalizationHaveObservableSemantics) {
  { std::ofstream(root_ / "reference.txt") << "Alpha, beta gamma delta"; }
  { std::ofstream(root_ / "same-words.txt") << "alpha BETA! gamma delta"; }
  { std::ofstream(root_ / "different-order.txt") << "alpha gamma beta delta"; }

  EXPECT_THAT(
      RunExpr({"-name", "same-words.txt", "-similar:4:100%", Path("reference.txt")}),
      ElementsAre(Path("same-words.txt")));
  EXPECT_THAT(RunExpr({"-name", "different-order.txt", "-similar:4:100%", Path("reference.txt")}), IsEmpty());
}

TEST_F(RunTest, SimilarSkipsBinaryAndMissingReferenceContent) {
  { std::ofstream(root_ / "reference.txt") << "one two three four five"; }
  {
    std::ofstream binary(root_ / "binary.txt", std::ios::binary);
    std::string content = "one two";
    content.push_back('\0');
    content.append("three four five");
    binary.write(content.data(), static_cast<std::streamsize>(content.size()));
  }
  EXPECT_THAT(RunExpr({"-name", "binary.txt", "-similar:0%", Path("reference.txt")}), IsEmpty());
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-similar:0%", Path("missing.txt")}), IsEmpty());
}

TEST_F(RunTest, DiffPolarityIsTrueWhenEqual) {
  { std::ofstream(root_ / "twin.txt") << "a"; }   // identical to a.txt (content "a")
  { std::ofstream(root_ / "other.txt") << "X"; }  // differs
  // -diff:none is the silent matcher (TRUE = same, like -cmp); -diff is an action, so an
  // explicit -print reveals the truth. a.txt == twin, a.txt != other.
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-diff:none", Path("twin.txt"), "-print"}), ElementsAre(Path("a.txt")));
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-diff:none", Path("other.txt"), "-print"}), IsEmpty());
  // ! -diff selects files that differ from their target (the "changed files" idiom).
  EXPECT_THAT(RunExpr({"-name", "a.txt", "!", "-diff:none", Path("other.txt"), "-print"}), ElementsAre(Path("a.txt")));
  // A missing / unreadable target counts as differing (false).
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-diff:none", Path("nope.txt"), "-print"}), IsEmpty());

  // Binary inputs use byte equality rather than text diffing.
  {
    constexpr std::array kBinaryTwin = {'a', '\0', 's', 'a', 'm', 'e'};
    std::ofstream binary(root_ / "binary-twin.txt", std::ios::binary);
    binary.write(kBinaryTwin.data(), static_cast<std::streamsize>(kBinaryTwin.size()));
  }
  {
    constexpr std::array kBinaryOther = {'a', '\0', 'd', 'i', 'f', 'f', 'e', 'r', 'e', 'n', 't'};
    std::ofstream binary(root_ / "binary-other.txt", std::ios::binary);
    binary.write(kBinaryOther.data(), static_cast<std::streamsize>(kBinaryOther.size()));
  }
  EXPECT_THAT(
      RunExpr({"-name", "binary-twin.txt", "-diff:none", Path("binary-twin.txt"), "-print"}),
      ElementsAre(Path("binary-twin.txt")));
  EXPECT_THAT(RunExpr({"-name", "binary-twin.txt", "-diff:none", Path("binary-other.txt"), "-print"}), IsEmpty());
}

TEST_F(RunTest, DiffIgnoreNormalizesComparison) {
  // Two files that differ only by trailing whitespace; the normalization globals make -diff
  // treat them as equal (TRUE, so the trailing -print fires). -diff:none is the silent matcher.
  { std::ofstream(root_ / "left.txt") << "one\ntwo   \nthree\n"; }
  { std::ofstream(root_ / "right.txt") << "one\ntwo\nthree\n"; }
  const std::string right = Path("right.txt");
  // Without normalization the trailing whitespace differs -> FALSE, no print.
  EXPECT_THAT(RunExpr({"-name", "left.txt", "-diff:none", right, "-print"}), IsEmpty());
  // --diff-ignore=trail and =ws both fold the whitespace so the sides compare equal.
  EXPECT_THAT(
      RunArgvRecords({"--diff-ignore=trail", root_.string(), "-name", "left.txt", "-diff:none", right, "-print"}),
      ElementsAre(Path("left.txt")));
  EXPECT_THAT(
      RunArgvRecords({"--diff-ignore=ws", root_.string(), "-name", "left.txt", "-diff:none", right, "-print"}),
      ElementsAre(Path("left.txt")));
  // --diff-ignore-matching drops lines matching the regex before comparing (the DEBUG line here).
  { std::ofstream(root_ / "mleft.txt") << "keep\nDEBUG x\nkeep2\n"; }
  { std::ofstream(root_ / "mright.txt") << "keep\nDEBUG y\nkeep2\n"; }
  EXPECT_THAT(
      RunArgvRecords(
          {"--diff-ignore-matching=^DEBUG", root_.string(), "-name", "mleft.txt", "-diff:none", Path("mright.txt"),
           "-print"}),
      ElementsAre(Path("mleft.txt")));
  // --diff-ignore=eofnl equates a file with and one without a final newline (via mbo #234).
  { std::ofstream(root_ / "nonl.txt") << "a\nb"; }      // no final newline
  { std::ofstream(root_ / "withnl.txt") << "a\nb\n"; }  // same content, with a final newline
  EXPECT_THAT(RunExpr({"-name", "nonl.txt", "-diff:none", Path("withnl.txt"), "-print"}), IsEmpty());
  EXPECT_THAT(
      RunArgvRecords(
          {"--diff-ignore=eofnl", root_.string(), "-name", "nonl.txt", "-diff:none", Path("withnl.txt"), "-print"}),
      ElementsAre(Path("nonl.txt")));
}

TEST_F(RunTest, DiffIgnoreRejectsUnknownToken) {
  // An unknown --diff-ignore token is a pre-walk usage error (exit 2), not a silent no-op.
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command,
      parser::Parse({"--diff-ignore=bogus", root_.string(), "-name", "a.txt", "-diff", Path("b.md")}));
  std::vector<std::string> records;
  absl::Status reported;
  const auto [errors, any_match] = RunFind(
      command, fs_, [&](std::string_view record) { records.emplace_back(record); },
      [&](std::string_view, absl::Status status) { reported = status; });
  EXPECT_THAT(records, IsEmpty());
  EXPECT_THAT(errors, 2);
  EXPECT_THAT(reported, StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("unknown --diff-ignore token 'bogus'")));
}

TEST_F(RunTest, InvalidValuedGlobalsAreRejectedBeforeTraversal) {
  struct Case {
    std::string flag;
    std::string message;
  };

  const std::array cases{
      Case{.flag = "--diff-algorithm=bogus", .message = "unknown diff algorithm 'bogus'"},
      Case{.flag = "--hash-algorithm=bogus", .message = "unknown hash algorithm 'bogus'"},
      Case{.flag = "--hash-encoding=bogus", .message = "unknown hash encoding 'bogus'"},
      Case{.flag = "--archive-aggregate=bogus", .message = "bad --archive-aggregate value 'bogus'"},
  };
  for (const Case& test : cases) {
    SCOPED_TRACE(test.flag);
    MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({test.flag, root_.string()}));
    absl::Status reported;
    const RunResult result = RunFind(
        command, fs_, [](std::string_view) {}, [&](std::string_view, absl::Status status) { reported = status; });
    EXPECT_THAT(result.errors, 2);
    EXPECT_THAT(result.any_match, IsFalse());
    EXPECT_THAT(reported, StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr(test.message)));
  }
}

TEST_F(RunTest, PackWithoutAnArchiveBackendIsRejectedBeforeTraversal) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"--pack=" + Path("output.tar"), root_.string()}));
  absl::Status reported;
  const RunResult result =
      RunFind(command, fs_, [](std::string_view) {}, [&](std::string_view, absl::Status status) { reported = status; });
  EXPECT_THAT(result.errors, 2);
  EXPECT_THAT(result.any_match, IsFalse());
  EXPECT_THAT(reported, StatusIs(absl::StatusCode::kUnimplemented, HasSubstr("without archive support")));
}

TEST_F(RunTest, DiffFormatAndContextGlobalsSetTheDefaults) {
  // A 7-line file with a single changed line (line 4); -diff emits the whole diff as one record.
  { std::ofstream(root_ / "one.txt") << "a\nb\nc\nd\ne\nf\ng\n"; }
  { std::ofstream(root_ / "two.txt") << "a\nb\nc\nX\ne\nf\ng\n"; }
  const std::string two = Path("two.txt");
  // Built-in default: unified with 3 lines of context (the hunk spans all 7 lines here).
  EXPECT_THAT(
      RunArgvRecords({root_.string(), "-name", "one.txt", "-diff", two}), ElementsAre(HasSubstr("@@ -1,7 +1,7 @@")));
  // --diff-context=1 narrows the unified hunk to one line of context each side.
  EXPECT_THAT(
      RunArgvRecords({"--diff-context=1", root_.string(), "-name", "one.txt", "-diff", two}),
      ElementsAre(HasSubstr("@@ -3,3 +3,3 @@")));
  // --diff-format=normal switches to the `NcN` normal format (no unified `@@` hunk header).
  EXPECT_THAT(
      RunArgvRecords({"--diff-format=normal", root_.string(), "-name", "one.txt", "-diff", two}),
      ElementsAre(AllOf(HasSubstr("4c4"), HasSubstr("< d"), HasSubstr("> X"), Not(HasSubstr("@@")))));
}

TEST_F(RunTest, ContextGlobalFeedsDiffContextWhenSymmetric) {
  { std::ofstream(root_ / "one.txt") << "a\nb\nc\nd\ne\nf\ng\n"; }
  { std::ofstream(root_ / "two.txt") << "a\nb\nc\nX\ne\nf\ng\n"; }
  const std::string two = Path("two.txt");
  // A symmetric --context=1 (grep before==after) also seeds the -diff default context.
  EXPECT_THAT(
      RunArgvRecords({"--context=1", root_.string(), "-name", "one.txt", "-diff", two}),
      ElementsAre(HasSubstr("@@ -3,3 +3,3 @@")));
  // An asymmetric --context (after != before) is grep-only; -diff falls back to its built-in 3.
  EXPECT_THAT(
      RunArgvRecords({"--context=A:1,B:0", root_.string(), "-name", "one.txt", "-diff", two}),
      ElementsAre(HasSubstr("@@ -1,7 +1,7 @@")));
  // --diff-context overrides --context for -diff regardless of order.
  EXPECT_THAT(
      RunArgvRecords({"--context=1", "--diff-context=5", root_.string(), "-name", "one.txt", "-diff", two}),
      ElementsAre(HasSubstr("@@ -1,7 +1,7 @@")));
}

TEST_F(RunTest, PerActionDiffStyleOverridesTheGlobals) {
  { std::ofstream(root_ / "one.txt") << "a\nb\nc\nd\ne\nf\ng\n"; }
  { std::ofstream(root_ / "two.txt") << "a\nb\nc\nX\ne\nf\ng\n"; }
  const std::string two = Path("two.txt");
  // -diff:c (context format) wins over --diff-format=normal; the `*** ` marker is context-diff.
  EXPECT_THAT(
      RunArgvRecords({"--diff-format=normal", root_.string(), "-name", "one.txt", "-diff:c", two}),
      ElementsAre(AllOf(HasSubstr("***"), Not(HasSubstr("4c4")))));
  // -diff:u5 (explicit context 5) wins over --diff-context=1: the hunk widens back to all 7 lines.
  EXPECT_THAT(
      RunArgvRecords({"--diff-context=1", root_.string(), "-name", "one.txt", "-diff:u5", two}),
      ElementsAre(HasSubstr("@@ -1,7 +1,7 @@")));
}

TEST_F(RunTest, DiffFormatAndContextRejectBadValues) {
  const auto run_expect_usage_error = [&](const std::vector<std::string>& argv, std::string_view message) {
    MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse(argv));
    std::vector<std::string> records;
    absl::Status reported;
    const auto [errors, any_match] = RunFind(
        command, fs_, [&](std::string_view record) { records.emplace_back(record); },
        [&](std::string_view, absl::Status status) { reported = status; });
    EXPECT_THAT(records, IsEmpty());
    EXPECT_THAT(errors, 2);
    EXPECT_THAT(reported, StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr(message)));
  };
  run_expect_usage_error(
      {"--diff-format=bogus", root_.string(), "-name", "a.txt", "-diff", Path("b.md")}, "unknown diff format 'bogus'");
  run_expect_usage_error(
      {"--diff-context=x", root_.string(), "-name", "a.txt", "-diff", Path("b.md")}, "bad --diff-context value 'x'");
}

TEST_F(RunTest, IgnoreFileExcludesMatchingEntriesRootedAtItsOwnDir) {
  { std::ofstream(root_ / "keep.log") << "x"; }
  { std::ofstream(root_ / "my.ignore") << "*.log\n"; }
  // --ignore-file reads the gitignore-format file and roots its patterns at the file's own
  // directory (here root_), so *.log is excluded anywhere beneath it while a.txt survives.
  const std::vector<std::string> out =
      RunArgvRecords({"--ignore-file=" + Path("my.ignore"), root_.string(), "-type", "f"});
  EXPECT_THAT(out, AllOf(Contains(Path("a.txt")), Not(Contains(Path("keep.log")))));
}

TEST_F(RunTest, IgnoreFileRootsAtItsOwnDirectoryNotTheSearchRoot) {
  { std::ofstream(root_ / "top.log") << "x"; }           // under root_, NOT under sub/
  { std::ofstream(root_ / "sub" / "deep.log") << "y"; }  // under sub/
  { std::ofstream(root_ / "sub" / "nested.ignore") << "*.log\n"; }
  // The ignore file lives in sub/, so its patterns root at sub/: sub/deep.log is excluded, but
  // the sibling top.log (outside sub/) is untouched -- the root is the file's dir, not the search
  // root, which is exactly why no separate --ignore-file-root flag is needed.
  const std::vector<std::string> out =
      RunArgvRecords({"--ignore-file=" + Path("sub/nested.ignore"), root_.string(), "-type", "f"});
  EXPECT_THAT(out, AllOf(Contains(Path("top.log")), Not(Contains(Path("sub/deep.log")))));
}

TEST_F(RunTest, HashActionPrintsDigestAndPath) {
  { std::ofstream(root_ / "abc.txt") << "abc"; }
  // -hash prints `<digest>  <path>` (the sha256sum layout); the default algorithm is sha256.
  EXPECT_THAT(
      RunExpr({"-name", "abc.txt", "-hash"}),
      ElementsAre("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  " + Path("abc.txt")));
  // -hash:ALGO[/ENCODING] selects the algorithm and hex/base64 rendering.
  EXPECT_THAT(
      RunExpr({"-name", "abc.txt", "-hash:md5"}), ElementsAre("900150983cd24fb0d6963f7d28e17f72  " + Path("abc.txt")));
  EXPECT_THAT(
      RunExpr({"-name", "abc.txt", "-hash:sha256/base64"}),
      ElementsAre("ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=  " + Path("abc.txt")));
}

TEST_F(RunTest, HashAlgorithmGlobalSetsTheDefaultForActionAndField) {
  { std::ofstream(root_ / "abc.txt") << "abc"; }
  // --hash-algorithm=md5 changes the default for a bare -hash action ...
  EXPECT_THAT(
      RunArgvRecords({"--hash-algorithm=md5", root_.string(), "-name", "abc.txt", "-hash"}),
      ElementsAre("900150983cd24fb0d6963f7d28e17f72  " + Path("abc.txt")));
  // ... and for a bare {hash} field (the %{hash} printf escape renders it).
  EXPECT_THAT(
      RunArgvRecords({"--hash-algorithm=md5", root_.string(), "-name", "abc.txt", "-printf", "%{hash}\n"}),
      ElementsAre("900150983cd24fb0d6963f7d28e17f72"));
}

TEST_F(RunTest, HashRejectsUnknownSpec) {
  // A bad -hash:ALGO[/ENCODING] spec is a pre-walk usage error (exit 2), not a silent no-op.
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({root_.string(), "-name", "a.txt", "-hash:crc32"}));
  std::vector<std::string> records;
  absl::Status reported;
  const auto [errors, any_match] = RunFind(
      command, fs_, [&](std::string_view record) { records.emplace_back(record); },
      [&](std::string_view, absl::Status status) { reported = status; });
  EXPECT_THAT(records, IsEmpty());
  EXPECT_THAT(errors, 2);
  EXPECT_THAT(reported, StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("'-hash:crc32'")));
}

TEST_F(RunTest, ColorAutoStaysPlainWhenStdoutIsNotATty) {
  // The captured stdout here is a pipe, so auto (the default) leaves even a
  // directory uncolored; only --color=always would force escapes.
  EXPECT_THAT(RunArgvRecords({root_.string(), "-name", "sub"}), ElementsAre(Path("sub")));
}

TEST_F(RunTest, DeleteRemovesMatchedFiles) {
  RunExpr({"-name", "*.txt", "-delete"});  // -delete implies -depth, so children go first
  EXPECT_THAT(fs::exists(root_ / "a.txt"), IsFalse());
  EXPECT_THAT(fs::exists(root_ / "sub" / "c.txt"), IsFalse());
  EXPECT_THAT(fs::exists(root_ / "b.md"), IsTrue());  // not matched
}

TEST_F(RunTest, DeleteDryRunPreviewsWithoutDeleting) {
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse({"--dry-run", root_.string(), "-name", "a.txt", "-delete"}));
  std::vector<std::string> records;
  RunFind(
      command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(fs::exists(root_ / "a.txt"), IsTrue());         // --dry-run: nothing deleted
  EXPECT_THAT(records, UnorderedElementsAre(Path("a.txt")));  // but previewed
}

TEST_F(RunTest, SafeRefusesDelete) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"--safe", root_.string(), "-delete"}));
  const auto [errors, any_match] =
      RunFind(command, fs_, [](std::string_view) {}, [](std::string_view, absl::Status) {});
  EXPECT_THAT(errors, 2);
  EXPECT_THAT(fs::exists(root_ / "a.txt"), IsTrue());  // refused: nothing deleted
}

TEST_F(RunTest, ExecRunsCommandPerMatch) {
  // -exec /bin/sh -c 'echo > "{}.ran"' ; creates a marker beside each matched file.
  RunExpr({"-name", "a.txt", "-exec", "/bin/sh", "-c", "echo > \"{}.ran\"", ";"});
  EXPECT_THAT(fs::exists(root_ / "a.txt.ran"), IsTrue());
}

TEST_F(RunTest, SafeRefusesExec) {
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command,
      parser::Parse({"--safe", root_.string(), "-exec", "/bin/sh", "-c", "echo > \"{}.ran\"", ";"}));
  const auto [errors, any_match] =
      RunFind(command, fs_, [](std::string_view) {}, [](std::string_view, absl::Status) {});
  EXPECT_THAT(errors, 2);
  EXPECT_THAT(fs::exists(root_ / "a.txt.ran"), IsFalse());  // refused: command not run
}

TEST_F(RunTest, SafetyRejectsEveryExecutionFamilyMemberBeforeTraversal) {
  const std::vector<std::string> primaries = {"-exec", "-execdir", "-ok", "-okdir", "-capture:x", "-capturedir:x"};
  for (const auto& primary : primaries) {
    MBO_ASSERT_OK_AND_ASSIGN(
        const auto command,
        parser::Parse({"--safe", Path("a.txt"), "-false", "-a", primary, "/usr/bin/printf", "unsafe", ";"}));
    std::vector<std::string> records;
    std::vector<std::string> diagnostics;
    const auto result = RunFind(
        command, fs_, [&](std::string_view r) { records.emplace_back(r); },
        [&](std::string_view, absl::Status status) { diagnostics.emplace_back(status.message()); });
    EXPECT_THAT(result.errors, 2) << primary;
    EXPECT_THAT(records, IsEmpty());
    EXPECT_THAT(diagnostics, ElementsAre(HasSubstr("blocked execution")));
  }
}

TEST_F(RunTest, DryRunCommandsNeverExecuteOrInventSubsequentMatches) {
  const std::vector<std::string> operators = {"-a", "-o", ",", "-xor", "-nand", "-nor", "-xnor"};
  for (const auto& op : operators) {
    MBO_ASSERT_OK_AND_ASSIGN(
        const auto command, parser::Parse(
                                {"--dry-run", Path("a.txt"), "!", "-exec", "/bin/sh", "-c", "touch " + Path("executed"),
                                 ";", op, "-fprint", Path("out")}));
    std::vector<std::string> records;
    const auto result = RunFind(
        command, fs_, [&](std::string_view r) { records.emplace_back(r); }, [](std::string_view, absl::Status) {});
    EXPECT_THAT(result.errors, 1) << op;
    EXPECT_THAT(records, ElementsAre(HasSubstr("would execute")));
    EXPECT_THAT(fs::exists(Path("executed")), IsFalse());
    EXPECT_THAT(fs::exists(Path("out")), IsFalse());
  }
}

TEST_F(RunTest, DryRunCoversPromptedBatchedAndCaptureExecution) {
  const std::vector<std::string> primaries = {"-execdir", "-ok", "-okdir", "-capture:x", "-capturedir:x"};
  for (const auto& primary : primaries) {
    MBO_ASSERT_OK_AND_ASSIGN(
        const auto command, parser::Parse(
                                {"--dry-run", "--template={capture.x}", Path("a.txt"), primary, "/bin/sh", "-c",
                                 "touch " + Path("executed"), ";"}));
    const auto result = RunFind(command, fs_, [](std::string_view) {}, [](std::string_view, absl::Status) {});
    EXPECT_THAT(result.errors, 1) << primary;
    EXPECT_THAT(fs::exists(Path("executed")), IsFalse());
  }
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto batch,
      parser::Parse(
          {"--dry-run", "-j2", Path("a.txt"), "-exec", "/bin/sh", "-c", "touch " + Path("executed"), "{}", "+"}));
  EXPECT_THAT(RunFind(batch, fs_, [](std::string_view) {}, [](std::string_view, absl::Status) {}).errors, 1);
  EXPECT_THAT(fs::exists(Path("executed")), IsFalse());
}

TEST_F(RunTest, NewFileOnlyOutputRetainsItsHandleAndRejectsCollisions) {
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto fresh,
      parser::Parse(
          {"--safe", "--no-safe-block-file-writing", Path("a.txt"), "-fprint", Path("new"), "-fprint", Path("new")}));
  EXPECT_THAT(RunFind(fresh, fs_, [](std::string_view) {}, [](std::string_view, absl::Status) {}).errors, 0);
  EXPECT_THAT(fs::file_size(Path("new")), 2 * (Path("a.txt").size() + 1));
  EXPECT_THAT(RunFind(fresh, fs_, [](std::string_view) {}, [](std::string_view, absl::Status) {}).errors, 2);
  fs::create_symlink(Path("missing"), Path("link"));
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto link, parser::Parse({"--block-file-overwrite", Path("a.txt"), "-fprint", Path("link")}));
  EXPECT_THAT(RunFind(link, fs_, [](std::string_view) {}, [](std::string_view, absl::Status) {}).errors, 1);
  EXPECT_THAT(fs::exists(Path("missing")), IsFalse());
}

TEST_F(RunTest, WritingBlocksCannotBeRemovedAndDryRunPreservesExistingOutput) {
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto blocked, parser::Parse({"--block-file-writing", "--no-safe", Path("a.txt"), "-fprint", Path("b.md")}));
  EXPECT_THAT(RunFind(blocked, fs_, [](std::string_view) {}, [](std::string_view, absl::Status) {}).errors, 2);
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto preview, parser::Parse({"--dry-run", Path("a.txt"), "-fprint", Path("b.md"), "-fprint", Path("new")}));
  std::vector<std::string> records;
  EXPECT_THAT(
      RunFind(
          preview, fs_, [&](std::string_view r) { records.emplace_back(r); }, [](std::string_view, absl::Status) {})
          .errors,
      0);
  EXPECT_THAT(records, ElementsAre(HasSubstr("would overwrite"), HasSubstr("would create")));
  EXPECT_THAT(fs::file_size(Path("b.md")), 1);
  EXPECT_THAT(fs::exists(Path("new")), IsFalse());
}

TEST_F(RunTest, UnknownTimezoneIsRefusedBeforeTraversal) {
  // An unknown --timezone is a usage error refused before the walk (exit 2), like
  // the --safe guards above: reported via on_error, emitting nothing.
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"--timezone=Not/AZone", root_.string()}));
  std::string err_path;
  absl::Status err_status;
  bool emitted = false;
  const auto [errors, any_match] = RunFind(
      command, fs_, [&](std::string_view) { emitted = true; },
      [&](std::string_view path, absl::Status status) {
        err_path = std::string(path);
        err_status = status;
      });
  EXPECT_THAT(errors, 2);
  EXPECT_THAT(err_path, "--timezone");
  EXPECT_THAT(err_status, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(emitted, IsFalse()) << "an invalid --timezone must not traverse";
}

TEST_F(RunTest, OversizedSizeUnitIsRefusedBeforeTraversal) {
  // -size with an over-64-bit unit (Z/Y/...) is a usage error refused before the
  // walk (exit 2), naming the limit -- not a silent per-entry no-match.
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({root_.string(), "-size", "+1Z"}));
  absl::Status err_status;
  bool emitted = false;
  const auto [errors, any_match] = RunFind(
      command, fs_, [&](std::string_view) { emitted = true; },
      [&](std::string_view, absl::Status status) { err_status = status; });
  EXPECT_THAT(errors, 2);
  EXPECT_THAT(err_status, StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("largest units")));
  EXPECT_THAT(emitted, IsFalse()) << "a malformed -size must not traverse";
}

TEST_F(RunTest, BlockSizeRedefinesTheBareSizeUnit) {
  // A 1000-byte file is 2 blocks at the default 512 (so -size 1 misses, -size 2
  // hits) but 1 block under --block-size=4k (so -size 1 hits). Proves the global
  // redefines the bare/`b` -size unit end to end.
  { std::ofstream(root_ / "kilo.bin", std::ios::binary) << std::string(1'000, 'x'); }
  EXPECT_THAT(RunArgvRecords({root_.string(), "-name", "kilo.bin", "-size", "1"}), IsEmpty());
  EXPECT_THAT(RunArgvRecords({root_.string(), "-name", "kilo.bin", "-size", "2"}), ElementsAre(Path("kilo.bin")));
  EXPECT_THAT(
      RunArgvRecords({"--block-size=4k", root_.string(), "-name", "kilo.bin", "-size", "1"}),
      ElementsAre(Path("kilo.bin")));
}

TEST_F(RunTest, BlockSizeAcceptsExplicitDecimalAndBinaryUnits) {
  { std::ofstream(root_ / "kilo.bin", std::ios::binary) << std::string(1'001, 'x'); }
  EXPECT_THAT(
      RunArgvRecords({"--block-size=1kB", root_.string(), "-name", "kilo.bin", "-size", "2"}),
      ElementsAre(Path("kilo.bin")));
  EXPECT_THAT(
      RunArgvRecords({"--block-size=1KiB", root_.string(), "-name", "kilo.bin", "-size", "1"}),
      ElementsAre(Path("kilo.bin")));
}

TEST_F(RunTest, InvalidBlockSizeIsRefusedBeforeTraversal) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"--block-size=0", root_.string()}));
  absl::Status err_status;
  bool emitted = false;
  const auto [errors, any_match] = RunFind(
      command, fs_, [&](std::string_view) { emitted = true; },
      [&](std::string_view, absl::Status status) { err_status = status; });
  EXPECT_THAT(errors, 2);
  EXPECT_THAT(err_status, StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("positive")));
  EXPECT_THAT(emitted, IsFalse()) << "an invalid --block-size must not traverse";
}

TEST_F(RunTest, ValidTimezoneIsAcceptedAndTheRunProceeds) {
  // A valid --timezone resolves and the run proceeds normally (here it does not
  // change the result, just proving the flag is accepted end to end).
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"--timezone=UTC", root_.string(), "-name", "a.txt"}));
  std::vector<std::string> records;
  const auto [errors, any_match] = RunFind(
      command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(errors, 0);
  EXPECT_THAT(records, UnorderedElementsAre(Path("a.txt")));
}

TEST_F(RunTest, TimezoneAppliesToTimeFieldFormatting) {
  // --timezone reaches time-field formatting too: {mtime:%z} is the numeric zone
  // offset, so under --timezone=UTC it is "+0000" regardless of the host's zone.
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse({"--timezone=UTC", "--template={mtime:%z}", root_.string(), "-name", "a.txt"}));
  std::vector<std::string> records;
  RunFind(
      command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, UnorderedElementsAre("+0000"));
}

TEST_F(RunTest, TheZoneSuffixSwitchSpellingsWork) {
  // --time-zone-suffix documented on / off as synonyms of always / never while the shared parser
  // rejected them, so =off silently kept the default (the offset stayed). Both spellings of each
  // side must reach the same result.
  const auto run = [&](std::string_view flag) {
    const auto command = parser::Parse({std::string(flag), "--template={mtime}", root_.string(), "-name", "a.txt"});
    EXPECT_THAT(command, IsOk());
    std::vector<std::string> records;
    RunFind(
        *command, fs_,
        [&](std::string_view record) {
          std::string text(record);
          if (!text.empty() && text.back() == '\n') {
            text.pop_back();
          }
          records.push_back(std::move(text));
        },
        [](std::string_view, absl::Status) {});
    return records.size() == 1 ? records.front() : std::string();
  };
  EXPECT_THAT(run("--time-zone-suffix=off"), Eq(run("--time-zone-suffix=never")));
  EXPECT_THAT(run("--time-zone-suffix=on"), Eq(run("--time-zone-suffix=always")));
  EXPECT_THAT(run("--time-zone-suffix=off"), Ne(run("--time-zone-suffix=on")));
}

TEST_F(RunTest, TzIsAnAliasForTimezone) {
  // --tz=ZONE is the short alias of --timezone=ZONE: under --tz=UTC the {mtime:%z}
  // numeric offset is "+0000" regardless of the host zone, just as --timezone=UTC.
  EXPECT_THAT(
      RunArgvRecords({"--tz=UTC", "--template={mtime:%z}", root_.string(), "-name", "a.txt"}),
      UnorderedElementsAre("+0000"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, FixedOffsetTimezoneAppliesToFormatting) {
  // A fixed UTC offset (+05:30) is accepted as a zone spec and reaches time-field
  // formatting: {mtime:%z} renders the zone's numeric offset, "+0530".
  EXPECT_THAT(
      RunArgvRecords({"--tz=+05:30", "--template={mtime:%z}", root_.string(), "-name", "a.txt"}),
      UnorderedElementsAre("+0530"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, AnyMatchIsTrueWhenExpressionMatches) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({root_.string(), "-name", "a.txt"}));
  const RunResult result = RunFind(command, fs_, [](std::string_view) {}, [](std::string_view, absl::Status) {});
  EXPECT_THAT(result.any_match, IsTrue());
}

TEST_F(RunTest, AnyMatchIsFalseWhenNothingMatches) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({root_.string(), "-name", "no-such-file.zzz"}));
  const RunResult result = RunFind(command, fs_, [](std::string_view) {}, [](std::string_view, absl::Status) {});
  EXPECT_THAT(result.any_match, IsFalse());
}

TEST_F(RunTest, AnyMatchReflectsExpressionNotEmittedOutput) {
  // any_match is the expression's truth, not output: with --implicit-print=no, a.txt
  // matches but nothing is emitted, yet any_match is still true (so --quiet on an
  // action-only expression like `-exec` still reports the match).
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse({"--implicit-print=no", root_.string(), "-name", "a.txt"}));
  std::vector<std::string> records;
  const RunResult result = RunFind(
      command, fs_, [&](std::string_view record) { records.emplace_back(record); },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, IsEmpty());
  EXPECT_THAT(result.any_match, IsTrue());
}

TEST_F(RunTest, TimeFormatGlobalSetsTheBareTimeFieldDefault) {
  // --time-format=epoch makes a bare {mtime} render as Unix seconds (all digits,
  // no date dashes), proving the global threads through to time-field formatting.
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command,
      parser::Parse({"--time-format=epoch", "--template={mtime}", root_.string(), "-name", "a.txt"}));
  std::vector<std::string> records;
  RunFind(
      command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  // epoch is all digits; a date format would carry dashes. ElementsAre folds the
  // single-match count and the content check into one matcher.
  EXPECT_THAT(records, ElementsAre(Not(HasSubstr("-"))));
}

TEST_F(RunTest, TemplateRendersImplicitPrint) {
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse({"--template={name}:{type}", root_.string(), "-name", "a.txt"}));
  std::vector<std::string> records;
  RunFind(
      command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, UnorderedElementsAre("a.txt:f"));  // {name}:{type} for a regular file
}

TEST_F(RunTest, TemplateRootFieldReportsTheSearchOperand) {
  // {root} is the command-line operand a match descends from (find %H); a nested
  // match (sub/c.txt) still reports the operand, exercising run.cc's wiring of
  // Visit::root into the render context.
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse({"--template={root}|{name}", root_.string(), "-name", "c.txt"}));
  std::vector<std::string> records;
  RunFind(
      command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, UnorderedElementsAre(root_.string() + "|c.txt"));
}

TEST_F(RunTest, ExecFieldsRendersNamedPlaceholders) {
  // --exec-fields routes -exec tokens through the field vocabulary: {path} is the
  // full path, so the marker lands beside the matched file (vs. a literal "{path}"
  // file in the cwd without the flag).
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command,
      parser::Parse(
          {"--exec-fields", root_.string(), "-name", "a.txt", "-exec", "/bin/sh", "-c", "echo > \"{path}.fld\"", ";"}));
  RunFind(command, fs_, [](std::string_view) {}, [](std::string_view, absl::Status) {});
  EXPECT_THAT(fs::exists(root_ / "a.txt.fld"), IsTrue());
}

TEST_F(RunTest, ExecFieldsSubstitutesRegexCaptures) {
  // --exec-fields + a -regex match: {1}/{2} resolve to the capture groups, written
  // to a marker beside the file ({path} keeps the marker absolute for cleanup).
  MBO_ASSERT_OK_AND_ASSIGN(
      auto command, parser::Parse(
                        {"--exec-fields", root_.string(), "-regex", ".*/(a)\\.(txt)", "-exec", "/bin/sh", "-c",
                         R"(printf '%s' "{1}.{2}" > "{path}.cap")", ";"}));
  parser::BindMatchers(command, parser::GrammarFromGlobals(command.globals), parser::CaseMode::kSensitive);
  RunFind(command, fs_, [](std::string_view) {}, [](std::string_view, absl::Status) {});
  const fs::path marker = root_ / "a.txt.cap";
  ASSERT_THAT(fs::exists(marker), IsTrue());
  std::ifstream in(marker);
  std::string content;
  std::getline(in, content);
  EXPECT_THAT(content, "a.txt");  // {1}="a", {2}="txt"
}

TEST_F(RunTest, DefinePopulatesDefNamespace) {
  // --define=NAME=VALUE surfaces as {def.NAME} in --template output (last wins).
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse(
                              {"--define=label=old", "--define=label=new", "--template={def.label}:{name}",
                               root_.string(), "-name", "a.txt"}));
  std::vector<std::string> records;
  RunFind(
      command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, UnorderedElementsAre("new:a.txt"));  // last --define wins
}

TEST_F(RunTest, CaptureBindsOutputForTemplate) {
  // -capture runs a command per match (with {} -> path) and binds its stdout to
  // {capture.NAME}; --template then prints it.
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse(
                              {"--template={capture.base}", root_.string(), "-name", "a.txt", "-capture:base",
                               "/bin/sh", "-c", "basename {}", ";"}));
  std::vector<std::string> records;
  RunFind(
      command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, UnorderedElementsAre("a.txt"));
}

TEST_F(RunTest, CaptureChainsPriorOutputs) {
  // A later -capture command references an earlier capture's {capture.*}.
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse(
                              {"--template={capture.b}", root_.string(), "-name", "a.txt", "-capture:a", "/bin/sh",
                               "-c", "printf X", ";", "-capture:b", "/bin/sh", "-c", "printf {capture.a}Y", ";"}));
  std::vector<std::string> records;
  RunFind(
      command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, UnorderedElementsAre("XY"));  // b = {capture.a}("X") + "Y"
}

TEST_F(RunTest, DuplicateCaptureNameIsErrorByDefault) {
  // Two -capture actions binding the same NAME, neither carrying `!` -> exit 2,
  // reported before traversal (silent clobbering would mean wrong data).
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse(
                              {root_.string(), "-capture:x", "/bin/sh", "-c", "printf a", ";", "-capture:x", "/bin/sh",
                               "-c", "printf b", ";"}));
  int errors = 0;
  const auto [code, any_match] =
      RunFind(command, fs_, [](std::string_view) {}, [&](std::string_view, absl::Status) { ++errors; });
  EXPECT_THAT(code, 2);
  EXPECT_THAT(errors, 1);
}

TEST_F(RunTest, BangModifierAllowsDuplicateCaptureNameLastWins) {
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse(
                              {"--template={capture.x}", root_.string(), "-name", "a.txt", "-capture:x", "/bin/sh",
                               "-c", "printf a", ";", "-capture:!x", "/bin/sh", "-c", "printf b", ";"}));
  std::vector<std::string> records;
  RunFind(
      command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, UnorderedElementsAre("b"));  // the `!` node re-binds, so the last -capture wins
}

TEST_F(RunTest, CaptureUsedByPrintfAndGrepTemplates) {
  const auto captures = std::to_array<std::string>({"-capture:x", "-capturedir:x"});
  for (const std::string& capture : captures) {
    const auto outputs = std::to_array<std::string>({"-printf", "-printfln"});
    for (const std::string& output : outputs) {
      EXPECT_THAT(
          RunExpr({"-name", "a.txt", capture, "/usr/bin/printf", "answer", ";", output, "%{capture.x}"}),
          ElementsAre("answer"));
      EXPECT_THAT(last_errors_, 0);
    }
    EXPECT_THAT(
        RunExpr({"-name", "a.txt", capture, "/usr/bin/printf", "answer", ";", "-grep:{capture.x}", "."}),
        ElementsAre("answer"));
    EXPECT_THAT(last_errors_, 0);
  }
}

TEST_F(RunTest, CaptureUsedByFilePrintf) {
  const auto actions = std::to_array<std::string>({"-fprintf", "-fprintfln"});
  for (const std::string& action : actions) {
    const std::string target = Path(action);
    EXPECT_THAT(
        RunExpr({"-name", "a.txt", "-capture:x", "/usr/bin/printf", "answer", ";", action, target, "%{capture.x}"}),
        IsEmpty());
    EXPECT_THAT(last_errors_, 0);
    EXPECT_THAT(fs_.ReadContent(target), IsOkAndHolds(Eq(action == "-fprintf" ? "answer" : "answer\n")));
  }
}

TEST_F(RunTest, CaptureUsedByLaterDirectoryCaptureAndComparison) {
  EXPECT_THAT(
      RunExpr(
          {"-name", "a.txt", "-capture:x", "/usr/bin/printf", Path("a.txt"), ";", "-capturedir:y", "/usr/bin/printf",
           "{capture.x}", ";", "-cmp", "{capture.y}", "--template={name}"}),
      ElementsAre("a.txt"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, CaptureUsedByDirectoryExec) {
  EXPECT_THAT(
      RunExpr(
          {"--exec-fields", "-name", "a.txt", "-capture:x", "/usr/bin/printf", "answer", ";", "-execdir", "/bin/sh",
           "-c", "test '{capture.x}' = answer", ";"}),
      IsEmpty());
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, CaptureUsedByColumn) {
  EXPECT_THAT(
      RunExpr(
          {"--format=csv", "--columns=capture.x", "-name", "a.txt", "-capture:x", "/usr/bin/printf", "answer", ";"}),
      ElementsAre("capture.x", "answer"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, DirectoryCapturesRejectUnusedAndDuplicateNames) {
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-capturedir:x", "/usr/bin/printf", "answer", ";"}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
  EXPECT_THAT(
      RunExpr(
          {"-name", "a.txt", "-capturedir:x", "/usr/bin/printf", "one", ";", "-capture:x", "/usr/bin/printf", "two",
           ";", "--template={capture.x}"}),
      IsEmpty());
  EXPECT_THAT(last_errors_, 2);
}

TEST_F(RunTest, LiteralCaptureSpellingsDoNotCountAsReferences) {
  const auto formats = std::to_array<std::string>({
      "{capture.x}",
      "%%{capture.x}",
      "%{{capture.x}}",
      "%{capture.x",
      R"(\%{capture.x})",
      "%A%{capture.x}",
      "%C%{capture.x}",
      "%T%{capture.x}",
  });
  for (const std::string& format : formats) {
    EXPECT_THAT(
        RunExpr({"-name", "a.txt", "-capture:x", "/usr/bin/printf", "answer", ";", "-printf", format}), IsEmpty())
        << format;
    EXPECT_THAT(last_errors_, 2) << format;
  }
  EXPECT_THAT(
      RunExpr({"-name", "a.txt", "-capture:x", "/usr/bin/printf", "answer", ";", "--template={{capture.x}}"}),
      IsEmpty());
  EXPECT_THAT(last_errors_, 2);
}

TEST_F(RunTest, InvalidBufferBoundsFailBeforeActionsInEitherCompareSide) {
  const auto options = std::to_array<std::string>({"--buffer=garbage", "--buffer=18446744073709551615T"});
  for (const std::string& option : options) {
    EXPECT_THAT(RunExpr({option, "-print"}), IsEmpty());
    EXPECT_THAT(last_errors_, 2);
    EXPECT_THAT(RunArgvRecords({"--compare", root_.string(), root_.string(), option, "-print"}), IsEmpty());
    EXPECT_THAT(last_errors_, 2);
  }
}

TEST_F(RunTest, InvalidHistogramWidthsFailBeforeActionsInEitherCompareSide) {
  const auto options = std::to_array<std::string>({"--histogram-width=garbage", "--histogram-width=0"});
  for (const std::string& option : options) {
    EXPECT_THAT(RunExpr({option, "-print"}), IsEmpty());
    EXPECT_THAT(last_errors_, 2);
    EXPECT_THAT(RunArgvRecords({"--compare", root_.string(), root_.string(), option, "-print"}), IsEmpty());
    EXPECT_THAT(last_errors_, 2);
  }
}

TEST_F(RunTest, UnusedCaptureIsError) {
  // -capture:x but {capture.x} is referenced nowhere -> exit 2 before traversal.
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command,
      parser::Parse({root_.string(), "-name", "a.txt", "-capture:x", "/bin/sh", "-c", "printf a", ";"}));
  int errors = 0;
  const auto [code, any_match] =
      RunFind(command, fs_, [](std::string_view) {}, [&](std::string_view, absl::Status) { ++errors; });
  EXPECT_THAT(code, 2);
  EXPECT_THAT(errors, 1);
}

TEST_F(RunTest, CaptureUsedByLaterExecIsNotFlagged) {
  // {capture.x} referenced in a later -exec counts as used -> no unused error.
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse(
                              {"--exec-fields", root_.string(), "-name", "a.txt", "-capture:x", "/bin/sh", "-c",
                               "printf a", ";", "-exec", "/bin/sh", "-c", "test \"{capture.x}\" = a", ";"}));
  int errors = 0;
  const auto [code, any_match] =
      RunFind(command, fs_, [](std::string_view) {}, [&](std::string_view, absl::Status) { ++errors; });
  EXPECT_THAT(code, 0);  // used by the -exec, so not flagged
  EXPECT_THAT(errors, 0);
}

TEST_F(RunTest, CaptureUsedByNegatedExecIsNotFlagged) {
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse(
                              {"--exec-fields", root_.string(), "-name", "a.txt", "-capture:x", "/bin/sh", "-c",
                               "printf a", ";", "!", "-exec", "/bin/sh", "-c", "test \"{capture.x}\" = b", ";"}));
  int errors = 0;
  const auto [code, any_match] =
      RunFind(command, fs_, [](std::string_view) {}, [&](std::string_view, absl::Status) { ++errors; });
  EXPECT_THAT(code, 0);
  EXPECT_THAT(any_match, IsTrue());
  EXPECT_THAT(errors, 0);
}

TEST_F(RunTest, ImplicitPrintNoSuppressesDefaultPrint) {
  // No action, so find would print -- --implicit-print=no forces it off.
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse({"--implicit-print=no", root_.string(), "-name", "a.txt"}));
  std::vector<std::string> records;
  RunFind(
      command, fs_, [&](std::string_view record) { records.emplace_back(record); },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, IsEmpty());
}

TEST_F(RunTest, ImplicitPrintYesPrintsAlongsideAction) {
  // -exec would suppress the implicit print; --implicit-print=yes forces it on.
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command,
      parser::Parse({"--implicit-print=yes", root_.string(), "-name", "a.txt", "-exec", "/bin/sh", "-c", "true", ";"}));
  std::vector<std::string> records;
  RunFind(
      command, fs_,
      [&](std::string_view record) {
        std::string text(record);
        if (!text.empty() && text.back() == '\n') {
          text.pop_back();
        }
        records.push_back(std::move(text));
      },
      [](std::string_view, absl::Status) {});
  EXPECT_THAT(records, UnorderedElementsAre(Path("a.txt")));
}

// The default --summary output is a right-aligned human table (grouped digits);
// these assert the stable --format=jsonl machine rows instead, so the exact counts
// are checked without depending on column padding. The aligned human rendering is
// covered end to end by cli/summary_test.sh.
TEST_F(RunTest, SummaryOverallReducesMatchesToACountAndSize) {
  // --summary suppresses the per-match print and emits one total row: a.txt and
  // sub/c.txt match (1 byte each), so 2 matches / 2 bytes.
  EXPECT_THAT(
      RunArgvRecords({"--summary", "--format=jsonl", root_.string(), "-name", "*.txt"}),
      ElementsAre(
          R"({"record":"summary","request":0,"summary":"overall","scope":"all","root":"","group":"total","count":2,"count_percent":100.00,"bytes":2,"size_percent":100.00,"is_total":true})"));
}

TEST_F(RunTest, SummaryByTypeGroupsThenTotals) {
  // --summary=type over the three files (1 byte each): one "file" group, then total.
  EXPECT_THAT(
      RunArgvRecords({"--summary=type", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(
          R"({"record":"summary","request":0,"summary":"type","scope":"all","root":"","group":"file","count":3,"count_percent":100.00,"bytes":3,"size_percent":100.00,"is_total":false})",
          R"({"record":"summary","request":0,"summary":"type","scope":"all","root":"","group":"total","count":3,"count_percent":100.00,"bytes":3,"size_percent":100.00,"is_total":true})"));
}

TEST_F(RunTest, SummaryByExtensionGroupsSortedThenTotals) {
  // --summary=ext over the files: "md" (b.md) sorts before "txt" (a.txt, sub/c.txt).
  EXPECT_THAT(
      RunArgvRecords({"--summary=ext", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(
          R"({"record":"summary","request":0,"summary":"ext","scope":"all","root":"","group":"md","count":1,"count_percent":33.33,"bytes":1,"size_percent":33.33,"is_total":false})",
          R"({"record":"summary","request":0,"summary":"ext","scope":"all","root":"","group":"txt","count":2,"count_percent":66.67,"bytes":2,"size_percent":66.67,"is_total":false})",
          R"({"record":"summary","request":0,"summary":"ext","scope":"all","root":"","group":"total","count":3,"count_percent":100.00,"bytes":3,"size_percent":100.00,"is_total":true})"));
}

TEST_F(RunTest, SummaryByLanguageGroupsThenTotals) {
  // --summary=lang: b.md is Markdown; a.txt and sub/c.txt have no known language ("(none)").
  // "(" sorts before letters, so the "(none)" bucket leads, then Markdown, then the total.
  EXPECT_THAT(
      RunArgvRecords({"--summary=lang", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(
          R"j({"record":"summary","request":0,"summary":"lang","scope":"all","root":"","group":"(none)","count":2,"count_percent":66.67,"bytes":2,"size_percent":66.67,"is_total":false})j",
          R"j({"record":"summary","request":0,"summary":"lang","scope":"all","root":"","group":"Markdown","count":1,"count_percent":33.33,"bytes":1,"size_percent":33.33,"is_total":false})j",
          R"j({"record":"summary","request":0,"summary":"lang","scope":"all","root":"","group":"total","count":3,"count_percent":100.00,"bytes":3,"size_percent":100.00,"is_total":true})j"));
}

TEST_F(RunTest, SummaryHashVerificationCountsPassedAndFailedChecksInOneWalk) {
  // sha256("a"): a.txt verifies; b.md and sub/c.txt reach the same -hasheq and fail. Failed
  // predicates make the overall expression false but remain part of the verification tally.
  constexpr std::string_view kSha256A = "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb";
  EXPECT_THAT(
      RunArgvRecords(
          {"--summary=type", "--summary=hash-verification", "--format=jsonl", root_.string(), "-type", "f", "-hasheq",
           std::string(kSha256A)}),
      ElementsAre(
          R"({"record":"summary","request":0,"summary":"type","scope":"all","root":"","group":"file","count":1,"count_percent":100.00,"bytes":1,"size_percent":100.00,"is_total":false})",
          R"({"record":"summary","request":0,"summary":"type","scope":"all","root":"","group":"total","count":1,"count_percent":100.00,"bytes":1,"size_percent":100.00,"is_total":true})",
          R"({"record":"summary","request":1,"summary":"hash-verification","scope":"all","root":"","group":"failed","count":2,"count_percent":66.67,"bytes":2,"size_percent":66.67,"is_total":false})",
          R"({"record":"summary","request":1,"summary":"hash-verification","scope":"all","root":"","group":"verified","count":1,"count_percent":33.33,"bytes":1,"size_percent":33.33,"is_total":false})",
          R"({"record":"summary","request":1,"summary":"hash-verification","scope":"all","root":"","group":"total","count":3,"count_percent":100.00,"bytes":3,"size_percent":100.00,"is_total":true})"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, SummaryHashVerificationDoesNotCountChecksSkippedByShortCircuiting) {
  constexpr std::string_view kSha256A = "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb";
  EXPECT_THAT(
      RunArgvRecords(
          {"--summary=hash-verification", "--format=jsonl", root_.string(), "-false", "-a", "-hasheq",
           std::string(kSha256A)}),
      ElementsAre(
          R"({"record":"summary","request":0,"summary":"hash-verification","scope":"all","root":"","group":"total","count":0,"count_percent":0.00,"bytes":0,"size_percent":0.00,"is_total":true})"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, SummaryHashVerificationRequiresExactlyOneHashCheck) {
  constexpr std::string_view kSha256A = "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb";
  EXPECT_THAT(RunArgvRecords({"--summary=hash-verification", root_.string(), "-type", "f"}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
  EXPECT_THAT(
      RunArgvRecords(
          {"--summary=hash-verification", root_.string(), "-hasheq", std::string(kSha256A), "-o", "-hasheq",
           std::string(kSha256A)}),
      IsEmpty());
  EXPECT_THAT(last_errors_, 2);
  // Negation changes expression truth, not the number or polarity of verification checks.
  EXPECT_THAT(
      RunArgvRecords(
          {"--summary=hash-verification", "--format=jsonl", root_.string(), "-name", "a.txt", "!", "-hasheq",
           "deadbeef"}),
      ElementsAre(
          R"({"record":"summary","request":0,"summary":"hash-verification","scope":"all","root":"","group":"failed","count":1,"count_percent":100.00,"bytes":1,"size_percent":100.00,"is_total":false})",
          R"({"record":"summary","request":0,"summary":"hash-verification","scope":"all","root":"","group":"total","count":1,"count_percent":100.00,"bytes":1,"size_percent":100.00,"is_total":true})"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, SummaryHashVerificationClassifiesMissingExpectationsAndNonRegularEntriesAsFailed) {
  EXPECT_THAT(
      RunArgvRecords(
          {"--summary=hash-verification", "--format=jsonl", root_.string(), "-name", "a.txt", "-hasheq",
           "{def.MISSING}"}),
      ElementsAre(
          R"({"record":"summary","request":0,"summary":"hash-verification","scope":"all","root":"","group":"failed","count":1,"count_percent":100.00,"bytes":1,"size_percent":100.00,"is_total":false})",
          R"({"record":"summary","request":0,"summary":"hash-verification","scope":"all","root":"","group":"total","count":1,"count_percent":100.00,"bytes":1,"size_percent":100.00,"is_total":true})"));
  EXPECT_THAT(last_errors_, 0);
  EXPECT_THAT(
      RunArgvRecords(
          {"--summary=hash-verification", "--format=jsonl", root_.string(), "-type", "d", "-hasheq", "deadbeef"}),
      ElementsAre(HasSubstr(R"("group":"failed","count":2)"), HasSubstr(R"("group":"total","count":2)")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, SummaryHashVerificationVerdictSurvivesDeferredReplayWithoutASecondHash) {
  { std::ofstream(root_ / "verify-00000-of-00002") << "a"; }
  { std::ofstream(root_ / "verify-00001-of-00002") << "b"; }
  constexpr std::string_view kSha256A = "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb";
  // The comma evaluates -hasheq before -shard-status deliberately defers both entries. Replay uses
  // its memoized prefix and the candidate's saved verdict, so each physical entry contributes once.
  EXPECT_THAT(
      RunArgvRecords(
          {"--summary=hash-verification", "--format=jsonl", root_.string(), "-name", "verify-*", "-a", "(", "-hasheq",
           std::string(kSha256A), ",", "-shard-status", "complete", ")"}),
      ElementsAre(
          R"({"record":"summary","request":0,"summary":"hash-verification","scope":"all","root":"","group":"failed","count":1,"count_percent":50.00,"bytes":1,"size_percent":50.00,"is_total":false})",
          R"({"record":"summary","request":0,"summary":"hash-verification","scope":"all","root":"","group":"verified","count":1,"count_percent":50.00,"bytes":1,"size_percent":50.00,"is_total":false})",
          R"({"record":"summary","request":0,"summary":"hash-verification","scope":"all","root":"","group":"total","count":2,"count_percent":100.00,"bytes":2,"size_percent":100.00,"is_total":true})"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, SummaryTopKeepsTheLargestGroupsBySize) {
  // --top=1: keep the largest group by size (txt, 2 bytes) and drop md (1 byte),
  // ordered by size; the total row still counts every matched group.
  EXPECT_THAT(
      RunArgvRecords({"--summary=ext", "--top=1", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(
          R"({"record":"summary","request":0,"summary":"ext","scope":"all","root":"","group":"txt","count":2,"count_percent":66.67,"bytes":2,"size_percent":66.67,"is_total":false})",
          R"({"record":"summary","request":0,"summary":"ext","scope":"all","root":"","group":"total","count":3,"count_percent":100.00,"bytes":3,"size_percent":100.00,"is_total":true})"));
}

TEST_F(RunTest, SummaryControlsRejectInvalidNumbersBeforeActions) {
  static constexpr auto kInvalidFlags = std::to_array<std::string_view>(
      {"--summary-precision=", "--summary-precision=garbage", "--summary-precision=-1", "--summary-precision=10",
       "--top=", "--top=garbage", "--top=-1", "--top=18446744073709551616"});
  for (const std::string_view flag : kInvalidFlags) {
    SCOPED_TRACE(flag);
    EXPECT_THAT(RunArgvRecords({root_.string(), "--summary=ext", std::string(flag), "-print"}), IsEmpty());
    EXPECT_THAT(last_errors_, 2);
    EXPECT_THAT(
        RunArgvRecords({"--compare=summary", root_.string(), Path("sub"), std::string(flag), "-print"}), IsEmpty());
    EXPECT_THAT(last_errors_, 2);
  }
}

TEST_F(RunTest, SummaryTopZeroRestoresAllGroupsAndLaterValuesWin) {
  const auto unlimited = RunArgvRecords({root_.string(), "-type", "f", "--summary=ext", "--format=jsonl"});
  EXPECT_THAT(
      RunArgvRecords({root_.string(), "-type", "f", "--summary=ext", "--top=1", "--top=0", "--format=jsonl"}),
      Eq(unlimited));
  EXPECT_THAT(
      RunArgvRecords({root_.string(), "-type", "f", "--summary=ext", "--top=0", "--top=1", "--format=jsonl"}),
      ElementsAre(HasSubstr(R"("group":"txt")"), HasSubstr(R"("group":"total","count":3)")));
}

TEST_F(RunTest, SummaryPrecisionAcceptsBothEndpointsAndLastValueWins) {
  EXPECT_THAT(
      RunArgvRecords({root_.string(), "-type", "f", "--summary", "--summary-precision=0", "--format=jsonl"}),
      Contains(HasSubstr(R"("count_percent":100,)")));
  EXPECT_THAT(
      RunArgvRecords(
          {root_.string(), "-type", "f", "--summary", "--summary-precision=0", "--summary-precision=9",
           "--format=jsonl"}),
      Contains(HasSubstr(R"("count_percent":100.000000000,)")));
}

TEST_F(RunTest, CompareSelectionRequiresComparisonBeforeActions) {
  EXPECT_THAT(RunArgvRecords({root_.string(), "--compare-select=none", "-print"}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
  EXPECT_THAT(
      RunArgvRecords({"--compare-select=none", "--compare", root_.string(), Path("sub"), "-type", "f"}), IsEmpty());
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, SummaryRejectsUnsupportedFormatsInsteadOfWritingPlainText) {
  static constexpr auto kListingFormats =
      std::to_array<std::string_view>({"--format=csv", "--format=tsv", "--format=nul", "--format=tree"});
  for (const std::string_view flag : kListingFormats) {
    SCOPED_TRACE(flag);
    EXPECT_THAT(RunArgvRecords({root_.string(), "--summary=ext", std::string(flag), "-print"}), IsEmpty());
    EXPECT_THAT(last_errors_, 2);
    EXPECT_THAT(
        RunArgvRecords({"--compare=summary", root_.string(), Path("sub"), std::string(flag), "-print"}), IsEmpty());
    EXPECT_THAT(last_errors_, 2);
    EXPECT_THAT(
        RunArgvRecords({root_.string(), "-type", "f", "--summary=ext", "--summary=none", std::string(flag)}),
        Not(IsEmpty()));
    EXPECT_THAT(last_errors_, 0);
  }
}

TEST_F(RunTest, AlignedSummaryMatchesPlainInBothModes) {
  const std::vector<std::string> ordinary = {root_.string(), "-type", "f", "--summary=ext"};
  const std::vector<std::string> comparison = {"--compare=summary", root_.string(), Path("sub"), "-type", "f",
                                               "--summary=ext"};
  const auto commands = std::to_array<std::vector<std::string>>({ordinary, comparison});
  for (auto args : commands) {
    const auto plain = RunArgvRecords(args);
    args.emplace_back("--format=aligned");
    EXPECT_THAT(RunArgvRecords(args), Eq(plain));
    EXPECT_THAT(last_errors_, 0);
    args.emplace_back("--columns=path");
    EXPECT_THAT(RunArgvRecords(args), IsEmpty());
    EXPECT_THAT(last_errors_, 2);
  }
}

TEST_F(RunTest, SummaryScopesRenderPlainLabels) {
  EXPECT_THAT(
      RunArgvRecords({"--summary=ext", "--summary-scope=root", root_.string(), "-type", "f"}),
      Contains(HasSubstr("Summary scope: root (" + root_.string() + ")")));
  EXPECT_THAT(
      RunArgvRecords({"--summary", "--summary-scope=all", root_.string(), "-type", "f"}),
      Contains("Summary scope: all"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, SummaryTopBreaksEqualSizeTiesByCountThenName) {
  EXPECT_THAT(fs_.WriteContent(Path("a.txt"), ""), IsOk());
  EXPECT_THAT(fs_.WriteContent(Path("b.md"), ""), IsOk());
  EXPECT_THAT(fs_.WriteContent(Path("sub/c.txt"), ""), IsOk());
  EXPECT_THAT(fs_.WriteContent(Path("d.h"), ""), IsOk());
  EXPECT_THAT(
      RunArgvRecords({"--summary=ext", "--top=2", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(
          R"({"record":"summary","request":0,"summary":"ext","scope":"all","root":"","group":"txt","count":2,"count_percent":50.00,"bytes":0,"size_percent":0.00,"is_total":false})",
          R"({"record":"summary","request":0,"summary":"ext","scope":"all","root":"","group":"h","count":1,"count_percent":25.00,"bytes":0,"size_percent":0.00,"is_total":false})",
          R"({"record":"summary","request":0,"summary":"ext","scope":"all","root":"","group":"total","count":4,"count_percent":100.00,"bytes":0,"size_percent":0.00,"is_total":true})"));
}

TEST_F(RunTest, ComparisonScopesSelectOrderedColumnGroups) {
  ASSERT_THAT(fs_.WriteContent(Path("sub/a.txt"), "aa"), IsOk());
  ASSERT_THAT(fs_.WriteContent(Path("sub/b.md"), "b"), IsOk());
  const auto records = RunArgvRecords(
      {"--compare=summary", root_.string(), Path("sub"), "-type", "f", "--summary=overall",
       "--summary-scope=diff,identical,left-total,right-total,different", "--format=jsonl"});
  EXPECT_THAT(
      records, Contains(AllOf(
                   HasSubstr(R"("scope":"left-only,right-only,different,identical,left-total,right-total")"),
                   HasSubstr(R"("left-only":{"count":3,"count_percent":100.00,"bytes":4)"),
                   HasSubstr(R"("right-only":{"count":1,"count_percent":100.00,"bytes":1)"),
                   HasSubstr(R"("different":{"count":1,"count_percent":100.00,"bytes":3)"),
                   HasSubstr(R"("identical":{"count":1,"count_percent":100.00,"bytes":2)"),
                   HasSubstr(R"("left-total":{"count":5,"count_percent":100.00,"bytes":6)"),
                   HasSubstr(R"("right-total":{"count":3,"count_percent":100.00,"bytes":4)"))));
}

TEST_F(RunTest, ComparisonCategoryTypeTransitionsCountPairsOnce) {
  ASSERT_THAT(fs_.WriteContent(Path("sub/sub"), "file"), IsOk());
  const auto records = RunArgvRecords(
      {"--compare=summary", root_.string(), Path("sub"), "--summary=type", "--summary-scope=different",
       "--format=jsonl"});
  EXPECT_THAT(
      records, Contains(AllOf(HasSubstr(R"("group":"directory -> file")"), HasSubstr(R"("different":{"count":1,)"))));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ComparisonSummaryRejectsEmptyScopeList) {
  EXPECT_THAT(
      RunArgvRecords({"--compare=summary", root_.string(), Path("sub"), "--summary=overall", "--summary-scope="}),
      IsEmpty());
  EXPECT_THAT(last_errors_, 2);
}

TEST_F(RunTest, ComparisonSummaryAccountsForBothSidesAndMissingEntries) {
  ASSERT_THAT(fs_.WriteContent(Path("sub/a.txt"), "aa"), IsOk());
  ASSERT_THAT(fs_.WriteContent(Path("sub/b.md"), "b"), IsOk());
  const auto records = RunArgvRecords(
      {"--compare=summary", root_.string(), Path("sub"), "-type", "f", "--summary=overall",
       "--summary-scope=all,compare", "--format=jsonl"});
  const std::string comparison_prefix = absl::StrCat(
      R"({"record":"summary","request":0,"summary":"compare","scope":"compare","left_root":)",
      nlohmann::json(root_.string()).dump(), R"(,"right_root":)", nlohmann::json(Path("sub")).dump(), ",");
  EXPECT_THAT(
      records,
      Contains(
          absl::StrCat(
              comparison_prefix,
              R"("type":"file","group":"different","count":1,"count_percent":16.67,"bytes":3,"size_percent":30.00,"is_total":false})")));
  EXPECT_THAT(
      records,
      Contains(
          absl::StrCat(
              comparison_prefix,
              R"("type":"file","group":"identical","count":1,"count_percent":16.67,"bytes":2,"size_percent":20.00,"is_total":false})")));
  EXPECT_THAT(
      records,
      Contains(
          absl::StrCat(
              comparison_prefix,
              R"("type":"all","group":"total","count":6,"count_percent":100.00,"bytes":10,"size_percent":100.00,"is_total":true})")));
  EXPECT_THAT(
      records,
      Contains(
          R"({"record":"summary","request":1,"summary":"overall","scope":"all","root":"","group":"total","count":8,"count_percent":100.00,"bytes":10,"size_percent":100.00,"is_total":true})"));
  EXPECT_THAT(
      records, Contains(AllOf(
                   HasSubstr(R"("scope":"left-total,right-total")"),
                   HasSubstr(R"("count":5,"count_percent":100.00,"bytes":6,"size_percent":100.00)"),
                   HasSubstr(R"("count":3,"count_percent":100.00,"bytes":4,"size_percent":100.00)"))));
  const auto one_sided = RunArgvRecords(
      {"--compare=summary", root_.string(), Path("sub"), "-type", "f", "--summary=ext",
       "--summary-scope=left-only,right-only", "--format=jsonl"});
  EXPECT_THAT(one_sided, Contains(AllOf(HasSubstr(R"("group":"md")"), HasSubstr(R"("right-only":null)"))));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ComparisonSummaryIncludesEmptyDirectoriesAndTypeTransitions) {
  // The existing subdirectory has no directory children, while the left root contains sub.
  auto records = RunArgvRecords({"--compare=summary", root_.string(), Path("sub"), "-type", "d", "--format=jsonl"});
  EXPECT_THAT(records, Contains(HasSubstr(R"("type":"directory","group":"left-only","count":1)")));
  EXPECT_THAT(records, Contains(HasSubstr(R"("type":"directory","group":"identical","count":1)")));
  EXPECT_THAT(records, Contains(HasSubstr(R"("type":"all","group":"total","count":2)")));
  ASSERT_THAT(fs_.WriteContent(Path("sub/sub"), "file"), IsOk());
  records = RunArgvRecords({"--compare=summary", root_.string(), Path("sub"), "--format=jsonl"});
  EXPECT_THAT(records, Contains(HasSubstr(R"("type":"directory -> file","group":"different","count":1)")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, PairedSummaryDistinguishesZeroBytesAndUsesSelectedCategoryDenominators) {
  ASSERT_THAT(fs_.WriteContent(Path("a.txt"), ""), IsOk());
  ASSERT_THAT(fs_.WriteContent(Path("sub/a.txt"), ""), IsOk());
  const auto records = RunArgvRecords(
      {"--compare=summary", root_.string(), Path("sub"), "-type", "f", "--summary=ext", "--summary-scope=identical",
       "--top=1", "--format=jsonl"});
  EXPECT_THAT(
      records,
      Contains(AllOf(
          HasSubstr(R"("scope":"identical")"), HasSubstr(R"("group":"txt")"),
          HasSubstr(R"("count":1,"count_percent":100.00,"bytes":0,"size_percent":0.00)"), Not(HasSubstr("null")))));
  const auto plain = RunArgvRecords(
      {"--compare=summary", root_.string(), Path("sub"), "-type", "f", "--summary=ext", "--summary-scope=compare",
       "--human=off"});
  EXPECT_THAT(plain, Contains(HasSubstr("left-total % count")).Times(1));
  EXPECT_THAT(plain, Contains(HasSubstr("-")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ComparisonSummaryDefaultsToPairedScopeAndExplicitScopeOverridesIt) {
  const auto implicit = RunArgvRecords(
      {"--compare=summary", root_.string(), Path("sub"), "-type", "f", "--summary=ext", "--format=jsonl"});
  EXPECT_THAT(implicit, Contains(HasSubstr(R"("scope":"left-total,right-total")")));
  EXPECT_THAT(
      RunArgvRecords(
          {"--summary-scope=compare", "--compare=summary", root_.string(), Path("sub"), "-type", "f", "--summary=ext",
           "--format=jsonl"}),
      Eq(implicit));
  const auto combined = RunArgvRecords(
      {"--summary-scope=all", "--compare=summary", root_.string(), Path("sub"), "-type", "f", "--summary=ext",
       "--format=jsonl"});
  EXPECT_THAT(combined, Contains(HasSubstr(R"("scope":"all","root":"","group":"total","count":4)")));
  EXPECT_THAT(
      RunArgvRecords(
          {"--compare=summary", root_.string(), Path("sub"), "-type", "f", "--summary=ext", "--summary-scope=all",
           "--format=jsonl"}),
      Eq(combined));
  EXPECT_THAT(
      RunArgvRecords({"--compare=summary", root_.string(), Path("sub"), "--format=jsonl"}),
      Each(HasSubstr(R"("summary":"compare","scope":"compare")")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, MarkdownSummariesRenderOrdinaryAndPairedTables) {
  const auto ordinary = RunArgvRecords({root_.string(), "-type", "f", "--summary=ext", "--format=md"});
  EXPECT_THAT(ordinary, Contains(HasSubstr("| Group")).Times(1));
  EXPECT_THAT(ordinary, Contains(HasSubstr("| Count")));
  EXPECT_THAT(ordinary, Contains(HasSubstr("| ----: | ------: |")));
  EXPECT_THAT(ordinary, Contains(HasSubstr("| txt")));
  EXPECT_THAT(RunArgvRecords({root_.string(), "-type", "f", "--summary=ext", "--format=markdown"}), Eq(ordinary));
  const auto comparison =
      RunArgvRecords({"--compare=summary", root_.string(), Path("sub"), "-type", "f", "--summary=ext", "--format=md"});
  EXPECT_THAT(comparison, Contains("\n## Comparison summary"));
  EXPECT_THAT(comparison, Contains("\n## Summary by extension"));
  EXPECT_THAT(comparison, Contains(HasSubstr("| Type")).Times(1));
  EXPECT_THAT(comparison, Contains(HasSubstr("| left-total count")).Times(1));
  EXPECT_THAT(comparison, Contains(HasSubstr("| right-total count")));
  EXPECT_THAT(comparison, Contains(HasSubstr("| ---------------: |")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, MarkdownSummaryHeadingsDescribeEachGrouping) {
  constexpr std::string_view kSha256A = "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb";
  const std::vector<std::pair<std::string, std::string>> cases = {
      {"overall", "Summary"},
      {"type", "Summary by file type"},
      {"ext", "Summary by extension"},
      {"lang", "Summary by language"},
      {"mime", "Summary by MIME type"},
      {"user", "Summary by owner"},
      {"group", "Summary by group"},
      {"hash", "Summary by hash"},
      {"hash-verification", "Hash verification summary"},
      {"{ext}", "Summary by template"},
  };
  for (const auto& [mode, title] : cases) {
    SCOPED_TRACE(mode);
    EXPECT_THAT(
        RunArgvRecords(
            {root_.string(), "-name", "a.txt", "-hasheq", std::string(kSha256A), "--summary=" + mode, "--format=md"}),
        Contains("\n## " + title));
    EXPECT_THAT(last_errors_, 0);
  }
}

TEST_F(RunTest, MarkdownScopedSummariesRepeatHeadingsAndKeepScopeWithEachTable) {
  const auto records = RunArgvRecords(
      {root_.string(), "-type", "f", "--summary=ext", "--summary=type", "--summary-scope=root", "--format=md"});
  EXPECT_THAT(records, Contains("\n## Summary by extension").Times(1));
  EXPECT_THAT(records, Contains("\n## Summary by file type").Times(1));
  EXPECT_THAT(records, Contains(HasSubstr("- Scope: root (")).Times(2));
  const auto fragment = RunArgvRecords(
      {"--compare=summary", root_.string(), Path("sub"), "-type", "f", "--summary=ext", "--no-header", "--format=md"});
  EXPECT_THAT(fragment, Not(Contains(HasSubstr("## "))));
  EXPECT_THAT(fragment, Contains(HasSubstr("- Scope:")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, SummarySizesAlignExactBytesWithScaledIntegerDigits) {
  ASSERT_THAT(fs_.WriteContent(Path("size-zero.a"), ""), IsOk());
  ASSERT_THAT(fs_.WriteContent(Path("size-scaled.b"), std::string(44'450, 'x')), IsOk());
  const auto records =
      RunArgvRecords({root_.string(), "-name", "size-*", "--summary=ext", "--human=si", "--summary-precision=2"});
  ASSERT_THAT(records, SizeIs(1));
  EXPECT_THAT(records.at(0) + "\n", WithDropIndent(EqualsText(R"out(
      Group  Count  % count      Size   % size
      a          1   50.00%   0     B    0.00%
      b          1   50.00%  44.45 kB  100.00%
      total      2  100.00%  44.45 kB  100.00%
      )out")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, MarkdownSummaryWorksWithCollectionsAndHonorsHeaderControl) {
  const auto records =
      RunArgvRecords({root_.string(), "-type", "f", "-collect", "--summary=ext", "--format=md", "--no-header"});
  EXPECT_THAT(records, Contains(HasSubstr("| txt")));
  EXPECT_THAT(records, Not(Contains(HasSubstr("| Group"))));
  EXPECT_THAT(records, Not(Contains(HasSubstr("| ---"))));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, MarkdownSummaryRejectsListingColumnsAndPreservesActionValidation) {
  EXPECT_THAT(RunArgvRecords({root_.string(), "--summary=ext", "--format=md", "--columns=path"}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
  EXPECT_THAT(RunArgvRecords({root_.string(), "--format=md", "-printf", "SHOULD_NOT_RUN"}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
  EXPECT_THAT(
      RunArgvRecords({"--compare=summary", root_.string(), Path("sub"), "--summary=none", "--format=md"}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
}

TEST_F(RunTest, SummaryRetainsZeroByteDimensionForEmptyFiles) {
  // Zero-byte files have a size dimension; distinguish them from absent entries.
  { std::ofstream(root_ / "e1.log"); }  // 0 bytes
  { std::ofstream(root_ / "e2.log"); }
  EXPECT_THAT(
      RunArgvRecords({"--summary=ext", "--format=jsonl", root_.string(), "-name", "*.log"}),
      ElementsAre(
          R"({"record":"summary","request":0,"summary":"ext","scope":"all","root":"","group":"log","count":2,"count_percent":100.00,"bytes":0,"size_percent":0.00,"is_total":false})",
          R"({"record":"summary","request":0,"summary":"ext","scope":"all","root":"","group":"total","count":2,"count_percent":100.00,"bytes":0,"size_percent":0.00,"is_total":true})"));
  // Unstyled output retains a numeric size column without a human-readable unit.
  EXPECT_THAT(RunArgvRecords({"--summary=ext", root_.string(), "-name", "*.log"}), Not(Contains(HasSubstr(" B"))));
}

TEST_F(RunTest, HistogramRejectsUnsupportedFormatsBeforeActions) {
  const auto formats = std::to_array<std::string_view>({"csv", "tsv", "nul", "tree"});
  for (const std::string_view format : formats) {
    const std::string flag = absl::StrCat("--format=", format);
    SCOPED_TRACE(format);
    EXPECT_THAT(RunArgvRecords({root_.string(), "--histogram=ext", flag, "-print"}), IsEmpty());
    EXPECT_THAT(last_errors_, 2);
    EXPECT_THAT(
        RunArgvRecords({"--compare", root_.string(), Path("sub"), "--histogram=ext", flag, "-print"}), IsEmpty());
    EXPECT_THAT(last_errors_, 2);
  }
}

TEST_F(RunTest, MarkdownHistogramRendersAlignedBucketValueTable) {
  const auto records = RunArgvRecords({root_.string(), "-type", "f", "--histogram=ext", "--format=md"});
  ASSERT_THAT(records, SizeIs(2));
  EXPECT_THAT(records.at(0), Eq("\n## Histogram ext"));
  EXPECT_THAT(records.at(1), StartsWith("\n|"));
  EXPECT_THAT(records.at(1).substr(1), WithDropIndent(EqualsText(R"out(
      | bucket | value |
      | ------ | ----: |
      | txt    |     2 |
      | md     |     1 |
      )out")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, MarkdownHistogramComposesWithSummariesAndRepeatedHistograms) {
  const auto records = RunArgvRecords(
      {root_.string(), "-type", "f", "--summary=type", "--histogram=ext", "--histogram=type", "--format=md"});
  EXPECT_THAT(records, Contains("\n## Summary by file type"));
  EXPECT_THAT(records, Contains("\n## Histogram ext"));
  EXPECT_THAT(records, Contains("\n## Histogram type"));
  EXPECT_THAT(records, Contains(HasSubstr("| txt    |     2 |")));
  EXPECT_THAT(records, Contains(HasSubstr("| file   |     3 |")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, MarkdownHistogramHonorsTopAndHeaderControlWithCollections) {
  const auto records = RunArgvRecords(
      {root_.string(), "-type", "f", "-collect", "--histogram=ext", "--top=1", "--no-header", "--format=md"});
  EXPECT_THAT(records, Contains(HasSubstr("| txt")));
  EXPECT_THAT(records, Not(Contains(HasSubstr("| md"))));
  EXPECT_THAT(records, Not(Contains(HasSubstr("## Histogram"))));
  EXPECT_THAT(records, Not(Contains(HasSubstr("| bucket"))));
  EXPECT_THAT(last_errors_, 0);
  EXPECT_THAT(RunArgvRecords({root_.string(), "--histogram=ext", "--format=md", "--columns=path"}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
}

TEST_F(RunTest, MarkdownHistogramEmptyPopulationStillHasSchema) {
  const auto records = RunArgvRecords({root_.string(), "-name", "not-present", "--histogram=ext", "--format=md"});
  ASSERT_THAT(records, SizeIs(2));
  EXPECT_THAT(records.at(1).substr(1), WithDropIndent(EqualsText(R"out(
      | bucket | value |
      | ------ | ----: |
      )out")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, HistogramByExtensionCountsPerBucketSortedByCount) {
  // --histogram=ext: txt (a.txt, sub/c.txt) has 2, md (b.md) has 1; bars sort by count
  // descending, so txt leads. No total row (a histogram is just bars). The jsonl rows are
  // block-tagged and the per-match listing is suppressed (only histogram rows appear).
  EXPECT_THAT(
      RunArgvRecords({"--histogram=ext", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(R"({"histogram":"ext","bucket":"txt","value":2})", R"({"histogram":"ext","bucket":"md","value":1})"));
}

TEST_F(RunTest, HistogramByTypeCountsMatches) {
  EXPECT_THAT(
      RunArgvRecords({"--histogram=type", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(R"({"histogram":"type","bucket":"file","value":3})"));
}

TEST_F(RunTest, HistogramCombinesWithSummaryEmittingBothBlocks) {
  // --summary and --histogram are independent, combinable reductions fed by one walk: the
  // summary rows come first, then the histogram bars, all block-tagged in --format=jsonl.
  EXPECT_THAT(
      RunArgvRecords({"--summary=type", "--histogram=ext", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(
          R"({"record":"summary","request":0,"summary":"type","scope":"all","root":"","group":"file","count":3,"count_percent":100.00,"bytes":3,"size_percent":100.00,"is_total":false})",
          R"({"record":"summary","request":0,"summary":"type","scope":"all","root":"","group":"total","count":3,"count_percent":100.00,"bytes":3,"size_percent":100.00,"is_total":true})",
          R"({"histogram":"ext","bucket":"txt","value":2})", R"({"histogram":"ext","bucket":"md","value":1})"));
}

TEST_F(RunTest, HistogramTopKeepsTheTallestBuckets) {
  // --top=1: keep only the tallest bucket (txt, 2); a histogram has no total row.
  EXPECT_THAT(
      RunArgvRecords({"--histogram=ext", "--top=1", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(R"({"histogram":"ext","bucket":"txt","value":2})"));
}

TEST_F(RunTest, RepeatedHistogramEmitsEachBlockInOrder) {
  // Repeatable: two --histogram flags -> two blocks, in the order given.
  EXPECT_THAT(
      RunArgvRecords({"--histogram=type", "--histogram=ext", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(
          R"({"histogram":"type","bucket":"file","value":3})", R"({"histogram":"ext","bucket":"txt","value":2})",
          R"({"histogram":"ext","bucket":"md","value":1})"));
}

TEST_F(RunTest, HistogramSumOfSizePerBucket) {
  // ext:sum(size): txt has two 1-byte files (2), md has one (1); sorted by value descending.
  EXPECT_THAT(
      RunArgvRecords({"--histogram=ext:sum(size)", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(
          R"j({"histogram":"ext:sum(size)","bucket":"txt","value":2})j",
          R"j({"histogram":"ext:sum(size)","bucket":"md","value":1})j"));
}

TEST_F(RunTest, HistogramMeanRendersFixedDecimals) {
  // type:mean(size): three 1-byte files -> mean 1.00 (default 2 decimals), a single bucket.
  EXPECT_THAT(
      RunArgvRecords({"--histogram=type:mean(size)", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(R"j({"histogram":"type:mean(size)","bucket":"file","value":1.00})j"));
}

TEST_F(RunTest, HistogramMaxOfSizeSortsTiesByKey) {
  // ext:max(size): every file is 1 byte, so both buckets max at 1; equal values sort by key.
  EXPECT_THAT(
      RunArgvRecords({"--histogram=ext:max(size)", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(
          R"j({"histogram":"ext:max(size)","bucket":"md","value":1})j",
          R"j({"histogram":"ext:max(size)","bucket":"txt","value":1})j"));
}

TEST_F(RunTest, HistogramMinOfSizeSelectsTheSmallestValuePerBucket) {
  { std::ofstream(root_ / "large.txt") << "12345"; }
  EXPECT_THAT(
      RunArgvRecords({"--histogram=ext:min(size)", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(
          R"j({"histogram":"ext:min(size)","bucket":"md","value":1})j",
          R"j({"histogram":"ext:min(size)","bucket":"txt","value":1})j"));
}

TEST_F(RunTest, HistogramLineRangesIgnoreDirectoriesAndBucketRegularFileLineCounts) {
  { std::ofstream(root_ / "a.txt") << "one\ntwo\nthree\n"; }
  { std::ofstream(root_ / "b.md") << "one\n"; }
  EXPECT_THAT(
      RunArgvRecords({"--histogram=lines", "--format=jsonl", root_.string()}),
      ElementsAre(R"({"histogram":"lines","bucket":"1-9","value":3})"));
}

TEST_F(RunTest, MissingPackOptionFileFailsBeforeTraversal) {
  EXPECT_THAT(
      RunArgvRecords({"--pack-option=@" + Path("absent-pack-options.json"), root_.string(), "-print"}), IsEmpty());
  EXPECT_THAT(last_errors_, Eq(2));
}

TEST_F(RunTest, HistogramBadMeasureIsAUsageError) {
  // A numeric metric with no aggregator, an unknown aggregator, and an unknown field each fail (2).
  RunArgvRecords({"--histogram=ext:lines", root_.string(), "-type", "f"});
  EXPECT_THAT(last_errors_, 2);
  RunArgvRecords({"--histogram=ext:avg(size)", root_.string(), "-type", "f"});
  EXPECT_THAT(last_errors_, 2);
  RunArgvRecords({"--histogram=ext:sum(bogus)", root_.string(), "-type", "f"});
  EXPECT_THAT(last_errors_, 2);
}

TEST_F(RunTest, HistogramDepthRangeIsPerLevelInAscendingOrder) {
  // A numeric-range bucket draws in ascending range order (a distribution), not by height. depth:
  // root is 0 (1), a.txt/b.md/sub are 1 (3), sub/c.txt is 2 (1).
  EXPECT_THAT(
      RunArgvRecords({"--histogram=depth", "--format=jsonl", root_.string()}),
      ElementsAre(
          R"({"histogram":"depth","bucket":"0","value":1})", R"({"histogram":"depth","bucket":"1","value":3})",
          R"({"histogram":"depth","bucket":"2","value":1})"));
}

TEST_F(RunTest, HistogramSizeRangeGroupsByMagnitude) {
  // A size-range bucket groups by order of magnitude: the three 1-byte files all land in "1-9".
  EXPECT_THAT(
      RunArgvRecords({"--histogram=size", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(R"({"histogram":"size","bucket":"1-9","value":3})"));
}

TEST_F(RunTest, SummaryByMimeGroupsByMediaType) {
  // --summary=mime reuses the {mime} field: a.txt / sub/c.txt are text/plain, b.md is text/markdown.
  // Rows sort by key (text/markdown before text/plain), then the total.
  EXPECT_THAT(
      RunArgvRecords({"--summary=mime", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(
          R"({"record":"summary","request":0,"summary":"mime","scope":"all","root":"","group":"text/markdown","count":1,"count_percent":33.33,"bytes":1,"size_percent":33.33,"is_total":false})",
          R"({"record":"summary","request":0,"summary":"mime","scope":"all","root":"","group":"text/plain","count":2,"count_percent":66.67,"bytes":2,"size_percent":66.67,"is_total":false})",
          R"({"record":"summary","request":0,"summary":"mime","scope":"all","root":"","group":"total","count":3,"count_percent":100.00,"bytes":3,"size_percent":100.00,"is_total":true})"));
}

TEST_F(RunTest, HistogramByMimeCountsPerMediaType) {
  // --histogram=mime: text/plain (a.txt, sub/c.txt) has 2, text/markdown (b.md) has 1; bars by count.
  EXPECT_THAT(
      RunArgvRecords({"--histogram=mime", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(
          R"({"histogram":"mime","bucket":"text/plain","value":2})",
          R"({"histogram":"mime","bucket":"text/markdown","value":1})"));
}

TEST_F(RunTest, HistogramByUserGroupsUnderTheOwner) {
  // --histogram=user reuses the {user} field. Every fixture file has the same owner (the test
  // process), so there is one bucket of 3; the owner name is runtime-dependent, so assert the
  // shape (one bucket, value 3), not the name.
  EXPECT_THAT(
      RunArgvRecords({"--histogram=user", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(MatchesRegex(R"(\{"histogram":"user","bucket":".+","value":3\})")));
}

TEST_F(RunTest, SummaryOwnerIsAnAliasOfUser) {
  // --summary=owner is the =user alias: one owner bucket of 3 (name runtime-dependent), then no
  // separate total row is emitted for a single group beyond it -- match the owner row's shape.
  EXPECT_THAT(
      RunArgvRecords({"--summary=owner", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(
          MatchesRegex(
              R"(\{"record":"summary","request":0,"summary":"user","scope":"all","root":"","group":".+","count":3,"count_percent":100.00,"bytes":3,"size_percent":100.00,"is_total":false\})"),
          R"({"record":"summary","request":0,"summary":"user","scope":"all","root":"","group":"total","count":3,"count_percent":100.00,"bytes":3,"size_percent":100.00,"is_total":true})"));
}

TEST_F(RunTest, HistogramByGroupGroupsUnderTheOwningGroup) {
  // --histogram=group reuses the {group} field; one owning-group bucket of 3 (name runtime-dependent).
  EXPECT_THAT(
      RunArgvRecords({"--histogram=group", "--format=jsonl", root_.string(), "-type", "f"}),
      ElementsAre(MatchesRegex(R"(\{"histogram":"group","bucket":".+","value":3\})")));
}

TEST_F(RunTest, LsEmitsOneLinePerMatchAndSuppressesImplicitPrint) {
  // -ls is an action, so it suppresses the implicit -print: exactly one line (the
  // ls-style listing) for the match, containing its path. The exact columns are
  // umask/fs-dependent (covered deterministically in evaluate_test); here we just
  // confirm the end-to-end wiring and the print suppression.
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-ls"}), ElementsAre(HasSubstr(Path("a.txt"))));
}

// Minimal in-memory FileSystem: a root directory holding one regular file whose
// metadata carries NO birth time. The real local FS records btime on macOS/Linux,
// so it cannot reproduce the "-Btime where birth time is unrecorded" impossible
// task; this can.
class NoBtimeFs : public vfs::FileSystem {
 public:
  explicit NoBtimeFs(std::string root) : root_(std::move(root)) {}

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view dir) const override {
    if (std::string(dir) != root_) {
      return absl::NotFoundError("NoBtimeFs: no such directory");
    }
    return std::vector<vfs::Entry>{
        vfs::Entry{.path = root_ + "/f.txt", .name = "f.txt", .type = vfs::FileType::kRegular}};
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool /*follow_symlinks*/) const override {
    vfs::Metadata md;
    md.type = std::string(path) == root_ ? vfs::FileType::kDirectory : vfs::FileType::kRegular;
    return md;  // btime deliberately left empty (unrecorded)
  }

  absl::Status Remove(std::string_view) const override { return absl::OkStatus(); }

  bool Access(std::string_view, vfs::AccessMode) const override { return true; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override {
    return absl::InvalidArgumentError("NoBtimeFs: not a symlink");
  }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return std::string("fakefs"); }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

  absl::StatusOr<std::string> ReadContent(std::string_view) const override { return std::string(); }

 private:
  std::string root_;
};

TEST_F(RunTest, DirectoryPolicyCannotRedirectAnInMemoryFilesystemToTheHost) {
  const NoBtimeFs fs("/fake");
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"/fake", "--output-root=/does-not-exist", "-delete"}));
  std::vector<absl::Status> diagnostics;
  const auto result = RunFind(
      command, fs, [](std::string_view) {},
      [&](std::string_view, absl::Status status) { diagnostics.push_back(std::move(status)); });
  EXPECT_THAT(result.errors, Eq(2));
  EXPECT_THAT(
      diagnostics,
      ElementsAre(
          StatusIs(absl::StatusCode::kPermissionDenied, HasSubstr("filesystem does not support directory policies"))));
}

TEST_F(RunTest, BtimeOnEntryWithoutBirthtimeFailsByDefault) {
  // Impossible task: -Btime against a filesystem that does not record birth time is
  // a hard error (exit 2), reported once with a self-documenting message.
  const NoBtimeFs fs("/fake");
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"/fake", "-Btime", "1"}));
  int reports = 0;
  std::string message;
  const auto [errors, any_match] = RunFind(
      command, fs, [](std::string_view) {},
      [&](std::string_view, absl::Status status) {
        ++reports;
        message = std::string(status.message());
      });
  EXPECT_THAT(errors, Not(0));  // hard error
  EXPECT_THAT(reports, 1);      // reported once, not per entry
  EXPECT_THAT(message, HasSubstr("birth time"));
}

TEST_F(RunTest, SkipUnsupportedDowngradesImpossibleBtimeToWarnAndSkip) {
  // --skip-unsupported turns the same impossible task into a warning + skip: the
  // run still reports once (so the user knows), but it is not an error (exit 0).
  const NoBtimeFs fs("/fake");
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"--skip-unsupported", "/fake", "-Btime", "1"}));
  int reports = 0;
  const auto [errors, any_match] =
      RunFind(command, fs, [](std::string_view) {}, [&](std::string_view, absl::Status) { ++reports; });
  EXPECT_THAT(errors, 0);   // skipped -> not an error
  EXPECT_THAT(reports, 1);  // but warned once
}

// In-memory FileSystem on a case-FOLDING volume (IsCaseSensitive -> false): a root
// holding one mixed-case regular file (Foo.txt). It reports case-insensitive
// regardless of the host runner, so the FS-native -name matching / --exact tests
// below are deterministic on case-sensitive CI (ext4) too.
class CaseFoldFs : public vfs::FileSystem {
 public:
  explicit CaseFoldFs(std::string root) : root_(std::move(root)) {}

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view dir) const override {
    if (std::string(dir) != root_) {
      return absl::NotFoundError("CaseFoldFs: no such directory");
    }
    return std::vector<vfs::Entry>{
        vfs::Entry{.path = root_ + "/Foo.txt", .name = "Foo.txt", .type = vfs::FileType::kRegular}};
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool /*follow_symlinks*/) const override {
    vfs::Metadata md;
    md.type = std::string(path) == root_ ? vfs::FileType::kDirectory : vfs::FileType::kRegular;
    return md;
  }

  absl::Status Remove(std::string_view) const override { return absl::OkStatus(); }

  bool Access(std::string_view, vfs::AccessMode) const override { return true; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override {
    return absl::InvalidArgumentError("CaseFoldFs: not a symlink");
  }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return std::string("fakefs"); }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return false; }  // a folding volume

  absl::StatusOr<std::string> ReadContent(std::string_view) const override { return std::string(); }

 private:
  std::string root_;
};

// Runs `-name <pattern>` over a CaseFoldFs (holding Foo.txt) in `style`, with or
// without --exact, and returns the concatenated emitted output.
std::string RunNameOnCaseFoldVolume(std::string_view pattern, std::optional<registry::Style> style, bool exact) {
  const CaseFoldFs fs("/fake");
  std::vector<std::string> args;
  if (exact) {
    args.emplace_back("--exact");
  }
  args.insert(args.end(), {"/fake", "-name", std::string(pattern)});
  const auto command = parser::Parse(args);
  EXPECT_THAT(command, IsOk());
  std::string out;
  const auto [errors, any_match] = RunFind(
      *command, fs, [&out](std::string_view record) { out += record; }, [](std::string_view, absl::Status) {}, style);
  EXPECT_THAT(errors, 0);
  return out;
}

TEST_F(RunTest, XffStyleFoldsNameOnCaseFoldingVolume) {
  // FS-native matching: the xff style matches -name the way the volume resolves
  // names, so a lower-case pattern matches the mixed-case Foo.txt on a folding FS.
  EXPECT_THAT(RunNameOnCaseFoldVolume("foo.txt", registry::Style::kXff, /*exact=*/false), HasSubstr("Foo.txt"));
}

TEST_F(RunTest, ExactOptsOutOfFsNativeFolding) {
  // --exact forces verbatim byte-exact matching even on a folding volume.
  EXPECT_THAT(RunNameOnCaseFoldVolume("foo.txt", registry::Style::kXff, /*exact=*/true), IsEmpty());
}

TEST_F(RunTest, FindStyleIsAlwaysByteExact) {
  // The find style is drop-in faithful: no FS-native folding, so a lower-case
  // pattern does not match Foo.txt regardless of the volume.
  EXPECT_THAT(RunNameOnCaseFoldVolume("foo.txt", registry::Style::kFind, /*exact=*/false), IsEmpty());
}

TEST_F(RunTest, InProcessDefaultStyleIsByteExact) {
  // std::nullopt style (the conservative in-process default) does not fold either;
  // FS-native matching is opt-in via the xff style the CLI resolves.
  EXPECT_THAT(RunNameOnCaseFoldVolume("foo.txt", std::nullopt, /*exact=*/false), IsEmpty());
}

TEST_F(RunTest, ExactCaseNameMatchesRegardlessOfFolding) {
  // The exact-case name matches in every style / with --exact -- folding only
  // widens what matches, it never stops the verbatim name from matching.
  EXPECT_THAT(RunNameOnCaseFoldVolume("Foo.txt", registry::Style::kXff, /*exact=*/false), HasSubstr("Foo.txt"));
  EXPECT_THAT(RunNameOnCaseFoldVolume("Foo.txt", registry::Style::kXff, /*exact=*/true), HasSubstr("Foo.txt"));
  EXPECT_THAT(RunNameOnCaseFoldVolume("Foo.txt", registry::Style::kFind, /*exact=*/false), HasSubstr("Foo.txt"));
}

TEST_F(RunTest, GrepEmitsPathLineTextAcrossTheWalk) {
  // -grep is an action, so it suppresses the implicit path-print and emits one
  // record per matching line, path:line:text, over the whole traversal.
  { std::ofstream(root_ / "a.txt") << "alpha\nTODO one\nbeta\nTODO two\n"; }  // overwrite the fixture's "a"
  EXPECT_THAT(
      RunExpr({"-name", "a.txt", "-grep", "TODO"}),
      ElementsAre(Path("a.txt") + ":2:TODO one", Path("a.txt") + ":4:TODO two"));
}

TEST_F(RunTest, GrepContextPrintsSurroundingLinesWithGroupSeparator) {
  { std::ofstream(root_ / "a.txt") << "1\nHIT\n3\n4\n5\nHIT\n7\n"; }
  // --context=1 (leading global): 1 line before/after each match; a match line uses ':', a context
  // line '-', and the two non-adjacent windows are divided by a "--" line -- like grep -C1.
  EXPECT_THAT(
      RunArgvRecords({"--context=1", root_.string(), "-name", "a.txt", "-grep", "HIT"}),
      ElementsAre(
          Path("a.txt") + "-1-1", Path("a.txt") + ":2:HIT", Path("a.txt") + "-3-3", "--", Path("a.txt") + "-5-5",
          Path("a.txt") + ":6:HIT", Path("a.txt") + "-7-7"));
}

TEST_F(RunTest, GrepAfterContextIsAsymmetric) {
  { std::ofstream(root_ / "a.txt") << "x\nHIT\ny\nz\n"; }
  // --after-context=1 (grep -A1): the match and one trailing line, no leading context.
  EXPECT_THAT(
      RunArgvRecords({"--after-context=1", root_.string(), "-name", "a.txt", "-grep", "HIT"}),
      ElementsAre(Path("a.txt") + ":2:HIT", Path("a.txt") + "-3-y"));
}

TEST_F(RunTest, GrepContextAcceptsEverySideSpellingAndLastValueWins) {
  { std::ofstream(root_ / "a.txt") << "zero\nHIT\ntwo\nthree\n"; }
  EXPECT_THAT(
      RunArgvRecords({"--context=A:3,a:1,B:3,b:0,C:2,c:1", root_.string(), "-name", "a.txt", "-grep", "HIT"}),
      ElementsAre(Path("a.txt") + "-1-zero", Path("a.txt") + ":2:HIT", Path("a.txt") + "-3-two"));
}

TEST_F(RunTest, GrepContextRejectsEveryMalformedValueClass) {
  static constexpr auto kCases = std::to_array<std::string_view>({
      "--context=missing-colon",
      "--context=A:not-a-number",
      "--context=D:1",
      "--before-context=bad",
      "--after-context=bad",
  });
  for (const std::string_view flag : kCases) {
    SCOPED_TRACE(flag);
    EXPECT_THAT(RunArgvRecords({std::string(flag), root_.string(), "-grep", "HIT"}), IsEmpty());
    EXPECT_THAT(last_errors_, 2);
  }
}

TEST_F(RunTest, GrepRegextypeExactMatchesLiterally) {
  { std::ofstream(root_ / "a.txt") << "price 3.50\nprice 3X50\n"; }
  // --regextype=EXACT (a leading global) makes '.' a literal, so only the real 3.50.
  EXPECT_THAT(
      RunArgvRecords({"--regextype=EXACT", root_.string(), "-name", "a.txt", "-grep", "3.50"}),
      ElementsAre(Path("a.txt") + ":1:price 3.50"));
}

TEST_F(RunTest, GrepRegextypeDefaultIsRe2) {
  { std::ofstream(root_ / "a.txt") << "price 3.50\nprice 3X50\n"; }
  // Default (RE2): '.' is a wildcard, so both lines match.
  EXPECT_THAT(
      RunArgvRecords({root_.string(), "-name", "a.txt", "-grep", "3.50"}),
      ElementsAre(Path("a.txt") + ":1:price 3.50", Path("a.txt") + ":2:price 3X50"));
}

TEST_F(RunTest, GrepRegextypeEreUsesThePlatformPosixEngine) {
  { std::ofstream(root_ / "a.txt") << "item 42\nnone\n"; }
  EXPECT_THAT(
      RunArgvRecords({"--regextype=ERE", root_.string(), "-name", "a.txt", "-grep", "[[:digit:]]+"}),
      ElementsAre(Path("a.txt") + ":1:item 42"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, UnsupportedRegextypeIsAUsageError) {
  // MATCH is reserved (#85), and PCRE2 is a build extra not linked into this (lean) test binary:
  // both are usage errors refused before the walk (exit 2), never a silent RE2 fallback.
  EXPECT_THAT(RunArgvRecords({"--regextype=MATCH", root_.string(), "-grep", "x"}), IsEmpty());
  EXPECT_THAT(last_errors_, Not(0));
  EXPECT_THAT(RunArgvRecords({"--regextype=PCRE2", root_.string(), "-grep", "x"}), IsEmpty());
  EXPECT_THAT(last_errors_, Not(0));
  EXPECT_THAT(RunArgvRecords({"--pcre", root_.string(), "-grep", "x"}), IsEmpty());
  EXPECT_THAT(last_errors_, Not(0));
}

TEST_F(RunTest, RegextypeUsesTheLastOccurrence) {
  { std::ofstream(root_ / "a.txt") << "price 3.50\nprice 3X50\n"; }
  EXPECT_THAT(
      RunArgvRecords({"--regextype=MATCH", "--regextype=EXACT", root_.string(), "-name", "a.txt", "-grep", "3.50"}),
      ElementsAre(Path("a.txt") + ":1:price 3.50"));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, GrepFormatRendersCustomTemplate) {
  // -grep:FORMAT overrides the default path:line:text with a field template.
  { std::ofstream(root_ / "a.txt") << "alpha\nTODO one\nbeta\n"; }
  EXPECT_THAT(RunExpr({"-name", "a.txt", "-grep:{line}|{text}", "TODO"}), ElementsAre("2|TODO one"));
}

TEST_F(RunTest, GrepCountEmitsPerFileCount) {
  // --count / -c (a leading global): path:count per file with matches, not the lines.
  { std::ofstream(root_ / "a.txt") << "TODO 1\nx\nTODO 2\n"; }
  EXPECT_THAT(
      RunArgvRecords({"--count", root_.string(), "-name", "a.txt", "-grep", "TODO"}),
      ElementsAre(Path("a.txt") + ":2"));
}

TEST_F(RunTest, ShardsCollapsesEachSetToOneLineAndPassesNonShardsThrough) {
  { std::ofstream(root_ / "data-00000-of-00003.tfrecord") << ""; }
  { std::ofstream(root_ / "data-00001-of-00003.tfrecord") << ""; }
  { std::ofstream(root_ / "data-00002-of-00003.tfrecord") << ""; }
  { std::ofstream(root_ / "vol.1") << ""; }  // dotnum scheme
  { std::ofstream(root_ / "vol.2") << ""; }
  // --shards (auto): each set collapses to its lowest-index representative; a.txt / b.md / sub/c.txt
  // (from SetUp) are non-shards and list unchanged.
  EXPECT_THAT(
      RunArgvRecords({root_.string(), "-type", "f", "--shards"}),
      UnorderedElementsAre(
          Path("data-00000-of-00003.tfrecord"), Path("vol.1"), Path("a.txt"), Path("b.md"), Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardsSchemeRestrictionTreatsUnselectedSchemesAsNonShards) {
  { std::ofstream(root_ / "data-00000-of-00003.tfrecord") << ""; }
  { std::ofstream(root_ / "data-00001-of-00003.tfrecord") << ""; }
  { std::ofstream(root_ / "data-00002-of-00003.tfrecord") << ""; }
  { std::ofstream(root_ / "vol.1") << ""; }
  { std::ofstream(root_ / "vol.2") << ""; }
  // --shards=of collapses only the -of- set; the dotnum vol.1 / vol.2 are listed unchanged.
  EXPECT_THAT(
      RunArgvRecords({root_.string(), "-type", "f", "--shards=of"}),
      UnorderedElementsAre(
          Path("data-00000-of-00003.tfrecord"), Path("vol.1"), Path("vol.2"), Path("a.txt"), Path("b.md"),
          Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardsUnknownSchemeIsAUsageError) {
  EXPECT_THAT(RunArgvRecords({root_.string(), "--shards=bogus"}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
}

TEST_F(RunTest, ShardsShowWildcardMasksTheIndex) {
  { std::ofstream(root_ / "data-00000-of-00003.tfrecord") << ""; }
  { std::ofstream(root_ / "data-00001-of-00003.tfrecord") << ""; }
  { std::ofstream(root_ / "data-00002-of-00003.tfrecord") << ""; }
  // The child is built with StrCat so the source carries no `??-` (a C++ trigraph).
  const std::string wildcard = absl::StrCat("data-", std::string(5, '?'), "-of-00003.tfrecord");
  EXPECT_THAT(
      RunArgvRecords({root_.string(), "-type", "f", "--shards", "--shards-show=wildcard"}),
      UnorderedElementsAre(Path(wildcard), Path("a.txt"), Path("b.md"), Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardsShowCountAppendsTheShardCount) {
  { std::ofstream(root_ / "data-00000-of-00003.tfrecord") << ""; }
  { std::ofstream(root_ / "data-00001-of-00003.tfrecord") << ""; }
  { std::ofstream(root_ / "data-00002-of-00003.tfrecord") << ""; }
  const std::string line = absl::StrCat("data-", std::string(5, '?'), "-of-00003.tfrecord (3 shards)");
  EXPECT_THAT(
      RunArgvRecords({root_.string(), "-type", "f", "--shards", "--shards-show=count"}),
      UnorderedElementsAre(Path(line), Path("a.txt"), Path("b.md"), Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardsAnnotateAnIncompleteSet) {
  // Shard 1 of 3 is missing: the set is flagged INCOMPLETE with its present/expected count.
  { std::ofstream(root_ / "data-00000-of-00003.tfrecord") << ""; }
  { std::ofstream(root_ / "data-00002-of-00003.tfrecord") << ""; }
  EXPECT_THAT(
      RunArgvRecords({root_.string(), "-type", "f", "--shards"}),
      UnorderedElementsAre(
          Path("data-00000-of-00003.tfrecord (2/3 - INCOMPLETE)"), Path("a.txt"), Path("b.md"), Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardsRenderASetContainingOnlyAnOutOfRangeFile) {
  { std::ofstream(root_ / "data-00002-of-00002") << ""; }
  EXPECT_THAT(
      RunExpr({"-type", "f", "--shards"}),
      UnorderedElementsAre(
          Path("data-00002-of-00002 (0/2 - INCOMPLETE)"), Path("a.txt"), Path("b.md"), Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardStatusCompleteMatchesOnlyRepresentativesInCompleteSets) {
  { std::ofstream(root_ / "complete-00000-of-00002") << ""; }
  { std::ofstream(root_ / "complete-00001-of-00002") << ""; }
  { std::ofstream(root_ / "incomplete-00000-of-00002") << ""; }
  EXPECT_THAT(
      RunExpr({"-type", "f", "-shard-status", "complete"}),
      UnorderedElementsAre(Path("complete-00000-of-00002"), Path("complete-00001-of-00002")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardStatusIncompleteMatchesEveryPresentRepresentativeInAnIncompleteSet) {
  { std::ofstream(root_ / "data-00000-of-00003") << ""; }
  { std::ofstream(root_ / "data-00002-of-00003") << ""; }
  EXPECT_THAT(
      RunExpr({"-type", "f", "-shard-status", "incomplete"}),
      UnorderedElementsAre(Path("data-00000-of-00003"), Path("data-00002-of-00003")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardStatusSuperfluousMatchesDuplicateCopiesAndOutOfRangeIndices) {
  { std::ofstream(root_ / "data-00000-of-00002.aaaaaaaa") << ""; }
  { std::ofstream(root_ / "data-00000-of-00002.bbbbbbbb") << ""; }
  { std::ofstream(root_ / "data-00001-of-00002") << ""; }
  { std::ofstream(root_ / "data-00002-of-00002") << ""; }
  EXPECT_THAT(
      RunExpr({"-type", "f", "-shard-status", "superfluous"}),
      UnorderedElementsAre(Path("data-00000-of-00002.bbbbbbbb"), Path("data-00002-of-00002")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardStatusClassifiesOnlyTheCohortReachingThatExpressionNode) {
  { std::ofstream(root_ / "data-00000-of-00002") << ""; }
  { std::ofstream(root_ / "data-00001-of-00002") << ""; }
  // Filtering away index 1 before the result-set predicate intentionally makes
  // the remaining cohort incomplete; predicates after it do not affect the set.
  EXPECT_THAT(RunExpr({"-name", "*-00000-*", "-shard-status", "incomplete"}), ElementsAre(Path("data-00000-of-00002")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardStatusFalseCanContinueIntoAnOrAlternative) {
  { std::ofstream(root_ / "data-00000-of-00001") << ""; }
  EXPECT_THAT(
      RunExpr({"-type", "f", "-shard-status", "incomplete", "-o", "-name", "a.txt"}), ElementsAre(Path("a.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardStatusRejectsAnUnknownStatusBeforeWalking) {
  EXPECT_THAT(RunExpr({"-shard-status", "broken"}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
}

TEST_F(RunTest, ShardStatusDoesNotClassifyDirectoriesWhoseNamesLookLikeShards) {
  ASSERT_THAT(fs::create_directory(root_ / "dir-00000-of-00001"), IsTrue());
  EXPECT_THAT(RunExpr({"-shard-status", "complete"}), IsEmpty());
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardStatusPreservesRepeatedVisitsThroughOverlappingRoots) {
  { std::ofstream(root_ / "data-00000-of-00001") << ""; }
  const std::string shard = Path("data-00000-of-00001");
  EXPECT_THAT(
      RunArgvRecords({root_.string(), shard, "-type", "f", "-shard-status", "complete"}), ElementsAre(shard, shard));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardStatusUsesCustomPatterns) {
  { std::ofstream(root_ / "img_001_v3.raw") << ""; }
  { std::ofstream(root_ / "img_002_v3.raw") << ""; }
  EXPECT_THAT(
      RunArgvRecords(
          {root_.string(), "-type", "f", "-shard-status", "complete",
           R"(--shard-pattern=(?P<stem>.*)_(?P<index>\d+)_v(?P<dup>\d+)\.raw)"}),
      UnorderedElementsAre(Path("img_001_v3.raw"), Path("img_002_v3.raw")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardStatusUsesMtimeToChooseTheRepresentative) {
  const fs::path older = root_ / "data-00000-of-00001.aaaaaaaa";
  const fs::path newer = root_ / "data-00000-of-00001.bbbbbbbb";
  { std::ofstream(older) << ""; }
  { std::ofstream(newer) << ""; }
  std::error_code ec;
  fs::last_write_time(older, fs::file_time_type::clock::now() - std::chrono::hours(1), ec);
  ASSERT_THAT(ec, Eq(std::error_code{}));
  fs::last_write_time(newer, fs::file_time_type::clock::now(), ec);
  ASSERT_THAT(ec, Eq(std::error_code{}));
  EXPECT_THAT(
      RunArgvRecords({root_.string(), "-type", "f", "-shard-status", "complete", "--shards-dedup=mtime"}),
      ElementsAre(Path("data-00000-of-00001.bbbbbbbb")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardStatusHonorsTheEnabledBuiltInSchemes) {
  { std::ofstream(root_ / "data.001") << ""; }
  EXPECT_THAT(RunArgvRecords({root_.string(), "-type", "f", "-shard-status", "complete", "--shards=of"}), IsEmpty());
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardStatusUsesAllBuiltInSchemesByDefault) {
  { std::ofstream(root_ / "data.001") << ""; }
  EXPECT_THAT(RunExpr({"-type", "f", "-shard-status", "complete"}), ElementsAre(Path("data.001")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardStatusHonorsDedupErrorWithoutCollapsedOutput) {
  { std::ofstream(root_ / "data-00000-of-00001.aaaaaaaa") << ""; }
  { std::ofstream(root_ / "data-00000-of-00001.bbbbbbbb") << ""; }
  EXPECT_THAT(
      RunArgvRecords({root_.string(), "-type", "f", "-shard-status", "superfluous", "--shards-dedup=error"}),
      ElementsAre(Path("data-00000-of-00001.bbbbbbbb")));
  EXPECT_THAT(last_errors_, 1);
}

TEST_F(RunTest, ShardsShowUnknownValueIsAUsageError) {
  EXPECT_THAT(RunArgvRecords({root_.string(), "--shards", "--shards-show=bogus"}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
}

TEST_F(RunTest, ShardsDedupErrorFlagsADuplicate) {
  // Two files are the same logical shard 0 (differ only by hex tail): --shards-dedup=error fails.
  { std::ofstream(root_ / "data-00000-of-00001.aaaaaaaa") << ""; }
  { std::ofstream(root_ / "data-00000-of-00001.bbbbbbbb") << ""; }
  // The set still collapses to one line (representative), but the conflict is counted as an error
  // (one conflicting set -> one error; the CLI maps any non-zero count to a failing exit).
  EXPECT_THAT(
      RunArgvRecords({root_.string(), "-type", "f", "--shards", "--shards-dedup=error"}),
      UnorderedElementsAre(Path("data-00000-of-00001.aaaaaaaa"), Path("a.txt"), Path("b.md"), Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, 1);
}

TEST_F(RunTest, ShardsDedupUnknownValueIsAUsageError) {
  EXPECT_THAT(RunArgvRecords({root_.string(), "--shards", "--shards-dedup=bogus"}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
}

TEST_F(RunTest, ShardPatternCustomSchemeCollapsesASet) {
  // A naming the built-ins do not recognize (`img_NNN_vX.raw`); a custom pattern groups it,
  // with the version treated as the dup (excluded from identity).
  { std::ofstream(root_ / "img_001_v3.raw") << ""; }
  { std::ofstream(root_ / "img_002_v3.raw") << ""; }
  { std::ofstream(root_ / "img_003_v3.raw") << ""; }
  EXPECT_THAT(
      RunArgvRecords(
          {root_.string(), "-type", "f", "--shards",
           R"(--shard-pattern=(?P<stem>.*)_(?P<index>\d+)_v(?P<dup>\d+)\.raw)"}),
      UnorderedElementsAre(Path("img_001_v3.raw"), Path("a.txt"), Path("b.md"), Path("sub/c.txt")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardPatternMissingRequiredGroupIsAUsageError) {
  EXPECT_THAT(RunArgvRecords({root_.string(), "--shards", R"(--shard-pattern=(?P<stem>.*)_(\d+))"}), IsEmpty());
  EXPECT_THAT(last_errors_, 2);
}

TEST_F(RunTest, ShardsSummaryAggregatesPerLogicalSet) {
  // Three shards of one set (sizes 4+2+1) plus one non-shard file. --summary=ext in shard mode
  // counts the set once and sums its size (7 bytes), not three separate files.
  { std::ofstream(root_ / "data-000-of-003.tfrecord") << "aaaa"; }
  { std::ofstream(root_ / "data-001-of-003.tfrecord") << "bb"; }
  { std::ofstream(root_ / "data-002-of-003.tfrecord") << "c"; }
  const std::vector<std::string> rows =
      RunArgvRecords({root_.string(), "-name", "*.tfrecord", "--shards", "--summary=ext"});
  // The tfrecord row aggregates the set into one unit of 7 bytes.
  EXPECT_THAT(rows, Contains(AllOf(HasSubstr("tfrecord"), HasSubstr("7"))));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, ShardsSummaryByShardCountGroupsSets) {
  { std::ofstream(root_ / "data-000-of-002.bin") << ""; }
  { std::ofstream(root_ / "data-001-of-002.bin") << ""; }
  // --summary={shard} groups by each set's shard count; the 2-shard set lands in a "2" bucket.
  const std::vector<std::string> rows =
      RunArgvRecords({root_.string(), "-name", "*.bin", "--shards", "--summary={shard}"});
  EXPECT_THAT(rows, Contains(HasSubstr("2")));
  EXPECT_THAT(last_errors_, 0);
}

TEST_F(RunTest, FlavorFacetsHaveStableStorage) {
  const auto facets = FlavorFacets();
  EXPECT_THAT(FlavorFacets().data(), Eq(facets.data()));
  std::string roots;
  std::string global;
  for (const FlavorFacet& facet : facets) {
    if (facet.behavior == "traversal order") {
      roots = facet.value({"--sort=roots"}, registry::Style::kXff);
      global = facet.value({"--sort=global"}, registry::Style::kXff);
    }
  }
  EXPECT_THAT(roots, Eq("roots"));
  EXPECT_THAT(global, Eq("global"));
}

TEST_F(RunTest, DryRunRejectsOutputCollisionsAndUnstatableDestinations) {
  const std::vector<std::string> targets = {Path("b.md"), Path("a.txt/child")};
  for (const auto& target : targets) {
    MBO_ASSERT_OK_AND_ASSIGN(
        const auto command, parser::Parse({"--dry-run", "--block-file-overwrite", Path("a.txt"), "-fprint", target}));
    std::vector<std::string> records;
    std::vector<std::string> diagnostics;
    const auto result = RunFind(
        command, fs_, [&](std::string_view line) { records.emplace_back(line); },
        [&](std::string_view, absl::Status status) { diagnostics.emplace_back(status.message()); });
    EXPECT_THAT(result.errors, 1);
    EXPECT_THAT(records, IsEmpty());
    EXPECT_THAT(diagnostics, SizeIs(1));
    EXPECT_THAT(fs::file_size(Path("b.md")), 1);
  }
}

TEST_F(RunTest, PackRefusesAnUnstatableDestinationBeforeTraversal) {
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command,
      parser::Parse({"--block-archive-overwrite", "--pack=" + Path("a.txt/out.tar"), Path("a.txt")}));
  EXPECT_THAT(
      RunFind(
          command, fs_, [](std::string_view) { ADD_FAILURE() << "must not traverse"; },
          [](std::string_view, absl::Status status) {
            EXPECT_THAT(status, StatusIs(absl::StatusCode::kFailedPrecondition));
          })
          .errors,
      2);
}

TEST_F(RunTest, FileDeletionBlockAppliesEvenWhenArchiveDeletionIsAllowed) {
  const std::vector<std::string> orders = {"--sort=none", "--sort=name"};
  for (const std::string& order : orders) {
    MBO_ASSERT_OK_AND_ASSIGN(
        auto command, parser::Parse({"--block-file-deletion", "--skip-unsupported", order, Path("a.txt"), "-delete"}));
    // Represents an INI file using separate archive controls, already expanded by the resolver.
    command.safety_flags_expanded = true;
    const auto result = RunFind(
        command, fs_, [](std::string_view) {},
        [](std::string_view, absl::Status status) {
          EXPECT_THAT(status, StatusIs(absl::StatusCode::kPermissionDenied));
        });
    EXPECT_THAT(result.errors, 1);
    EXPECT_THAT(fs::exists(Path("a.txt")), IsTrue());
  }
}

TEST_F(RunTest, DryRunRendersExecFieldsAndStopsDeferredExpressions) {
  const std::vector<std::string> orders = {"--sort=none", "--sort=name"};
  for (const std::string& order : orders) {
    MBO_ASSERT_OK_AND_ASSIGN(
        const auto command, parser::Parse(
                                {"--dry-run", "--exec-fields", order, Path("a.txt"), "-fuzzy", "a.txt", "-top", "1",
                                 "-exec", "echo", "{name}", ";"}));
    std::vector<std::string> records;
    const auto result = RunFind(
        command, fs_, [&](std::string_view line) { records.emplace_back(line); },
        [](std::string_view, absl::Status status) {
          EXPECT_THAT(status, StatusIs(absl::StatusCode::kFailedPrecondition));
        });
    EXPECT_THAT(result.errors, 1);
    EXPECT_THAT(records, ElementsAre(AllOf(HasSubstr("would execute"), HasSubstr("a.txt"))));
  }
}

#if defined(__linux__)
TEST_F(RunTest, OutputWriteFailureIsReportedEvenWithSkipUnsupported) {
  MBO_ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse({"--skip-unsupported", Path("a.txt"), "-fprint", "/dev/full"}));
  EXPECT_THAT(
      RunFind(
          command, fs_, [](std::string_view) {},
          [](std::string_view, absl::Status status) {
            EXPECT_THAT(status, StatusIs(absl::StatusCode::kResourceExhausted));
          })
          .errors,
      1);
}
#endif

}  // namespace
}  // namespace xff::engine
