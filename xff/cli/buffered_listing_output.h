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

#ifndef XFF_CLI_BUFFERED_LISTING_OUTPUT_H_
#define XFF_CLI_BUFFERED_LISTING_OUTPUT_H_

#include <array>
#include <cstddef>
#include <ostream>
#include <streambuf>
#include <utility>

namespace xff::cli {

// Batch non-interactive listing records before the existing stdout buffer. In particular,
// retain ostream flush semantics: stderr's tie flushes these bytes before diagnostics.
// Terminal-taking expressions and pagers keep their direct output behavior.
class BufferedListingOutput final : public std::streambuf {
 public:
  static constexpr std::size_t kCapacity = 4'096;

  explicit BufferedListingOutput(std::ostream& output) : output_(output), original_(*output.rdbuf()) {
    setp(buffer_.begin(), buffer_.end());
    output_.rdbuf(this);
  }

  ~BufferedListingOutput() override {
    // Admission requires the ordinary non-throwing stream policy. Restore any failed
    // state after rdbuf(), which otherwise clears it while restoring the original buffer.
    output_.flush();
    const auto state = output_.rdstate();
    output_.rdbuf(&original_);
    output_.clear(state);
  }

  BufferedListingOutput(const BufferedListingOutput&) = delete;
  BufferedListingOutput& operator=(const BufferedListingOutput&) = delete;
  BufferedListingOutput(BufferedListingOutput&&) = delete;
  BufferedListingOutput& operator=(BufferedListingOutput&&) = delete;

 protected:
  int sync() override { return FlushBuffer() ? original_.pubsync() : -1; }

  int_type overflow(int_type value) override {
    if (!FlushBuffer()) {
      return traits_type::eof();
    }
    if (!traits_type::eq_int_type(value, traits_type::eof())) {
      *pptr() = traits_type::to_char_type(value);
      pbump(1);
    }
    return traits_type::not_eof(value);
  }

  // XFF_ABI_POINTER: std::streambuf requires this borrowed character-buffer override.
  std::streamsize xsputn(const char* data, std::streamsize count) override {
    if (count <= 0) {
      return 0;
    }
    if (std::cmp_greater_equal(count, buffer_.size())) {
      return FlushBuffer() ? original_.sputn(data, count) : 0;
    }
    if (count > epptr() - pptr() && !FlushBuffer()) {
      return 0;
    }
    traits_type::copy(pptr(), data, static_cast<std::size_t>(count));
    pbump(static_cast<int>(count));
    return count;
  }

 private:
  bool FlushBuffer() {
    const auto count = static_cast<std::streamsize>(pptr() - pbase());
    setp(buffer_.begin(), buffer_.end());
    return count == 0 || original_.sputn(buffer_.data(), count) == count;
  }

  std::ostream& output_;
  std::streambuf& original_;
  // Default initialization intentionally leaves unused byte storage untouched.
  std::array<char, kCapacity> buffer_;
};

}  // namespace xff::cli

#endif  // XFF_CLI_BUFFERED_LISTING_OUTPUT_H_
