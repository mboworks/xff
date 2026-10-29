// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#include "xff/presentation/patch/patch.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "mbo/diff/diff.h"
#include "mbo/digest/digest.h"
#include "mbo/file/artefact.h"
#include "mbo/status/status_macros.h"

namespace xff::patch {
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

std::string HeaderName(std::string_view path) {
  for (const unsigned char byte : path) {
    if (byte <= 32 || byte >= 127 || byte == '\\' || byte == '"') {
      return Quote(path);
    }
  }
  return std::string(path);
}

namespace {
absl::Status ValidatePath(std::string_view path, bool destination = true) {
  if (path.empty() || path.starts_with('/') || path.contains('\0')) {
    return absl::InvalidArgumentError("diff requires a nonempty relative output path");
  }
  for (const std::string_view component : absl::StrSplit(path, '/')) {
    if (component.empty() || component == "." || component == ".." || (destination && component == ".git")) {
      return absl::InvalidArgumentError("diff refuses unsafe output path components");
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> InjectedRoot(std::string_view value) {
  if (value.empty()) {
    return std::string();
  }
  std::string root(value);
  if (root.ends_with('/')) {
    root.pop_back();
  }
  MBO_RETURN_IF_ERROR(ValidatePath(root));
  return root;
}

absl::StatusOr<SidePaths> ConfigureSide(SidePaths side, std::string_view key, std::string_view value) {
  if (key == "strip") {
    if (value.empty() || value.find_first_not_of("0123456789") != std::string_view::npos
        || !absl::SimpleAtoi(value, &side.strip)) {
      return absl::InvalidArgumentError(absl::StrCat("invalid diff strip count: ", value));
    }
  } else if (key == "root") {
    MBO_ASSIGN_OR_RETURN(side.root, InjectedRoot(value));
  } else if (key == "prefix") {
    MBO_RETURN_IF_ERROR(ValidatePath(absl::StrCat(value, "file")));
    side.prefix = value;
  } else if (key == "rewrite") {
    MBO_ASSIGN_OR_RETURN(auto rewrite, fields::RewriteProgram::Compile(value));
    side.rewrites.push_back(std::move(rewrite));
  } else {
    return absl::InvalidArgumentError(absl::StrCat("unknown diff path option: ", key));
  }
  return side;
}

absl::Status CheckClaim(
    const std::map<std::string, std::string>& claimed,
    std::string_view label,
    std::string_view source) {
  if (source.empty()) {
    return absl::OkStatus();
  }
  const auto found = claimed.find(std::string(label));
  if (found != claimed.end() && found->second != source) {
    return absl::InvalidArgumentError(
        absl::StrCat(
            "diff output path collision at ", Quote(label), ": ", Quote(found->second), " and ", Quote(source)));
  }
  return absl::OkStatus();
}

std::string BlobId(std::string_view content) {
  std::string blob = absl::StrCat("blob ", content.size());
  blob.push_back('\0');
  blob.append(content);
  return mbo::digest::ToHexString(mbo::digest::sha1::Digest(blob));
}

absl::Status ValidateMode(std::string_view mode) {
  if (mode != "100644" && mode != "100755" && mode != "120000") {
    return absl::InvalidArgumentError("diff cannot represent this file mode");
  }
  return absl::OkStatus();
}

absl::Status ValidateUpdate(const Names& names, File left, File right) {
  MBO_RETURN_IF_ERROR(ValidatePath(names.left));
  MBO_RETURN_IF_ERROR(ValidatePath(names.right));
  MBO_RETURN_IF_ERROR(ValidateMode(left.mode));
  return ValidateMode(right.mode);
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

absl::StatusOr<std::string> SidePaths::Map(std::string_view logical_path) const {
  MBO_RETURN_IF_ERROR(ValidatePath(logical_path, false));
  for (std::size_t remaining = strip; remaining != 0; --remaining) {
    const auto slash = logical_path.find('/');
    if (slash == std::string_view::npos) {
      return absl::InvalidArgumentError("diff strip count consumes the entire logical path");
    }
    logical_path.remove_prefix(slash + 1);
  }
  std::string result = root.empty() ? std::string(logical_path) : absl::StrCat(root, "/", logical_path);
  for (const auto& rewrite : rewrites) {
    result = rewrite.Apply(result);
  }
  MBO_RETURN_IF_ERROR(ValidatePath(result));
  result.insert(0, prefix);
  MBO_RETURN_IF_ERROR(ValidatePath(result));
  return result;
}

absl::StatusOr<Paths> Paths::Resolve(absl::Span<const std::string> globals) {
  Paths paths;
  for (const std::string& global : globals) {
    std::string_view flag = global;
    if (!flag.starts_with("--diff-left-") && !flag.starts_with("--diff-right-")) {
      continue;
    }
    SidePaths& side = flag.starts_with("--diff-left-") ? paths.left : paths.right;
    flag.remove_prefix(flag.starts_with("--diff-left-") ? 12 : 13);
    const auto equals = flag.find('=');
    if (equals == std::string_view::npos) {
      return absl::InvalidArgumentError(absl::StrCat("missing value for ", global));
    }
    const auto key = flag.substr(0, equals);
    const auto value = flag.substr(equals + 1);
    MBO_ASSIGN_OR_RETURN(side, ConfigureSide(std::move(side), key, value));
  }
  return paths;
}

absl::Status Destinations::Claim(const Names& names, std::string_view left_source, std::string_view right_source) {
  MBO_RETURN_IF_ERROR(CheckClaim(left_, names.left, left_source));
  MBO_RETURN_IF_ERROR(CheckClaim(right_, names.right, right_source));
  if (!left_source.empty()) {
    left_.try_emplace(names.left, left_source);
  }
  if (!right_source.empty()) {
    right_.try_emplace(names.right, right_source);
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> Creation(std::string_view path, std::string_view content, std::string_view mode) {
  MBO_RETURN_IF_ERROR(ValidatePath(path));
  return AbsentSide({.left = absl::StrCat("a/", path), .right = absl::StrCat("b/", path)}, content, mode, false);
}

absl::StatusOr<std::string> AbsentSide(
    const Names& names,
    std::string_view content,
    std::string_view mode,
    bool reverse) {
  MBO_RETURN_IF_ERROR(ValidatePath(names.left));
  MBO_RETURN_IF_ERROR(ValidatePath(names.right));
  MBO_RETURN_IF_ERROR(ValidateMode(mode));
  const std::string digest = BlobId(content);
  const std::string before = Quote(names.left);
  const std::string after = Quote(names.right);
  std::string result = absl::StrCat(
      "diff --git ", before, " ", after, reverse ? "\ndeleted file mode " : "\nnew file mode ", mode, "\nindex ",
      reverse ? digest : "0000000000000000000000000000000000000000", "..",
      reverse ? "0000000000000000000000000000000000000000" : digest, "\n");
  if (content.empty()) {
    return result;
  }
  if (content.contains('\0')) {
    absl::StrAppend(&result, "GIT binary patch\n", Literal(reverse ? "" : content), Literal(reverse ? content : ""));
    return result;
  }
  const auto lines = std::count(content.begin(), content.end(), '\n') + (content.ends_with('\n') ? 0 : 1);
  if (reverse) {
    absl::StrAppend(&result, "--- ", before, "\n+++ /dev/null\n@@ -1,", lines, " +0,0 @@\n");
  } else {
    absl::StrAppend(&result, "--- /dev/null\n+++ ", after, "\n@@ -0,0 +1,", lines, " @@\n");
  }
  const std::string_view marker = reverse ? "-" : "+";
  while (!content.empty()) {
    const auto end = content.find('\n');
    if (end == std::string_view::npos) {
      absl::StrAppend(&result, marker, content, "\n\\ No newline at end of file\n");
      break;
    }
    absl::StrAppend(&result, marker, content.substr(0, end + 1));
    content.remove_prefix(end + 1);
  }
  return result;
}

absl::StatusOr<std::string> Update(const Names& names, File left, File right, const mbo::diff::DiffOptions& options) {
  MBO_RETURN_IF_ERROR(ValidateUpdate(names, left, right));
  if ((left.mode == "120000") != (right.mode == "120000")) {
    MBO_ASSIGN_OR_RETURN(auto removed, AbsentSide(names, left.content, left.mode, true));
    MBO_ASSIGN_OR_RETURN(const auto created, AbsentSide(names, right.content, right.mode, false));
    removed.append(created);
    return removed;
  }
  if (left.content == right.content && left.mode == right.mode) {
    return std::string();
  }
  std::string result = absl::StrCat("diff --git ", Quote(names.left), " ", Quote(names.right), "\n");
  if (left.mode != right.mode) {
    absl::StrAppend(&result, "old mode ", left.mode, "\nnew mode ", right.mode, "\n");
  }
  if (left.content == right.content) {
    return result;
  }
  absl::StrAppend(&result, "index ", BlobId(left.content), "..", BlobId(right.content));
  if (left.mode == right.mode) {
    absl::StrAppend(&result, " ", left.mode);
  }
  result.push_back('\n');
  if (left.content.contains('\0') || right.content.contains('\0')) {
    absl::StrAppend(&result, "GIT binary patch\n", Literal(right.content), Literal(left.content));
  } else {
    MBO_ASSIGN_OR_RETURN(
        const auto diff,
        mbo::diff::Diff::FileDiff(
            mbo::file::Artefact{.data = std::string(left.content), .name = HeaderName(names.left)},
            mbo::file::Artefact{.data = std::string(right.content), .name = HeaderName(names.right)}, options));
    result.append(diff);
  }
  return result;
}
}  // namespace xff::patch
