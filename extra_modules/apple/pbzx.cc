// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/apple/pbzx.h"

#include <lzma.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "mbo/status/status_macros.h"

namespace xff::apple {
namespace {
constexpr std::uint64_t kChunkLimit = 64ULL * 1'024 * 1'024;
constexpr std::uint64_t kDecoderLimit = 128ULL * 1'024 * 1'024;

absl::StatusOr<std::string> ReadExact(vfs::ReadStream& stream, std::size_t count) {
  std::string result;
  result.reserve(count);
  while (result.size() < count) {
    MBO_ASSIGN_OR_RETURN(const std::string part, stream.Read(count - result.size()));
    if (part.empty()) {
      return absl::DataLossError("truncated PBZX stream");
    }
    result += part;
  }
  return result;
}

std::uint64_t BigEndian(std::string_view bytes) {
  std::uint64_t result = 0;
  for (const unsigned char byte : bytes) {
    result = (result << 8U) | byte;
  }
  return result;
}

absl::StatusOr<std::string> DecodeChunk(std::string input, std::size_t output_size) {
  if (input.size() == output_size) {
    return input;
  }
  std::uint64_t memory = kDecoderLimit;
  std::size_t consumed = 0;
  std::size_t produced = 0;
  std::string output(output_size, '\0');
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) // XFF_ABI_POINTER: codec byte buffer.
  const auto* data = reinterpret_cast<const std::uint8_t*>(input.data());
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) // XFF_ABI_POINTER: codec byte buffer.
  auto* destination = reinterpret_cast<std::uint8_t*>(output.data());
  const lzma_ret status = ::lzma_stream_buffer_decode(
      &memory, 0, nullptr, data, &consumed, input.size(), destination, &produced, output.size());
  if (status == LZMA_MEMLIMIT_ERROR || status == LZMA_MEM_ERROR) {
    return absl::ResourceExhaustedError("PBZX XZ decoder memory limit exceeded");
  }
  if (status != LZMA_OK || consumed != input.size() || produced != output.size()) {
    return absl::DataLossError("invalid PBZX XZ chunk or decoded length");
  }
  return output;
}

class PbzxStream final : public vfs::ReadStream {
 public:
  PbzxStream(std::unique_ptr<vfs::ReadStream> input, std::shared_ptr<vfs::ReadBudget> budget)
      : input_(std::move(input)), budget_(std::move(budget)) {}

  absl::StatusOr<std::string> Read(std::size_t max_bytes) override {
    if (max_bytes == 0) {
      return std::string();
    }
    if (offset_ == decoded_.size() && !done_) {
      MBO_RETURN_IF_ERROR(NextChunk());
    }
    const std::size_t count = std::min(max_bytes, decoded_.size() - offset_);
    std::string result = decoded_.substr(offset_, count);
    offset_ += count;
    return result;
  }

 private:
  absl::Status NextChunk() {
    std::string().swap(decoded_);
    cached_ = {};
    offset_ = 0;
    MBO_ASSIGN_OR_RETURN(std::string header, input_->Read(16));
    if (header.empty()) {
      done_ = true;
      return absl::OkStatus();
    }
    if (header.size() < 16) {
      MBO_ASSIGN_OR_RETURN(const std::string rest, ReadExact(*input_, 16 - header.size()));
      header += rest;
    }
    const std::uint64_t decoded_size = BigEndian(std::string_view(header).substr(0, 8));
    const std::uint64_t stored_size = BigEndian(std::string_view(header).substr(8));
    if (decoded_size == 0 && stored_size == 0) {
      MBO_ASSIGN_OR_RETURN(const std::string tail, input_->Read(1));
      if (!tail.empty()) {
        return absl::DataLossError("PBZX data after terminator");
      }
      done_ = true;
      return absl::OkStatus();
    }
    if (decoded_size == 0 || stored_size == 0) {
      return absl::DataLossError("invalid PBZX chunk length");
    }
    if (decoded_size > kChunkLimit || stored_size > kChunkLimit) {
      return absl::ResourceExhaustedError("PBZX chunk exceeds 64 MiB limit");
    }
    MBO_ASSIGN_OR_RETURN(auto retained, budget_->Reserve(static_cast<std::size_t>(decoded_size)));
    const auto workspace = static_cast<std::size_t>(stored_size + (stored_size == decoded_size ? 0 : kDecoderLimit));
    MBO_ASSIGN_OR_RETURN(auto temporary, budget_->Reserve(workspace));
    MBO_ASSIGN_OR_RETURN(std::string input, ReadExact(*input_, static_cast<std::size_t>(stored_size)));
    MBO_ASSIGN_OR_RETURN(decoded_, DecodeChunk(std::move(input), static_cast<std::size_t>(decoded_size)));
    cached_ = std::move(retained);
    return absl::OkStatus();
  }

  std::unique_ptr<vfs::ReadStream> input_;
  std::shared_ptr<vfs::ReadBudget> budget_;
  vfs::ReadBudget::Reservation cached_;
  std::string decoded_;
  std::size_t offset_ = 0;
  bool done_ = false;
};

class PbzxSource final : public vfs::ReadSource {
 public:
  explicit PbzxSource(vfs::SharedReadSource source) : ReadSource(source->Budget()), source_(std::move(source)) {}

  absl::StatusOr<std::unique_ptr<vfs::ReadStream>> Open() const override {
    MBO_ASSIGN_OR_RETURN(auto stream, source_->Open());
    MBO_ASSIGN_OR_RETURN(const std::string header, ReadExact(*stream, 12));
    if (!header.starts_with("pbzx")) {
      return absl::DataLossError("PBZX source changed");
    }
    const auto size = BigEndian(std::string_view(header).substr(4));
    if (size == 0 || size > kChunkLimit) {
      return absl::UnimplementedError("unsupported PBZX chunk-size header");
    }
    return std::make_unique<PbzxStream>(std::move(stream), Budget());
  }

 private:
  vfs::SharedReadSource source_;
};
}  // namespace

absl::StatusOr<vfs::SharedReadSource> DecodePbzx(vfs::SharedReadSource source) {
  MBO_ASSIGN_OR_RETURN(auto stream, source->Open());
  std::string prefix;
  while (prefix.size() < 4) {
    MBO_ASSIGN_OR_RETURN(const std::string part, stream->Read(4 - prefix.size()));
    if (part.empty()) {
      break;
    }
    prefix += part;
  }
  if (prefix != "pbzx") {
    return absl::InvalidArgumentError("not PBZX");
  }
  auto decoded = std::make_shared<PbzxSource>(std::move(source));
  MBO_RETURN_IF_ERROR(decoded->Open().status());
  return decoded;
}
}  // namespace xff::apple
