// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"
#include "xff/engine/evaluate.h"
#include "xff/engine/run.h"
#include "xff/parser/parser.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {
namespace {
using ::mbo::testing::EqualsText;
using ::mbo::testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Not;
using ::testing::SizeIs;

struct DiffFs final : vfs::FileSystem {
  std::map<std::string, std::string, std::less<>> files{
      {"root/a", "version 1\nkeep\nold\n"},
      {"root/b", "version 2\nkeep\nnew\n"},
      {"peer", "version 3\nkeep\nold\n"},
  };
  mutable std::size_t reads = 0;

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view path) const override {
    if (path != "root") {
      return absl::NotFoundError("unknown directory");
    }
    return std::vector<vfs::Entry>{
        {.path = "root/a", .name = "a", .type = vfs::FileType::kRegular},
        {.path = "root/b", .name = "b", .type = vfs::FileType::kRegular},
    };
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool) const override {
    return vfs::Metadata{
        .type = path == "root" ? vfs::FileType::kDirectory : vfs::FileType::kRegular,
        .size = 20,
        .mode = 0644,
    };
  }

  absl::StatusOr<std::string> ReadContent(std::string_view path) const override {
    ++reads;
    const auto found = files.find(path);
    return found == files.end() ? absl::StatusOr<std::string>{absl::NotFoundError("missing peer")} : found->second;
  }

  absl::Status Remove(std::string_view) const override { return absl::PermissionDeniedError("read-only fixture"); }

  bool Access(std::string_view, vfs::AccessMode) const override { return true; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override { return absl::NotFoundError("not a link"); }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return "memory"; }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }
};

struct DiffConfigurationTest : ::testing::Test {
  DiffFs filesystem;
};

TEST_F(DiffConfigurationTest, DeduplicatesStylesAndResolvesDefaultsWithoutObservingFiles) {
  ASSERT_OK_AND_ASSIGN(
      const auto command,
      parser::Parse({"root", "-diff:u1", "peer", ",", "-rdiff:u1", "peer", ",", "-diff:c", "peer"}));
  ASSERT_OK_AND_ASSIGN(
      const auto configurations, PrepareDiffConfigurations(
                                     *command.expression, DiffDefaults{
                                                              .algorithm = "direct",
                                                              .ignore = "ws,eofnl",
                                                              .ignore_matching = "^version ",
                                                              .format = mbo::diff::DiffOptions::OutputFormat::kNormal,
                                                              .context = 5,
                                                          }));
  EXPECT_THAT(configurations, SizeIs(2));
  const auto& unified = *configurations.at("u1");
  EXPECT_THAT(unified.output_format, Eq(mbo::diff::DiffOptions::OutputFormat::kUnified));
  EXPECT_THAT(unified.context_size, Eq(1));
  EXPECT_THAT(unified.algorithm, Eq(mbo::diff::DiffOptions::Algorithm::kDirect));
  EXPECT_THAT(unified.ignore_all_space, IsTrue());
  EXPECT_THAT(unified.ignore_missing_final_newline, IsTrue());
  EXPECT_THAT(unified.time_format, IsEmpty());
  EXPECT_THAT(configurations.at("c")->context_size, Eq(5));
  EXPECT_THAT(filesystem.reads, Eq(0));
}

TEST_F(DiffConfigurationTest, ReportsInvalidNormalizationAndDoesNotPrepareOtherConsumers) {
  ASSERT_OK_AND_ASSIGN(const auto diff, parser::Parse({"root", "-diff", "peer"}));
  EXPECT_THAT(
      PrepareDiffConfigurations(*diff.expression, {.ignore_matching = "["}),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("invalid --diff-ignore-matching regex")));
  EXPECT_THAT(
      PrepareDiffConfigurations(*diff.expression, {.ignore = "invalid"}),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("unknown --diff-ignore token")));
  ASSERT_OK_AND_ASSIGN(const auto plain, parser::Parse({"root", "-name", "a"}));
  ASSERT_OK_AND_ASSIGN(const auto empty, PrepareDiffConfigurations(*plain.expression, {.ignore_matching = "["}));
  EXPECT_THAT(empty, IsEmpty());
}

TEST_F(DiffConfigurationTest, PreparedAndDirectPathsPreserveOutputReadsAndConditionalEvaluation) {
  ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"root", "-diff:u1", "peer"}));
  ASSERT_OK_AND_ASSIGN(
      const auto configurations, PrepareDiffConfigurations(*command.expression, {.ignore_matching = "^version "}));
  const Visit visit{
      .path = "root/b",
      .name = "b",
      .root = "root",
      .metadata = {.type = vfs::FileType::kRegular, .mode = 0644},
  };
  std::string direct;
  std::string prepared;
  Control control;
  const auto direct_sink = [&](std::string_view text) { direct.append(text); };
  const auto prepared_sink = [&](std::string_view text) { prepared.append(text); };
  EvalContext direct_context{
      .visit = visit,
      .emit = direct_sink,
      .fs = filesystem,
      .now = absl::UnixEpoch(),
      .tz = absl::UTCTimeZone(),
      .diff_ignore_matching = "^version ",
      .control = control,
  };
  EXPECT_THAT(Evaluate(*command.expression, direct_context), IsFalse());
  EXPECT_THAT(filesystem.reads, Eq(2));
  EvalContext prepared_context{
      .visit = visit,
      .emit = prepared_sink,
      .fs = filesystem,
      .now = absl::UnixEpoch(),
      .tz = absl::UTCTimeZone(),
      .diff_configurations = configurations,
      .control = control,
  };
  EXPECT_THAT(Evaluate(*command.expression, prepared_context), IsFalse());
  EXPECT_THAT(filesystem.reads, Eq(4));
  EXPECT_THAT(prepared, EqualsText(direct));
  EXPECT_THAT(prepared, Not(IsEmpty()));

  ASSERT_OK_AND_ASSIGN(const auto skipped, parser::Parse({"root", "-false", "-diff", "peer"}));
  EXPECT_THAT(Evaluate(*skipped.expression, prepared_context), IsFalse());
  EXPECT_THAT(filesystem.reads, Eq(4));

  // A direct caller may supply a partial table; an absent style retains the legacy
  // caller-context path rather than inventing defaults or dereferencing an absent record.
  ASSERT_OK_AND_ASSIGN(const auto unlisted, parser::Parse({"root", "-diff:n", "peer"}));
  prepared.clear();
  EXPECT_THAT(Evaluate(*unlisted.expression, prepared_context), IsFalse());
  EXPECT_THAT(filesystem.reads, Eq(6));
  EXPECT_THAT(prepared, HasSubstr("1c1"));
}

TEST_F(DiffConfigurationTest, InvalidGlobalRegexStillFailsBeforeAnyContentObservation) {
  ASSERT_OK_AND_ASSIGN(
      const auto command, parser::Parse({"--diff-ignore-matching=[", "root", "-false", "-diff", "peer"}));
  std::vector<absl::Status> errors;
  std::string output;
  const auto result = RunFind(
      command, filesystem, [&](std::string_view text) { output.append(text); },
      [&](std::string_view, absl::Status status) { errors.push_back(std::move(status)); });
  EXPECT_THAT(result.errors, Eq(2));
  EXPECT_THAT(errors, ElementsAre(StatusIs(absl::StatusCode::kInvalidArgument)));
  EXPECT_THAT(output, IsEmpty());
  EXPECT_THAT(filesystem.reads, Eq(0));
}

TEST_F(DiffConfigurationTest, SeparateRunsHonorFinalGlobalsAndPreserveEarlyValidation) {
  const auto run = [&](const std::vector<std::string>& arguments) {
    std::vector<std::string> records;
    ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse(arguments));
    const auto result = RunFind(
        command, filesystem, [&](std::string_view text) { records.emplace_back(text); },
        [](std::string_view, absl::Status status) { ADD_FAILURE() << status; });
    EXPECT_THAT(result.errors, Eq(0));
    if (arguments.front() == "--diff-ignore-matching=^version ") {
      EXPECT_THAT(records, IsEmpty());
    } else {
      EXPECT_THAT(records, ElementsAre(HasSubstr("version")));
    }
  };
  run({"--diff-ignore-matching=^version ", "root/a", "-diff", "peer"});
  run({"root/a", "-diff", "peer"});
  run({"--diff-ignore-matching=^version ", "root/a", "-diff", "peer"});
}
}  // namespace
}  // namespace xff::engine
