// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"
#include "xff/engine/evaluate.h"
#include "xff/hash/hash.h"
#include "xff/parser/parser.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {
namespace {
using ::mbo::testing::EqualsText;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Optional;

struct HashFs final : vfs::FileSystem {
  std::string bytes = "a\nb\n";
  bool fail = false;
  mutable std::size_t reads = 0;

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view) const override { return std::vector<vfs::Entry>{}; }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view, bool) const override {
    return vfs::Metadata{.type = vfs::FileType::kRegular, .size = bytes.size()};
  }

  absl::StatusOr<std::string> ReadContent(std::string_view) const override {
    ++reads;
    return fail ? absl::StatusOr<std::string>{absl::PermissionDeniedError("denied fixture")} : bytes;
  }

  absl::Status Remove(std::string_view) const override {
    return absl::PermissionDeniedError("isolated read-only input");
  }

  bool Access(std::string_view, vfs::AccessMode) const override { return true; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override { return absl::NotFoundError("not a symlink"); }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return "memory"; }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }
};

struct Observation {
  bool matched = false;
  std::string text;
  std::size_t reads = 0;
  std::optional<bool> verified;
};

struct Defaults {
  std::string_view algorithm;
  std::string_view encoding;
};

struct Input {
  std::string_view bytes = "a\nb\n";
  bool fail = false;
};

struct HashPatternTest : ::testing::Test {
  static Observation Observe(
      const parser::Expr& expression,
      mbo::types::OptionalRef<const PreparedExpression> prepared,
      Defaults defaults,
      Input input = {}) {
    HashFs filesystem;
    filesystem.bytes = input.bytes;
    filesystem.fail = input.fail;
    Observation result;
    Control control;
    const auto sink = [&](std::string_view text) { result.text.append(text); };
    const Visit visit{
        .path = "root/file",
        .name = "file",
        .root = "root",
        .metadata = {.type = vfs::FileType::kRegular, .size = input.bytes.size()},
    };
    EvalContext context{
        .visit = visit,
        .emit = sink,
        .fs = filesystem,
        .now = absl::UnixEpoch(),
        .tz = absl::UTCTimeZone(),
        .hash_verification = result.verified,
        .hash_algorithm = defaults.algorithm,
        .hash_encoding = defaults.encoding,
        .control = control,
    };
    result.matched = prepared ? prepared->Evaluate(context).matched : Evaluate(expression, context);
    result.reads = filesystem.reads;
    return result;
  }
};

TEST_F(HashPatternTest, ReusedMovedPlansObserveEachContextsDefaultsWithoutOwningViews) {
  constexpr auto kDefaults = std::to_array<Defaults>({
      {},
      {.algorithm = "md5", .encoding = "base64"},
      {.algorithm = "sha1", .encoding = "hex"},
      {.algorithm = "garbage", .encoding = "base64"},
      {.algorithm = "sha512", .encoding = "garbage"},
  });
  constexpr auto kRequests = std::to_array<std::string_view>({
      "-hash",
      "-hash:md5/base64",
      "-hash:/base64",
      "-hash:sha1/",
  });
  for (const auto request : kRequests) {
    SCOPED_TRACE(request);
    ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"root", std::string(request)}));
    ASSERT_OK_AND_ASSIGN(auto initial, PreparedExpression::Prepare(*command.expression));
    const auto prepared = std::move(initial);
    for (const auto defaults : kDefaults) {
      SCOPED_TRACE(defaults.algorithm);
      SCOPED_TRACE(defaults.encoding);
      const auto reference = Observe(*command.expression, {}, defaults);
      const auto current = Observe(*command.expression, prepared, defaults);
      EXPECT_THAT(current.matched, Eq(reference.matched));
      EXPECT_THAT(current.text, EqualsText(reference.text));
      EXPECT_THAT(current.reads, Eq(reference.reads));
    }
  }
}

TEST_F(HashPatternTest, EveryRegisteredAlgorithmAndEncodingRetainsItsDigestOutput) {
  constexpr auto kEncodings = std::to_array<hash::Encoding>({
      hash::Encoding::kHex,
      hash::Encoding::kBase64,
  });
  for (const auto algorithm : hash::AlgorithmNames()) {
    for (const auto encoding : kEncodings) {
      const std::string option =
          "-hash:" + std::string(algorithm) + (encoding == hash::Encoding::kHex ? "/hex" : "/base64");
      SCOPED_TRACE(option);
      ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"root", option}));
      ASSERT_OK_AND_ASSIGN(const auto prepared, PreparedExpression::Prepare(*command.expression));
      const auto result = Observe(*command.expression, prepared, {.algorithm = "garbage", .encoding = "garbage"});
      const auto digest = hash::HashData(algorithm, "a\nb\n", encoding);
      EXPECT_THAT(
          digest.transform([](const std::string& value) { return value + "  root/file\n"; }),
          Optional(EqualsText(result.text)));
      EXPECT_THAT(result.reads, Eq(1));
      EXPECT_THAT(result.matched, IsTrue());
    }
  }
}

TEST_F(HashPatternTest, ByteInputsFailuresAndSkippedHashActionsRetainObservations) {
  const auto inputs = std::to_array<Input>({
      {.bytes = ""},
      {.bytes = std::string_view("a\0b", 3)},
      {.bytes = "unused", .fail = true},
  });
  ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"root", "-hash:md5/base64"}));
  ASSERT_OK_AND_ASSIGN(const auto prepared, PreparedExpression::Prepare(*command.expression));
  for (const auto input : inputs) {
    const auto reference = Observe(*command.expression, {}, {}, input);
    const auto current = Observe(*command.expression, prepared, {}, input);
    EXPECT_THAT(current.matched, Eq(reference.matched));
    EXPECT_THAT(current.text, EqualsText(reference.text));
    EXPECT_THAT(current.reads, Eq(reference.reads));
  }
  ASSERT_OK_AND_ASSIGN(const auto skipped, parser::Parse({"root", "-false", "-hash:md5/base64"}));
  ASSERT_OK_AND_ASSIGN(const auto skip_plan, PreparedExpression::Prepare(*skipped.expression));
  const auto result = Observe(*skipped.expression, skip_plan, {});
  EXPECT_THAT(result.matched, IsFalse());
  EXPECT_THAT(result.text, IsEmpty());
  EXPECT_THAT(result.reads, Eq(0));
  EXPECT_THAT(result.verified, Eq(std::nullopt));
}

TEST_F(HashPatternTest, MissingExpectedArgumentRemainsFalseWithoutPreparingAnOperand) {
  ASSERT_OK_AND_ASSIGN(auto command, parser::Parse({"root", "-hasheq", "unused"}));
  command.expression->args.clear();
  ASSERT_OK_AND_ASSIGN(const auto prepared, PreparedExpression::Prepare(*command.expression));
  const auto result = Observe(*command.expression, prepared, {});
  EXPECT_THAT(result.matched, IsFalse());
  EXPECT_THAT(result.text, IsEmpty());
  EXPECT_THAT(result.reads, Eq(0));
  EXPECT_THAT(result.verified, Optional(IsFalse()));
  EXPECT_THAT(prepared.OperandCount(), Eq(0));
}

TEST_F(HashPatternTest, MalformedSpecificationsAndExpectedTemplatesRetainConditionalReads) {
  constexpr auto kExpected = std::to_array<std::string_view>({
      "{lines}",
      "{def.MISSING}",
      "",
  });
  for (const auto expected : kExpected) {
    ASSERT_OK_AND_ASSIGN(auto command, parser::Parse({"root", "-hasheq", std::string(expected)}));
    command.expression->hash_spec = "bad-algorithm";
    ASSERT_OK_AND_ASSIGN(const auto prepared, PreparedExpression::Prepare(*command.expression));
    const auto reference = Observe(*command.expression, {}, {});
    const auto current = Observe(*command.expression, prepared, {});
    EXPECT_THAT(current.matched, IsFalse());
    EXPECT_THAT(current.text, IsEmpty());
    EXPECT_THAT(current.reads, Eq(reference.reads));
    EXPECT_THAT(current.verified, Optional(IsFalse()));
    EXPECT_THAT(reference.verified, Optional(IsFalse()));
  }
}
}  // namespace
}  // namespace xff::engine
