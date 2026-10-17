// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "xff/license/license.h"

namespace xff::license {
namespace {

using ::mbo::testing::EqualsText;
using ::mbo::testing::WithDropIndent;
using ::testing::Ne;

struct LicenseSectionTest : ::testing::Test {};

TEST_F(LicenseSectionTest, NoticeTextSeparatesAnExtensionFromTheMainProgram) {
  // Registration is process-global, so this check runs in its own test binary.
  Register({
      .section = "example",
      .component = "Example extension",
      .spdx = "BSD-3-Clause",
      .text = "Copyright the example authors.",
  });

  const std::string text = NoticeText();
  const std::size_t separator = text.find("\n--- Build extension: example ---\n");
  ASSERT_THAT(separator, Ne(std::string::npos));
  EXPECT_THAT(text.substr(separator + 1), WithDropIndent(EqualsText(R"out(
        --- Build extension: example ---

        Example extension  [BSD-3-Clause]
          Copyright the example authors.
      )out")));
}

}  // namespace
}  // namespace xff::license
