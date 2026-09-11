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

#include "xff/engine/collect.h"

#include <string>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/engine/walk.h"
#include "xff/parser/parser.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::SizeIs;

struct CollectTest : ::testing::Test {
  // A Visit over caller-owned storage, so a test can let that storage die and prove the collection
  // still reads correctly (the reason CollectedEntry copies rather than borrows).
  static Visit MakeVisit(
      std::string_view path,
      std::string_view name,
      std::string_view root,
      const vfs::Metadata& metadata,
      int depth = 1) {
    return Visit{.path = path, .name = name, .root = root, .depth = depth, .metadata = metadata};
  }
};

TEST_F(CollectTest, AddStoresUnderTheNamedCollection) {
  const vfs::Metadata md{.type = vfs::FileType::kRegular, .size = 42};
  Collections collections;
  {
    // Deliberately scoped: the Visit's views and metadata reference these locals only.
    const std::string path = "./a/b.txt";
    const std::string name = "b.txt";
    const std::string root = ".";
    collections.Add("keep", MakeVisit(path, name, root, md));
  }
  EXPECT_THAT(collections.Names(), ElementsAre("keep"));
  ASSERT_THAT(collections.Entries("keep"), SizeIs(1));
  const CollectedEntry& entry = collections.Entries("keep").front();
  EXPECT_THAT(entry.path, Eq("./a/b.txt"));
  EXPECT_THAT(entry.name, Eq("b.txt"));
  EXPECT_THAT(entry.root, Eq("."));
  EXPECT_THAT(entry.metadata.size, Eq(42));
}

TEST_F(CollectTest, AsVisitRebuildsTheEntryForASink) {
  const vfs::Metadata md{.type = vfs::FileType::kRegular, .size = 7};
  Collections collections;
  {
    const std::string path = "./x.txt";
    const std::string name = "x.txt";
    const std::string root = ".";
    collections.Add(kDefaultCollection, MakeVisit(path, name, root, md, 3));
  }
  ASSERT_THAT(collections.Entries(kDefaultCollection), SizeIs(1));
  const Visit visit = collections.Entries(kDefaultCollection).front().AsVisit();
  EXPECT_THAT(visit.path, Eq("./x.txt"));
  EXPECT_THAT(visit.name, Eq("x.txt"));
  EXPECT_THAT(visit.depth, Eq(3));
  EXPECT_THAT(visit.metadata.size, Eq(7));
}

TEST_F(CollectTest, NamesAreOrderedAndSizeCountsEveryCollection) {
  const vfs::Metadata md{.type = vfs::FileType::kRegular};
  const std::string path = "./f";
  const std::string name = "f";
  const std::string root = ".";
  Collections collections;
  collections.Add("zeta", MakeVisit(path, name, root, md));
  collections.Add("alpha", MakeVisit(path, name, root, md));
  collections.Add("alpha", MakeVisit(path, name, root, md));
  EXPECT_THAT(collections.Names(), ElementsAre("alpha", "zeta"));  // name order, not insertion order
  EXPECT_THAT(collections.Entries("alpha"), SizeIs(2));
  EXPECT_THAT(collections.Size(), Eq(3));
}

TEST_F(CollectTest, UnknownNameReadsAsEmptyRatherThanFailing) {
  const Collections collections;
  EXPECT_THAT(collections.Empty(), IsTrue());
  EXPECT_THAT(collections.Entries("never-collected"), IsEmpty());
}

TEST_F(CollectTest, ABudgetOfZeroMeansNoLimit) {
  const vfs::Metadata md{.type = vfs::FileType::kRegular};
  Collections collections;
  for (int i = 0; i < 100; ++i) {
    EXPECT_THAT(collections.Add("all", MakeVisit("./f", "f", ".", md)), IsTrue());
  }
  EXPECT_THAT(collections.Overflowed(), IsFalse());
  EXPECT_THAT(collections.Size(), Eq(100));
}

TEST_F(CollectTest, ARowBudgetRefusesTheEntryPastTheCeiling) {
  const vfs::Metadata md{.type = vfs::FileType::kRegular};
  Collections collections;
  collections.SetBudget(Collections::Budget{.rows = 2});
  EXPECT_THAT(collections.Add("all", MakeVisit("./a", "a", ".", md)), IsTrue());
  EXPECT_THAT(collections.Add("all", MakeVisit("./b", "b", ".", md)), IsTrue());
  EXPECT_THAT(collections.Add("all", MakeVisit("./c", "c", ".", md)), IsFalse());
  EXPECT_THAT(collections.Overflowed(), IsTrue());
  // The refused entry is NOT stored: the caller reports the overflow rather than reducing a
  // collection that silently lost entries.
  EXPECT_THAT(collections.Size(), Eq(2));
}

TEST_F(CollectTest, ARowBudgetCountsEveryCollectionTogether) {
  const vfs::Metadata md{.type = vfs::FileType::kRegular};
  Collections collections;
  collections.SetBudget(Collections::Budget{.rows = 2});
  EXPECT_THAT(collections.Add("one", MakeVisit("./a", "a", ".", md)), IsTrue());
  EXPECT_THAT(collections.Add("two", MakeVisit("./b", "b", ".", md)), IsTrue());
  EXPECT_THAT(collections.Add("one", MakeVisit("./c", "c", ".", md)), IsFalse());
}

TEST_F(CollectTest, AByteBudgetMeasuresTheStoredText) {
  const vfs::Metadata md{.type = vfs::FileType::kRegular};
  Collections collections;
  // "./aaaa" + "aaaa" + "." == 11 bytes per entry, so a 12-byte budget admits exactly one.
  collections.SetBudget(Collections::Budget{.bytes = 12});
  EXPECT_THAT(collections.Add("all", MakeVisit("./aaaa", "aaaa", ".", md)), IsTrue());
  EXPECT_THAT(collections.Add("all", MakeVisit("./aaaa", "aaaa", ".", md)), IsFalse());
  EXPECT_THAT(collections.Overflowed(), IsTrue());
}

TEST_F(CollectTest, OverflowStaysStickyForTheRestOfTheWalk) {
  const vfs::Metadata md{.type = vfs::FileType::kRegular};
  Collections collections;
  collections.SetBudget(Collections::Budget{.rows = 1});
  EXPECT_THAT(collections.Add("all", MakeVisit("./a", "a", ".", md)), IsTrue());
  EXPECT_THAT(collections.Add("all", MakeVisit("./b", "b", ".", md)), IsFalse());
  EXPECT_THAT(collections.Add("all", MakeVisit("./c", "c", ".", md)), IsFalse());
  EXPECT_THAT(collections.Overflowed(), IsTrue());
}

testing::Matcher<CollectSite> SiteIs(std::string_view name, bool override_name) {
  return AllOf(
      Field("name", &CollectSite::name, name), Field("override_name", &CollectSite::override_name, override_name));
}

TEST_F(CollectTest, BareCollectUsesTheDefaultName) {
  MBO_ASSERT_OK_AND_ASSIGN(const parser::Command cmd, parser::Parse({".", "-collect"}));
  EXPECT_THAT(CollectSites(*cmd.expression), ElementsAre(SiteIs(kDefaultCollection, false)));
}

TEST_F(CollectTest, NamedCollectReportsItsName) {
  MBO_ASSERT_OK_AND_ASSIGN(const parser::Command cmd, parser::Parse({".", "-collect:big"}));
  EXPECT_THAT(CollectSites(*cmd.expression), ElementsAre(SiteIs("big", false)));
}

TEST_F(CollectTest, SitesKeepAstOrderAndCarryTheBangModifier) {
  MBO_ASSERT_OK_AND_ASSIGN(
      const parser::Command cmd, parser::Parse({".", "-collect:a", "-o", "-collect:b", "-o", "-collect:!a"}));
  EXPECT_THAT(CollectSites(*cmd.expression), ElementsAre(SiteIs("a", false), SiteIs("b", false), SiteIs("a", true)));
}

TEST_F(CollectTest, AnExpressionWithoutCollectReportsNoSites) {
  MBO_ASSERT_OK_AND_ASSIGN(const parser::Command cmd, parser::Parse({".", "-type", "f"}));
  EXPECT_THAT(CollectSites(*cmd.expression), IsEmpty());
}

}  // namespace
}  // namespace xff::engine
