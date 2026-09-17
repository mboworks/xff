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

#include "xff/registry/registry.h"

#include <array>
#include <optional>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/registry/descriptor.h"

namespace xff::registry {
namespace {

using ::testing::_;
using ::testing::Eq;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Le;
using ::testing::Ne;
using ::testing::Not;
using ::testing::Optional;
using ::testing::Ref;
using ::testing::SizeIs;

struct RegistryTest : ::testing::Test {};

TEST_F(RegistryTest, CaptureCapabilitiesHaveConsistentArgumentLayouts) {
  for (const Descriptor& descriptor : All()) {
    if (descriptor.binds_capture) {
      EXPECT_THAT(descriptor.kind, Eq(Kind::kAction)) << descriptor.name;
      EXPECT_THAT(descriptor.binding, Eq(Binding::kLabelRegex)) << descriptor.name;
      EXPECT_THAT(descriptor.preserves_implicit_output, Eq(true)) << descriptor.name;
      EXPECT_THAT(descriptor.argument_fields.syntax, Eq(ArgumentFields::Syntax::kTemplate)) << descriptor.name;
      EXPECT_THAT(descriptor.argument_fields.first, Eq(2)) << descriptor.name;
      EXPECT_THAT(descriptor.argument_fields.remaining, Eq(true)) << descriptor.name;
      EXPECT_THAT(descriptor.argument_fields.requires_exec_fields, Eq(false)) << descriptor.name;
    }
    if (descriptor.argument_fields.remaining) {
      EXPECT_THAT(descriptor.arity, Eq(-1)) << descriptor.name;
    }
    if (descriptor.argument_fields.requires_exec_fields) {
      EXPECT_THAT(descriptor.argument_fields.syntax, Eq(ArgumentFields::Syntax::kTemplate)) << descriptor.name;
    }
  }
}

TEST_F(RegistryTest, DescriptorSupportsStringification) {
  ASSERT_THAT(Lookup("-name"), Optional(_));
  EXPECT_THAT(absl::StrCat(*Lookup("-name")), Eq("-name"));
  EXPECT_THAT(absl::StrFormat("%v", *Lookup("-name")), Eq("-name"));
}

TEST_F(RegistryTest, LooksUpKnownTokens) {
  const mbo::types::OptionalRef<const Descriptor> name = Lookup("-name");
  ASSERT_THAT(name, Optional(_));
  EXPECT_THAT(name->kind, Kind::kTest);
  EXPECT_THAT(name->arity, 1);

  const mbo::types::OptionalRef<const Descriptor> print = Lookup("-print");
  ASSERT_THAT(print, Optional(_));
  EXPECT_THAT(print->kind, Kind::kAction);
  EXPECT_THAT(print->arity, 0);

  const mbo::types::OptionalRef<const Descriptor> or_op = Lookup("-o");
  ASSERT_THAT(or_op, Optional(_));
  EXPECT_THAT(or_op->kind, Kind::kOperator);

  const mbo::types::OptionalRef<const Descriptor> bang = Lookup("!");
  ASSERT_THAT(bang, Optional(_));
  EXPECT_THAT(bang->kind, Kind::kOperator);
}

TEST_F(RegistryTest, UnknownTokenIsNull) {
  EXPECT_THAT(Lookup("-nonexistent"), Eq(std::nullopt));
  EXPECT_THAT(Lookup(""), Eq(std::nullopt));
  EXPECT_THAT(Lookup("."), Eq(std::nullopt));
}

TEST_F(RegistryTest, SecurityRelevantPrimariesAreClassified) {
  // The exec family runs arbitrary commands (kSecurity); -delete loses data
  // (kSafety); everything else is unclassified (kNone). The config policy gate
  // (phase C) keys its safe-by-default deny off this.
  static constexpr std::array kSecurityPrimaries = std::to_array<std::string_view>({
      "-exec",
      "-execdir",
      "-ok",
      "-okdir",
      "-capture",
      "-capturedir",
  });
  for (const std::string_view name : kSecurityPrimaries) {
    EXPECT_THAT(Lookup(name), Optional(Field("safety", &Descriptor::safety, Safety::kSecurity))) << name;
  }
  EXPECT_THAT(Lookup("-delete"), Optional(Field("safety", &Descriptor::safety, Safety::kSafety)));
  EXPECT_THAT(Lookup("-name"), Optional(Field("safety", &Descriptor::safety, Safety::kNone)));
  EXPECT_THAT(Lookup("-print"), Optional(Field("safety", &Descriptor::safety, Safety::kNone)));
}

TEST_F(RegistryTest, CaptureFamilyDeclaresLabelRegexBinding) {
  // -capture/-capturedir carry an attached =NAME[=REGEX] on the token; the parser
  // reads this binding from the registry instead of hardcoding the names.
  EXPECT_THAT(Lookup("-capture"), Optional(Field("binding", &Descriptor::binding, Binding::kLabelRegex)));
  EXPECT_THAT(Lookup("-capturedir"), Optional(Field("binding", &Descriptor::binding, Binding::kLabelRegex)));
  EXPECT_THAT(Lookup("-exec"), Optional(Field("binding", &Descriptor::binding, Binding::kNone)));  // no =label
  EXPECT_THAT(Lookup("-name"), Optional(Field("binding", &Descriptor::binding, Binding::kNone)));
}

TEST_F(RegistryTest, XffExtensionsAreStyleTagged) {
  // The xff-native primaries carry Style::kXff so the strict find style (phase D)
  // can reject them; everything inherited from find stays kFind (the default).
  static constexpr std::array kXffPrimaries = std::to_array<std::string_view>(
      {"-println", "-printfln", "-capture", "-capturedir", "-xor", "-nand", "-nor", "-xnor"});
  for (const std::string_view name : kXffPrimaries) {
    EXPECT_THAT(Lookup(name), Optional(Field("style", &Descriptor::style, Style::kXff))) << name;
  }
  static constexpr std::array kFindStylePrimaries = std::to_array<std::string_view>({
      "-print",
      "-printf",
      "-name",
      "-exec",
      "-delete",
  });
  for (const std::string_view name : kFindStylePrimaries) {
    EXPECT_THAT(Lookup(name), Optional(Field("style", &Descriptor::style, Style::kFind))) << name;
  }
}

TEST_F(RegistryTest, ExtendedLogicalOperatorsAreOperators) {
  // -xor/-nand/-nor/-xnor are xff logical operators (the style tagging is asserted
  // above); confirm they are classified as operators, not tests/actions.
  static constexpr std::array kXffOperators = std::to_array<std::string_view>({
      "-xor",
      "-nand",
      "-nor",
      "-xnor",
  });
  for (const std::string_view name : kXffOperators) {
    EXPECT_THAT(Lookup(name), Optional(Field("kind", &Descriptor::kind, Kind::kOperator))) << name;
  }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity): a flat per-descriptor validation sweep.
TEST_F(RegistryTest, EveryDescriptorCarriesAWellFormedSummary) {
  // The help system, generated --help, and the planned man-page / .md doc
  // generators all read `summary`, so every descriptor must carry a one-line
  // synopsis: non-empty, single line, no trailing period.
  EXPECT_THAT(All(), Not(IsEmpty()));
  for (const Descriptor& descriptor : All()) {
    ASSERT_THAT(descriptor.summary, Not(IsEmpty())) << descriptor.name;
    EXPECT_THAT(descriptor.summary, SizeIs(Le(90U))) << descriptor.name;
    EXPECT_THAT(descriptor.summary.back(), Ne('.')) << descriptor.name;
    EXPECT_THAT(descriptor.summary, Not(HasSubstr("\n"))) << descriptor.name;
  }
}

TEST_F(RegistryTest, AllEnumeratesTheSameDescriptorsLookupResolves) {
  // All() and Lookup() must read the same table, so generators and the parser
  // never drift: each enumerated descriptor resolves back to itself by name.
  for (const Descriptor& descriptor : All()) {
    EXPECT_THAT(Lookup(descriptor.name), Optional(Ref(descriptor))) << descriptor.name;
  }
}

TEST_F(RegistryTest, AliasesResolveToTheirCanonicalDescriptorAndNeverCollide) {
  for (const Descriptor& descriptor : All()) {
    if (descriptor.alias.empty()) {
      continue;
    }
    EXPECT_THAT(Lookup(descriptor.alias), Optional(Ref(descriptor))) << descriptor.alias;
    EXPECT_THAT(descriptor.alias.compare(descriptor.name), Ne(0));
    for (const Descriptor& other : All()) {
      if (&descriptor == &other) {
        continue;
      }
      EXPECT_THAT(descriptor.alias.compare(other.name), Ne(0))
          << descriptor.name << " aliases canonical " << other.name;
      EXPECT_THAT(descriptor.alias.compare(other.alias), Ne(0))
          << descriptor.name << " and " << other.name << " share an alias";
    }
  }
}

}  // namespace
}  // namespace xff::registry
