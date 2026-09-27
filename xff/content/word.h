// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#ifndef XFF_CONTENT_WORD_H_
#define XFF_CONTENT_WORD_H_

#include <concepts>
#include <cstddef>
#include <string_view>

namespace xff::content::text {
// Offsets are code-unit offsets. Implementations inspect the complete scalar immediately
// before/after the offset. Malformed/partial scalars are non-word. No encoding autodetection.
// Bytes >= 128 are non-word. Also used for arbitrary-byte search.
struct Ascii {
  using Char = char;
  using View = std::basic_string_view<Char>;
  static bool WordBefore(View text, std::size_t offset);
  static bool WordAfter(View text, std::size_t offset);
};

// Every byte denotes its ISO-8859-1 scalar; not Windows-1252.
struct Latin1 {
  using Char = char;
  using View = std::basic_string_view<Char>;
  static bool WordBefore(View text, std::size_t offset);
  static bool WordAfter(View text, std::size_t offset);
};

// UTF-8 byte sequences, including ASCII.
struct Utf8 {
  using Char = char;
  using View = std::basic_string_view<Char>;
  static bool WordBefore(View text, std::size_t offset);
  static bool WordAfter(View text, std::size_t offset);
};

// Native-endian UTF-16 code units; file byte order must already be decoded.
struct Utf16 {
  using Char = char16_t;
  using View = std::basic_string_view<Char>;
  static bool WordBefore(View text, std::size_t offset);
  static bool WordAfter(View text, std::size_t offset);
};

// Unicode scalar values; surrogates and values above U+10FFFF are invalid.
struct Utf32 {
  using Char = char32_t;
  using View = std::basic_string_view<Char>;
  static bool WordBefore(View text, std::size_t offset);
  static bool WordAfter(View text, std::size_t offset);
};

// This contract covers word classification, not transcoding or file decoding.
template<typename Classifier>
concept WordClassifier = requires(typename Classifier::View text, std::size_t offset) {
  requires std::same_as<typename Classifier::View, std::basic_string_view<typename Classifier::Char>>;
  { Classifier::WordBefore(text, offset) } -> std::same_as<bool>;
  { Classifier::WordAfter(text, offset) } -> std::same_as<bool>;
};

template<WordClassifier Classifier>
bool HasWordBoundaries(typename Classifier::View text, std::size_t start, std::size_t length) {
  return start <= text.size() && length <= text.size() - start && !Classifier::WordBefore(text, start)
         && !Classifier::WordAfter(text, start + length);
}

static_assert(WordClassifier<Ascii>);
static_assert(WordClassifier<Latin1>);
static_assert(WordClassifier<Utf8>);
static_assert(WordClassifier<Utf16>);
static_assert(WordClassifier<Utf32>);
}  // namespace xff::content::text
#endif  // XFF_CONTENT_WORD_H_
