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

#include "xff/license/notice.h"

#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::license {
namespace {

using ::testing::AllOf;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::IsEmpty;

// Registered at FILE SCOPE, out of alphabetical order on purpose: this is how real components
// register (a file-scope Registrar per translation unit), and the sort below must not depend on the
// order registration happened in.
const Registrar kZulu{{.component = "zulu-codec", .spdx = "MIT", .text = "Copyright (c) Zulu."}};
const Registrar kAlpha{{.component = "alpha-codec", .spdx = "BSD-2-Clause", .text = "Copyright (c) Alpha."}};
const Registrar kExtraLibrary{
    {.section = "example extra", .component = "extra-library", .spdx = "Zlib", .text = "Library."}};
const Registrar kExtraLead{{
    .section = "example extra",
    .section_lead = true,
    .component = "example extra",
    .spdx = "Apache-2.0",
    .text = "Extension.",
}};

struct NoticeTest : ::testing::Test {};

TEST_F(NoticeTest, NoticesAreSortedByComponentRegardlessOfRegistrationOrder) {
  // The whole point of sorting: static-init order across translation units is unspecified, so
  // without this the generated NOTICE file would reorder itself between builds and the committed
  // copy would drift for no reason.
  std::vector<std::string_view> components;
  for (const Notice& notice : Notices()) {
    components.push_back(notice.component);
  }
  EXPECT_THAT(components, ElementsAre("alpha-codec", "zulu-codec", "example extra", "extra-library"));
}

TEST_F(NoticeTest, AnExtensionNoticeLeadsItsLibraries) {
  EXPECT_THAT(
      Notices(),
      Contains(AllOf(
          Field("section", &Notice::section, "example extra"), Field("section_lead", &Notice::section_lead, true),
          Field("component", &Notice::component, "example extra"))));
}

TEST_F(NoticeTest, ARegistrarContributesTheWholeNotice) {
  // Not just the name: the SPDX id and the notice text are what a NOTICE file must reproduce, so a
  // component registering only half of it would be a compliance bug.
  EXPECT_THAT(
      Notices(),
      Contains(AllOf(
          Field("component", &Notice::component, "alpha-codec"), Field("spdx", &Notice::spdx, "BSD-2-Clause"),
          Field("text", &Notice::text, "Copyright (c) Alpha."))));
}

TEST_F(NoticeTest, RegisterAddsToTheProcessWideSetAndStaysSorted) {
  // Register() is the non-Registrar entry point; a later registration must slot into the sort order
  // rather than append, so ordering cannot depend on when a component was linked in.
  Register({.component = "middle-codec", .spdx = "Zlib", .text = "Copyright (c) Middle."});
  std::vector<std::string_view> components;
  for (const Notice& notice : Notices()) {
    components.push_back(notice.component);
  }
  EXPECT_THAT(components, ElementsAre("alpha-codec", "middle-codec", "zulu-codec", "example extra", "extra-library"));
}

TEST_F(NoticeTest, NoticesReturnsACopySoCallersCannotCorruptTheRegistry) {
  // Callers assemble text from this; mutating their copy must not affect the next caller.
  std::vector<Notice> first = Notices();
  const std::vector<Notice>::size_type before = first.size();
  first.clear();
  EXPECT_THAT(Notices().size(), before);
}

TEST_F(NoticeTest, LicenseBodiesAreSortedAndFoundBySpdxIdentifier) {
  RegisterLicenseBody({.spdx = "Zlib-test", .text = "zlib body"});
  RegisterLicenseBody({.spdx = "Apache-test", .text = "apache body"});

  EXPECT_THAT(
      LicenseBodies(),
      Contains(
          AllOf(Field("spdx", &LicenseBody::spdx, "Apache-test"), Field("text", &LicenseBody::text, "apache body"))));
  EXPECT_THAT(LicenseBodyFor("Apache-test"), "apache body");
  EXPECT_THAT(LicenseBodyFor("missing-test"), IsEmpty());
}

TEST_F(NoticeTest, DuplicateLicenseBodyKeepsTheFirstRegistration) {
  RegisterLicenseBody({.spdx = "First-wins-test", .text = "first body"});
  RegisterLicenseBody({.spdx = "First-wins-test", .text = "second body"});

  EXPECT_THAT(LicenseBodyFor("First-wins-test"), "first body");
}

TEST_F(NoticeTest, ABodyRegistrarContributesItsWholeEntry) {
  const LicenseBodyRegistrar registrar{{.spdx = "Registrar-test", .text = "registered body"}};
  EXPECT_THAT(
      LicenseBodies(),
      Contains(AllOf(
          Field("spdx", &LicenseBody::spdx, "Registrar-test"), Field("text", &LicenseBody::text, "registered body"))));
}

}  // namespace
}  // namespace xff::license
