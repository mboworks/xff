// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/brotli/brotli_codec.h"

#include <brotli/decode.h>
#include <brotli/encode.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "mbo/status/status_macros.h"
#include "xff/archive/archive_backend.h"
#include "xff/archive/archive_register.h"
#include "xff/vfs/mutations.h"

namespace xff::brotli {
namespace {

namespace stdfs = ::std::filesystem;

constexpr std::size_t kBlockSize = std::size_t{64} * 1'024;
constexpr int kDefaultQuality = 11;
constexpr int kMinimumQuality = 0;
constexpr int kMaximumQuality = 11;
constexpr int kDefaultWindowBits = 22;
constexpr int kMinimumWindowBits = 10;
constexpr int kMaximumWindowBits = 24;
constexpr std::array<std::uint8_t, 4> kFramingSignature = {0x91, 0x0a, 0x42, 0x52};

struct DecoderDeleter {
  void operator()(BrotliDecoderState* state) const noexcept {
    if (state != nullptr) {
      BrotliDecoderDestroyInstance(state);
    }
  }
};

struct EncoderDeleter {
  void operator()(BrotliEncoderState* state) const noexcept {
    if (state != nullptr) {
      BrotliEncoderDestroyInstance(state);
    }
  }
};

using DecoderPtr = std::unique_ptr<BrotliDecoderState, DecoderDeleter>;
using EncoderPtr = std::unique_ptr<BrotliEncoderState, EncoderDeleter>;

void WriteVarint(std::ostream& output, std::uint64_t value) {
  while (true) {
    auto byte = static_cast<std::uint8_t>(value & 0x7fU);
    value >>= 7U;
    if (value != 0) {
      byte |= 0x80U;
    }
    output.put(static_cast<char>(byte));
    if (value == 0) {
      return;
    }
  }
}

std::size_t VarintSize(std::uint64_t value) {
  std::size_t size = 1;
  while (value >= 0x80U) {
    value >>= 7U;
    ++size;
  }
  return size;
}

absl::StatusOr<std::uint64_t> ReadVarint(std::istream& input, std::string_view label) {
  std::uint64_t value = 0;
  for (unsigned int index = 0; index < 9; ++index) {
    const int read = input.get();
    if (read == std::istream::traits_type::eof()) {
      return absl::DataLossError(absl::StrCat("truncated RFC 9841 varint in '", label, "'"));
    }
    const auto byte = static_cast<std::uint8_t>(read);
    value |= static_cast<std::uint64_t>(byte & 0x7fU) << (index * 7U);
    if ((byte & 0x80U) == 0) {
      return value;
    }
  }
  return absl::DataLossError(absl::StrCat("oversized RFC 9841 varint in '", label, "'"));
}

absl::StatusOr<std::uint8_t> ReadByte(std::istream& input, std::string_view label) {
  const int read = input.get();
  if (read == std::istream::traits_type::eof()) {
    return absl::DataLossError(absl::StrCat("truncated RFC 9841 header in '", label, "'"));
  }
  return static_cast<std::uint8_t>(read);
}

absl::StatusOr<int> IntegerOption(
    const absl::btree_map<std::string, std::string>& options,
    std::string_view name,
    int fallback,
    int minimum,
    int maximum) {
  const auto found = options.find(name);
  if (found == options.end()) {
    return fallback;
  }
  int value = 0;
  if (!absl::SimpleAtoi(found->second, &value) || value < minimum || value > maximum) {
    return absl::InvalidArgumentError(
        absl::StrCat(
            "--pack-option=", name, "=", found->second, " must be an integer from ", minimum, " to ", maximum));
  }
  return value;
}

absl::Status EncodeFile(const stdfs::path& source, vfs::TemporaryOutput& output, int quality, int window_bits) {
  // XFF_HOST_IO: Brotli adapter reads the explicitly selected host input file.
  std::ifstream input(source, std::ios::binary);
  if (!input.is_open()) {
    return absl::NotFoundError(absl::StrCat("cannot open temporary tar '", source.string(), "'"));
  }
  const EncoderPtr encoder{BrotliEncoderCreateInstance(nullptr, nullptr, nullptr)};
  if (encoder == nullptr) {
    // LCOV_EXCL_START: Brotli exposes no allocator-failure injection when using its default allocator.
    return absl::ResourceExhaustedError("cannot allocate the Brotli encoder");
    // LCOV_EXCL_STOP
  }
  if (BrotliEncoderSetParameter(encoder.get(), BROTLI_PARAM_QUALITY, static_cast<std::uint32_t>(quality))
          == BROTLI_FALSE
      || BrotliEncoderSetParameter(encoder.get(), BROTLI_PARAM_LGWIN, static_cast<std::uint32_t>(window_bits))
             == BROTLI_FALSE) {
    return absl::InvalidArgumentError("the Brotli encoder rejected its validated settings");
  }

  std::array<std::uint8_t, kBlockSize> input_buffer{};
  std::array<std::uint8_t, kBlockSize> output_buffer{};
  bool finished_input = false;
  std::size_t available_input = 0;
  const std::uint8_t* next_input = input_buffer.data();
  while (BrotliEncoderIsFinished(encoder.get()) == BROTLI_FALSE) {
    if (available_input == 0 && !finished_input) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) -- byte-oriented stream/C API boundary.
      input.read(reinterpret_cast<char*>(input_buffer.data()), static_cast<std::streamsize>(input_buffer.size()));
      available_input = static_cast<std::size_t>(input.gcount());
      next_input = input_buffer.data();
      finished_input = input.eof();
      if (input.bad()) {
        return absl::DataLossError(absl::StrCat("read failed part way through '", source.string(), "'"));
      }
    }
    std::size_t available_output = output_buffer.size();
    std::uint8_t* next_output = output_buffer.data();
    const BrotliEncoderOperation operation = finished_input ? BROTLI_OPERATION_FINISH : BROTLI_OPERATION_PROCESS;
    if (BrotliEncoderCompressStream(
            encoder.get(), operation, &available_input, &next_input, &available_output, &next_output, nullptr)
        == BROTLI_FALSE) {
      return absl::DataLossError("Brotli encoding failed");
    }
    const std::size_t produced = output_buffer.size() - available_output;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) -- byte-oriented stream/C API boundary.
    MBO_RETURN_IF_ERROR(output.Write(std::string_view(reinterpret_cast<const char*>(output_buffer.data()), produced)));
  }
  return absl::OkStatus();
}

absl::Status WriteFramed(const stdfs::path& raw, vfs::TemporaryOutput& destination, std::uint64_t uncompressed_size) {
  std::error_code error;
  const std::uint64_t raw_size = stdfs::file_size(raw, error);
  if (error) {
    return absl::UnavailableError(absl::StrCat("cannot size '", raw.string(), "': ", error.message()));
  }
  // XFF_HOST_IO: Brotli adapter reads the explicitly selected host input file.
  std::ifstream input(raw, std::ios::binary);
  if (!input.is_open()) {
    return absl::NotFoundError(absl::StrCat("cannot open '", raw.string(), "'"));
  }
  std::ostringstream output;
  for (const std::uint8_t byte : kFramingSignature) {
    output.put(static_cast<char>(byte));
  }
  output.put(0);  // Version 0, one resource, no metadata, directory, or footer.
  const std::uint64_t chunk_size = 1 + 1 + VarintSize(uncompressed_size) + 1 + raw_size;
  WriteVarint(output, chunk_size);
  output.put(2);  // Data chunk.
  output.put(2);  // RFC 7932 Brotli codec.
  WriteVarint(output, uncompressed_size);
  output.put(0);  // Ordinary extractable resource, no hash.
  MBO_RETURN_IF_ERROR(destination.Write(output.str()));
  std::array<char, kBlockSize> buffer{};
  while (input.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || input.gcount() > 0) {
    MBO_RETURN_IF_ERROR(destination.Write(std::string_view(buffer.data(), static_cast<std::size_t>(input.gcount()))));
  }
  if (input.bad()) {
    return absl::DataLossError(absl::StrCat("read failed part way through '", raw.string(), "'"));
  }
  return absl::OkStatus();
}

struct DecoderInputState {
  std::uint64_t remaining;
  std::size_t available = 0;
  const std::uint8_t* next;
  bool done = false;
};

absl::Status FillDecoderInput(
    std::istream& input,
    std::string_view label,
    const std::optional<std::uint64_t> compressed_size,
    std::array<std::uint8_t, kBlockSize>& buffer,
    DecoderInputState& state) {
  if (state.available != 0 || state.done) {
    return absl::OkStatus();
  }
  const std::size_t requested = compressed_size.has_value()
                                    ? static_cast<std::size_t>(std::min<std::uint64_t>(state.remaining, buffer.size()))
                                    : buffer.size();
  if (requested == 0) {
    state.done = true;
    return absl::OkStatus();
  }
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) -- byte-oriented stream/C API boundary.
  input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(requested));
  state.available = static_cast<std::size_t>(input.gcount());
  state.next = buffer.data();
  if (compressed_size.has_value()) {
    state.remaining -= state.available;
    if (state.available < requested && input.eof()) {
      return absl::DataLossError(absl::StrCat("truncated Brotli stream in '", label, "'"));
    }
    state.done = state.remaining == 0;
  } else {
    state.done = input.eof();
  }
  if (input.bad()) {
    return absl::DataLossError(absl::StrCat("read failed part way through '", label, "'"));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> FinishDecodedStream(
    std::istream& input,
    std::string_view label,
    const std::optional<std::uint64_t> compressed_size,
    const std::optional<std::uint64_t> expected_size,
    const DecoderInputState& state,
    std::string output) {
  const bool trailing = state.available != 0 || (compressed_size.has_value() && state.remaining != 0)
                        || (!compressed_size.has_value() && input.peek() != std::istream::traits_type::eof());
  if (trailing) {
    return absl::DataLossError(absl::StrCat("trailing bytes after the Brotli stream in '", label, "'"));
  }
  if (expected_size.has_value() && output.size() != expected_size.value_or(0)) {
    return absl::DataLossError(
        absl::StrCat(
            "RFC 9841 size mismatch in '", label, "': expected ", expected_size.value_or(0), ", decoded ",
            output.size()));
  }
  return output;
}

absl::StatusOr<std::string> DecodeRawStream(
    std::istream& input,
    std::string_view label,
    std::optional<std::uint64_t> compressed_size,
    std::optional<std::uint64_t> expected_size,
    std::uint64_t max_bytes) {
  const DecoderPtr decoder{BrotliDecoderCreateInstance(nullptr, nullptr, nullptr)};
  if (decoder == nullptr) {
    // LCOV_EXCL_START: Brotli exposes no allocator-failure injection when using its default allocator.
    return absl::ResourceExhaustedError("cannot allocate the Brotli decoder");
    // LCOV_EXCL_STOP
  }
  std::array<std::uint8_t, kBlockSize> input_buffer{};
  std::array<std::uint8_t, kBlockSize> output_buffer{};
  std::string output;
  DecoderInputState state{
      .remaining = compressed_size.value_or(0),
      .next = input_buffer.data(),
  };

  while (true) {
    MBO_RETURN_IF_ERROR(FillDecoderInput(input, label, compressed_size, input_buffer, state));
    std::size_t available_output = output_buffer.size();
    std::uint8_t* next_output = output_buffer.data();
    const BrotliDecoderResult result = BrotliDecoderDecompressStream(
        decoder.get(), &state.available, &state.next, &available_output, &next_output, nullptr);
    const std::size_t produced = output_buffer.size() - available_output;
    if (max_bytes != 0 && output.size() + produced > max_bytes) {
      return absl::ResourceExhaustedError(absl::StrCat(label, " exceeds the ", max_bytes, " byte limit"));
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) -- byte-oriented stream/C API boundary.
    output.append(reinterpret_cast<const char*>(output_buffer.data()), produced);
    if (result == BROTLI_DECODER_RESULT_SUCCESS) {
      return FinishDecodedStream(input, label, compressed_size, expected_size, state, std::move(output));
    }
    if (result == BROTLI_DECODER_RESULT_ERROR) {
      return absl::DataLossError(
          absl::StrCat(
              "invalid Brotli stream in '", label,
              "': ", BrotliDecoderErrorString(BrotliDecoderGetErrorCode(decoder.get()))));
    }
    if (result == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT && state.done && state.available == 0) {
      return absl::DataLossError(absl::StrCat("truncated Brotli stream in '", label, "'"));
    }
  }
}

struct EncodingOptions {
  int quality;
  int window_bits;
  bool raw;
};

absl::StatusOr<EncodingOptions> ResolveEncodingOptions(const archive::PackOptions& options) {
  absl::btree_map<std::string, std::string> resolved;
  for (const archive::PackOption& option : options.options) {
    if (option.name != "framing" && option.name != "level" && option.name != "window") {
      return absl::InvalidArgumentError(
          absl::StrCat("--pack-option=", option.name, " does not apply to Brotli tar output"));
    }
    resolved[option.name] = option.value;
  }
  MBO_ASSIGN_OR_RETURN(
      const int quality, IntegerOption(resolved, "level", kDefaultQuality, kMinimumQuality, kMaximumQuality));
  MBO_ASSIGN_OR_RETURN(
      const int window_bits,
      IntegerOption(resolved, "window", kDefaultWindowBits, kMinimumWindowBits, kMaximumWindowBits));
  const std::string framing = resolved.contains("framing") ? resolved.at("framing") : "rfc9841";
  if (framing != "rfc9841" && framing != "raw") {
    return absl::InvalidArgumentError("--pack-option=framing must be `rfc9841` or `raw`");
  }
  return EncodingOptions{.quality = quality, .window_bits = window_bits, .raw = framing == "raw"};
}

absl::Status FrameEncodedTar(const stdfs::path& tar, const stdfs::path& raw, vfs::TemporaryOutput& compressed) {
  std::error_code size_error;
  const std::uint64_t tar_size = stdfs::file_size(tar, size_error);
  if (size_error) {
    return absl::UnavailableError(absl::StrCat("cannot size '", tar.string(), "': ", size_error.message()));
  }
  return WriteFramed(raw, compressed, tar_size);
}

absl::StatusOr<std::string> DecodeStream(std::istream& input, std::string_view path, std::uint64_t max_bytes) {
  std::array<std::uint8_t, kFramingSignature.size()> signature{};
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) -- byte-oriented stream/C API boundary.
  input.read(reinterpret_cast<char*>(signature.data()), static_cast<std::streamsize>(signature.size()));
  if (signature != kFramingSignature) {
    input.clear();
    input.seekg(0);
    return DecodeRawStream(input, path, std::nullopt, std::nullopt, max_bytes);
  }
  MBO_ASSIGN_OR_RETURN(const std::uint8_t flags, ReadByte(input, path));
  if (flags != 0) {
    return absl::UnimplementedError(
        absl::StrCat("RFC 9841 container features beyond one resource are not supported in '", path, "'"));
  }
  MBO_ASSIGN_OR_RETURN(const std::uint64_t chunk_size, ReadVarint(input, path));
  MBO_ASSIGN_OR_RETURN(const std::uint8_t chunk_type, ReadByte(input, path));
  MBO_ASSIGN_OR_RETURN(const std::uint8_t codec, ReadByte(input, path));
  if (chunk_type != 2 || codec != 2) {
    return absl::UnimplementedError(absl::StrCat("RFC 9841 stream in '", path, "' is not one Brotli data resource"));
  }
  MBO_ASSIGN_OR_RETURN(const std::uint64_t expected_size, ReadVarint(input, path));
  MBO_ASSIGN_OR_RETURN(const std::uint8_t data_flags, ReadByte(input, path));
  if (data_flags != 0) {
    return absl::UnimplementedError(absl::StrCat("RFC 9841 data flags are not supported in '", path, "'"));
  }
  const std::uint64_t header_size = 1 + 1 + VarintSize(expected_size) + 1;
  if (chunk_size < header_size) {
    return absl::DataLossError(absl::StrCat("invalid RFC 9841 chunk length in '", path, "'"));
  }
  MBO_ASSIGN_OR_RETURN(
      std::string decoded, DecodeRawStream(input, path, chunk_size - header_size, expected_size, max_bytes));
  if (input.peek() != std::istream::traits_type::eof()) {
    return absl::DataLossError(absl::StrCat("trailing chunks or bytes in RFC 9841 stream '", path, "'"));
  }
  return decoded;
}

}  // namespace

absl::StatusOr<std::string> Decode(
    std::string_view path,
    std::optional<std::string_view> bytes,
    std::uint64_t max_bytes) {
  if (bytes.has_value()) {
    std::istringstream memory{std::string(*bytes)};
    return DecodeStream(memory, path, max_bytes);
  }
  // XFF_HOST_IO: Brotli adapter reads its explicitly selected host container.
  std::ifstream file(std::string(path), std::ios::binary);
  if (!file.is_open()) {
    return absl::NotFoundError(absl::StrCat("cannot open '", path, "'"));
  }
  return DecodeStream(file, path, max_bytes);
}

absl::Status PackTar(
    std::string_view path,
    const std::vector<archive::PackFile>& files,
    const archive::PackOptions& options) {
  MBO_ASSIGN_OR_RETURN(const EncodingOptions encoding, ResolveEncodingOptions(options));

  MBO_ASSIGN_OR_RETURN(
      auto scratch, vfs::TemporaryDirectory::Create(std::string(path) + ".xff-brotli", options.mutations));
  const stdfs::path tar = stdfs::path(scratch->Path()) / "input.tar";
  MBO_RETURN_IF_ERROR(archive::PackNativeArchiveContainer(tar.string(), files, {.mutations = options.mutations}));
  MBO_ASSIGN_OR_RETURN(
      auto compressed, vfs::TemporaryOutput::Create(std::string(path) + ".xff-pack", options.mutations));
  if (encoding.raw) {
    MBO_RETURN_IF_ERROR(EncodeFile(tar, *compressed, encoding.quality, encoding.window_bits));
  } else {
    MBO_ASSIGN_OR_RETURN(auto raw, vfs::TemporaryOutput::Create(scratch->Path() + "/raw", options.mutations));
    MBO_RETURN_IF_ERROR(EncodeFile(tar, *raw, encoding.quality, encoding.window_bits));
    MBO_RETURN_IF_ERROR(FrameEncodedTar(tar, raw->Path(), *compressed));
  }
  return compressed->Publish(path);
}

}  // namespace xff::brotli
