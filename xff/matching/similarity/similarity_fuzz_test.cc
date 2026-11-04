// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <span>
#include <string>
#include <string_view>

#include "xff/matching/similarity/similarity.h"

namespace {

constexpr std::size_t kMaxShingleWidth = 32;

}  // namespace

// XFF_ABI_POINTER: rules_fuzzing requires libFuzzer's C entry-point signature.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size == 0) {
    return 0;
  }
  const std::span<const std::uint8_t> bytes(data, size);
  const std::uint8_t control = bytes.front();
  const std::size_t width = control >= '0' && control <= '9' ? control - '0' : control % (kMaxShingleWidth + 1);
  const std::span<const std::uint8_t> payload = bytes.subspan(1);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const std::string_view input(reinterpret_cast<const char*>(payload.data()), payload.size());
  const std::size_t separator = input.find('\n');
  const std::string_view lhs = input.substr(0, separator);
  const std::string_view rhs = separator == std::string_view::npos ? std::string_view{} : input.substr(separator + 1);

  const int forward = xff::similarity::WordShinglePercent(lhs, rhs, width);
  // Deliberately reverse the arguments: symmetry is one of the public score contracts under test.
  // NOLINTNEXTLINE(readability-suspicious-call-argument)
  const int reverse = xff::similarity::WordShinglePercent(rhs, lhs, width);
  const xff::similarity::ByteWordShingleReference reference(std::string(rhs), width);
  if (forward < 0 || forward > 100 || forward != reverse || reference.Percent(lhs) != forward
      || !reference.Matches(rhs, width)) {
    std::abort();
  }
  const int threshold = (control % 103) - 1;
  const bool expected = forward >= threshold;
  if (xff::similarity::WordShingleAtLeast(lhs, rhs, width, threshold) != expected
      || reference.AtLeast(lhs, threshold) != expected
      || !xff::similarity::WordShingleAtLeast(lhs, rhs, width, std::numeric_limits<int>::min())
      || xff::similarity::WordShingleAtLeast(lhs, rhs, width, std::numeric_limits<int>::max())
      || !reference.AtLeast(lhs, std::numeric_limits<int>::min())
      || reference.AtLeast(lhs, std::numeric_limits<int>::max())) {
    std::abort();
  }
  if (width == 0) {
    if (forward != 0) {
      std::abort();
    }
  } else if (
      xff::similarity::WordShinglePercent(lhs, lhs, width) != 100
      || xff::similarity::WordShinglePercent(rhs, rhs, width) != 100) {
    std::abort();
  }
  return 0;
}
