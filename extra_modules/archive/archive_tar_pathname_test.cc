// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

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

struct ArchiveTarPathnameTest : ::testing::Test {};

TEST_F(ArchiveTarPathnameTest, KeepsRegularAsciiNamesAndBytes) {
  for (const std::string_view name : {"file.txt", "dir/file.txt", "with space.txt", "tab\tname", "line\nname"}) {
    SCOPED_TRACE(name);
    EXPECT_THAT(
        ListMembers(archive_tar_pathname_test_support::MakeTar(name)),
        IsOkAndHolds(ElementsAre(AllOf(Field(&Member::path, name), Field(&Member::is_directory, false)))));
  }
}

TEST_F(ArchiveTarPathnameTest, RecognizesLegacyRegularHeaderWithTrailingSlashAsDirectory) {
  for (const std::string_view name : {"dir/", "a/b/", "/"}) {
    SCOPED_TRACE(name);
    EXPECT_THAT(
        ListMembers(archive_tar_pathname_test_support::MakeTar(name)),
        IsOkAndHolds(ElementsAre(AllOf(Field(&Member::path, name), Field(&Member::is_directory, true)))));
  }
}

TEST_F(ArchiveTarPathnameTest, KeepsExplicitDirectoryWithoutTrailingSlash) {
  EXPECT_THAT(
      ListMembers(archive_tar_pathname_test_support::MakeTar("dir", '5')),
      IsOkAndHolds(ElementsAre(AllOf(Field(&Member::path, "dir"), Field(&Member::is_directory, true)))));
}

TEST_F(ArchiveTarPathnameTest, HandlesFullWidthUstarNameField) {
  const std::string name(100, 'a');
  EXPECT_THAT(
      ListMembers(archive_tar_pathname_test_support::MakeTar(name)),
      IsOkAndHolds(ElementsAre(AllOf(Field(&Member::path, name), Field(&Member::is_directory, false)))));
  const std::string directory = std::string(99, 'a') + '/';
  EXPECT_THAT(
      ListMembers(archive_tar_pathname_test_support::MakeTar(directory)),
      IsOkAndHolds(ElementsAre(AllOf(Field(&Member::path, directory), Field(&Member::is_directory, true)))));
}

TEST_F(ArchiveTarPathnameTest, KeepsEscapeBasedPathOnOriginalConversionPath) {
  // ESC can introduce a stateful encoding: never classify that pathname as plain ASCII.
  const std::string name = "escape\x1b$Bname/";
  EXPECT_THAT(
      ListMembers(archive_tar_pathname_test_support::MakeTar(name)),
      IsOkAndHolds(ElementsAre(AllOf(Field(&Member::path, name), Field(&Member::is_directory, true)))));
}

}  // namespace
}  // namespace xff::archive
