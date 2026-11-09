// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_PRESENTATION_RENDER_RECORD_BUFFER_H_
#define XFF_PRESENTATION_RENDER_RECORD_BUFFER_H_

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace xff::render {

// One coordinator's reusable raw record. Consumers must finish using the borrowed bytes before
// returning and must not re-enter this buffer. Queued output must copy the bytes, as with a temporary
// Renderer::Record result. No escaping or color is applied.
class RawRecordBuffer final {
 public:
  static constexpr std::size_t kMaxRetainedBytes = 1UZ << 20U;
  static constexpr std::size_t kInlineBytes = 256;

  explicit RawRecordBuffer(char terminator) : terminator_(terminator) {}

  template<std::invocable<std::string_view> Consumer>
  void EmitRecord(std::string_view path, Consumer&& consume) {
    EmitRecord(path, terminator_, std::forward<Consumer>(consume));
  }

  // Explicit actions can alternate newline and NUL records while retaining the same storage.
  template<std::invocable<std::string_view> Consumer>
  void EmitRecord(std::string_view path, char terminator, Consumer&& consume) {
#if defined(__linux__)
    // The common short-path record needs only a copy and a terminator. Leave unused bytes
    // uninitialized and avoid growing or resizing the fallback string for these records.
    // Linux qualification establishes reduced instructions; macOS retains its qualified layout.
    if (path.size() < inline_.size()) {
      const std::span<char> record = std::span(inline_).first(path.size() + 1);
      std::ranges::copy(path, record.begin());
      record.back() = terminator;
      std::invoke(std::forward<Consumer>(consume), std::string_view(record.data(), record.size()));
      return;
    }
#endif
    // XFF_ABI_POINTER: std::string::resize_and_overwrite requires a writable buffer callback.
    bytes_.resize_and_overwrite(path.size() + 1, [path, terminator](char* data, std::size_t) noexcept {
      const std::span<char> record(data, path.size() + 1);
      std::ranges::copy(path, record.begin());
      record.back() = terminator;
      return record.size();
    });
    std::invoke(std::forward<Consumer>(consume), std::string_view(bytes_));
    if (bytes_.capacity() > kMaxRetainedBytes) {
      std::string{}.swap(bytes_);
    }
  }

  std::size_t RetainedCapacity() const {
    return std::max({
        bytes_.capacity(),
#if defined(__linux__)
        inline_.size(),
#endif
    });
  }

 private:
  std::string bytes_;
#if defined(__linux__)
  std::array<char, kInlineBytes> inline_;
#endif
  char terminator_;
};

}  // namespace xff::render

#endif  // XFF_PRESENTATION_RENDER_RECORD_BUFFER_H_
