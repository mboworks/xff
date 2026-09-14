// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/cli/html.h"

#include <cstddef>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/cli/globals.h"
#include "xff/cli/html_backend.h"
#include "xff/registry/registry.h"

namespace xff::cli {
namespace {

using ::testing::AllOf;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsTrue;
using ::testing::Not;

struct HtmlTest : ::testing::Test {};

TEST_F(HtmlTest, HasStandaloneDocumentAndReferenceSections) {
  EXPECT_THAT(
      HtmlReference(),
      AllOf(
          HasSubstr("<!doctype html>"), HasSubstr("<meta charset=\"utf-8\">"), HasSubstr("<section id=\"options\">"),
          HasSubstr("<section id=\"topic-expressions\">"), HasSubstr("</html>")));
}

TEST_F(HtmlTest, DocumentsEveryGlobalAndPrimary) {
  const std::string doc = HtmlReference();
  for (const GlobalFlag& flag : Globals()) {
    EXPECT_THAT(doc, HasSubstr(absl::StrCat("<code>", HtmlEscape(flag.display), "</code>"))) << flag.name;
  }
  for (const registry::Descriptor& descriptor : registry::All()) {
    EXPECT_THAT(doc, HasSubstr(absl::StrCat("<code>", descriptor.name))) << descriptor.name;
  }
}

TEST_F(HtmlTest, ContainsNoExternalRuntimeAssets) {
  const std::string doc = HtmlReference();
  EXPECT_THAT(doc, Not(HasSubstr("<script")));
  EXPECT_THAT(doc, Not(HasSubstr("<link")));
}

TEST_F(HtmlTest, UsesNonemptyUniqueAnchors) {
  constexpr std::string_view kPrefix = " id=\"";
  const std::string doc = HtmlReference();
  absl::flat_hash_set<std::string_view> anchors;
  std::size_t pos = 0;
  while ((pos = doc.find(kPrefix, pos)) != std::string::npos) {
    const std::size_t begin = pos + kPrefix.size();
    const std::size_t end = doc.find('"', begin);
    ASSERT_THAT(end != std::string::npos, IsTrue());
    const std::string_view anchor = std::string_view(doc).substr(begin, end - begin);
    EXPECT_THAT(anchor, Not(IsEmpty()));
    EXPECT_THAT(anchors.insert(anchor).second, IsTrue()) << anchor;
    pos = end + 1;
  }
}

TEST_F(HtmlTest, CanonicalTopicAnchorsBelongToTheTopicSections) {
  const std::string doc = HtmlReference();
  EXPECT_THAT(doc, HasSubstr("<section id=\"topic-time\">\n<h2>Time formats</h2>"));
  EXPECT_THAT(doc, HasSubstr("<section id=\"topic-compare\">\n<h2>Comparing trees</h2>"));
  EXPECT_THAT(doc, HasSubstr("<section id=\"topic-grammars\">\n<h2>Regex grammars</h2>"));
  EXPECT_THAT(doc, HasSubstr("<section id=\"topic-stats\">\n<h2>Statistics</h2>"));
}

}  // namespace
}  // namespace xff::cli
