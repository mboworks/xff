// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/cli/config_validation.h"

#include <array>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/cli/globals.h"
#include "xff/config/config.h"
#include "xff/config/policy.h"
#include "xff/config/safety.h"
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
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::NotNull;
using ::testing::SizeIs;

struct ConfigValidationTest : ::testing::Test {
  void TearDown() override { env::ClearForTesting(); }
};

TEST_F(ConfigValidationTest, ExplainProfilesRetainsInvalidAndComposedDeclarations) {
  const auto checked = ValidateConfigFile(
      config::ParseIni(R"ini(
[good]
--color=never -name '*.cc' -print
[bad]
-type garbage
[dependent]
--config=bad
)ini"),
      {}, "/profiles.ini", config::Source::kUser);
  EXPECT_THAT(checked.status, IsOk());
  EXPECT_THAT(checked.config.named, SizeIs(1));
  EXPECT_THAT(checked.profiles, SizeIs(3));
  const std::string out = ExplainProfiles(checked.profiles, {}, {"good"});
  EXPECT_THAT(out, HasSubstr("profile\tgood\tuser\tavailable\tselected\tdeclares=globals,predicates,actions"));
  EXPECT_THAT(out, HasSubstr("profile\tbad\tuser\tdisabled\tnot-selected"));
  EXPECT_THAT(out, HasSubstr("unknown value 'garbage'"));
  EXPECT_THAT(out, HasSubstr("references disabled config [bad]"));
}

TEST_F(ConfigValidationTest, ProfileDeclarationsIgnorePrimaryArgumentsAndRetainEmptySections) {
  const auto checked = ValidateConfigFile(
      config::ParseIni(R"ini(
[action]
-exec echo --color=never -name \;
[empty]
[duplicate]
-name x
[duplicate]
-name y
)ini"),
      {}, "/profiles.ini", config::Source::kUser);
  const std::string out = ExplainProfiles(checked.profiles, {}, {});
  EXPECT_THAT(out, HasSubstr("profile\taction\tuser\tavailable\tnot-selected\tdeclares=actions"));
  EXPECT_THAT(out, HasSubstr("profile\tempty\tuser\tavailable\tnot-selected\tdeclares=empty"));
  EXPECT_THAT(out, HasSubstr("name is declared more than once in this file"));
}

TEST_F(ConfigValidationTest, ExplainProfilesDistinguishesSkipsReservedNamesAndCrossFileDisablement) {
  const auto valid = ValidateConfigFile(config::ParseIni("[shared]\n-name x\n[xff]\n-print\n"), {}, "/system.ini");
  const auto invalid =
      ValidateConfigFile(config::ParseIni("[shared]\n-type garbage\n"), {}, "/user.ini", config::Source::kUser);
  std::vector<ConfigProfile> profiles = valid.profiles;
  profiles.insert(profiles.end(), invalid.profiles.begin(), invalid.profiles.end());
  EXPECT_THAT(ExplainProfiles(profiles, {}, {}), HasSubstr("disabled declaration of this name in another file"));
  config::ConfigInputs skipped;
  skipped.no_system_config = true;
  const std::string out = ExplainProfiles(valid.profiles, skipped, {});
  EXPECT_THAT(out, HasSubstr("profile\tshared\tsystem\tskipped"));
  EXPECT_THAT(out, HasSubstr("named sections excluded by config skip request"));
  EXPECT_THAT(out, HasSubstr("profile\txff\tsystem\treserved-preset"));
}

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

TEST_F(ConfigValidationTest, EnvironmentExpansionUsesSharedGrammarAndExistingValidation) {
  env::SetForTesting("XFF_INI_PATTERN", "space name;#literal");
  env::SetForTesting("XFF_INI_COLOR", std::nullopt);
  env::SetForTesting("XFF_INI_MISSING", std::nullopt);
  ASSERT_OK_AND_ASSIGN(const auto text, Fixture("environment", "shared.ini"));
  const auto sources =
      std::to_array<config::Source>({config::Source::kSystem, config::Source::kUser, config::Source::kXffrc});
  for (const auto source : sources) {
    const auto parsed = config::ParseXffrc(text);
    const auto valid = ValidateConfigFile(parsed, {"report"}, "shared.ini", source);
    EXPECT_THAT(valid.status, IsOk());
    EXPECT_THAT(valid.config.globals, ElementsAre("--color=never"));
    EXPECT_THAT(valid.disabled_configs, ElementsAre("missing"));
    EXPECT_THAT(valid.diagnostics, ElementsAre(AllOf(HasSubstr("shared.ini:8"), HasSubstr("set the pattern"))));
    ASSERT_THAT(valid.config.named, SizeIs(1));
    EXPECT_THAT(valid.config.named[0].lines[0].tokens, ElementsAre("-name", "space name;#literal"));
    EXPECT_THAT(valid.config.named[0].lines[1].tokens, ElementsAre("-printf", "%{env.XFF_INI_PATTERN}"));
    const auto invalid = ValidateConfigFile(parsed, {"missing"}, "shared.ini", source);
    EXPECT_THAT(invalid.status, StatusIs(absl::StatusCode::kInvalidArgument));
  }
  env::SetForTesting("XFF_INI_COLOR", "garbage");
  EXPECT_THAT(
      ValidateConfigFile(config::ParseIni(text), {}, "shared.ini").status,
      StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(
      ValidateConfigFile(
          config::ParseIni("--output-root=${XFF_INI_MISSING}"), {}, "system.ini", config::Source::kSystem)
          .status,
      StatusIs(absl::StatusCode::kInvalidArgument));
  ASSERT_OK_AND_ASSIGN(const auto cli, parser::Parse({".", "-name", "${XFF_INI_MISSING}"}));
  EXPECT_THAT(cli.globals, IsEmpty());
}

TEST_F(ConfigValidationTest, BufferLimitsValidateCompleteConfigFiles) {
  ASSERT_OK_AND_ASSIGN(const auto text, Fixture("buffer_bounds"));
  const auto sources =
      std::to_array<config::Source>({config::Source::kSystem, config::Source::kUser, config::Source::kXffrc});
  for (const auto source : sources) {
    const auto valid = ValidateConfigFile(config::ParseIni(text), {"good"}, "limits.ini", source);
    EXPECT_THAT(valid.status, IsOk());
    EXPECT_THAT(valid.disabled_configs, ElementsAre("bad-buffer", "overflow"));
    const auto sections = std::to_array<std::string_view>({"bad-buffer", "overflow"});
    for (const std::string_view name : sections) {
      EXPECT_THAT(
          ValidateConfigFile(config::ParseIni(text), {std::string(name)}, "limits.ini", source).status,
          StatusIs(absl::StatusCode::kInvalidArgument));
    }
    EXPECT_THAT(
        ValidateConfigFile(config::ParseIni("--buffer=garbage"), {}, "limits.ini", source).status,
        StatusIs(absl::StatusCode::kInvalidArgument));
  }
}

TEST_F(ConfigValidationTest, HistogramWidthsValidateCompleteConfigFiles) {
  ASSERT_OK_AND_ASSIGN(const auto text, Fixture("histogram_width"));
  const auto sources =
      std::to_array<config::Source>({config::Source::kSystem, config::Source::kUser, config::Source::kXffrc});
  for (const auto source : sources) {
    const auto valid = ValidateConfigFile(config::ParseIni(text), {"good"}, "limits.ini", source);
    EXPECT_THAT(valid.status, IsOk());
    EXPECT_THAT(valid.disabled_configs, ElementsAre("bad-width", "overflow"));
    const auto sections = std::to_array<std::string_view>({"bad-width", "overflow"});
    for (const std::string_view name : sections) {
      EXPECT_THAT(
          ValidateConfigFile(config::ParseIni(text), {std::string(name)}, "limits.ini", source).status,
          StatusIs(absl::StatusCode::kInvalidArgument));
    }
    EXPECT_THAT(
        ValidateConfigFile(config::ParseIni("--histogram-width=0"), {}, "limits.ini", source).status,
        StatusIs(absl::StatusCode::kInvalidArgument));
  }
}

TEST_F(ConfigValidationTest, DirectoryRootsAreGlobalTrustedAndSystemAuthoritative) {
  ASSERT_OK_AND_ASSIGN(const auto system_text, Fixture("directories"));
  ASSERT_OK_AND_ASSIGN(const auto user_text, Fixture("directories", "user.ini"));
  const auto system =
      ValidateConfigFile(config::ParseIni(system_text), {"report"}, "system.ini", config::Source::kSystem);
  const auto user = ValidateConfigFile(config::ParseIni(user_text), {"report"}, "user.ini", config::Source::kUser);
  EXPECT_THAT(system.diagnostics, IsEmpty());
  EXPECT_THAT(user.disabled_configs, ElementsAre("invalid-root"));
  EXPECT_THAT(user.status, IsOk());
  const config::ConfigInputs inputs{.system = system.config, .user = user.config};
  std::vector<std::string> globals;
  for (const auto& flag : config::ResolveConfigInOrder(inputs, {"--config=report", "--no-safe"}, "xff")) {
    globals.push_back(flag.flag);
  }
  const auto safety = config::ResolveSafety(globals, true);
  EXPECT_THAT(safety.output_root, Eq("/srv/xff/results"));
  EXPECT_THAT(safety.temp_root, Eq("/srv/xff/scratch"));
  EXPECT_THAT(safety.Blocks(config::Capability::kOutputFileOverwrite), Eq(true));
  EXPECT_THAT(safety.Blocks(config::Capability::kFileWriting), Eq(true));
  EXPECT_THAT(safety.Blocks(config::Capability::kOutputFileWriting), Eq(false));
}

TEST_F(ConfigValidationTest, DirectoryRootsRejectDuplicateAndExplicitFileDeclarations) {
  config::ConfigInputs duplicate;
  duplicate.system = config::ParseIni("--output-root=/first\n--output-root=/second");
  EXPECT_THAT(config::ValidateConfigSkips(duplicate), StatusIs(absl::StatusCode::kInvalidArgument));
  config::ConfigInputs explicit_file;
  explicit_file.xffrc = {{.path = "task.rc", .config = config::ParseIni("--temp-root=/scratch")}};
  EXPECT_THAT(config::ValidateConfigSkips(explicit_file), StatusIs(absl::StatusCode::kInvalidArgument));
  for (const std::string_view value :
       std::to_array<std::string_view>({"--output-root", "--temp-root=relative", "--output-root=/"})) {
    const auto invalid = ValidateConfigFile(config::ParseIni(value), {}, "user.ini", config::Source::kUser);
    EXPECT_THAT(invalid.diagnostics, SizeIs(1));
    EXPECT_THAT(invalid.config.globals, IsEmpty());
  }
}

TEST_F(ConfigValidationTest, FullFileQuotingUsesTheSameGrammarForEveryTier) {
  ASSERT_OK_AND_ASSIGN(const std::string fixture, Fixture("quoting", "shared.ini"));
  const auto sources =
      std::to_array<config::Source>({config::Source::kSystem, config::Source::kUser, config::Source::kXffrc});
  for (const config::Source source : sources) {
    const ConfigFileValidation validated =
        ValidateConfigFile(config::ParseIni(fixture), {"hash", "space", "equals", "command"}, "shared.ini", source);
    EXPECT_THAT(validated.diagnostics, IsEmpty());
    EXPECT_THAT(validated.status, IsOk());
    EXPECT_THAT(validated.config.globals, ElementsAre("--color=never", "--template=literal # text"));
    ASSERT_THAT(validated.config.named, SizeIs(4));
    EXPECT_THAT(validated.config.named[0].lines[0].tokens, ElementsAre("-name", "#*"));
    EXPECT_THAT(validated.config.named[1].lines[0].tokens, ElementsAre("-name", "space name"));
    EXPECT_THAT(validated.config.named[2].lines[0].tokens, ElementsAre("-name", "="));
    EXPECT_THAT(validated.config.named[3].lines[0].tokens, ElementsAre("-exec", "echo", "#literal", "#", "{}", ";"));
  }
}

TEST_F(ConfigValidationTest, UnterminatedQuotesDisableSelectedSectionWithItsStartingLine) {
  const ConfigFileValidation result =
      ValidateConfigFile(config::ParseIni("--hidden\n[broken]\n-name 'unfinished\n"), {"broken"}, "bad.ini");
  EXPECT_THAT(result.status, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(result.diagnostics, ElementsAre(AllOf(HasSubstr("bad.ini:3"), HasSubstr("unterminated single quote"))));
  EXPECT_THAT(result.config.globals, ElementsAre("--hidden"));
  EXPECT_THAT(result.config.named, IsEmpty());
}

TEST_F(ConfigValidationTest, InvalidGlobalSourceLinesCannotReturnDuringPolicyFiltering) {
  const ConfigFileValidation validated = ValidateConfigFile(
      config::ParseIni("--color=garbage\n--hidden\n-name 'unfinished"), {}, "task.xffrc", config::Source::kXffrc);
  EXPECT_THAT(validated.diagnostics, SizeIs(2));
  ASSERT_THAT(validated.config.global_lines, SizeIs(1));
  config::ConfigInputs inputs;
  inputs.xffrc = {{.path = "task.xffrc", .config = validated.config}};
  const config::GateResult gated = config::GateConfig(inputs, false);
  EXPECT_THAT(gated.config.xffrc[0].config.globals, ElementsAre("--hidden"));
}

TEST_F(ConfigValidationTest, SpacedAssignmentsAreNotAlternateFlagSyntax) {
  const ConfigFileValidation validated =
      ValidateConfigFile(config::ParseIni("--color = auto\n[empty]\n; comment\n"), {"empty"}, "bad.ini");
  EXPECT_THAT(validated.diagnostics, SizeIs(1));
  EXPECT_THAT(validated.config.globals, IsEmpty());
  EXPECT_THAT(validated.status, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ConfigValidationTest, ExecTerminationAndCommentsUseDistinctTokens) {
  const ConfigFileValidation validated = ValidateConfigFile(
      config::ParseIni(R"ini([escaped]
-exec echo x \; ; comment
[quoted]
-exec echo x ";" # comment
[trailing]
-exec echo \; bla
[missing]
-exec echo x ; comment
)ini"),
      {"escaped", "quoted"}, "exec.ini");
  EXPECT_THAT(validated.status, IsOk());
  ASSERT_THAT(validated.config.named, SizeIs(2));
  EXPECT_THAT(validated.config.named[0].lines[0].tokens, ElementsAre("-exec", "echo", "x", ";"));
  EXPECT_THAT(validated.config.named[1].lines[0].tokens, ElementsAre("-exec", "echo", "x", ";"));
  EXPECT_THAT(
      validated.diagnostics, ElementsAre(AllOf(HasSubstr("[trailing]"), HasSubstr("bla")), HasSubstr("[missing]")));
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
  inputs.user = config::ParseXffrc("-exec echo --sort=tree \\; --sort=global");
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
  EXPECT_THAT(validation.status, StatusIs(absl::StatusCode::kInvalidArgument));
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
  EXPECT_THAT(validation.status, StatusIs(absl::StatusCode::kInvalidArgument));
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
  EXPECT_THAT(validation.status, StatusIs(absl::StatusCode::kInvalidArgument));
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
  EXPECT_THAT(validation.status, StatusIs(absl::StatusCode::kInvalidArgument));
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
  EXPECT_THAT(validation.status, IsOk());
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
  EXPECT_THAT(validation.status, StatusIs(absl::StatusCode::kInvalidArgument));
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
  EXPECT_THAT(validation.status, IsOk());
  EXPECT_THAT(validation.config.globals, ElementsAre("--block-execution", "--no-allow-xffrc"));
  EXPECT_THAT(validation.diagnostics, ElementsAre(HasSubstr("must precede every system config section")));
  EXPECT_THAT(validation.disabled_configs, ElementsAre("invalid"));
  EXPECT_THAT(validation.config.named, SizeIs(3));
}

TEST_F(ConfigValidationTest, SkipControlPairsKeepFirstGlobalDecisionAndDisableMisplacedSections) {
  ASSERT_OK_AND_ASSIGN(const std::string fixture, Fixture("skip_controls"));
  const ConfigFileValidation validation = ValidateConfigFile(config::ParseIni(fixture), {});
  EXPECT_THAT(validation.status, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(
      validation.config.globals, ElementsAre("--require-system-globals", "--require-user-globals", "--color=auto"));
  EXPECT_THAT(validation.diagnostics, SizeIs(4));
  EXPECT_THAT(validation.config.named, IsEmpty());
  EXPECT_THAT(validation.disabled_configs, ElementsAre("system", "user"));
}

TEST_F(ConfigValidationTest, PrimaryArgumentsAreNeitherDependenciesNorSystemControls) {
  ASSERT_OK_AND_ASSIGN(const std::string fixture, Fixture("literal_arguments"));
  const ConfigFileValidation validation = ValidateConfigFile(config::ParseIni(fixture), {"literal"});
  EXPECT_THAT(validation.status, IsOk());
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

TEST_F(ConfigValidationTest, InvalidGlobalLineFailsValidation) {
  ASSERT_OK_AND_ASSIGN(const std::string fixture, Fixture("globals"));
  const ConfigFileValidation validation = ValidateConfigFile(config::ParseIni(fixture), {"dev"}, "globals/system.ini");

  EXPECT_THAT(validation.status, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(validation.diagnostics, ElementsAre(AllOf(HasSubstr("globals/system.ini:5"), HasSubstr("development"))));
  EXPECT_THAT(
      validation.config.globals,
      ElementsAre("--no-require-system-globals", "--no-require-user-globals", "--color=auto", "--hidden"));
  ASSERT_THAT(validation.config.named, SizeIs(2));
}

TEST_F(ConfigValidationTest, InvalidNamedSectionIsAtomicAndDoesNotAffectSibling) {
  ASSERT_OK_AND_ASSIGN(const std::string fixture, Fixture("atomic"));
  const ConfigFileValidation validation =
      ValidateConfigFile(config::ParseIni(fixture), {"healthy"}, "atomic/system.ini");

  EXPECT_THAT(validation.status, IsOk());
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
      validation.status,
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("selected config [outer] is disabled")));
  EXPECT_THAT(validation.diagnostics, SizeIs(3));
  EXPECT_THAT(validation.diagnostics[0], HasSubstr("disabling config [broken]"));
  EXPECT_THAT(validation.diagnostics[1], AllOf(HasSubstr("[middle]"), HasSubstr("[broken]")));
  EXPECT_THAT(validation.diagnostics[2], AllOf(HasSubstr("[outer]"), HasSubstr("[middle]")));
  ASSERT_THAT(validation.config.named, SizeIs(1));
  EXPECT_THAT(validation.config.named[0].name, Eq("independent"));
}

TEST_F(ConfigValidationTest, DetailedPolicyRequiresAnExplicitListEvenWhenEmpty) {
  const auto result = ValidateConfigFile(config::ParseIni("--block-policy-categories"), {}, "system.ini");
  EXPECT_THAT(result.config.globals, IsEmpty());
  EXPECT_THAT(result.diagnostics, ElementsAre(HasSubstr("requires =LIST")));
}

TEST_F(ConfigValidationTest, ApplyingSelectionsMustExistInActiveFiles) {
  config::ConfigInputs inputs;
  inputs.system = config::ParseIni("[system]");
  inputs.user = config::ParseIni("[user]\n[xff:2]");
  inputs.xffrc = {{.path = "task", .config = config::ParseIni("[explicit]")}};
  const auto resolve = [&](const std::vector<std::string>& globals) {
    return ValidateConfigSelections(inputs, config::ResolveConfigInOrder(inputs, globals, "custom-alias"));
  };
  EXPECT_THAT(resolve({"--config=system", "--config=user", "--config=explicit", "--config=xff:2"}), IsOk());
  EXPECT_THAT(resolve({"--config=missing"}), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("[missing]")));
  inputs.no_system_config = true;
  EXPECT_THAT(resolve({"--config=system"}), StatusIs(absl::StatusCode::kInvalidArgument));
  inputs.no_user_config = true;
  EXPECT_THAT(resolve({"--config=user"}), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(resolve({"--config=explicit", "--config=find", "--config=rg"}), IsOk());
}

TEST_F(ConfigValidationTest, CombinedSkipMakesOnlyTrustedNamedSectionsUnavailable) {
  config::ConfigInputs inputs;
  inputs.system = config::ParseIni("[system]\n--hidden");
  inputs.user = config::ParseIni("[user]\n--sort");
  inputs.xffrc = {{.path = "explicit", .config = config::ParseIni("[explicit]\n--color=never")}};
  inputs.no_config = true;
  EXPECT_THAT(
      ValidateConfigSelections(inputs, {{.flag = "--config=system"}}), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(
      ValidateConfigSelections(inputs, {{.flag = "--config=user"}}), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(ValidateConfigSelections(inputs, {{.flag = "--config=explicit"}}), IsOk());
}

TEST_F(ConfigValidationTest, CompositionMayReferenceLaterFilesButNotMissingNames) {
  config::ConfigInputs inputs;
  inputs.system = config::ParseIni("[outer]\n--config=later");
  inputs.user = config::ParseIni("[later]\n--hidden");
  const auto resolve = [&] {
    return ValidateConfigSelections(inputs, config::ResolveConfigInOrder(inputs, {"--config=outer"}, "xff"));
  };
  EXPECT_THAT(resolve(), IsOk());
  inputs.user = config::ParseIni("[later]\n--config=typo");
  EXPECT_THAT(resolve(), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("[typo]")));
}

TEST_F(ConfigValidationTest, InertSectionsAndPrimaryArgumentsDoNotSelectConfigs) {
  config::ConfigInputs inputs;
  inputs.user = config::ParseIni("[inert]\n--config=missing\n[literal]\n-printf '--config=missing'");
  EXPECT_THAT(
      ValidateConfigSelections(inputs, config::ResolveConfigInOrder(inputs, {"--config=literal"}, "alias")), IsOk());
}

TEST_F(ConfigValidationTest, NamedRootsAreRejectedInEveryConfigSource) {
  const auto sources =
      std::to_array<config::Source>({config::Source::kSystem, config::Source::kUser, config::Source::kXffrc});
  for (const auto source : sources) {
    const auto globals = ValidateConfigFile(config::ParseIni("--root=source=/tree"), {}, "input.ini", source);
    EXPECT_THAT(globals.status, StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("command-line only")));
    const auto named =
        ValidateConfigFile(config::ParseIni("[pack]\n--root=source=/tree"), {"pack"}, "input.ini", source);
    EXPECT_THAT(named.status, StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(named.disabled_configs, ElementsAre("pack"));
  }
}

TEST_F(ConfigValidationTest, BootstrapFlagsAreRejectedInGlobalsAndDisableNamedSections) {
  for (const GlobalFlag& flag : Globals()) {
    if (!flag.cli_only) {
      continue;
    }
    const std::string argument = flag.name == "--xffrc" ? "--xffrc=task" : std::string(flag.name);
    const auto globals = ValidateConfigFile(config::ParseIni(argument), {});
    EXPECT_THAT(globals.status, StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("command-line only")));
    const auto named = ValidateConfigFile(config::ParseIni(absl::StrCat("[profile]\n", argument)), {"profile"});
    EXPECT_THAT(named.status, StatusIs(absl::StatusCode::kInvalidArgument));
    EXPECT_THAT(named.disabled_configs, ElementsAre("profile"));
  }
}

}  // namespace
}  // namespace xff::cli
