// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "language_database.h"

#include <brotli/decode.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "language_db_data_cc_generated.h"
#include "xff/matching/language/language_database_api.h"

namespace xff::language_db {

absl::StatusOr<std::string> Decode(std::span<const std::uint8_t> compressed, std::size_t expected_uncompressed_size) {
  std::size_t size = 0;
  BrotliDecoderResult decode = BROTLI_DECODER_RESULT_ERROR;
  std::string result;
  // XFF_ABI_POINTER: Brotli's C API writes decoded bytes through a uint8_t pointer.
  result.resize_and_overwrite(expected_uncompressed_size, [&](char* buffer, std::size_t writable_bytes) noexcept {
    size = writable_bytes;
    decode =
        BrotliDecoderDecompress(compressed.size(), compressed.data(), &size, reinterpret_cast<std::uint8_t*>(buffer));
    return decode == BROTLI_DECODER_RESULT_SUCCESS ? size : 0;
  });
  if (decode != BROTLI_DECODER_RESULT_SUCCESS) {
    return absl::DataLossError("the embedded language database is not valid Brotli data");
  }
  if (size != expected_uncompressed_size) {
    return absl::DataLossError(
        absl::StrCat(
            "the embedded language database decoded to ", size, " bytes, expected ", expected_uncompressed_size));
  }
  return result;
}

namespace {

std::string_view Json() {
  static const std::string kJson = [] {
    absl::StatusOr<std::string> json = Decode(data::Compressed(), data::UncompressedSize());
    CHECK_OK(json);  // LCOV_EXCL_BR_LINE: fatal invariant; generated payload is verified at build time.
    return std::move(*json);
  }();  // LCOV_EXCL_BR_LINE: compiler-generated static-local initialization branches.
  return kJson;
}

const language::DatabaseRegistrar kDatabase{{
    .name = "github-linguist 9.6.0",
    .json = &Json,
}};

}  // namespace
}  // namespace xff::language_db
