// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/archive/archive_extension.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"

namespace xff::archive {
namespace {
struct NamedSourceDecoder {
  std::string name;
  SourceDecoder decoder;
};

std::vector<NamedSourceDecoder>& SourceDecoders() {
  static std::vector<NamedSourceDecoder> decoders;
  return decoders;
}

std::vector<CompressionExtension>& Extensions() {
  static std::vector<CompressionExtension> extensions;
  return extensions;
}

bool HasSuffix(std::string_view lower_name, std::string_view suffix) {
  return lower_name.size() > suffix.size() && lower_name.ends_with(suffix);
}

}  // namespace

void RegisterCompressionExtension(CompressionExtension extension) {
  auto& extensions = Extensions();
  const auto found = absl::c_find_if(
      extensions, [&extension](const CompressionExtension& current) { return current.name == extension.name; });
  if (found == extensions.end()) {
    extensions.push_back(std::move(extension));
  } else {
    *found = std::move(extension);
  }
}

mbo::types::OptionalRef<const CompressionExtension> CompressionExtensionFor(std::string_view container) {
  const std::string lower = absl::AsciiStrToLower(container);
  for (const auto& extension : Extensions()) {
    if (absl::c_any_of(extension.suffixes, [&lower](const std::string& suffix) {
          return HasSuffix(lower, absl::AsciiStrToLower(suffix));
        })) {
      return extension;
    }
  }
  return std::nullopt;
}

std::optional<std::string> CompressionExtensionStem(std::string_view container) {
  const std::string_view::size_type slash = container.rfind('/');
  const std::string_view name = slash == std::string_view::npos ? container : container.substr(slash + 1);
  const std::string lower = absl::AsciiStrToLower(name);
  const auto extension = CompressionExtensionFor(name);
  if (!extension.has_value()) {
    return std::nullopt;
  }
  std::string_view best;
  for (const std::string& suffix : extension->suffixes) {
    const std::string lower_suffix = absl::AsciiStrToLower(suffix);
    if (lower_suffix.size() > best.size() && HasSuffix(lower, lower_suffix)) {
      best = suffix;
    }
  }
  // CompressionExtensionFor() selected this extension from the same suffix set, so at least one
  // suffix necessarily matched. The loop above only refines that match to the longest spelling.
  return std::string(name.substr(0, name.size() - best.size()));
}

std::vector<ReadFormatInfo> CompressionExtensionReadFormats() {
  std::vector<ReadFormatInfo> formats;
  for (const auto& extension : Extensions()) {
    formats.insert(formats.end(), extension.read_formats.begin(), extension.read_formats.end());
  }
  return formats;
}

std::vector<std::string> CompressionExtensionPackFormats() {
  std::vector<std::string> formats;
  for (const auto& extension : Extensions()) {
    formats.insert(formats.end(), extension.pack_formats.begin(), extension.pack_formats.end());
  }
  return formats;
}

std::vector<PackOptionInfo> CompressionExtensionPackVocabulary() {
  std::vector<PackOptionInfo> vocabulary;
  for (const auto& extension : Extensions()) {
    vocabulary.insert(vocabulary.end(), extension.pack_vocabulary.begin(), extension.pack_vocabulary.end());
  }
  return vocabulary;
}

std::string CompressionExtensionPackFormatFor(std::string_view path) {
  const std::string lower = absl::AsciiStrToLower(path);
  std::string best;
  for (const auto& extension : Extensions()) {
    for (const std::string& format : extension.pack_formats) {
      const std::string suffix = absl::StrCat(".", absl::AsciiStrToLower(format));
      if (format.size() > best.size() && HasSuffix(lower, suffix)) {
        best = format;
      }
    }
  }
  return best;
}

absl::StatusOr<std::string> DecodeCompressionExtension(
    std::string_view container,
    std::optional<std::string_view> bytes,
    std::uint64_t max_bytes) {
  const auto extension = CompressionExtensionFor(container);
  if (!extension.has_value()) {
    return absl::InvalidArgumentError(absl::StrCat("no compression extension owns '", container, "'"));
  }
  if (!extension->decoder) {
    return absl::InvalidArgumentError("not a supported streaming container");
  }
  return extension->decoder(container, bytes, max_bytes);
}

absl::Status PackCompressionExtension(
    std::string_view path,
    const std::vector<PackFile>& files,
    const PackOptions& options) {
  const std::string format = CompressionExtensionPackFormatFor(path);
  for (const auto& extension : Extensions()) {
    if (absl::c_linear_search(extension.pack_formats, format)) {
      return extension.packer(path, files, options);
    }
  }
  return absl::InvalidArgumentError(absl::StrCat("no compression extension can pack '", path, "'"));
}

void RegisterSourceDecoder(std::string name, SourceDecoder decoder) {
  auto& decoders = SourceDecoders();
  const auto found = std::ranges::find(decoders, name, &NamedSourceDecoder::name);
  if (found != decoders.end()) {
    found->decoder = std::move(decoder);
  } else {
    decoders.push_back({.name = std::move(name), .decoder = std::move(decoder)});
  }
  std::ranges::sort(decoders, {}, &NamedSourceDecoder::name);
}

absl::StatusOr<vfs::SharedReadSource> DecodeSource(vfs::SharedReadSource source) {
  for (const auto& decoder : SourceDecoders()) {
    auto result = decoder.decoder(source);
    if (result.ok() || !absl::IsInvalidArgument(result.status())) {
      return result;
    }
  }
  return source;
}

}  // namespace xff::archive
