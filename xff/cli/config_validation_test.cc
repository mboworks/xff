// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/cli/config_validation.h"

#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/config/config.h"
#include "xff/config/policy.h"
#include "xff/config/xffrc.h"
#include "xff/env/env.h"
#include "xff/parser/parser.h"

namespace xff::cli {
namespace {

using ::mbo::testing::IsOk;
using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::FieldsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::NotNull;
using ::testing::SizeIs;

struct ConfigValidationTest : ::testing::Test {};

// XFF_HOST_IO: reads explicitly declared Bazel runfile fixtures and reports failures as status.
absl::StatusOr<std::string> Fixture(std::string_view directory, std::string_view file = "system.ini") {
  const std::string path = absl::StrCat(
      env::Get("TEST_SRCDIR").value_or(""), "/", env::Get("TEST_WORKSPACE").value_or(""),
      "/xff/cli/testdata/config_validation/", directory, "/", file);
  // XFF_HOST_IO: this test adapter reads an explicitly declared Bazel runfile fixture.
  std::ifstream stream(path);
  if (!stream) {
    return absl::NotFoundError(absl::StrCat("cannot open fixture ", path));
  }
  std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
  if (stream.bad()) {
    return absl::InternalError(absl::StrCat("cannot read fixture ", path));
  }
  return text;
}

TEST_F(ConfigValidationTest, ReportsCanonicalAliasAndNegatedOverrides) {
  config::ConfigInputs inputs;
  inputs.system.globals = {"--timezone=utc", "--tz=local"};
  EXPECT_THAT(ConfigOverrideNotices(inputs), ElementsAre(HasSubstr("setting --timezone is overridden")));

  inputs.system.globals = {"--hidden", "--no-hidden"};
  EXPECT_THAT(ConfigOverrideNotices(inputs), ElementsAre(HasSubstr("setting --hidden is overridden")));
}

TEST_F(ConfigValidationTest, PermitsAccumulatingSettings) {
  config::ConfigInputs inputs;
  inputs.system.globals = {"--exclude=one", "--exclude=two", "--config=one", "--config=two"};
  EXPECT_THAT(ConfigOverrideNotices(inputs), IsEmpty());
}

TEST_F(ConfigValidationTest, KeyedSettingsAccumulateByNameAndReportSameNameOverrides) {
  config::ConfigInputs inputs;
  inputs.system.globals = {"--define=A=one", "--define=B=two"};
  EXPECT_THAT(ConfigOverrideNotices(inputs), IsEmpty());

  inputs.system.globals.emplace_back("--define=A=three");
  EXPECT_THAT(ConfigOverrideNotices(inputs), ElementsAre(HasSubstr("setting --define=A is overridden")));
}

TEST_F(ConfigValidationTest, ReportsOverridesWithinOneUserSection) {
  config::ConfigInputs inputs;
  inputs.user = config::ParseXffrc("[debug]\n--sort=tree\n--sort=none\n[other]\n--sort=global");
  EXPECT_THAT(ConfigOverrideNotices(inputs), ElementsAre(HasSubstr("user config section '[debug]'")));
}

TEST_F(ConfigValidationTest, KeepsDifferentSectionsAndFilesIndependent) {
  config::ConfigInputs inputs;
  inputs.user = config::ParseXffrc("[debug]\n--sort=tree\n[other]\n--sort=global");
  inputs.xffrc = {
      {.path = "/one", .config = config::ParseXffrc("--color=always")},
      {.path = "/two", .config = config::ParseXffrc("--color=never")},
  };
  EXPECT_THAT(ConfigOverrideNotices(inputs), IsEmpty());
}

TEST_F(ConfigValidationTest, DoesNotTreatPrimaryArgumentsAsGlobalSettings) {
  config::ConfigInputs inputs;
  inputs.user = config::ParseXffrc("-exec echo --sort=tree ; --sort=global");
  EXPECT_THAT(ConfigOverrideNotices(inputs), IsEmpty());
}

TEST_F(ConfigValidationTest, StopsRecognizingSettingsAfterDoubleDash) {
  config::ConfigInputs inputs;
  inputs.user = config::ParseXffrc("--sort=tree -- --sort=global");
  EXPECT_THAT(ConfigOverrideNotices(inputs), IsEmpty());
}

TEST_F(ConfigValidationTest, RepeatedNamesDisableEveryDeclarationAndDependentConfig) {
  const ConfigFileValidation validation = ValidateConfigFile(
      config::ParseIni(R"ini([dev]
--hidden
[outer]
--config=dev
[ dev ]
--color=never
[healthy]
-type f
)ini"),
      {"outer"}, "repeated.ini");
  EXPECT_THAT(validation.disabled_configs, ElementsAre("dev", "outer"));
  EXPECT_THAT(validation.config.named, ElementsAre(Field("name", &config::IniSection::name, "healthy")));
  EXPECT_THAT(
      validation.diagnostics, ElementsAre(
                                  AllOf(HasSubstr("repeated.ini:5"), HasSubstr("declared more than once")),
                                  HasSubstr("references disabled config [dev]")));
  EXPECT_THAT(validation.selected_configs_status, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ConfigValidationTest, RepeatedReferencesComposeWithoutRepeatingDeclarations) {
  const ConfigFileValidation validation = ValidateConfigFile(
      config::ParseIni(R"ini([dev]
--config=checks --config=checks
[checks]
--hidden
)ini"),
      {"dev"});
  EXPECT_THAT(validation.diagnostics, IsEmpty());
  EXPECT_THAT(validation.disabled_configs, IsEmpty());
  config::ConfigInputs inputs;
  inputs.system = validation.config;
  const auto resolved = config::ResolveConfigInOrder(inputs, {"--config=dev"}, "xff");
  EXPECT_THAT(
      resolved, ElementsAre(
                    Field("flag", &config::ResolvedFlag::flag, "--config=dev"),
                    Field("flag", &config::ResolvedFlag::flag, "--config=checks"),
                    Field("flag", &config::ResolvedFlag::flag, "--hidden"),
                    Field("flag", &config::ResolvedFlag::flag, "--config=checks")));
}

TEST_F(ConfigValidationTest, UserSectionsUseTheSameAtomicValidationAndDeclarationRules) {
  const auto validation = ValidateConfigFile(
      config::ParseXffrc(R"ini([dev]
--hidden
[dev]
--color=auto
[invalid]
-type garbage
[healthy]
--no-allow-xffrc
)ini"),
      {"dev"}, "user.ini", config::Source::kUser);
  EXPECT_THAT(validation.disabled_configs, ElementsAre("dev", "invalid"));
  EXPECT_THAT(validation.selected_configs_status, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(validation.config.named, ElementsAre(Field("name", &config::IniSection::name, "healthy")));
}

TEST_F(ConfigValidationTest, ExplicitSectionsUseTheSameAtomicValidationAndDeclarationRules) {
  const auto validation = ValidateConfigFile(
      config::ParseXffrc(R"ini([dev]
[dev]
[invalid]
-type garbage
[healthy]
-type f
)ini"),
      {"dev"}, "task.xffrc", config::Source::kXffrc);
  EXPECT_THAT(validation.disabled_configs, ElementsAre("dev", "invalid"));
  EXPECT_THAT(validation.selected_configs_status, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(validation.config.named, ElementsAre(Field("name", &config::IniSection::name, "healthy")));
}

TEST_F(ConfigValidationTest, EmptyNamesNeverBecomeUnconditionalDefaults) {
  const auto validation =
      ValidateConfigFile(config::ParseXffrc("[ ]\n--hidden"), {}, "user.ini", config::Source::kUser);
  EXPECT_THAT(validation.config.globals, IsEmpty());
  EXPECT_THAT(validation.config.named, IsEmpty());
  EXPECT_THAT(validation.diagnostics, ElementsAre(HasSubstr("user.ini:1: disabling empty config name")));
}

TEST_F(ConfigValidationTest, EmptyRepeatedDeclarationStillDisablesConfig) {
  const ConfigFileValidation validation = ValidateConfigFile(config::ParseIni("[dev]\n[dev]"), {"dev"});
  EXPECT_THAT(validation.disabled_configs, ElementsAre("dev"));
  EXPECT_THAT(validation.config.named, IsEmpty());
  EXPECT_THAT(validation.selected_configs_status, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ConfigValidationTest, NamedSectionsReportOverridesWithinTheirOwnScope) {
  config::ConfigInputs inputs;
  inputs.system = config::ParseIni(R"ini(
[first]
--color=auto
--color=never
[second]
--color=always
)ini");
  EXPECT_THAT(
      ConfigOverrideNotices(inputs),
      ElementsAre(AllOf(HasSubstr("setting --color is overridden"), HasSubstr("section '[first]'"))));
}

TEST_F(ConfigValidationTest, GroupedExpressionAndSequenceRemainValid) {
  const ConfigFileValidation validation =
      ValidateConfigFile(config::ParseIni("[grouped]\n( -name foo -o -name bar ) , -type f"), {"grouped"});
  EXPECT_THAT(validation.diagnostics, IsEmpty());
  EXPECT_THAT(validation.selected_configs_status, IsOk());
  config::ConfigInputs inputs;
  inputs.system = validation.config;
  ASSERT_OK_AND_ASSIGN(
      const parser::Command configured,
      ApplyResolvedConfig({}, config::ResolveConfigInOrder(inputs, {"--config=grouped"}, "xff")));
  ASSERT_THAT(configured.expression, NotNull());
  EXPECT_THAT(configured.expression->kind, parser::Expr::Kind::kComma);
}

TEST_F(ConfigValidationTest, UnterminatedExecDisablesOnlyItsSection) {
  const ConfigFileValidation validation =
      ValidateConfigFile(config::ParseIni("[broken]\n-exec echo {}\n[healthy]\n-type f"), {"broken"});
  EXPECT_THAT(validation.disabled_configs, ElementsAre("broken"));
  EXPECT_THAT(validation.diagnostics, ElementsAre(HasSubstr("requires a terminating ';' or '+'")));
  EXPECT_THAT(validation.selected_configs_status, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(validation.config.named, ElementsAre(Field("name", &config::IniSection::name, "healthy")));
}

TEST_F(ConfigValidationTest, PreservesProgrammaticallySuppliedGlobals) {
  config::ConfigFile config{.globals = {"--color=auto"}};
  const ConfigFileValidation validation = ValidateConfigFile(std::move(config), {});
  EXPECT_THAT(validation.config.globals, ElementsAre("--color=auto"));
}

TEST_F(ConfigValidationTest, SystemControlsDisableNamedSectionsAndFormerReservedNamesAreOrdinary) {
  ASSERT_OK_AND_ASSIGN(const std::string fixture, Fixture("controls"));
  const ConfigFileValidation validation =
      ValidateConfigFile(config::ParseIni(fixture), {"policy", "defaults", "global"});
  EXPECT_THAT(validation.selected_configs_status, IsOk());
  EXPECT_THAT(validation.config.globals, ElementsAre("--no-allow-exec", "--no-allow-xffrc"));
  EXPECT_THAT(validation.diagnostics, ElementsAre(HasSubstr("must precede every system config section")));
  EXPECT_THAT(validation.disabled_configs, ElementsAre("invalid"));
  EXPECT_THAT(validation.config.named, SizeIs(3));
}

TEST_F(ConfigValidationTest, SkipControlPairsKeepFirstGlobalDecisionAndDisableMisplacedSections) {
  ASSERT_OK_AND_ASSIGN(const std::string fixture, Fixture("skip_controls"));
  const ConfigFileValidation validation = ValidateConfigFile(config::ParseIni(fixture), {});
  EXPECT_THAT(validation.selected_configs_status, IsOk());
  EXPECT_THAT(
      validation.config.globals, ElementsAre("--require-system-config", "--require-user-config", "--color=auto"));
  EXPECT_THAT(validation.diagnostics, SizeIs(4));
  EXPECT_THAT(validation.config.named, IsEmpty());
  EXPECT_THAT(validation.disabled_configs, ElementsAre("system", "user"));
}

TEST_F(ConfigValidationTest, TrustedGlobalProhibitionSurvivesCliUserAndExplicitArming) {
  ASSERT_OK_AND_ASSIGN(const std::string fixture, Fixture("safety"));
  ASSERT_OK_AND_ASSIGN(const std::string user_fixture, Fixture("safety", "user.rc"));
  ASSERT_OK_AND_ASSIGN(const std::string task_fixture, Fixture("safety", "task.rc"));
  const ConfigFileValidation validation = ValidateConfigFile(config::ParseIni(fixture), {});
  EXPECT_THAT(validation.diagnostics, IsEmpty());
  config::ConfigInputs inputs;
  inputs.system = validation.config;
  inputs.user = config::ParseXffrc(user_fixture);
  inputs.xffrc = {{.path = "task.rc", .config = config::ParseXffrc(task_fixture)}};
  inputs.no_system_config = true;
  EXPECT_THAT(config::ValidateConfigSkips(inputs), IsOk());
  const bool armed = config::ArmedFromTrustedTier(inputs, {"--allow-exec"}, "--allow-exec");
  EXPECT_THAT(armed, IsFalse());
  const config::GateResult gated = config::GateConfig(inputs, armed);
  EXPECT_THAT(gated.config.user.globals, ElementsAre("--allow-exec"));
  EXPECT_THAT(
      gated.config.xffrc,
      ElementsAre(FieldsAre("task.rc", Field("global_lines", &config::ConfigFile::global_lines, SizeIs(2)))));
  EXPECT_THAT(gated.drops, SizeIs(2));
}

TEST_F(ConfigValidationTest, PrimaryArgumentsAreNeitherDependenciesNorSystemControls) {
  ASSERT_OK_AND_ASSIGN(const std::string fixture, Fixture("literal_arguments"));
  const ConfigFileValidation validation = ValidateConfigFile(config::ParseIni(fixture), {"literal"});
  EXPECT_THAT(validation.selected_configs_status, IsOk());
  EXPECT_THAT(validation.disabled_configs, ElementsAre("broken"));
  EXPECT_THAT(validation.config.named, ElementsAre(Field("name", &config::IniSection::name, "literal")));
  config::ConfigInputs inputs;
  inputs.system = validation.config;
  EXPECT_THAT(config::ValidateConfigSkips(inputs), IsOk());
}

TEST_F(ConfigValidationTest, ValidatesExactCliSpellingsValuesAndPrimaryArguments) {
  ASSERT_OK_AND_ASSIGN(const std::string fixture, Fixture("invalid_values"));
  const ConfigFileValidation validation = ValidateConfigFile(config::ParseIni(fixture), {});
  EXPECT_THAT(validation.config.globals, ElementsAre("--hidden"));
  EXPECT_THAT(validation.disabled_configs, ElementsAre("invalid_arity", "invalid_type"));
  EXPECT_THAT(validation.diagnostics, SizeIs(4));
}

TEST_F(ConfigValidationTest, MultipleDirectivesPerIniLineComposeWithTheCliExpression) {
  ASSERT_OK_AND_ASSIGN(const std::string fixture, Fixture("multiple"));
  const ConfigFileValidation validation = ValidateConfigFile(config::ParseIni(fixture), {"either"});
  EXPECT_THAT(validation.diagnostics, IsEmpty());
  config::ConfigInputs inputs;
  inputs.system = validation.config;
  const auto resolved = config::ResolveConfigInOrder(inputs, {"--config=either"}, "xff");
  ASSERT_OK_AND_ASSIGN(parser::Command original, parser::Parse({"/virtual", "-type", "f"}));
  ASSERT_OK_AND_ASSIGN(const parser::Command configured, ApplyResolvedConfig(std::move(original), resolved));
  EXPECT_THAT(configured.roots, ElementsAre("/virtual"));
  ASSERT_THAT(configured.expression, NotNull());
  EXPECT_THAT(configured.expression->kind, parser::Expr::Kind::kAnd);
  ASSERT_THAT(configured.expression->lhs, NotNull());
  EXPECT_THAT(configured.expression->lhs->kind, parser::Expr::Kind::kOr);
  ASSERT_THAT(configured.expression->rhs, NotNull());
  EXPECT_THAT(configured.expression->rhs->args, ElementsAre("f"));
}

TEST_F(ConfigValidationTest, MultipleGlobalsAndAPrimaryRemainSeparate) {
  ASSERT_OK_AND_ASSIGN(const std::string fixture, Fixture("multiple"));
  const ConfigFileValidation validation = ValidateConfigFile(config::ParseIni(fixture), {"mixed"});
  config::ConfigInputs inputs;
  inputs.system = validation.config;
  const auto resolved = config::ResolveConfigInOrder(inputs, {"--config=mixed"}, "xff");
  ASSERT_OK_AND_ASSIGN(const parser::Command configured, ApplyResolvedConfig({}, resolved));
  EXPECT_THAT(configured.globals, ElementsAre("--config=mixed", "--hidden", "--color=never"));
  ASSERT_THAT(configured.expression, NotNull());
  EXPECT_THAT(configured.expression->args, ElementsAre("foo"));
}

TEST_F(ConfigValidationTest, InvalidGlobalLineIsDiagnosedAndOnlyThatLineIsIgnored) {
  ASSERT_OK_AND_ASSIGN(const std::string fixture, Fixture("globals"));
  const ConfigFileValidation validation = ValidateConfigFile(config::ParseIni(fixture), {"dev"}, "globals/system.ini");

  EXPECT_THAT(validation.selected_configs_status, IsOk());
  EXPECT_THAT(validation.diagnostics, ElementsAre(AllOf(HasSubstr("globals/system.ini:5"), HasSubstr("development"))));
  EXPECT_THAT(
      validation.config.globals,
      ElementsAre("--no-require-system-config", "--no-require-user-config", "--color=auto", "--hidden"));
  ASSERT_THAT(validation.config.named, SizeIs(2));
}

TEST_F(ConfigValidationTest, InvalidNamedSectionIsAtomicAndDoesNotAffectSibling) {
  ASSERT_OK_AND_ASSIGN(const std::string fixture, Fixture("atomic"));
  const ConfigFileValidation validation =
      ValidateConfigFile(config::ParseIni(fixture), {"healthy"}, "atomic/system.ini");

  EXPECT_THAT(validation.selected_configs_status, IsOk());
  EXPECT_THAT(
      validation.diagnostics, ElementsAre(AllOf(
                                  HasSubstr("atomic/system.ini:5"), HasSubstr("disabling config [broken]"),
                                  HasSubstr("unexpected token 'development'"))));
  ASSERT_THAT(validation.config.named, SizeIs(1));
  EXPECT_THAT(validation.config.named[0].name, Eq("healthy"));
  EXPECT_THAT(validation.config.named[0].lines[0].tokens, ElementsAre("--color=never"));
}

TEST_F(ConfigValidationTest, DisablementPropagatesAndSelectingItIsAHardError) {
  ASSERT_OK_AND_ASSIGN(const std::string fixture, Fixture("transitive"));
  const ConfigFileValidation validation =
      ValidateConfigFile(config::ParseIni(fixture), {"outer"}, "transitive/system.ini");

  EXPECT_THAT(
      validation.selected_configs_status,
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("selected config [outer] is disabled")));
  EXPECT_THAT(validation.diagnostics, SizeIs(3));
  EXPECT_THAT(validation.diagnostics[0], HasSubstr("disabling config [broken]"));
  EXPECT_THAT(validation.diagnostics[1], AllOf(HasSubstr("[middle]"), HasSubstr("[broken]")));
  EXPECT_THAT(validation.diagnostics[2], AllOf(HasSubstr("[outer]"), HasSubstr("[middle]")));
  ASSERT_THAT(validation.config.named, SizeIs(1));
  EXPECT_THAT(validation.config.named[0].name, Eq("independent"));
}

}  // namespace
}  // namespace xff::cli
