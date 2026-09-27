// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"
#include "xff/engine/run.h"
#include "xff/matching/regex/regex.h"
#include "xff/parser/parser.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {
namespace {
using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOk;
using ::mbo::testing::WithDropIndent;
using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;

struct UnicodeFs final : vfs::FileSystem {
  std::string bytes;

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view) const override { return std::vector<vfs::Entry>{}; }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view, bool) const override {
    return vfs::Metadata{.type = vfs::FileType::kRegular, .size = bytes.size()};
  }

  absl::Status Remove(std::string_view) const override {
    ADD_FAILURE() << "Unicode search attempted mutation";
    return absl::PermissionDeniedError("read-only");
  }

  bool Access(std::string_view, vfs::AccessMode) const override { return true; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override { return std::string(); }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return "memory"; }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

  absl::StatusOr<std::string> ReadContent(std::string_view) const override { return bytes; }
};

struct RgUnicodeTest : ::testing::Test {
  UnicodeFs fs;
  std::string output;

  RunResult Run(std::vector<std::string> args, std::string_view grammar) {
    args.insert(args.begin(), {"--rg", "-H", "--regextype=" + std::string(grammar)});
    args.emplace_back("input");
    auto parsed = parser::Parse(args);
    EXPECT_THAT(parsed, IsOk());
    if (!parsed.ok()) {
      return {.errors = 2};
    }
    auto command = *std::move(parsed);
    parser::BindMatchers(
        command, parser::GrammarFromGlobals(command.globals),
        parser::ResolveCaseMode(command.globals, registry::Style::kXff));
    output.clear();
    return RunFind(
        command, fs, [this](std::string_view text) { output.append(text); },
        [](std::string_view, absl::Status status) { EXPECT_THAT(status, IsOk()); });
  }
};

TEST_F(RgUnicodeTest, Utf8WordBoundariesDifferFromExplicitByteMode) {
  fs.bytes = "éhit hité «hit» hit\u0301\n";
  for (const std::string_view grammar : {"RE2", "PCRE2"}) {
    SCOPED_TRACE(grammar);
    EXPECT_THAT(Run({"-wo", "hit"}, grammar).any_match, IsTrue());
    EXPECT_THAT(output, EqualsText("input:hit\n"));
    EXPECT_THAT(Run({"--no-unicode", "-wo", "hit"}, grammar).any_match, IsTrue());
    EXPECT_THAT(output, WithDropIndent(EqualsText(R"out(
      input:hit
      input:hit
      input:hit
      input:hit
)out")));
    EXPECT_THAT(Run({"--no-unicode", "--unicode", "-wo", "hit"}, grammar).any_match, IsTrue());
    EXPECT_THAT(output, EqualsText("input:hit\n"));
  }
}

TEST_F(RgUnicodeTest, RegexBackendsReceiveTheSelectedTextMode) {
  fs.bytes = "é\n";
  for (const std::string_view grammar : {"RE2", "PCRE2"}) {
    SCOPED_TRACE(grammar);
    EXPECT_THAT(Run({"--count-matches", "."}, grammar).errors, Eq(0));
    EXPECT_THAT(output, EqualsText("input:1\n"));
    EXPECT_THAT(Run({"--no-unicode", "--count-matches", "."}, grammar).errors, Eq(0));
    EXPECT_THAT(output, EqualsText("input:2\n"));
    EXPECT_THAT(Run({"--count-matches", ""}, grammar).errors, Eq(0));
    EXPECT_THAT(output, EqualsText("input:2\n"));
    EXPECT_THAT(Run({"--no-unicode", "--count-matches", ""}, grammar).errors, Eq(0));
    EXPECT_THAT(output, EqualsText("input:3\n"));
  }
}

TEST_F(RgUnicodeTest, ByteModeCanSearchNonUtf8InputWithoutTranscoding) {
  fs.bytes = "caf\xe9\n";
  for (const std::string_view grammar : {"RE2", "PCRE2"}) {
    SCOPED_TRACE(grammar);
    EXPECT_THAT(Run({"--no-unicode", "-x", "caf."}, grammar).any_match, IsTrue());
    EXPECT_THAT(output, EqualsText("input:caf\xe9\n"));
    EXPECT_THAT(Run({"--unicode", "-x", "caf."}, grammar).any_match, IsFalse());
  }
}

TEST_F(RgUnicodeTest, WorkerMatchersRetainExplicitTextInterpretation) {
  ASSERT_OK_AND_ASSIGN(
      const auto bytes, regex::Matcher::Compile(".", false, regex::Grammar::kRe2, regex::TextMode::kBytes));
  ASSERT_OK_AND_ASSIGN(const auto bytes_worker, bytes.ForkForWorker());
  EXPECT_THAT(bytes_worker.FullMatch("\xe9"), IsTrue());
  EXPECT_THAT(bytes_worker.FullMatch("é"), IsFalse());
  ASSERT_OK_AND_ASSIGN(
      const auto utf8, regex::Matcher::Compile(".", false, regex::Grammar::kRe2, regex::TextMode::kUtf8));
  ASSERT_OK_AND_ASSIGN(const auto utf8_worker, utf8.ForkForWorker());
  EXPECT_THAT(utf8_worker.FullMatch("é"), IsTrue());
  EXPECT_THAT(utf8_worker.FullMatch("\xe9"), IsFalse());
}

TEST_F(RgUnicodeTest, Utf8StillFindsValidTextAroundMalformedBytes) {
  fs.bytes = "\xff hit \xff\n";
  for (const std::string_view grammar : {"RE2", "PCRE2"}) {
    EXPECT_THAT(Run({"-wo", "hit"}, grammar).any_match, IsTrue());
    EXPECT_THAT(output, EqualsText("input:hit\n"));
  }
}
}  // namespace
}  // namespace xff::engine
