// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/matching/similarity/similarity.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::similarity {
namespace {

using ::testing::Eq;
using ::testing::Ge;
using ::testing::IsFalse;
using ::testing::IsTrue;

// Independent, deliberately allocating reference for the existing byte-word contract.
std::set<std::string> ReferenceByteShingles(std::string_view text, std::size_t width) {
  std::vector<std::string> words;
  std::string word;
  const auto flush = [&] {
    if (!word.empty()) {
      words.push_back(word);
      word.clear();
    }
  };
  for (const unsigned char byte : text) {
    const bool ascii_letter = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z');
    if (byte >= 128 || ascii_letter || (byte >= '0' && byte <= '9')) {
      word.push_back(static_cast<char>(byte >= 'A' && byte <= 'Z' ? byte + ('a' - 'A') : byte));
    } else {
      flush();
    }
  }
  flush();
  std::set<std::string> shingles;
  if (words.empty() || width == 0) {
    return shingles;
  }
  const std::size_t count = std::min(width, words.size());
  for (std::size_t start = 0; start <= words.size() - count; ++start) {
    std::string shingle = words.at(start);
    for (std::size_t offset = 1; offset < count; ++offset) {
      shingle.push_back('\0');
      shingle.append(words.at(start + offset));
    }
    shingles.insert(std::move(shingle));
  }
  return shingles;
}

int ReferenceBytePercent(std::string_view left, std::string_view right, std::size_t width) {
  if (width == 0) {
    return 0;
  }
  const auto lhs = ReferenceByteShingles(left, width);
  const auto rhs = ReferenceByteShingles(right, width);
  if (lhs.empty() && rhs.empty()) {
    return 100;
  }
  std::size_t common = 0;
  for (const auto& shingle : lhs) {
    common += rhs.contains(shingle) ? 1 : 0;
  }
  const std::size_t total = lhs.size() + rhs.size() - common;
  return static_cast<int>(((100 * common) + (total / 2)) / total);
}

struct SimilarityTest : ::testing::Test {};

TEST_F(SimilarityTest, ReusableReferenceMatchesIndependentByteWordOracle) {
  static constexpr auto kTexts = std::to_array<std::string_view>({
      "",
      "alpha",
      "Alpha beta",
      "alpha BETA! gamma delta",
      "other gamma delta",
      "one one two two three",
      " , ! ",
  });
  static constexpr auto kWidths = std::to_array<std::size_t>({0, 1, 3, 5, 100});
  for (const auto width : kWidths) {
    for (const auto right : kTexts) {
      const ByteWordShingleReference reference(std::string(right), width);
      EXPECT_THAT(reference.Matches(right, width), IsTrue());
      EXPECT_THAT(reference.Matches(right, width + 1), IsFalse());
      EXPECT_THAT(reference.StorageBytes(), Ge(right.size()));
      for (const auto left : kTexts) {
        SCOPED_TRACE(left);
        SCOPED_TRACE(right);
        SCOPED_TRACE(width);
        EXPECT_THAT(reference.Percent(left), Eq(ReferenceBytePercent(left, right, width)));
      }
    }
  }
}

TEST_F(SimilarityTest, ReusableReferenceKeepsShortAndLongBackingStorageAcrossOwnerMoves) {
  std::vector<ByteWordShingleReference> references;
  static constexpr auto kReferences = std::to_array<std::string_view>({
      "a b c",
      "one two three four five six seven eight nine ten eleven twelve",
  });
  references.reserve(kReferences.size());
  for (const auto text : kReferences) {
    references.emplace_back(std::string(text), 3);
  }
  for (std::size_t index = 0; index < kReferences.size(); ++index) {
    const std::string_view text = kReferences.at(index);
    ByteWordShingleReference moved(std::move(references.at(index)));
    EXPECT_THAT(moved.Matches(text, 3), IsTrue());
    EXPECT_THAT(moved.Percent(text), Eq(100));
    EXPECT_THAT(moved.Percent("a b other"), Eq(ReferenceBytePercent("a b other", text, 3)));
    EXPECT_THAT(references.at(index).Matches(text, 3), IsFalse());
    EXPECT_THAT(references.at(index).StorageBytes(), Eq(0));
    references.at(index) = std::move(moved);
    EXPECT_THAT(references.at(index).Percent(text), Eq(100));
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move): verify the explicit moved-from contract.
    EXPECT_THAT(moved.Percent(text), Eq(0));
  }
}

TEST_F(SimilarityTest, IdenticalWordShinglesScoreOneHundred) {
  EXPECT_THAT(WordShinglePercent("one two three four five six", "one two three four five six", 5), Eq(100));
}

TEST_F(SimilarityTest, JaccardUsesUniqueContiguousShingles) {
  // {one two three four five, two three four five six} versus
  // {one two three four five, two three four five seven}: intersection 1, union 3.
  EXPECT_THAT(WordShinglePercent("one two three four five six", "one two three four five seven", 5), Eq(33));
}

TEST_F(SimilarityTest, TokenizationFoldsAsciiCaseAndSeparatesPunctuation) {
  EXPECT_THAT(WordShinglePercent("Hello, WORLD! again", "hello world again", 3), Eq(100));
}

TEST_F(SimilarityTest, ShortInputsContributeOneWholeInputShingle) {
  EXPECT_THAT(WordShinglePercent("one two", "ONE TWO", 5), Eq(100));
  EXPECT_THAT(WordShinglePercent("one two", "one three", 5), Eq(0));
}

TEST_F(SimilarityTest, EmptyInputsHaveDefinedSetSemantics) {
  EXPECT_THAT(WordShinglePercent("", "", 5), Eq(100));
  EXPECT_THAT(WordShinglePercent("", "one", 5), Eq(0));
  EXPECT_THAT(WordShinglePercent("one", "", 5), Eq(0));
  EXPECT_THAT(WordShinglePercent("one", "one", 0), Eq(0));
}

TEST_F(SimilarityTest, DuplicateShinglesDoNotIncreaseTheirWeight) {
  EXPECT_THAT(WordShinglePercent("a b a b", "a b a", 2), Eq(100));
}

TEST_F(SimilarityTest, WordBoundariesCannotCollideInTheShingleRepresentation) {
  EXPECT_THAT(WordShinglePercent("ab c", "a bc", 2), Eq(0));
}

TEST_F(SimilarityTest, NormalizedSeparatorsAndLongWordsRetainExactSetSemantics) {
  const auto inputs = std::to_array<std::string>({
      "",
      "!!!...---",
      " a b c d e f ",
      "A\tB\nC\rD-E_F",
      "a a a a a a",
      "a b a b a b a",
      std::string("a\0b\0c", 5),
      std::string(256, 'A') + " " + std::string(512, 'b'),
      std::string(256, 'a') + " " + std::string(512, 'c'),
  });
  constexpr auto kWidths = std::to_array<std::size_t>({0, 1, 2, 3, 5, 100, std::numeric_limits<std::size_t>::max()});
  for (const auto& left : inputs) {
    for (const auto& right : inputs) {
      for (const auto width : kWidths) {
        SCOPED_TRACE(width);
        EXPECT_THAT(WordShinglePercent(left, right, width), Eq(ReferenceBytePercent(left, right, width)));
      }
    }
  }
}

TEST_F(SimilarityTest, AsymmetricSetsKeepScoresInBothProbeDirections) {
  for (const auto count : {8, 32, 1'000}) {
    SCOPED_TRACE(count);
    std::string large;
    for (int index = 0; index < count; ++index) {
      large.append("token").append(std::to_string(index)).push_back(' ');
    }
    constexpr std::string_view kSmall = "TOKEN0 token1 TOKEN2 token3";
    for (const auto width : {1UZ, 3UZ, 5UZ}) {
      SCOPED_TRACE(width);
      const auto expected = ReferenceBytePercent(large, kSmall, width);
      EXPECT_THAT(WordShinglePercent(large, kSmall, width), Eq(expected));
      EXPECT_THAT(WordShinglePercent(kSmall, large, width), Eq(expected));
      const ByteWordShingleReference large_reference(large, width);
      const ByteWordShingleReference small_reference(std::string(kSmall), width);
      EXPECT_THAT(large_reference.Percent(kSmall), Eq(expected));
      EXPECT_THAT(small_reference.Percent(large), Eq(expected));
    }
  }
}

TEST_F(SimilarityTest, DuplicateAndUniqueLongWordsPreserveIndependentSetScores) {
  static constexpr auto kPaddingBytes = std::to_array<std::size_t>({0, 15, 16, 22, 23, 64, 256});
  for (const auto padding : kPaddingBytes) {
    SCOPED_TRACE(padding);
    std::string left;
    std::string right;
    for (const auto index : {0, 1, 2, 3, 4, 5, 6, 7}) {
      const std::string prefix(padding, 'a');
      const std::string word = prefix + std::to_string(index);
      left.append(word).append(" ").append(word).append(" ");
      right.append(prefix).append(std::to_string(index + 1)).append(" ");
    }
    const auto expected = ReferenceBytePercent(left, right, 1);
    EXPECT_THAT(WordShinglePercent(left, right, 1), Eq(expected));
    const ByteWordShingleReference reference(right, 1);
    EXPECT_THAT(reference.Percent(left), Eq(expected));
    EXPECT_THAT(reference.Matches(right, 1), IsTrue());
  }
}

TEST_F(SimilarityTest, SeededOpaqueByteInputsMatchTheIndependentReference) {
  // This is a byte-tokenization test, not a Unicode case-folding test. All high-bit
  // bytes are word constituents, including byte sequences that are not valid UTF-8.
  // NOLINTNEXTLINE(bugprone-random-generator-seed,cert-msc32-c,cert-msc51-cpp): reproducible test corpus.
  std::mt19937 random(20'261'102);
  constexpr auto kWidths = std::to_array<std::size_t>({0, 1, 2, 5, 20});
  for (std::size_t sample = 0; sample < 1'000; ++sample) {
    std::string left;
    std::string right;
    const std::size_t length = random() % 256;
    for (std::size_t index = 0; index < length; ++index) {
      left.push_back(static_cast<char>(random() % 256));
      right.push_back(index % 5 == 0 ? static_cast<char>(random() % 256) : left.back());
    }
    for (const auto width : kWidths) {
      SCOPED_TRACE(sample);
      SCOPED_TRACE(width);
      const auto expected = ReferenceBytePercent(left, right, width);
      EXPECT_THAT(WordShinglePercent(left, right, width), Eq(expected));
      const ByteWordShingleReference reference(right, width);
      EXPECT_THAT(reference.Percent(left), Eq(expected));
      EXPECT_THAT(reference.Matches(right, width), IsTrue());
      EXPECT_THAT(WordShinglePercent(right, left, width), Eq(expected));
      EXPECT_THAT(WordShinglePercent(left, left, width), Eq(width == 0 ? 0 : 100));
    }
  }
}

}  // namespace
}  // namespace xff::similarity
