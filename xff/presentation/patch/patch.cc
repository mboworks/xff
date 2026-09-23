// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#include "xff/presentation/patch/patch.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "mbo/digest/digest.h"

namespace xff::patch {
namespace {
std::string Quote(std::string_view path) {
  std::string result = "\"";
  for (const unsigned char byte : path) {
    if (byte == '\\' || byte == '"') {
      result.push_back('\\');
      result.push_back(static_cast<char>(byte));
    } else if (byte < 32 || byte >= 127) {
      result.push_back('\\');
      result.push_back(static_cast<char>('0' + ((static_cast<std::uint32_t>(byte) >> 6U) & 7U)));
      result.push_back(static_cast<char>('0' + ((static_cast<std::uint32_t>(byte) >> 3U) & 7U)));
      result.push_back(static_cast<char>('0' + (static_cast<std::uint32_t>(byte) & 7U)));
    } else {
      result.push_back(static_cast<char>(byte));
    }
  }
  result.push_back('"');
  return result;
}

// RFC 1950/1951 stored blocks: a valid zlib stream without a compression dependency.
// Git's literal patch accepts stored blocks; output size trades compression for a lean core.
std::string StoredZlib(std::string_view content) {
  std::string result("\x78\x01", 2);
  std::uint32_t low = 1;
  std::uint32_t high = 0;
  std::size_t offset = 0;
  for (;;) {
    const auto length = static_cast<std::uint32_t>(std::min<std::size_t>(65'535, content.size() - offset));
    result.push_back(offset + length == content.size() ? '\x01' : '\0');
    result.push_back(static_cast<char>(length & 255U));
    result.push_back(static_cast<char>(length >> 8U));
    const auto inverse = (length ^ 0xffffU);
    result.push_back(static_cast<char>(inverse & 255U));
    result.push_back(static_cast<char>(inverse >> 8U));
    const std::string_view block = content.substr(offset, length);
    result.append(block);
    for (const unsigned char byte : block) {
      low = (low + byte) % 65'521;
      high = (high + low) % 65'521;
    }
    offset += length;
    if (offset == content.size()) {
      break;
    }
  }
  const std::uint32_t checksum = (high << 16U) | low;
  constexpr auto kShifts = std::to_array<unsigned>({24, 16, 8, 0});
  for (const unsigned shift : kShifts) {
    result.push_back(static_cast<char>((checksum >> shift) & 255U));
  }
  return result;
}

std::string Literal(std::string_view content) {
  constexpr std::string_view kAlphabet =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!#$%&()*+-;<=>?@^_`{|}~";
  const std::string compressed = StoredZlib(content);
  std::string result = absl::StrCat("literal ", content.size(), "\n");
  for (std::size_t offset = 0; offset < compressed.size(); offset += 52) {
    const std::string_view line = std::string_view(compressed).substr(offset, 52);
    result.push_back(static_cast<char>((line.size() <= 26 ? 'A' - 1 : 'a' - 27) + line.size()));
    for (std::size_t index = 0; index < line.size(); index += 4) {
      std::uint32_t value = 0;
      for (std::size_t byte = 0; byte < 4; ++byte) {
        value <<= 8U;
        if (index + byte < line.size()) {
          value |= static_cast<unsigned char>(line.at(index + byte));
        }
      }
      std::array<char, 5> encoded;
      for (std::size_t digit = encoded.size(); digit > 0; --digit) {
        encoded.at(digit - 1) = kAlphabet.at(value % 85);
        value /= 85;
      }
      result.append(encoded.data(), encoded.size());
    }
    result.push_back('\n');
  }
  result.push_back('\n');
  return result;
}
}  // namespace

absl::StatusOr<std::string> Creation(std::string_view path, std::string_view content, std::string_view mode) {
  if (path.empty() || path.starts_with('/') || path.contains('\0')) {
    return absl::InvalidArgumentError("-diff requires a nonempty relative output path");
  }
  for (const std::string_view component : absl::StrSplit(path, '/')) {
    if (component.empty() || component == "." || component == ".." || component == ".git") {
      return absl::InvalidArgumentError("-diff refuses unsafe output path components");
    }
  }
  if (mode != "100644" && mode != "100755" && mode != "120000") {
    return absl::InvalidArgumentError("-diff cannot represent this file mode");
  }
  std::string blob = absl::StrCat("blob ", content.size());
  blob.push_back('\0');
  blob.append(content);
  const std::string digest = mbo::digest::ToHexString(mbo::digest::sha1::Digest(blob));
  const std::string before = Quote(absl::StrCat("a/", path));
  const std::string after = Quote(absl::StrCat("b/", path));
  std::string result = absl::StrCat(
      "diff --git ", before, " ", after, "\nnew file mode ", mode, "\nindex 0000000000000000000000000000000000000000..",
      digest, "\n");
  if (content.empty()) {
    return result;
  }
  if (content.contains('\0')) {
    absl::StrAppend(&result, "GIT binary patch\n", Literal(content), Literal(""));
    return result;
  }
  const auto lines = std::count(content.begin(), content.end(), '\n') + (content.ends_with('\n') ? 0 : 1);
  absl::StrAppend(&result, "--- /dev/null\n+++ ", after, "\n@@ -0,0 +1,", lines, " @@\n");
  while (!content.empty()) {
    const auto end = content.find('\n');
    if (end == std::string_view::npos) {
      absl::StrAppend(&result, "+", content, "\n\\ No newline at end of file\n");
      break;
    }
    absl::StrAppend(&result, "+", content.substr(0, end + 1));
    content.remove_prefix(end + 1);
  }
  return result;
}
}  // namespace xff::patch
