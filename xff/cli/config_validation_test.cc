// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/cli/config_validation.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/config/config.h"
#include "xff/config/xffrc.h"

namespace xff::cli {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

struct ConfigValidationTest : ::testing::Test {};

TEST_F(ConfigValidationTest, ReportsCanonicalAliasAndNegatedOverrides) {
  config::ConfigInputs inputs;
  inputs.system.defaults = {"--timezone=utc", "--tz=local"};
  EXPECT_THAT(ConfigOverrideNotices(inputs), ElementsAre(HasSubstr("setting --timezone is overridden")));

  inputs.system.defaults = {"--hidden", "--no-hidden"};
  EXPECT_THAT(ConfigOverrideNotices(inputs), ElementsAre(HasSubstr("setting --hidden is overridden")));
}

TEST_F(ConfigValidationTest, PermitsAccumulatingSettings) {
  config::ConfigInputs inputs;
  inputs.system.defaults = {"--exclude=one", "--exclude=two", "--config=one", "--config=two"};
  EXPECT_THAT(ConfigOverrideNotices(inputs), IsEmpty());
}

TEST_F(ConfigValidationTest, KeyedSettingsAccumulateByNameAndReportSameNameOverrides) {
  config::ConfigInputs inputs;
  inputs.system.defaults = {"--define=A=one", "--define=B=two"};
  EXPECT_THAT(ConfigOverrideNotices(inputs), IsEmpty());

  inputs.system.defaults.emplace_back("--define=A=three");
  EXPECT_THAT(ConfigOverrideNotices(inputs), ElementsAre(HasSubstr("setting --define=A is overridden")));
}

TEST_F(ConfigValidationTest, CombinesRepeatedLogicalUserSections) {
  config::ConfigInputs inputs;
  inputs.user = config::ParseXffrc("debug: --sort=tree\nother: --sort=global\ndebug: --sort=none");
  EXPECT_THAT(ConfigOverrideNotices(inputs), ElementsAre(HasSubstr("user config section 'debug:'")));
}

TEST_F(ConfigValidationTest, KeepsDifferentSectionsAndFilesIndependent) {
  config::ConfigInputs inputs;
  inputs.user = config::ParseXffrc("debug: --sort=tree\nother: --sort=global");
  inputs.xffrc = {
      {.path = "/one", .lines = config::ParseXffrc("common: --color=always")},
      {.path = "/two", .lines = config::ParseXffrc("common: --color=never")},
  };
  EXPECT_THAT(ConfigOverrideNotices(inputs), IsEmpty());
}

TEST_F(ConfigValidationTest, DoesNotTreatPrimaryArgumentsAsGlobalSettings) {
  config::ConfigInputs inputs;
  inputs.user = config::ParseXffrc("common: -exec echo --sort=tree ; --sort=global");
  EXPECT_THAT(ConfigOverrideNotices(inputs), IsEmpty());
}

TEST_F(ConfigValidationTest, StopsRecognizingSettingsAfterDoubleDash) {
  config::ConfigInputs inputs;
  inputs.user = config::ParseXffrc("common: --sort=tree -- --sort=global");
  EXPECT_THAT(ConfigOverrideNotices(inputs), IsEmpty());
}

}  // namespace
}  // namespace xff::cli
