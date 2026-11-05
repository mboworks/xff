// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <fnmatch.h>

#include <array>
#include <clocale>
#include <string>
#include <string_view>

#include "absl/cleanup/cleanup.h"
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
using ::testing::Gt;
using ::testing::IsNull;
using ::testing::Not;

constexpr auto kLocales = std::to_array<std::string_view>({
    "C",
    "POSIX",
    "C.UTF-8",
    "C.utf8",
    "en_US.UTF-8",
    "tr_TR.UTF-8",
    "tr_TR.ISO8859-9",
});

struct GlobUnicodeTest : ::testing::Test {
  static void Check() {
    // Accented Latin letters exercise multibyte input/patterns. The invalid byte deliberately
    // exercises native fnmatch's behavior for malformed input, without imposing UTF-8 semantics.
    static constexpr auto kPatterns = std::to_array<std::string_view>({
        "*",
        "*.txt",
        "c*",
        "I",
        "i",
        "s",
        "k",
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
        "I",
        "i",
        "K",
        "k",
        "S",
        "s",
        "IiK",
        "İ",  // Dotted capital I differs under Turkish case mapping.
        "ı",  // Dotless lowercase I is not the ASCII letter i.
        "ſ",  // Long s can share a wide-character fold with ASCII s.
        "K",  // Kelvin sign can share a wide-character fold with ASCII k.
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
};

TEST_F(GlobUnicodeTest, MultibyteAndInvalidBytesRetainNativeLocaleSemantics) {
  Check();
}

TEST_F(GlobUnicodeTest, ActiveThreadLocalesRetainNativeFoldingAndFallbacks) {
  std::size_t available = 0;
  std::string tested_locales;
  for (const auto name : kLocales) {
    // XFF_ABI_POINTER: POSIX locale_t is an opaque libc-owned locale handle.
    auto* const selected = ::newlocale(LC_CTYPE_MASK, std::string(name).c_str(), nullptr);
    if (selected == nullptr) {
      continue;
    }
    ++available;
    if (!tested_locales.empty()) {
      tested_locales.push_back(' ');
    }
    tested_locales.append(name);
    // XFF_ABI_POINTER: uselocale returns the previous opaque thread-local handle.
    auto* const previous = ::uselocale(selected);
    const absl::Cleanup restore = [&] {
      if (previous != nullptr) {
        ::uselocale(previous);
      }
      ::freelocale(selected);
    };
    ASSERT_THAT(previous, Not(IsNull()));
    SCOPED_TRACE(name);
    Check();
  }
  RecordProperty("thread_locales", tested_locales);
  EXPECT_THAT(available, Gt(0));
}

TEST_F(GlobUnicodeTest, PreparedPlanTracksLocaleChangesAfterPreparation) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({".", "-iname", "I"}));
  MBO_ASSERT_OK_AND_ASSIGN(const auto prepared, PreparedExpression::Prepare(*command.expression));
  const auto worker = prepared.MakeWorker();
  const vfs::LocalFs fs;
  const vfs::Metadata metadata{.type = vfs::FileType::kRegular};
  const auto emit = [](std::string_view) {};
  constexpr auto kTexts = std::to_array<std::string_view>({"I", "i", "İ", "ı"});
  std::size_t available = 0;
  for (const auto name : kLocales) {
    // XFF_ABI_POINTER: newlocale returns an opaque mutable POSIX locale handle.
    auto* const selected = ::newlocale(LC_CTYPE_MASK, std::string(name).c_str(), nullptr);
    if (selected == nullptr) {
      continue;
    }
    ++available;
    // XFF_ABI_POINTER: uselocale returns the previous opaque thread locale handle.
    auto* const previous = ::uselocale(selected);
    const absl::Cleanup restore = [&] {
      if (previous != nullptr) {
        ::uselocale(previous);
      }
      ::freelocale(selected);
    };
    ASSERT_THAT(previous, Not(IsNull()));
    SCOPED_TRACE(name);
    for (const auto text : kTexts) {
      const Visit visit{.path = text, .name = text, .metadata = metadata};
      // NOLINTNEXTLINE(misc-const-correctness): EvalContext mutates this control.
      Control control;
      EvalContext context{
          .visit = visit,
          .emit = emit,
          .fs = fs,
          .now = absl::UnixEpoch(),
          .tz = absl::UTCTimeZone(),
          .control = control,
      };
      const bool expected = ::fnmatch("I", std::string(text).c_str(), FNM_CASEFOLD) == 0;
      EXPECT_THAT(Evaluate(*command.expression, context), Eq(expected));
      EXPECT_THAT(worker.Evaluate(context).matched, Eq(expected));
      EXPECT_THAT(worker.EvaluateIterative(context).matched, Eq(expected));
    }
  }
  EXPECT_THAT(available, Gt(0));
}

}  // namespace
}  // namespace xff::engine
