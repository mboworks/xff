// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <clocale>
#include <string>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/archive/archive_reader.h"
#include "xff/archive/archive_tar_pathname_test_support.h"

namespace xff::archive {
namespace {

using ::mbo::testing::IsOkAndHolds;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::NotNull;

// NOLINTBEGIN(concurrency-mt-unsafe): This single-threaded fixture saves and restores the C locale.
struct ArchiveTarPathnameUnicodeTest : ::testing::Test {
  void SetUp() override {
    // XFF_ABI_POINTER: setlocale returns a borrowed C locale name; copy it before mutation.
    const char* previous = std::setlocale(LC_CTYPE, nullptr);
    ASSERT_THAT(previous, NotNull());
    previous_locale = previous;
    ASSERT_THAT(std::setlocale(LC_CTYPE, "C"), NotNull());
  }

  void TearDown() override { EXPECT_THAT(std::setlocale(LC_CTYPE, previous_locale.c_str()), NotNull()); }

  std::string previous_locale;
};

TEST_F(ArchiveTarPathnameUnicodeTest, PreservesUtf8NamesAndTrailingSlashClassification) {
  // UTF-8 e-acute, a CJK character, and a combining mark; byte spelling must remain intact.
  for (const std::string_view stem : {"caf\xc3\xa9", "\xe6\x96\x87", "e\xcc\x81"}) {
    SCOPED_TRACE(stem);
    EXPECT_THAT(
        ListMembers(archive_tar_pathname_test_support::MakeTar(stem)),
        IsOkAndHolds(ElementsAre(AllOf(Field(&Member::path, stem), Field(&Member::is_directory, false)))));
    const std::string directory = std::string(stem) + '/';
    EXPECT_THAT(
        ListMembers(archive_tar_pathname_test_support::MakeTar(directory)),
        IsOkAndHolds(ElementsAre(AllOf(Field(&Member::path, directory), Field(&Member::is_directory, true)))));
  }
}

TEST_F(ArchiveTarPathnameUnicodeTest, PreservesInvalidMultibyteNameAndNarrowFallback) {
  // 0xff and a truncated UTF-8 leading byte cannot be converted in the C locale.
  for (const std::string_view stem : {"bad-\xff", "truncated-\xc3"}) {
    SCOPED_TRACE(stem);
    EXPECT_THAT(
        ListMembers(archive_tar_pathname_test_support::MakeTar(stem)),
        IsOkAndHolds(ElementsAre(AllOf(Field(&Member::path, stem), Field(&Member::is_directory, false)))));
    const std::string directory = std::string(stem) + '/';
    EXPECT_THAT(
        ListMembers(archive_tar_pathname_test_support::MakeTar(directory)),
        IsOkAndHolds(ElementsAre(AllOf(Field(&Member::path, directory), Field(&Member::is_directory, true)))));
  }
}

TEST_F(ArchiveTarPathnameUnicodeTest, PreservesUtf8DirectoryClassificationInUtf8Locale) {
  // Linux provides C.UTF-8; macOS provides en_US.UTF-8. These names exercise the same encoding.
  if (std::setlocale(LC_CTYPE, "C.UTF-8") == nullptr) {
    ASSERT_THAT(std::setlocale(LC_CTYPE, "en_US.UTF-8"), NotNull());
  }
  const std::string name = "caf\xc3\xa9/";
  EXPECT_THAT(
      ListMembers(archive_tar_pathname_test_support::MakeTar(name)),
      IsOkAndHolds(ElementsAre(AllOf(Field(&Member::path, name), Field(&Member::is_directory, true)))));
}

// NOLINTEND(concurrency-mt-unsafe)

}  // namespace
}  // namespace xff::archive
