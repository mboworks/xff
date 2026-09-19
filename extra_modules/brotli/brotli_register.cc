// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "xff/archive/archive_extension.h"
#include "xff/archive/archive_register.h"
#include "xff/brotli/brotli_codec.h"

namespace xff::brotli {
namespace {

constexpr std::array kBrotliTarFormats = std::to_array<std::string_view>({
    "tar.br",
    "tbr",
});
constexpr std::array kBrotliSuffixes = std::to_array<std::string_view>({
    ".tar.br",
    ".tbr",
    ".br",
});
constexpr std::array kBrotliTarSuffixes = std::to_array<std::string_view>({
    ".tar.br",
    ".tbr",
});
constexpr std::array kBrotliFileSuffixes = std::to_array<std::string_view>({
    ".br",
});

template<std::size_t Size>
std::vector<std::string> Strings(const std::array<std::string_view, Size>& values) {
  return {values.begin(), values.end()};
}

// NOLINTNEXTLINE(fuchsia-statically-constructed-objects,cert-err58-cpp)
const struct BrotliRegistrar {
  BrotliRegistrar() {
    archive::RegisterCompressionExtension({
        .name = "brotli",
        .suffixes = Strings(kBrotliSuffixes),
        .read_formats =
            {
                {
                    .name = "tar",
                    .suffixes = Strings(kBrotliTarSuffixes),
                    .detail = "tar archives compressed with Brotli",
                },
                {
                    .name = "file",
                    .suffixes = Strings(kBrotliFileSuffixes),
                    .detail = "a raw Brotli-compressed single file",
                },
            },
        .pack_formats = Strings(kBrotliTarFormats),
        .pack_vocabulary =
            {
                {
                    .name = "framing",
                    .value_syntax = "rfc9841|raw",
                    .formats = kBrotliTarFormats,
                    .detail = "Brotli representation (default `rfc9841`; use `raw` for legacy tools)",
                },
                {
                    .name = "level",
                    .value_syntax = "0..11",
                    .formats = kBrotliTarFormats,
                    .detail = "Brotli quality (default `11`)",
                },
                {
                    .name = "window",
                    .value_syntax = "10..24",
                    .formats = kBrotliTarFormats,
                    .detail = "Brotli LZ77 window bits (default `22`)",
                },
            },
        .decoder = &Decode,
        .packer = &PackTar,
    });
    archive::RegisterArchiveBackend();
  }
} kRegisterBrotli;

}  // namespace
}  // namespace xff::brotli
