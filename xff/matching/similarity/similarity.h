// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_MATCHING_SIMILARITY_SIMILARITY_H_
#define XFF_MATCHING_SIMILARITY_SIMILARITY_H_

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace xff::similarity {

// Jaccard similarity of the unique contiguous word-shingle sets, rounded to an integer percentage
// in [0, 100]. Words are maximal runs of ASCII alphanumerics or bytes >= 0x80; ASCII case is folded,
// with no Unicode decoding or non-ASCII case folding. ASCII punctuation/whitespace are separators. A non-empty input
// shorter than `width` contributes one shingle containing all its words, so short files remain meaningfully comparable.
// Width zero is invalid and returns 0.
[[nodiscard]] int WordShinglePercent(std::string_view lhs, std::string_view rhs, std::size_t width);

// Boolean form with exactly WordShinglePercent rounding and width semantics.
[[nodiscard]] bool WordShingleAtLeast(std::string_view lhs, std::string_view rhs, std::size_t width, int threshold);

// A stable immutable byte-word representation. Moving the owner preserves its backing storage;
// copying is deliberately unavailable. This is not a filesystem cache or Unicode decoding.
class ByteWordShingleReference final {
 public:
  ByteWordShingleReference() noexcept;
  ByteWordShingleReference(std::string bytes, std::size_t width);
  ~ByteWordShingleReference();
  ByteWordShingleReference(ByteWordShingleReference&&) noexcept;
  ByteWordShingleReference& operator=(ByteWordShingleReference&&) noexcept;
  ByteWordShingleReference(const ByteWordShingleReference&) = delete;
  ByteWordShingleReference& operator=(const ByteWordShingleReference&) = delete;

  [[nodiscard]] bool Matches(std::string_view bytes, std::size_t width) const;
  [[nodiscard]] int Percent(std::string_view candidate) const;
  [[nodiscard]] bool AtLeast(std::string_view candidate, int threshold) const;
  // Tracked capacity only: excludes allocator metadata and hash-table control bytes, not RSS.
  [[nodiscard]] std::size_t StorageBytes() const;

 private:
  struct Data;
  std::unique_ptr<const Data> data_;
};

}  // namespace xff::similarity

#endif  // XFF_MATCHING_SIMILARITY_SIMILARITY_H_
