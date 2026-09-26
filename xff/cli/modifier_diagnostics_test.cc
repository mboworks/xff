// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/cli/modifier_diagnostics.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/status/status_macros.h"
#include "mbo/testing/status.h"
#include "xff/archive/archive_backend.h"
#include "xff/cli/config_validation.h"
#include "xff/config/config.h"
#include "xff/config/ini.h"
#include "xff/parser/parser.h"
#include "xff/registry/descriptor.h"
#include "xff/vfs/filesystem.h"

namespace xff::cli {
namespace {

using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;

struct ModifierDiagnosticsTest : ::testing::Test {
  static absl::StatusOr<std::string> Notes(
      const std::vector<std::string>& args,
      registry::Style style = registry::Style::kXff) {
    MBO_ASSIGN_OR_RETURN(const auto command, parser::Parse(args));
    std::vector<config::ResolvedFlag> application;
    application.reserve(command.globals.size());
    for (const std::string& global : command.globals) {
      application.push_back({.flag = global, .source = config::Source::kCli});
    }
    return InactiveModifierNotes(command, application, style);
  }
};

struct ArchiveModifierDiagnosticsTest : ModifierDiagnosticsTest {
  void SetUp() override {
    archive::RegisterContainerReader(
        "modifier-diagnostics",
        [](std::string_view, std::optional<std::string_view>,
           archive::MemberPathOptions) -> absl::StatusOr<std::unique_ptr<vfs::FileSystem>> {
          ADD_FAILURE() << "Inspection must never open an archive";
          return absl::InternalError("unexpected archive access");
        },
        {});
  }

  void TearDown() override { archive::RegisterContainerReader("modifier-diagnostics", archive::ContainerOpener{}, {}); }
};

TEST_F(ModifierDiagnosticsTest, HashActionsConsumeOnlyUnspecifiedDefaults) {
  EXPECT_THAT(Notes({".", "--hash-algorithm=md5", "-hash"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(Notes({".", "--hash-encoding=base64", "-hasheq", "expected"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(
      Notes({".", "--hash-algorithm=md5", "-hash:sha256"}),
      IsOkAndHolds(HasSubstr("requires a hash consumer without an explicit algorithm")));
  EXPECT_THAT(Notes({".", "--hash-encoding=base64", "-hash:sha256"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(Notes({".", "--hash-algorithm=md5", "-hash:/hex"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(
      Notes({".", "--hash-encoding=base64", "-hash:/hex"}),
      IsOkAndHolds(HasSubstr("requires a hash consumer without an explicit encoding")));
  EXPECT_THAT(Notes({".", "--hash-encoding=base64", "-hash:sha256/"}), IsOkAndHolds(Eq("")));
}

TEST_F(ModifierDiagnosticsTest, HashFieldsUseParsedNativeQualifiers) {
  EXPECT_THAT(
      Notes({".", "--hash-algorithm=md5", "--template={hash:sha256}"}), IsOkAndHolds(HasSubstr("inactive-modifier")));
  EXPECT_THAT(Notes({".", "--hash-encoding=base64", "--template={hash:sha256}"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(Notes({".", "--hash-algorithm=md5", "--template={hash:s/a/b/}"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(Notes({".", "--hash-encoding=base64", "--template={hash:sha256/hex} {hash}"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(Notes({".", "--hash-algorithm=md5", "-name", "{hash}"}), IsOkAndHolds(HasSubstr("inactive-modifier")));
  EXPECT_THAT(
      Notes({".", "--hash-algorithm=md5", "--define=x={hash}", "--template={def.x}"}),
      IsOkAndHolds(HasSubstr("inactive-modifier")));
}

TEST_F(ModifierDiagnosticsTest, PrintfAndExecRespectTheirActualFieldSyntax) {
  EXPECT_THAT(Notes({".", "--hash-algorithm=md5", "-printf", "%{hash}"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(
      Notes({".", "--hash-algorithm=md5", "-printf", "%%{hash}"}), IsOkAndHolds(HasSubstr("inactive-modifier")));
  EXPECT_THAT(
      Notes({".", "--hash-algorithm=md5", "-exec", "echo", "{hash}", ";"}),
      IsOkAndHolds(HasSubstr("inactive-modifier")));
  EXPECT_THAT(
      Notes({".", "--hash-algorithm=md5", "--exec-fields", "-exec", "echo", "{hash}", ";"}), IsOkAndHolds(Eq("")));
}

TEST_F(ModifierDiagnosticsTest, GrepCountSuppressesHashFieldsInItsLineTemplate) {
  EXPECT_THAT(Notes({".", "--hash-algorithm=md5", "-grep:{hash}", "pattern"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(
      Notes({".", "--hash-algorithm=md5", "--count", "-grep:{hash}", "pattern"}),
      IsOkAndHolds(HasSubstr("inactive-modifier")));
}

TEST_F(ModifierDiagnosticsTest, SuppressedListingDoesNotConsumeHashDefaults) {
  EXPECT_THAT(
      Notes({".", "--hash-algorithm=md5", "--template={hash}", "--implicit-print=no"}),
      IsOkAndHolds(HasSubstr("inactive-modifier")));
  EXPECT_THAT(
      Notes({".", "--hash-algorithm=md5", "--template={hash}", "--summary=ext"}),
      IsOkAndHolds(HasSubstr("inactive-modifier")));
  EXPECT_THAT(
      Notes({".", "--hash-algorithm=md5", "--template={hash}", "-print"}),
      IsOkAndHolds(HasSubstr("inactive-modifier")));
  EXPECT_THAT(
      Notes({".", "--hash-algorithm=md5", "--template={hash}", "--columns=name", "--format=csv"}),
      IsOkAndHolds(HasSubstr("inactive-modifier")));
  EXPECT_THAT(Notes({".", "--hash-algorithm=md5", "--columns=hash", "--format=csv"}), IsOkAndHolds(Eq("")));
}

TEST_F(ModifierDiagnosticsTest, HashSummariesRespectClearingAndDefaultOverrides) {
  EXPECT_THAT(Notes({".", "--hash-algorithm=md5", "--summary=hash"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(Notes({".", "--hash-encoding=base64", "--summary={hash:sha256}"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(
      Notes({".", "--hash-algorithm=md5", "--summary=hash", "--summary=none"}),
      IsOkAndHolds(HasSubstr("inactive-modifier")));
  EXPECT_THAT(Notes({"--compare", "left", "right", "--hash-algorithm=md5", "--summary=hash"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(
      Notes({"--compare=summary", "left", "right", "--hash-algorithm=md5"}),
      IsOkAndHolds(HasSubstr("inactive-modifier")));
}

TEST_F(ModifierDiagnosticsTest, MissingArchiveBackendDoesNotProvideAConsumer) {
  EXPECT_THAT(Notes({".", "--archive-separator=::"}), IsOkAndHolds(HasSubstr("requires available archive traversal")));
  EXPECT_THAT(
      Notes({".", "--archive=all", "--archive-depth=2"}),
      StatusIs(absl::StatusCode::kUnimplemented, HasSubstr("not built into this binary")));
}

TEST_F(ArchiveModifierDiagnosticsTest, MemberPathsUseResolvedFlavorAndFinalTraversalMode) {
  EXPECT_THAT(Notes({".", "--archive-separator=::", "--archive-prefix=URI"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(
      Notes({".", "--archive-prefix=URI"}, registry::Style::kFind),
      IsOkAndHolds(HasSubstr("requires available archive traversal")));
  EXPECT_THAT(Notes({".", "--archive=roots", "--archive-prefix=URI"}, registry::Style::kFind), IsOkAndHolds(Eq("")));
  EXPECT_THAT(
      Notes({".", "--archive=all", "--archive=none", "--archive-separator=::"}),
      IsOkAndHolds(HasSubstr("inactive-modifier")));
}

TEST_F(ArchiveModifierDiagnosticsTest, DepthRequiresTraversalBeyondRoots) {
  EXPECT_THAT(Notes({".", "--archive-depth=2"}), IsOkAndHolds(HasSubstr("roots does not nest")));
  EXPECT_THAT(Notes({".", "--archive-any", "--archive-depth=2"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(
      Notes({".", "--archive-any", "--archive=roots", "--archive-depth=2"}),
      IsOkAndHolds(HasSubstr("roots does not nest")));
  EXPECT_THAT(Notes({".", "--archive=all", "--archive-depth=2", "--archive-any"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(Notes({".", "--archive=any", "--archive-depth=2"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(Notes({"-z+", ".", "--archive-depth=2"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(Notes({"-z+", "-z-", ".", "--archive-depth=2"}), IsOkAndHolds(HasSubstr("inactive-modifier")));
}

TEST_F(ArchiveModifierDiagnosticsTest, AggregateNeedsAnEffectiveReductionAndArchiveTraversal) {
  EXPECT_THAT(Notes({".", "--archive-aggregate=container"}), IsOkAndHolds(HasSubstr("ordinary summary")));
  EXPECT_THAT(Notes({".", "--archive-aggregate=container", "--summary=ext"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(
      Notes({".", "--archive-aggregate=container", "--summary=ext", "--summary=none"}),
      IsOkAndHolds(HasSubstr("inactive-modifier")));
  EXPECT_THAT(
      Notes({".", "--archive=none", "--archive-aggregate=container", "--summary=ext"}),
      IsOkAndHolds(HasSubstr("inactive-modifier")));
  EXPECT_THAT(Notes({".", "--archive-aggregate=members", "--histogram=ext"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(Notes({".", "--archive-aggregate=members", "--shards"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(Notes({".", "--archive-aggregate=members", "--pack=out.tar"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(
      Notes({"left", "right", "--compare=summary", "--archive-aggregate=members"}),
      IsOkAndHolds(HasSubstr("inactive-modifier")));
  EXPECT_THAT(
      Notes({"left", "right", "--compare=summary", "--archive-aggregate=members", "--summary=ext"}),
      IsOkAndHolds(Eq("")));
}

TEST_F(ModifierDiagnosticsTest, MatchOutputConsumesGrepModifiersOnlyWhenDefaultOutputIsActive) {
  EXPECT_THAT(Notes({"-M", ".", "-rxc", "foo", "--count"}), IsOkAndHolds(IsEmpty()));
  EXPECT_THAT(Notes({".", "-content", "foo", "--match-output", "--context=1"}), IsOkAndHolds(IsEmpty()));
  EXPECT_THAT(Notes({"-M", ".", "-rxc", "foo", "-print", "--count"}), IsOkAndHolds(HasSubstr("--count")));
  EXPECT_THAT(Notes({"-M", ".", "-rxc", "foo", "--no-match-output", "--count"}), IsOkAndHolds(HasSubstr("--count")));
}

TEST_F(ModifierDiagnosticsTest, CountRequiresGrepIncludingItsAttachedFormAndAlias) {
  EXPECT_THAT(Notes({".", "--count"}), IsOkAndHolds(HasSubstr("requires -grep or active --match-output")));
  EXPECT_THAT(Notes({"-c", ".", "-grep", "x"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(Notes({".", "--count", "-grep:{text}", "x"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(
      Notes({".", "--before-context=2", "--count", "-grep", "x"}), IsOkAndHolds(HasSubstr("--count suppresses")));
  EXPECT_THAT(Notes({".", "--after-context=2", "-grep", "x"}), IsOkAndHolds(Eq("")));
}

TEST_F(ModifierDiagnosticsTest, DiffDefaultsDistinguishActionsOverridesAndTreeOutput) {
  EXPECT_THAT(Notes({".", "--diff-format=y", "-diff", "other"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(
      Notes({".", "--diff-format=y", "-diff:u", "other"}), IsOkAndHolds(HasSubstr("without an attached style")));
  EXPECT_THAT(
      Notes({"left", "right", "--compare=diff", "--diff-format=y", "--diff-algorithm=direct"}),
      IsOkAndHolds(HasSubstr("tree diffs use unified")));
  EXPECT_THAT(Notes({"left", "right", "--compare=diff", "--diff-algorithm=direct"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(
      Notes({"left", "right", "--compare", "--diff-algorithm=direct"}), IsOkAndHolds(HasSubstr("requires -diff or")));
  EXPECT_THAT(Notes({".", "--diff-ignore=ws", "-diff", "other"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(Notes({".", "--diff-ignore-matching=x"}), IsOkAndHolds(HasSubstr("requires a -diff action")));
}

TEST_F(ModifierDiagnosticsTest, SharedContextUsesTheActualDiffStyleAndSymmetry) {
  EXPECT_THAT(Notes({".", "--before-context=2", "--after-context=2", "-diff", "other"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(
      Notes({".", "--context=2", "-diff:u4", "other"}), IsOkAndHolds(HasSubstr("symmetric default -diff context")));
  EXPECT_THAT(
      Notes({".", "--context=2", "--diff-context=4", "-diff", "other"}),
      IsOkAndHolds(HasSubstr("inactive-modifier\t--context=2")));
  EXPECT_THAT(
      Notes({".", "--diff-context=2", "-diff:n", "other"}), IsOkAndHolds(HasSubstr("explicit per-action context")));
  EXPECT_THAT(Notes({"left", "right", "--compare=diff", "--diff-context=2"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(Notes({".", "--context=A:3", "-diff", "other"}), IsOkAndHolds(HasSubstr("inactive-modifier")));
  EXPECT_THAT(Notes({".", "--diff-format=y", "-diff:none", "other"}), IsOkAndHolds(HasSubstr("inactive-modifier")));
}

TEST_F(ModifierDiagnosticsTest, ShardListingDependsOnFinalReductionState) {
  EXPECT_THAT(Notes({".", "--shards-show=count"}), IsOkAndHolds(HasSubstr("requires an ordinary --shards listing")));
  EXPECT_THAT(Notes({".", "--shards", "--shards-show=count"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(
      Notes({".", "--shards", "--shards-show=count", "--summary=ext"}), IsOkAndHolds(HasSubstr("inactive-modifier")));
  EXPECT_THAT(Notes({".", "--shards", "--shards-show=count", "--summary=ext", "--summary=none"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(
      Notes({"left", "right", "--compare", "--shards", "--shards-show=count"}),
      IsOkAndHolds(HasSubstr("inactive-modifier")));
  EXPECT_THAT(Notes({".", "--shards-dedup=error", "-shard-status", "complete"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(Notes({".", "--shards-dedup=error"}), IsOkAndHolds(HasSubstr("requires ordinary --shards grouping")));
}

TEST_F(ModifierDiagnosticsTest, ReductionConsumersIncludeTheirFormatAndClearingRules) {
  EXPECT_THAT(Notes({".", "--histogram=ext", "--histogram-width=20"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(
      Notes({".", "--histogram=ext", "--histogram-width=20", "--format=jsonl"}), IsOkAndHolds(HasSubstr("with bars")));
  EXPECT_THAT(Notes({".", "--top=2", "--summary=ext"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(Notes({".", "--top=2", "--histogram=ext"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(Notes({".", "--top=2", "--histogram=size"}), IsOkAndHolds(HasSubstr("numeric ranges")));
  EXPECT_THAT(Notes({".", "--summary-precision=3", "--histogram=ext:mean(size)"}), IsOkAndHolds(Eq("")));
  EXPECT_THAT(
      Notes({".", "--count", "--diff-ignore=ws", "(", "-grep", "x", "-o", "-diff", "other", ")"}),
      IsOkAndHolds(Eq("")));
  EXPECT_THAT(Notes({"left", "right", "--compare=summary", "--top=2"}), IsOkAndHolds(HasSubstr("not top-limited")));
  EXPECT_THAT(
      Notes({".", "--summary-precision=3", "--summary=ext", "--summary=none"}),
      IsOkAndHolds(HasSubstr("requires an active summary")));
}

TEST_F(ModifierDiagnosticsTest, ConfiguredHashConsumersAndDefaultsUseResolvedProvenance) {
  config::ConfigInputs inputs;
  inputs.user = config::ParseIni(R"ini([digests]
--summary=hash
[defaults]
--hash-algorithm=sha512
)ini");
  ASSERT_OK_AND_ASSIGN(auto cli, parser::Parse({".", "--hash-algorithm=md5", "--config=digests"}));
  auto application = config::ResolveConfigInOrder(inputs, cli.globals, "xff");
  ASSERT_OK_AND_ASSIGN(const auto effective, ApplyResolvedConfig(std::move(cli), application));
  EXPECT_THAT(InactiveModifierNotes(effective, application), IsOkAndHolds(Eq("")));
  ASSERT_OK_AND_ASSIGN(auto dormant, parser::Parse({".", "--hash-algorithm=md5", "--config=defaults"}));
  application = config::ResolveConfigInOrder(inputs, dormant.globals, "xff");
  ASSERT_OK_AND_ASSIGN(const auto configured, ApplyResolvedConfig(std::move(dormant), application));
  EXPECT_THAT(InactiveModifierNotes(configured, application), IsOkAndHolds(Eq("")));
}

TEST_F(ModifierDiagnosticsTest, ConfiguredConsumersCountAndDormantOrSupersededDefaultsStayQuiet) {
  config::ConfigInputs inputs;
  inputs.user = config::ParseIni("[search]\n-grep x\n[defaults]\n--count\n");
  ASSERT_OK_AND_ASSIGN(auto cli, parser::Parse({".", "--count", "--config=search"}));
  auto application = config::ResolveConfigInOrder(inputs, cli.globals, "xff");
  ASSERT_OK_AND_ASSIGN(const auto effective, ApplyResolvedConfig(std::move(cli), application));
  EXPECT_THAT(InactiveModifierNotes(effective, application), IsOkAndHolds(Eq("")));
  ASSERT_OK_AND_ASSIGN(auto dormant, parser::Parse({".", "--count", "--config=defaults"}));
  application = config::ResolveConfigInOrder(inputs, dormant.globals, "xff");
  ASSERT_OK_AND_ASSIGN(const auto configured, ApplyResolvedConfig(std::move(dormant), application));
  EXPECT_THAT(InactiveModifierNotes(configured, application), IsOkAndHolds(Eq("")));
}

TEST_F(ModifierDiagnosticsTest, ArgumentTokensDoNotBecomeModifierRequests) {
  ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({".", "-exec", "echo", "--count", ";"}));
  EXPECT_THAT(
      InactiveModifierNotes(command, {{.flag = "--count", .source = config::Source::kCli, .is_argument = true}}),
      IsOkAndHolds(Eq("")));
  EXPECT_THAT(Notes({".", "--count", "--count"}), IsOkAndHolds(Not(HasSubstr("\ninactive-modifier"))));
}

}  // namespace
}  // namespace xff::cli
