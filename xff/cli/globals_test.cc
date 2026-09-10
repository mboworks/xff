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

#include "xff/cli/globals.h"

#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/hash/hash.h"

namespace xff::cli {
namespace {

using ::mbo::testing::IsOk;
using ::mbo::testing::StatusIs;
using ::testing::_;
using ::testing::Contains;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Le;
using ::testing::Ne;
using ::testing::Not;
using ::testing::Optional;
using ::testing::Ref;
using ::testing::SizeIs;

struct GlobalsTest : ::testing::Test {};

// NOLINTNEXTLINE(readability-function-cognitive-complexity): a flat per-field validation sweep.
TEST_F(GlobalsTest, EveryGlobalIsWellFormed) {
  EXPECT_THAT(Globals(), Not(IsEmpty()));
  for (const GlobalFlag& flag : Globals()) {
    ASSERT_THAT(flag.name, Not(IsEmpty())) << flag.name;
    EXPECT_THAT(flag.display, Not(IsEmpty())) << flag.name;
    EXPECT_THAT(flag.group, Not(IsEmpty())) << flag.name;
    EXPECT_THAT(flag.header, Not(IsEmpty())) << flag.name;
    ASSERT_THAT(flag.summary, Not(IsEmpty())) << flag.name;
    EXPECT_THAT(flag.summary, SizeIs(Le(90U))) << flag.name;
    EXPECT_THAT(flag.summary.back(), Ne('.')) << flag.name;
    EXPECT_THAT(flag.name.front(), Eq('-')) << flag.name;          // an option starts with a dash
    EXPECT_THAT(flag.display, HasSubstr(flag.name)) << flag.name;  // the header shows the canonical name
  }
}

TEST_F(GlobalsTest, HashAlgorithmValuesMatchTheHashLib) {
  // The documented --hash-algorithm value table must stay identical to xff/hash's
  // AlgorithmNames() SOT (same members, same order), so the help cannot drift from the
  // algorithms the -hash action / {hash} field actually accept.
  const mbo::types::OptionalRef<const GlobalFlag> flag = LookupGlobal("--hash-algorithm");
  ASSERT_THAT(flag, Optional(_));
  std::vector<std::string_view> documented;
  documented.reserve(flag->values.size());
  for (const ValueDoc& value : flag->values) {
    documented.push_back(value.value);
  }
  EXPECT_THAT(documented, ElementsAreArray(hash::AlgorithmNames()));
}

TEST_F(GlobalsTest, LookupResolvesNameAndAlias) {
  const mbo::types::OptionalRef<const GlobalFlag> jobs = LookupGlobal("-j");
  const mbo::types::OptionalRef<const GlobalFlag> timezone = LookupGlobal("--tz");
  ASSERT_THAT(jobs, Optional(_));
  ASSERT_THAT(timezone, Optional(_));
  EXPECT_THAT(LookupGlobal("--sort"), Optional(_));
  EXPECT_THAT(LookupGlobal("--jobs"), Optional(Ref(*jobs)));          // same entry
  EXPECT_THAT(LookupGlobal("--timezone"), Optional(Ref(*timezone)));  // ditto
  EXPECT_THAT(LookupGlobal("--nonesuch"), Eq(std::nullopt));
}

TEST_F(GlobalsTest, StringifiesAsCanonicalName) {
  const mbo::types::OptionalRef<const GlobalFlag> jobs = LookupGlobal("--jobs");
  ASSERT_THAT(jobs, Optional(_));
  EXPECT_THAT(absl::StrCat(*jobs), "--jobs");
}

TEST_F(GlobalsTest, ConfigAndSymlinkFlagsDocumentTheirNonObviousBoundaries) {
  EXPECT_THAT(
      LookupGlobal("--config"),
      Optional(Field("details", &GlobalFlag::details, HasSubstr("Every occurrence remains an active selector"))));
  EXPECT_THAT(
      LookupGlobal("--no-config"),
      Optional(Field("details", &GlobalFlag::details, HasSubstr("still inspected for policy"))));
  EXPECT_THAT(
      LookupGlobal("--no-system-config"),
      Optional(Field("details", &GlobalFlag::details, HasSubstr("--allow-no-system-config"))));
  EXPECT_THAT(
      LookupGlobal("--no-user-config"),
      Optional(Field("details", &GlobalFlag::details, HasSubstr("--allow-no-user-config"))));
  EXPECT_THAT(
      LookupGlobal("--explain"), Optional(Field("details", &GlobalFlag::details, HasSubstr("does not walk roots"))));
  EXPECT_THAT(LookupGlobal("-H"), Optional(Field("details", &GlobalFlag::details, HasSubstr("dangling root symlink"))));
  EXPECT_THAT(
      LookupGlobal("-L"), Optional(Field("details", &GlobalFlag::details, HasSubstr("Filesystem loops are detected"))));
  EXPECT_THAT(
      LookupGlobal("-P"),
      Optional(Field("details", &GlobalFlag::details, HasSubstr("including a symlink supplied as a root operand"))));
}

TEST_F(GlobalsTest, ComposableExtraFlagCarriesItsExtraKeyAndIsOffInTheLeanBuild) {
  const mbo::types::OptionalRef<const GlobalFlag> archive = LookupGlobal("--archive");
  ASSERT_THAT(archive, Optional(_));
  EXPECT_THAT(archive->extra, Eq("archive"));           // the SOT link from the flag to its build extra
  EXPECT_THAT(ExtraEnabled("archive"), IsFalse());      // not compiled into the lean default binary
  EXPECT_THAT(ExtraEnabled("pcre2"), IsFalse());        // same gate as archive, same lean answer
  EXPECT_THAT(ExtraEnabled("brotli"), IsFalse());       // removable extension of the archive extra
  EXPECT_THAT(ExtraEnabled("language-db"), IsFalse());  // removable comprehensive language vocabulary
  EXPECT_THAT(ExtraEnabled("mime-db"), IsFalse());      // removable comprehensive media vocabulary
  EXPECT_THAT(ExtraEnabled("squashfs"), IsFalse());     // removable filesystem-image reader
  EXPECT_THAT(EnabledExtras(), IsEmpty());              // the notice line's source: nothing in a lean build
  EXPECT_THAT(ExtraEnabled("nonesuch"), IsFalse());     // an unknown extra reads as off
  EXPECT_THAT(LookupGlobal("--sort"), Optional(Field("extra", &GlobalFlag::extra, IsEmpty())));
}

TEST_F(GlobalsTest, EveryGlobalResolvesByItsOwnName) {
  for (const GlobalFlag& flag : Globals()) {
    EXPECT_THAT(LookupGlobal(flag.name), Optional(Ref(flag))) << flag.name;
  }
}

TEST_F(GlobalsTest, IsKnownGlobalAcceptsEveryTableNameAndAlias) {
  for (const GlobalFlag& flag : Globals()) {
    EXPECT_THAT(IsKnownGlobal(flag.name), IsTrue()) << flag.name;
    if (!flag.alias.empty()) {
      EXPECT_THAT(IsKnownGlobal(flag.alias), IsTrue()) << flag.alias;
    }
  }
}

TEST_F(GlobalsTest, IsKnownGlobalAcceptsValuedFormsAndCompatAliases) {
  EXPECT_THAT(IsKnownGlobal("--sort=tree"), IsTrue());     // valued name=VALUE
  EXPECT_THAT(IsKnownGlobal("--define=A=B"), IsTrue());    // value may itself contain '='
  EXPECT_THAT(IsKnownGlobal("--gitignore=on"), IsTrue());  // bare-or-valued flag, valued form
  EXPECT_THAT(IsKnownGlobal("--tz=utc"), IsTrue());        // valued via an alias
  EXPECT_THAT(IsKnownGlobal("-j=4"), IsTrue());            // valued short form
  EXPECT_THAT(IsKnownGlobal("-j4"), IsTrue());             // conventional attached short argument
  EXPECT_THAT(IsKnownGlobal("-jall"), IsTrue());
  EXPECT_THAT(IsKnownGlobal("-0"), IsTrue());    // compat: --format=nul
  EXPECT_THAT(IsKnownGlobal("-g+"), IsTrue());   // compat: --gitignore=on
  EXPECT_THAT(IsKnownGlobal("-g-"), IsTrue());   // compat: --gitignore=off
  EXPECT_THAT(IsKnownGlobal("-z++"), IsTrue());  // the top read rung (= --archive=any)
  EXPECT_THAT(IsKnownGlobal("-Z"), IsTrue());    // the same rungs with writing armed
  EXPECT_THAT(IsKnownGlobal("-Z+"), IsTrue());
  EXPECT_THAT(IsKnownGlobal("-Z++"), IsTrue());
  // `-Z-` is known so the engine can explain the contradiction rather than have it reported as an
  // unknown option; it is still a usage error.
  EXPECT_THAT(IsKnownGlobal("-Z-"), IsTrue());
}

TEST_F(GlobalsTest, IsKnownGlobalRejectsUnknownFlagsAndBadValuedKeys) {
  EXPECT_THAT(IsKnownGlobal("--bogus"), IsFalse());
  EXPECT_THAT(IsKnownGlobal("--srot"), IsFalse());     // a typo of --sort
  EXPECT_THAT(IsKnownGlobal("-Y"), IsFalse());         // an unclaimed letter (-Z is the write archive ladder)
  EXPECT_THAT(IsKnownGlobal("--safe=x"), IsFalse());   // --safe takes no value, so a valued form is unknown
  EXPECT_THAT(IsKnownGlobal("--bogus=1"), IsFalse());  // unknown key with a value
}

TEST_F(GlobalsTest, EveryTableCheckedFlagHasAValueTableToCheckAgainst) {
  // Both table-backed modes match named values against the flag's own `values` table, so an EMPTY
  // table would reject every named value while reporting an empty accepted-list.
  for (const GlobalFlag& flag : Globals()) {
    if (flag.value_check == GlobalFlag::ValueCheck::kEnum
        || flag.value_check == GlobalFlag::ValueCheck::kEnumOrTemplate) {
      EXPECT_THAT(flag.values, Not(IsEmpty())) << flag.name;
    }
  }
}

TEST_F(GlobalsTest, ATableCheckedFlagAcceptsEveryValueItDocumentsAndRejectsATypo) {
  // The table is the SOT for the help AND the check, so what is printed is what is accepted.
  for (const GlobalFlag& flag : Globals()) {
    if (flag.value_check != GlobalFlag::ValueCheck::kEnum
        && flag.value_check != GlobalFlag::ValueCheck::kEnumOrTemplate) {
      continue;
    }
    for (const ValueDoc& value : flag.values) {
      EXPECT_THAT(ValidateGlobalValue(absl::StrCat(flag.name, "=", value.value)), IsOk())
          << flag.name << "=" << value.value;
    }
    EXPECT_THAT(
        ValidateGlobalValue(absl::StrCat(flag.name, "=zznotavalue")),
        StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("unknown value")))
        << flag.name;
  }
}

TEST_F(GlobalsTest, SummaryHashVerificationDoesNotAliasDigestGrouping) {
  EXPECT_THAT(ValidateGlobalValue("--summary=hash"), IsOk());
  EXPECT_THAT(ValidateGlobalValue("--summary=hash-verification"), IsOk());
  EXPECT_THAT(ValidateGlobalValue("--summary={ext}-{type}"), IsOk());
  EXPECT_THAT(
      ValidateGlobalValue("--summary=verification"),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("unknown value")));
}

TEST_F(GlobalsTest, TheSharedVocabularyFlagsTakeEverySpellingOfIt) {
  // A tri-state flag accepts more than it documents (yes / 1 beside on), so it must not be
  // checked as an enum - that would reject spellings the shared parser handles.
  static constexpr auto kTristateSpellings =
      std::to_array<std::string_view>({"auto", "always", "never", "on", "off", "yes", "no", "true", "false", "1", "0"});
  for (const GlobalFlag& flag : Globals()) {
    if (flag.value_check != GlobalFlag::ValueCheck::kTristate) {
      continue;
    }
    for (const std::string_view spelling : kTristateSpellings) {
      EXPECT_THAT(ValidateGlobalValue(absl::StrCat(flag.name, "=", spelling)), IsOk()) << flag.name << "=" << spelling;
    }
  }
}

TEST_F(GlobalsTest, AFreeTextFlagIsNeverRejected) {
  // Most valued flags take a path, a format or a regex; the check must stay out of their way.
  EXPECT_THAT(ValidateGlobalValue("--define=A=B"), IsOk());
  EXPECT_THAT(ValidateGlobalValue("--template={path}"), IsOk());
  EXPECT_THAT(ValidateGlobalValue("--xffrc=/tmp/x"), IsOk());
  EXPECT_THAT(ValidateGlobalValue("--sort"), IsOk());     // no '=': nothing to check
  EXPECT_THAT(ValidateGlobalValue("-z++"), IsOk());       // a sign-suffixed short, likewise
  EXPECT_THAT(ValidateGlobalValue("--bogus=x"), IsOk());  // unknown flag: IsKnownGlobal's job
}

TEST_F(GlobalsTest, EveryDeclaredSignFormIsAccepted) {
  // The point of declaring them: IsKnownGlobal derives its answer from the flags, so a form that a
  // flag advertises can never be reported as an unknown option (which is what `-Z` was until the
  // literal list caught up).
  for (const GlobalFlag& flag : Globals()) {
    for (const std::string_view form : flag.sign_forms) {
      EXPECT_THAT(IsKnownGlobal(form), IsTrue()) << flag.name << " declares " << form;
    }
  }
}

TEST_F(GlobalsTest, ASignFormNamesItsOwnFlagAndBelongsToOnlyOne) {
  // Two cross-checks against drift: the ladder's base has to appear in the flag's own synopsis (so
  // the help shows what the parser takes), and no two flags may claim the same spelling.
  std::vector<std::string_view> seen;
  for (const GlobalFlag& flag : Globals()) {
    for (const std::string_view form : flag.sign_forms) {
      const std::string_view base = form.substr(0, 2);  // "-z" of "-z++"
      EXPECT_THAT(flag.display, HasSubstr(base)) << flag.name << " hides " << form;
      EXPECT_THAT(seen, Not(Contains(form))) << form << " is claimed twice";
      seen.push_back(form);
    }
  }
}

TEST_F(GlobalsTest, AnUndeclaredSignFormStaysUnknown) {
  // The ladders stop where they are declared; a longer run of signs is a typo, not a rung.
  EXPECT_THAT(IsKnownGlobal("-z+++"), IsFalse());
  EXPECT_THAT(IsKnownGlobal("-Z+++"), IsFalse());
  EXPECT_THAT(IsKnownGlobal("-g++"), IsFalse());
  EXPECT_THAT(IsKnownGlobal("-s++"), IsFalse());
}

}  // namespace
}  // namespace xff::cli
