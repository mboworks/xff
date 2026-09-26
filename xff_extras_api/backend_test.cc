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

#include "xff/matching/regex/backend.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"

namespace xff::regex {
namespace {

using ::mbo::testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Optional;

// A trivial backend: the seam is what an EXTRA implements from its own module, so implementing one
// here proves the interface is usable with nothing but this module - the property the whole
// xff_extras_api split exists to guarantee. It only has to behave, not be a real regex engine.
class LiteralBackend : public RegexBackend {
 public:
  explicit LiteralBackend(std::string pattern) : pattern_(std::move(pattern)) {}

  bool FullMatch(std::string_view text) const override { return text == pattern_; }

  bool PartialMatch(std::string_view text) const override { return text.contains(pattern_); }

  std::optional<std::pair<std::size_t, std::size_t>> FindFirst(std::string_view text, std::size_t start)
      const override {
    const std::size_t at = text.find(pattern_, start);
    if (at == std::string_view::npos) {
      return std::nullopt;
    }
    return std::pair{at, pattern_.size()};
  }

  std::optional<std::vector<std::string>> FullMatchCaptures(std::string_view text) const override {
    if (!FullMatch(text)) {
      return std::nullopt;
    }
    return std::vector<std::string>{pattern_};  // group 0 only: no capture groups in a literal
  }

  std::string Rewrite(std::string_view text, std::string_view replacement, bool global) const override {
    std::string out(text);
    for (std::size_t at = out.find(pattern_); at != std::string::npos;
         at = out.find(pattern_, at + replacement.size())) {
      out.replace(at, pattern_.size(), replacement);
      if (!global) {
        break;
      }
    }
    return out;
  }

 private:
  std::string pattern_;
};

struct RegexBackendSeamTest : ::testing::Test {};

TEST_F(RegexBackendSeamTest, AnExtraCanImplementTheSeamUsingOnlyThisModule) {
  const LiteralBackend backend("ab");
  const RegexBackend& seam = backend;  // held abstractly, the way Matcher owns it
  EXPECT_THAT(seam.FullMatch("ab"), IsTrue());
  EXPECT_THAT(seam.FullMatch("xaby"), IsFalse());
  EXPECT_THAT(seam.PartialMatch("xaby"), IsTrue());
  EXPECT_THAT(seam.FindFirst("xaby"), Optional(std::pair<std::size_t, std::size_t>{1, 2}));
  EXPECT_THAT(seam.FindFirst("nope"), Eq(std::nullopt));
  EXPECT_THAT(seam.FullMatchCaptures("ab"), Optional(ElementsAre("ab")));
  EXPECT_THAT(seam.Rewrite("ab ab", "X", /*global=*/false), "X ab");
  EXPECT_THAT(seam.Rewrite("ab ab", "X", /*global=*/true), "X X");
}

// The registration slot is process-wide and set at static init, so these two tests are ordered by
// name on purpose: the "unregistered" expectations must run BEFORE anything registers a factory.
TEST_F(RegexBackendSeamTest, AUnregisteredPcre2GrammarIsUnimplementedNotAnInvalidPattern) {
  // This is the lean build's behaviour and the reason the state is distinct: "the grammar is not
  // built into this binary" must never be confused with "your pattern is bad", and must never
  // silently fall back to RE2.
  ASSERT_THAT(Pcre2Available(), IsFalse());
  EXPECT_THAT(MakePcre2Backend("anything", /*case_insensitive=*/false), StatusIs(absl::StatusCode::kUnimplemented));
}

TEST_F(RegexBackendSeamTest, BRegisteringAFactoryMakesTheGrammarAvailableAndIsUsed) {
  // What the real PCRE2 backend's Pcre2Registrar does at static init, and what a full build relies
  // on: after registration the grammar reports available and MakePcre2Backend delegates.
  RegisterPcre2Backend(
      [](std::string_view pattern, bool case_insensitive) -> absl::StatusOr<std::unique_ptr<const RegexBackend>> {
        if (pattern.empty()) {
          return absl::InvalidArgumentError("empty pattern");  // a bad pattern stays InvalidArgument
        }
        return std::make_unique<const LiteralBackend>(case_insensitive ? "CASELESS" : std::string(pattern));
      });
  EXPECT_THAT(Pcre2Available(), IsTrue());

  MBO_ASSERT_OK_AND_ASSIGN(const auto backend, MakePcre2Backend("ab", /*case_insensitive=*/false));
  EXPECT_THAT((backend)->FullMatch("ab"), IsTrue());

  // The factory receives case_insensitive, so a caller's fold request reaches the engine.
  MBO_ASSERT_OK_AND_ASSIGN(const auto folded, MakePcre2Backend("ab", /*case_insensitive=*/true));
  EXPECT_THAT((folded)->FullMatch("CASELESS"), IsTrue());

  // A pattern the engine rejects is InvalidArgument, distinct from the Unimplemented above.
  EXPECT_THAT(MakePcre2Backend("", /*case_insensitive=*/false), StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace xff::regex
