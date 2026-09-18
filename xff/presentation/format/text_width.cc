// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/presentation/format/text_width.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "utf8proc.h"

namespace xff::format {
namespace {

struct Scalar {
  utf8proc_int32_t value;
  std::size_t bytes;
};

Scalar Decode(std::string_view text) {
  utf8proc_int32_t value = 0;
  std::array<utf8proc_uint8_t, 4> bytes{};
  const std::size_t count = std::min(text.size(), bytes.size());
  for (std::size_t index = 0; index < count; ++index) {
    bytes.at(index) = static_cast<utf8proc_uint8_t>(text.at(index));
  }
  const auto length = utf8proc_iterate(bytes.data(), static_cast<utf8proc_ssize_t>(count), &value);
  return length > 0 ? Scalar{.value = value, .bytes = static_cast<std::size_t>(length)}
                    : Scalar{.value = 0xFFFD, .bytes = 1};
}

struct Grapheme {
  std::string_view text;
  std::size_t columns = 0;
};

std::vector<Grapheme> Graphemes(std::string_view text) {
  std::vector<Grapheme> result;
  utf8proc_int32_t previous = 0;
  utf8proc_int32_t state = 0;
  std::size_t start = 0;
  std::size_t columns = 0;
  std::size_t regional_indicators = 0;
  for (std::size_t pos = 0; pos < text.size();) {
    const Scalar scalar = Decode(text.substr(pos));
    if (pos != 0 && utf8proc_grapheme_break_stateful(previous, scalar.value, &state)) {
      result.push_back({.text = text.substr(start, pos - start), .columns = columns});
      start = pos;
      columns = 0;
      regional_indicators = 0;
    }
    columns = std::max(columns, static_cast<std::size_t>(utf8proc_charwidth(scalar.value)));
    if (scalar.value >= 0x1F1E6 && scalar.value <= 0x1F1FF) {
      ++regional_indicators;
    }
    // Emoji presentation, keycaps, and paired regional indicators occupy two columns
    // in ordinary terminals. Ambiguous-width characters retain the library's width one.
    if (scalar.value == 0xFE0F || scalar.value == 0x20E3 || regional_indicators == 2) {
      columns = 2;
    }
    previous = scalar.value;
    pos += scalar.bytes;
  }
  if (start < text.size()) {
    result.push_back({.text = text.substr(start), .columns = columns});
  }
  return result;
}

}  // namespace

std::size_t TextColumns(std::string_view text) {
  if (std::ranges::all_of(text, [](unsigned char ch) { return ch >= 0x20 && ch <= 0x7E; })) {
    return text.size();
  }
  std::size_t width = 0;
  for (const Grapheme& grapheme : Graphemes(text)) {
    width += grapheme.columns;
  }
  return width;
}

std::vector<std::string> WrapColumns(std::string_view text, std::size_t width) {
  if (width == 0 || text.empty()) {
    return {std::string(text)};
  }
  std::vector<std::string> lines;
  std::string line;
  std::size_t columns = 0;
  for (const Grapheme& grapheme : Graphemes(text)) {
    if (!line.empty() && columns + grapheme.columns > width) {
      lines.push_back(std::move(line));
      line.clear();
      columns = 0;
    }
    line.append(grapheme.text);
    columns += grapheme.columns;
  }
  if (!line.empty()) {
    lines.push_back(std::move(line));
  }
  return lines;
}

}  // namespace xff::format
