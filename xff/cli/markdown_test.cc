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

#include "xff/cli/markdown.h"

#include <cstddef>
#include <string>

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/cli/globals.h"
#include "xff/registry/registry.h"

namespace xff::cli {
namespace {

using ::testing::AllOf;
using ::testing::Eq;
using ::testing::HasSubstr;

struct MarkdownTest : ::testing::Test {};

TEST_F(MarkdownTest, HasTitleAndSectionHeadings) {
  EXPECT_THAT(
      MarkdownReference(), AllOf(
                               HasSubstr("# xff"), HasSubstr("## Options"), HasSubstr("### Config"),
                               HasSubstr("## Expression"), HasSubstr("### Tests"), HasSubstr("### Operators")));
}

TEST_F(MarkdownTest, DocumentsEveryGlobalAndPrimaryAsCode) {
  const std::string doc = MarkdownReference();
  for (const GlobalFlag& flag : Globals()) {
    EXPECT_THAT(doc, HasSubstr(absl::StrCat("`", flag.display, "`"))) << flag.name;
  }
  for (const registry::Descriptor& descriptor : registry::All()) {
    EXPECT_THAT(doc, HasSubstr(absl::StrCat("`", descriptor.name))) << descriptor.name;
  }
}

TEST_F(MarkdownTest, TagsEntriesWithTheirClassification) {
  // Accepted modes are explicit; native primaries require --xff during an rg search.
  EXPECT_THAT(MarkdownReference(), HasSubstr("_(global, find, xff, rg)_"));
  EXPECT_THAT(MarkdownReference(), HasSubstr("_(test, find, xff)_"));
}

TEST_F(MarkdownTest, TopicFlagsAreNotDuplicatedInTheFullReference) {
  // A topic-tagged flag is documented once - in its grouped Options entry. The topic section
  // folded into the full reference contributes narrative + examples, not a second per-flag entry
  // (an entry begins with "- `<display>`"; inline prose mentions differ). Checked on the stats
  // flags: the config topic's "Layers" table intentionally names `--xffrc=FILE` as a tier row,
  // which is separate explanatory narrative, not the flag-list duplication this guards against.
  const std::string doc = MarkdownReference();
  const auto entry_count = [&doc](const GlobalFlag& flag) {
    const std::string entry = absl::StrCat("- `", flag.display, "`");
    std::size_t count = 0;
    for (std::size_t pos = doc.find(entry); pos != std::string::npos; pos = doc.find(entry, pos + 1)) {
      ++count;
    }
    return count;
  };
  for (const GlobalFlag& flag : Globals()) {
    if (flag.topic == "stats") {
      EXPECT_THAT(entry_count(flag), Eq(std::size_t{1})) << flag.name;
    }
  }
}

}  // namespace
}  // namespace xff::cli
