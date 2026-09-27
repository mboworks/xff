// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#include <string>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xff/content/word.h"

namespace xff::content {
namespace {
using ::testing::IsFalse;
using ::testing::IsTrue;

struct WordUnicodeTest : ::testing::Test {};

TEST_F(WordUnicodeTest, UnicodeCategoriesAreWordsRatherThanAllNonAsciiBytes) {
  // Letters, combining/enclosing marks, decimal/letter numbers, connector punctuation,
  // Alphabetic symbols, and the otherwise invisible join controls.
  const auto words = {"a", "_", "é", "Ω", "Ⅷ", "４", "‿", "Ⓐ", "🅰", "\u0301", "\u20dd", "\u200c", "\u200d"};
  for (const std::string_view word : words) {
    EXPECT_THAT(text::Utf8::WordAfter(word, 0), IsTrue()) << word;
    EXPECT_THAT(text::Utf8::WordBefore(word, word.size()), IsTrue()) << word;
  }
  const auto nonwords = {"", " ", "!", "«", "»", "☀", "½", "\xff", "\xc3"};
  for (const std::string_view word : nonwords) {
    EXPECT_THAT(text::Utf8::WordAfter(word, 0), IsFalse()) << word;
    EXPECT_THAT(text::Utf8::WordBefore(word, word.size()), IsFalse()) << word;
  }
}

TEST_F(WordUnicodeTest, BoundariesRespectOffsetsAndIncompleteScalars) {
  const std::string text = "é!";
  EXPECT_THAT(text::Utf8::WordBefore(text, 0), IsFalse());
  EXPECT_THAT(text::Utf8::WordBefore(text, 1), IsFalse());
  EXPECT_THAT(text::Utf8::WordBefore(text, 2), IsTrue());
  EXPECT_THAT(text::Utf8::WordAfter(text, 1), IsFalse());
  EXPECT_THAT(text::Utf8::WordAfter(text, 2), IsFalse());
  EXPECT_THAT(text::Utf8::WordAfter(text, text.size()), IsFalse());
  EXPECT_THAT(text::Utf8::WordBefore(text, text.size() + 1), IsFalse());
}

// A missing operation or a merely bool-convertible return type must fail the contract.
struct MissingAfter {
  using Char = char;
  using View = std::string_view;

  static bool WordBefore(View, std::size_t) { return false; }
};

struct WrongResult {
  using Char = char;
  using View = std::string_view;

  static int WordBefore(View, std::size_t) { return 0; }

  static int WordAfter(View, std::size_t) { return 0; }
};

static_assert(!text::WordClassifier<MissingAfter>);
static_assert(!text::WordClassifier<WrongResult>);
static_assert(!text::WordClassifier<int>);

template<text::WordClassifier Classifier>
void CheckBoundaries(typename Classifier::View input) {
  EXPECT_THAT(text::HasWordBoundaries<Classifier>(input, 0, input.size()), IsTrue());
  EXPECT_THAT(text::HasWordBoundaries<Classifier>(input, 0, 0), IsFalse());
  EXPECT_THAT(text::HasWordBoundaries<Classifier>(input, input.size(), 0), IsFalse());
  EXPECT_THAT(text::HasWordBoundaries<Classifier>(input, input.size() + 1, 0), IsFalse());
  EXPECT_THAT(text::HasWordBoundaries<Classifier>(input, 0, input.size() + 1), IsFalse());
  EXPECT_THAT(text::HasWordBoundaries<Classifier>({}, 0, 0), IsTrue());
  EXPECT_THAT(Classifier::WordBefore({}, 0), IsFalse());
  EXPECT_THAT(Classifier::WordAfter({}, 0), IsFalse());
  EXPECT_THAT(Classifier::WordBefore(input, input.size() + 1), IsFalse());
  EXPECT_THAT(Classifier::WordAfter(input, input.size()), IsFalse());
}

TEST_F(WordUnicodeTest, OneConstrainedConsumerAcceptsEveryEncoding) {
  CheckBoundaries<text::Ascii>("word");
  CheckBoundaries<text::Latin1>("caf\xe9");
  CheckBoundaries<text::Utf8>("café");
  CheckBoundaries<text::Utf16>(u"café");
  CheckBoundaries<text::Utf32>(U"café");
}

TEST_F(WordUnicodeTest, BytesHaveExplicitAsciiOrLatin1Interpretation) {
  EXPECT_THAT(text::Ascii::WordAfter("\xe9", 0), IsFalse());
  EXPECT_THAT(text::Latin1::WordAfter("\xe9", 0), IsTrue());
  EXPECT_THAT(text::Latin1::WordBefore("\xe9", 1), IsTrue());
  EXPECT_THAT(text::Utf8::WordAfter("\xe9", 0), IsFalse());
  EXPECT_THAT(text::Latin1::WordAfter("\xd7", 0), IsFalse());  // Multiplication sign.
  EXPECT_THAT(text::Ascii::WordAfter("Z", 0), IsTrue());
  EXPECT_THAT(text::Ascii::WordBefore("9", 1), IsTrue());
  EXPECT_THAT(text::Utf8::WordBefore("a\x80", 2), IsFalse());
}

TEST_F(WordUnicodeTest, Utf16PairsAndUtf32ScalarsShareUnicodeSemantics) {
  EXPECT_THAT(text::Utf16::WordAfter(u"𐐀", 0), IsTrue());
  EXPECT_THAT(text::Utf16::WordBefore(u"𐐀", 2), IsTrue());
  EXPECT_THAT(text::Utf16::WordBefore(u"𐐀", 1), IsFalse());
  EXPECT_THAT(text::Utf16::WordAfter(u"𐐀", 1), IsFalse());
  const std::u16string missing_low{char16_t{0xD800}};
  const std::u16string bad_low{char16_t{0xD800}, u'a'};
  const std::u16string stray_low{u'a', char16_t{0xDC00}};
  EXPECT_THAT(text::Utf16::WordAfter(missing_low, 0), IsFalse());
  EXPECT_THAT(text::Utf16::WordAfter(bad_low, 0), IsFalse());
  EXPECT_THAT(text::Utf16::WordBefore(stray_low, 2), IsFalse());
  EXPECT_THAT(text::Utf32::WordBefore(U"𐐀", 1), IsTrue());
  EXPECT_THAT(text::Utf32::WordAfter(U"☀", 0), IsFalse());
  const std::u32string surrogate{char32_t{0xD800}};
  const std::u32string too_large{char32_t{0x110000}};
  EXPECT_THAT(text::Utf32::WordAfter(surrogate, 0), IsFalse());
  EXPECT_THAT(text::Utf32::WordAfter(too_large, 0), IsFalse());
}
}  // namespace
}  // namespace xff::content
