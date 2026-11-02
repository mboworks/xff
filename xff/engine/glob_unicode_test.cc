// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <fnmatch.h>

#include <array>
#include <string>
#include <string_view>

#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/engine/evaluate.h"
#include "xff/engine/walk.h"
#include "xff/parser/parser.h"
#include "xff/vfs/local_fs.h"

namespace xff::engine {
namespace {

using ::testing::Eq;

struct GlobUnicodeTest : ::testing::Test {};

TEST_F(GlobUnicodeTest, MultibyteAndInvalidBytesRetainNativeLocaleSemantics) {
  // Accented Latin letters exercise multibyte input/patterns. The invalid byte deliberately
  // exercises native fnmatch's behavior for malformed input, without imposing UTF-8 semantics.
  static constexpr auto kPatterns = std::to_array<std::string_view>({
      "*",
      "*.txt",
      "c*",
      "*é*",
      "café.txt",
      "CAFÉ.TXT",
      "?*.txt",
      "[[:alpha:]]*",
  });
  static constexpr auto kTexts = std::to_array<std::string_view>({
      "café.txt",
      "CAFÉ.TXT",
      "é",
      "\377.txt",
  });
  const vfs::LocalFs fs;
  const vfs::Metadata metadata{.type = vfs::FileType::kRegular};
  const auto emit = [](std::string_view) {};
  for (const std::string_view pattern : kPatterns) {
    MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({".", "-name", std::string(pattern)}));
    MBO_ASSERT_OK_AND_ASSIGN(const auto prepared, PreparedExpression::Prepare(*command.expression));
    const auto worker = prepared.MakeWorker();
    for (const std::string_view text : kTexts) {
      for (const bool fold : {false, true}) {
        SCOPED_TRACE(std::string(pattern) + " / " + std::string(text));
        const Visit visit{.path = text, .name = text, .metadata = metadata};
        // NOLINTNEXTLINE(misc-const-correctness): EvalContext mutates this control.
        Control control;
        EvalContext context{
            .visit = visit,
            .emit = emit,
            .fs = fs,
            .now = absl::UnixEpoch(),
            .tz = absl::UTCTimeZone(),
            .fold_name_case = fold,
            .control = control,
        };
        const bool expected =
            ::fnmatch(std::string(pattern).c_str(), std::string(text).c_str(), fold ? FNM_CASEFOLD : 0) == 0;
        EXPECT_THAT(worker.Evaluate(context).matched, Eq(expected));
      }
    }
  }
}

}  // namespace
}  // namespace xff::engine
