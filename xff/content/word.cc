// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#include "xff/content/word.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

#include "utf8proc.h"

namespace xff::content::text {
namespace {
bool IsAsciiWord(unsigned char value) {
  return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9')
         || value == '_';
}

bool IsUnicodeWord(char32_t scalar) {
  if (scalar > 0x10FFFF || (scalar >= 0xD800 && scalar <= 0xDFFF)) {
    return false;
  }
  const auto category = utf8proc_category(static_cast<utf8proc_int32_t>(scalar));
  // Alphabetic outside Letter, Letter_Number and Mark: Unicode PropList.txt's
  // Other_Alphabetic symbol ranges (Unicode 16). utf8proc supplies all categories.
  const bool alphabetic_symbol = (scalar >= 0x24B6 && scalar <= 0x24E9) || (scalar >= 0x1F130 && scalar <= 0x1F149)
                                 || (scalar >= 0x1F150 && scalar <= 0x1F169)
                                 || (scalar >= 0x1F170 && scalar <= 0x1F189);
  return (category >= UTF8PROC_CATEGORY_LU && category <= UTF8PROC_CATEGORY_ME) || category == UTF8PROC_CATEGORY_ND
         || category == UTF8PROC_CATEGORY_NL || category == UTF8PROC_CATEGORY_PC || scalar == 0x200C || scalar == 0x200D
         || alphabetic_symbol;
}

bool IsUtf8Word(std::string_view text, bool complete = false) {
  if (text.empty()) {
    return false;
  }
  const auto first = static_cast<unsigned char>(text.front());
  if (first < 128) {
    return (!complete || text.size() == 1) && IsAsciiWord(first);
  }
  std::array<utf8proc_uint8_t, 4> bytes{};
  const std::size_t count = std::min(bytes.size(), text.size());
  for (std::size_t index = 0; index < count; ++index) {
    bytes.at(index) = static_cast<utf8proc_uint8_t>(text.at(index));
  }
  utf8proc_int32_t scalar = 0;
  const auto length = utf8proc_iterate(bytes.data(), static_cast<utf8proc_ssize_t>(count), &scalar);
  return length > 0 && (!complete || static_cast<std::size_t>(length) == text.size())
         && IsUnicodeWord(static_cast<char32_t>(scalar));
}

}  // namespace

bool Utf8::WordAfter(std::string_view text, std::size_t offset) {
  return offset < text.size() && IsUtf8Word(text.substr(offset));
}

bool Utf8::WordBefore(std::string_view text, std::size_t offset) {
  if (offset == 0 || offset > text.size()) {
    return false;
  }
  std::size_t start = offset - 1;
  while (start > 0 && offset - start < 4 && (static_cast<unsigned char>(text.at(start)) & 0xC0U) == 0x80U) {
    --start;
  }
  return IsUtf8Word(text.substr(start, offset - start), true);
}

bool Ascii::WordAfter(std::string_view text, std::size_t offset) {
  return offset < text.size() && IsAsciiWord(static_cast<unsigned char>(text.at(offset)));
}

bool Ascii::WordBefore(std::string_view text, std::size_t offset) {
  return offset != 0 && offset <= text.size() && WordAfter(text, offset - 1);
}

bool Latin1::WordAfter(std::string_view text, std::size_t offset) {
  return offset < text.size() && IsUnicodeWord(static_cast<unsigned char>(text.at(offset)));
}

bool Latin1::WordBefore(std::string_view text, std::size_t offset) {
  return offset != 0 && offset <= text.size() && WordAfter(text, offset - 1);
}

bool Utf32::WordAfter(std::u32string_view text, std::size_t offset) {
  return offset < text.size() && IsUnicodeWord(text.at(offset));
}

bool Utf32::WordBefore(std::u32string_view text, std::size_t offset) {
  return offset != 0 && offset <= text.size() && WordAfter(text, offset - 1);
}

bool Utf16::WordAfter(std::u16string_view text, std::size_t offset) {
  if (offset >= text.size()) {
    return false;
  }
  char32_t scalar = text.at(offset);
  if (scalar >= 0xD800 && scalar <= 0xDBFF) {
    if (offset + 1 == text.size() || text.at(offset + 1) < 0xDC00 || text.at(offset + 1) > 0xDFFF) {
      return false;
    }
    scalar = 0x10000 + ((scalar - 0xD800U) << 10U) + (text.at(offset + 1) - 0xDC00);
  }
  return IsUnicodeWord(scalar);
}

bool Utf16::WordBefore(std::u16string_view text, std::size_t offset) {
  if (offset == 0 || offset > text.size()) {
    return false;
  }
  std::size_t start = offset - 1;
  if (start > 0 && text.at(start) >= 0xDC00 && text.at(start) <= 0xDFFF) {
    --start;
    if (text.at(start) < 0xD800 || text.at(start) > 0xDBFF) {
      return false;
    }
  }
  return WordAfter(text.substr(0, offset), start);
}
}  // namespace xff::content::text
