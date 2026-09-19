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

#include "xff/license/license.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"

namespace xff::license {
namespace {

using ::mbo::testing::EqualsText;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsTrue;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::StartsWith;

// Reads a data-dep file from the test's runfiles, or an empty optional if unavailable.
std::string ReadRunfile(std::string_view relative) {
  // Bazel's runfiles env vars, read once in a single-threaded test; getenv is safe here.
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const char* const srcdir = std::getenv("TEST_SRCDIR");
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  const char* const workspace = std::getenv("TEST_WORKSPACE");
  EXPECT_THAT(srcdir, NotNull());
  EXPECT_THAT(workspace, NotNull());
  const std::ifstream file(absl::StrCat(srcdir, "/", workspace, "/", relative));
  EXPECT_THAT(file.is_open(), IsTrue()) << relative;
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

testing::Matcher<Notice> ComponentIs(const std::string& name) {
  return Field("component", &Notice::component, name);
}

struct LicenseTest : ::testing::Test {};

TEST_F(LicenseTest, NoticesIncludeTheAlwaysLinkedCoreDeps) {
  const std::vector<Notice> notices = Notices();
  EXPECT_THAT(notices, Contains(ComponentIs("Abseil (C++)")));
  EXPECT_THAT(notices, Contains(ComponentIs("RE2")));
  EXPECT_THAT(notices, Contains(ComponentIs("mboworks/mbo")));
  for (const Notice& notice : notices) {
    EXPECT_THAT(notice.spdx, Not(IsEmpty())) << notice.component;
    EXPECT_THAT(notice.text, Not(IsEmpty())) << notice.component;
  }
}

TEST_F(LicenseTest, NoticesAreSortedByComponentForDeterministicOutput) {
  const std::vector<Notice> notices = Notices();
  EXPECT_THAT(
      std::ranges::is_sorted(
          notices, [](const Notice& lhs, const Notice& rhs) { return lhs.component < rhs.component; }),
      IsTrue());
}

TEST_F(LicenseTest, CopyrightNoticeStatesTheOwnerAndGrant) {
  const std::string_view text = CopyrightNotice();
  EXPECT_THAT(
      std::string(text),
      AllOf(
          HasSubstr("xff - eXtended File Find"), HasSubstr("Copyright M. Boerger, the MBO Works authors"),
          HasSubstr("Licensed under the Apache License, Version 2.0.")));
}

TEST_F(LicenseTest, NoticeTextLeadsWithTheCopyrightNoticeThenComponents) {
  const std::string text = NoticeText();
  EXPECT_THAT(text, StartsWith(CopyrightNotice()));  // the shared attribution header
  EXPECT_THAT(text, HasSubstr("RE2"));               // a component
  EXPECT_THAT(text, HasSubstr("BSD-3-Clause"));      // its SPDX id
  EXPECT_THAT(text, HasSubstr("Apache-2.0"));        // a core dep's SPDX id
}

TEST_F(LicenseTest, LicenseTextIsTheApacheLicenseInFull) {
  const std::string_view text = LicenseText();
  EXPECT_THAT(std::string(text), AllOf(HasSubstr("Apache License"), HasSubstr("Version 2.0")));
}

TEST_F(LicenseTest, CommittedLicenseFileEqualsLicenseText) {
  // LicenseText is generated from //:LICENSE, so this confirms the embed round-trips exactly (e.g.
  // the raw-string delimiter never collides with the license text).
  EXPECT_THAT(ReadRunfile("LICENSE"), EqualsText(std::string(LicenseText())));
}

TEST_F(LicenseTest, TheApacheBodyIsRetrievableByItsSpdxId) {
  // Keyed by SPDX rather than by component, so the one embedded text answers for every Apache-2.0
  // component (xff itself, Abseil, mboworks/mbo) instead of being duplicated per name.
  EXPECT_THAT(std::string(LicenseBodyFor("Apache-2.0")), EqualsText(std::string(LicenseText())));
}

TEST_F(LicenseTest, UnicodeDependencyRetainsItsCompleteUpstreamLicense) {
  EXPECT_THAT(NoticeText(), HasSubstr("utf8proc"));
  EXPECT_THAT(LicenseBodyFor("LicenseRef-utf8proc"), HasSubstr("Public Software Group"));
  EXPECT_THAT(LicenseBodyFor("LicenseRef-utf8proc"), HasSubstr("Unicode data license"));
}

TEST_F(LicenseTest, TheRe2LicenseBodyIsRetrievableByItsSpdxId) {
  EXPECT_THAT(LicenseBodyFor("BSD-3-Clause"), HasSubstr("Redistribution and use"));
  EXPECT_THAT(LicenseBodyFor("no-such-license"), IsEmpty());
}

TEST_F(LicenseTest, RegisteringOneSpdxTwiceKeepsTheFirst) {
  // Two components naming one license is the expected case, so a second registration must not
  // replace the first - the result would otherwise depend on static-init order across TUs.
  RegisterLicenseBody({.spdx = "Apache-2.0", .text = "not the real text"});
  EXPECT_THAT(std::string(LicenseBodyFor("Apache-2.0")), EqualsText(std::string(LicenseText())));
}

}  // namespace
}  // namespace xff::license
