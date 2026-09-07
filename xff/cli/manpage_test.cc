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

#include "xff/cli/manpage.h"

#include <string>

#include "absl/strings/str_replace.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/cli/globals.h"
#include "xff/registry/registry.h"

namespace xff::cli {
namespace {

using ::testing::AllOf;
using ::testing::HasSubstr;
using ::testing::StartsWith;

// Undo the roff hyphen/backslash escaping so assertions can match plain names.
std::string Plain(const std::string& page) {
  return absl::StrReplaceAll(page, {{"\\-", "-"}, {"\\\\", "\\"}});
}

struct ManPageTest : ::testing::Test {};

TEST_F(ManPageTest, HasTheStandardManSections) {
  const std::string page = ManPage();
  EXPECT_THAT(
      page, AllOf(
                HasSubstr(".TH xff 1"), HasSubstr(".SH NAME"), HasSubstr(".SH SYNOPSIS"), HasSubstr(".SH DESCRIPTION"),
                HasSubstr(".SH OPTIONS"), HasSubstr(".SH EXPRESSION"), HasSubstr(".SH EXIT STATUS"),
                HasSubstr(".SH SEE ALSO")));
}

TEST_F(ManPageTest, UsesTheInvokedProgramName) {
  const std::string page = ManPage("xff_full");
  EXPECT_THAT(page, StartsWith(".TH xff_full 1"));
  EXPECT_THAT(page, HasSubstr("\n.SH NAME\nxff_full \\- eXtended File Find,"));
  EXPECT_THAT(page, HasSubstr("\n.B xff_full\n"));
}

TEST_F(ManPageTest, GroupsOptionsAndExpressionsIntoSubsections) {
  const std::string plain = Plain(ManPage());
  EXPECT_THAT(
      plain,
      AllOf(HasSubstr(".SS Config"), HasSubstr(".SS Traversal"), HasSubstr(".SS Tests"), HasSubstr(".SS Operators")));
}

TEST_F(ManPageTest, DocumentsEveryGlobalAndPrimary) {
  const std::string plain = Plain(ManPage());
  for (const GlobalFlag& flag : Globals()) {
    EXPECT_THAT(plain, HasSubstr(flag.name)) << flag.name;
  }
  for (const registry::Descriptor& descriptor : registry::All()) {
    EXPECT_THAT(plain, HasSubstr(descriptor.name)) << descriptor.name;
  }
}

TEST_F(ManPageTest, TagsEntriesWithTheirClassification) {
  // Flags are tagged (global, xff|find); primaries (kind, xff|find, [safety]).
  EXPECT_THAT(ManPage(), HasSubstr("(global, xff)"));
  EXPECT_THAT(ManPage(), HasSubstr("(test, find)"));
}

}  // namespace
}  // namespace xff::cli
