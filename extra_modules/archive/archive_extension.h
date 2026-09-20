// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_ARCHIVE_ARCHIVE_EXTENSION_H_
#define XFF_ARCHIVE_ARCHIVE_EXTENSION_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "mbo/types/optional_ref.h"
#include "xff/archive/archive_backend.h"

namespace xff::archive {

// One removable compression layer that extends the archive extra without making libarchive own it.
// The decoder receives either path bytes or already-materialized nested-container bytes. Tar pack
// formats use the ordinary archive packer contract; a raw single-file compressor is deliberately
// not a pack format because it cannot preserve multiple member names.
struct CompressionExtension {
  using Decoder =
      absl::AnyInvocable<absl::StatusOr<std::string>(std::string_view, std::optional<std::string_view>, std::uint64_t)
                             const>;

  std::string name;
  std::vector<std::string> suffixes;
  std::vector<ReadFormatInfo> read_formats;
  std::vector<std::string> pack_formats;
  std::vector<PackOptionInfo> pack_vocabulary;
  Decoder decoder;
  ContainerPacker packer;
};

// Streaming decoders inspect a bounded prefix and return InvalidArgument for other formats.
using SourceDecoder = absl::AnyInvocable<absl::StatusOr<vfs::SharedReadSource>(vfs::SharedReadSource) const>;
void RegisterSourceDecoder(std::string name, SourceDecoder decoder);
absl::StatusOr<vfs::SharedReadSource> DecodeSource(vfs::SharedReadSource source);

template<typename Sink>
void AbslStringify(Sink& sink, const CompressionExtension& extension) {
  sink.Append(extension.name);
}

// Registers an extension at static initialization. A duplicate name is replaced, keeping tests and
// relinking deterministic. The caller then invokes RegisterArchiveBackend() so the core seam's
// format snapshots are refreshed regardless of translation-unit initialization order.
void RegisterCompressionExtension(CompressionExtension extension);

[[nodiscard]] mbo::types::OptionalRef<const CompressionExtension> CompressionExtensionFor(std::string_view container);
[[nodiscard]] std::optional<std::string> CompressionExtensionStem(std::string_view container);
[[nodiscard]] std::vector<ReadFormatInfo> CompressionExtensionReadFormats();
[[nodiscard]] std::vector<std::string> CompressionExtensionPackFormats();
[[nodiscard]] std::vector<PackOptionInfo> CompressionExtensionPackVocabulary();
[[nodiscard]] std::string CompressionExtensionPackFormatFor(std::string_view path);

[[nodiscard]] absl::StatusOr<std::string> DecodeCompressionExtension(
    std::string_view container,
    std::optional<std::string_view> bytes,
    std::uint64_t max_bytes = 0);
[[nodiscard]] absl::Status PackCompressionExtension(
    std::string_view path,
    const std::vector<PackFile>& files,
    const PackOptions& options);

}  // namespace xff::archive

#endif  // XFF_ARCHIVE_ARCHIVE_EXTENSION_H_
