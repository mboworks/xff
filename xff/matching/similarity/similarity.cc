// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/matching/similarity/similarity.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/ascii.h"

namespace xff::similarity {
namespace {

bool IsByteWordChar(char value) {
  return static_cast<unsigned char>(value) >= 0x80 || absl::ascii_isalnum(value);
}

std::vector<std::string> OwnedByteWords(std::string_view text) {
  std::vector<std::string> words;
  std::string word;
  for (const char value : text) {
    if (IsByteWordChar(value)) {
      word.push_back(absl::ascii_tolower(value));
    } else if (!word.empty()) {
      words.push_back(std::move(word));
      word.clear();
    }
  }
  if (!word.empty()) {
    words.push_back(std::move(word));
  }
  return words;
}

absl::flat_hash_set<std::string> OwnedSingleByteShingles(std::string_view text) {
  const auto words = OwnedByteWords(text);
  absl::flat_hash_set<std::string> result;
  for (const auto& word : words) {
    result.insert(word);
  }
  return result;
}

struct NormalizedByteWords {
  std::string bytes;
  std::size_t count = 0;
};

NormalizedByteWords NormalizeByteWords(std::string_view text) {
  NormalizedByteWords result;
  bool in_word = false;
  for (const char value : text) {
    if (!IsByteWordChar(value)) {
      in_word = false;
      continue;
    }
    if (!in_word) {
      if (result.count != 0) {
        result.bytes.push_back('\0');
      }
      ++result.count;
      in_word = true;
    }
    result.bytes.push_back(absl::ascii_tolower(value));
  }
  return result;
}

// The borrowed input must remain stable until the returned views are destroyed.
absl::flat_hash_set<std::string_view> ByteWordShingles(const NormalizedByteWords& words, std::size_t width) {
  absl::flat_hash_set<std::string_view> result;
  if (words.count == 0 || width == 0) {
    return result;
  }
  const std::string_view bytes = words.bytes;
  if (width >= words.count) {
    result.insert(bytes);
    return result;
  }
  std::size_t end = 0;
  // There are more words than the window, so each initial boundary exists.
  for (std::size_t word = 0; word < width; ++word) {
    end = bytes.find('\0', end);
    if (word + 1 < width) {
      ++end;
    }
  }
  std::size_t begin = 0;
  while (true) {
    result.insert(bytes.substr(begin, end - begin));
    if (end == bytes.size()) {
      break;
    }
    begin = bytes.find('\0', begin) + 1;
    end = bytes.find('\0', end + 1);
    if (end == std::string_view::npos) {
      end = bytes.size();
    }
  }
  return result;
}

template<typename ShingleSet>
int ShinglePercent(const ShingleSet& left, const ShingleSet& right) {
  if (left.empty() && right.empty()) {
    return 100;
  }
  std::size_t intersection = 0;
  for (const auto& shingle : left) {
    intersection += right.contains(shingle) ? 1 : 0;
  }
  const std::size_t union_size = left.size() + right.size() - intersection;
  return static_cast<int>(((100 * intersection) + (union_size / 2)) / union_size);
}

}  // namespace

int WordShinglePercent(std::string_view lhs, std::string_view rhs, std::size_t width) {
  if (width == 0) {
    return 0;
  }
  if (lhs == rhs) {
    return 100;
  }
  if (width == 1) {
    return ShinglePercent(OwnedSingleByteShingles(lhs), OwnedSingleByteShingles(rhs));
  }
  const auto left_words = NormalizeByteWords(lhs);
  const auto right_words = NormalizeByteWords(rhs);
  return ShinglePercent(ByteWordShingles(left_words, width), ByteWordShingles(right_words, width));
}

}  // namespace xff::similarity
