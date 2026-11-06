// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/vfs/read_source.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "mbo/status/status_macros.h"

namespace xff::vfs {
namespace {
// Keep capability changes and owned-only decoders valid for every individual read.
absl::StatusOr<std::size_t> ReadStreamInto(ReadStream& stream, std::span<char> buffer, bool caller_owned) {
  if (caller_owned) {
    MBO_ASSIGN_OR_RETURN(const auto count, stream.ReadInto(buffer));
    if (count > buffer.size()) {
      return absl::DataLossError("read source returned more bytes than requested");
    }
    return count;
  }
  MBO_ASSIGN_OR_RETURN(const auto bytes, stream.Read(buffer.size()));
  if (bytes.size() > buffer.size()) {
    return absl::DataLossError("read source returned more bytes than requested");
  }
  std::ranges::copy(bytes, buffer.begin());
  return bytes.size();
}

absl::StatusOr<std::size_t> ReadStreamFullyInto(ReadStream& stream, std::span<char> buffer) {
  std::size_t filled = 0;
  while (filled < buffer.size()) {
    MBO_ASSIGN_OR_RETURN(const auto count, ReadStreamInto(stream, buffer.subspan(filled), stream.SupportsReadInto()));
    if (count == 0) {
      break;
    }
    filled += count;
  }
  return filled;
}

class MemoryStream final : public ReadStream {
 public:
  MemoryStream(std::shared_ptr<const std::string> bytes, std::size_t offset)
      : bytes_(std::move(bytes)), offset_(offset) {}

  absl::StatusOr<std::string> Read(std::size_t max_bytes) override {
    const std::size_t count = std::min(max_bytes, bytes_->size() - offset_);
    std::string result = bytes_->substr(offset_, count);
    offset_ += count;
    return result;
  }

  bool SupportsReadInto() const noexcept override { return true; }

  absl::StatusOr<std::size_t> ReadInto(std::span<char> buffer) override {
    const std::size_t count = std::min(buffer.size(), bytes_->size() - offset_);
    std::ranges::copy(std::string_view(*bytes_).substr(offset_, count), buffer.begin());
    offset_ += count;
    return count;
  }

  absl::StatusOr<std::optional<std::uint64_t>> Skip(std::uint64_t requested) override {
    const auto count = std::min<std::uint64_t>(requested, bytes_->size() - offset_);
    offset_ += static_cast<std::size_t>(count);
    return std::optional<std::uint64_t>(count);
  }

 private:
  std::shared_ptr<const std::string> bytes_;
  std::size_t offset_ = 0;
};

class MemorySource final : public ReadSource {
 public:
  MemorySource(std::shared_ptr<const std::string> bytes, std::shared_ptr<ReadBudget> budget)
      : ReadSource(std::move(budget)), bytes_(std::move(bytes)) {}

  absl::StatusOr<std::uint64_t> Size() const override { return bytes_->size(); }

  absl::StatusOr<std::unique_ptr<ReadStream>> Open() const override { return OpenAt(0); }

  absl::StatusOr<std::unique_ptr<ReadStream>> OpenAt(std::uint64_t offset) const override {
    return std::make_unique<MemoryStream>(bytes_, std::min<std::uint64_t>(offset, bytes_->size()));
  }

 private:
  std::shared_ptr<const std::string> bytes_;
};

// XFF_HOST_IO: translate an immediately preceding read-only syscall; errno is read only on failure.
absl::Status HostReadStatus(std::int64_t result, std::string_view operation) {
  return result < 0 ? absl::ErrnoToStatus(errno, operation) : absl::OkStatus();
}

class HostStream final : public ReadStream {
 public:
  HostStream(int fd, std::uint64_t offset) : fd_(fd), position_(offset) {}

  // XFF_HOST_IO: releases the exclusively owned read-only descriptor.
  ~HostStream() override { ::close(fd_); }

  HostStream(const HostStream&) = delete;
  HostStream& operator=(const HostStream&) = delete;
  HostStream(HostStream&&) = delete;
  HostStream& operator=(HostStream&&) = delete;

  // XFF_HOST_IO: reads the owned descriptor; OS errors must not become successful EOF.
  absl::StatusOr<std::string> Read(std::size_t max_bytes) override {
    std::string result(std::min(max_bytes, 64UZ * 1'024), '\0');
    const auto count = ::read(fd_, result.data(), result.size());
    if (count < 0) {
      return absl::DataLossError(absl::ErrnoToStatus(errno, "reading container source failed").message());
    }
    result.resize(static_cast<std::size_t>(count));
    position_ += static_cast<std::uint64_t>(count);
    return result;
  }

  bool SupportsReadInto() const noexcept override { return true; }

  // XFF_HOST_IO: reads into the cursor owner's reserved storage using its read-only descriptor.
  absl::StatusOr<std::size_t> ReadInto(std::span<char> buffer) override {
    const auto count = ::read(fd_, buffer.data(), std::min(buffer.size(), 64UZ * 1'024));
    if (count < 0) {
      return absl::DataLossError(absl::ErrnoToStatus(errno, "reading container source failed").message());
    }
    position_ += static_cast<std::uint64_t>(count);
    return static_cast<std::size_t>(count);
  }

  // XFF_HOST_IO: queries and advances only this exclusively owned read-only descriptor.
  absl::StatusOr<std::optional<std::uint64_t>> Skip(std::uint64_t requested) override {
    if (requested == 0) {
      return std::optional<std::uint64_t>(0);
    }
    if (access_ == Access::kUnknown) {
      // XFF_HOST_IO: descriptor metadata is queried lazily only for an eligible forward skip.
      struct stat metadata{};
      // XFF_HOST_IO: inspect the owned descriptor, rather than reopening or probing its path.
      MBO_RETURN_IF_ERROR(HostReadStatus(::fstat(fd_, &metadata), "cannot stat container cursor"));
      access_ = S_ISREG(metadata.st_mode) ? Access::kRegular : Access::kSequential;
      size_ = static_cast<std::uint64_t>(metadata.st_size);
    }
    if (access_ != Access::kRegular) {
      return std::nullopt;
    }
    if (position_ >= size_) {
      return std::optional<std::uint64_t>(0);
    }
    const std::uint64_t count = std::min(requested, size_ - position_);
    // Both the current offset and cached regular-file size fit off_t; count is bounded by size.
    // XFF_HOST_IO: change the read cursor position without modifying any host bytes.
    MBO_RETURN_IF_ERROR(HostReadStatus(
        ::lseek(fd_, static_cast<off_t>(position_ + count), SEEK_SET), "cannot advance container cursor"));
    position_ += count;
    return std::optional<std::uint64_t>(count);
  }

 private:
  enum class Access { kUnknown, kSequential, kRegular };
  int fd_;
  Access access_ = Access::kUnknown;
  std::uint64_t position_ = 0;
  std::uint64_t size_ = 0;
};

class HostSource final : public ReadSource {
 public:
  HostSource(std::string path, std::shared_ptr<ReadBudget> budget)
      : ReadSource(std::move(budget)), path_(std::move(path)) {}

  // XFF_HOST_IO: obtains the read source size without opening a writable handle.
  std::optional<std::string_view> HostPath() const override { return path_; }

  absl::StatusOr<std::uint64_t> Size() const override {
    // XFF_HOST_IO: local metadata record for this read-only host adapter.
    struct stat metadata{};
    // XFF_HOST_IO: query metadata for the read-only source path.
    MBO_RETURN_IF_ERROR(HostReadStatus(::stat(path_.c_str(), &metadata), "cannot stat container source"));
    return static_cast<std::uint64_t>(metadata.st_size);
  }

  absl::StatusOr<std::unique_ptr<ReadStream>> Open() const override { return OpenAt(0); }

  // XFF_HOST_IO: opens and positions an independently owned read-only host descriptor.
  absl::StatusOr<std::unique_ptr<ReadStream>> OpenAt(std::uint64_t offset) const override {
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) {
      return absl::OutOfRangeError("container offset exceeds host offset range");
    }
    // XFF_HOST_IO: input-only open; never creates or truncates a file. O_CLOEXEC protects child processes.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    const int fd = ::open(path_.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
      return absl::ErrnoToStatus(errno, absl::StrCat("cannot open container source: ", path_));
    }
    auto stream = std::make_unique<HostStream>(fd, offset);
    if (offset != 0 && ::lseek(fd, static_cast<off_t>(offset), SEEK_SET) < 0) {
      return absl::ErrnoToStatus(errno, "cannot seek container source");
    }
    return stream;
  }

 private:
  std::string path_;
};
}  // namespace

MemoryReadSourceData MakeMemoryReadSource(std::string bytes, std::shared_ptr<ReadBudget> budget) {
  auto owned_bytes = std::make_shared<const std::string>(std::move(bytes));
  return {
      .source = std::make_shared<MemorySource>(owned_bytes, std::move(budget)),
      .bytes = std::move(owned_bytes),
  };
}

SharedReadSource MemoryReadSource(std::string bytes, std::shared_ptr<ReadBudget> budget) {
  return std::make_shared<MemorySource>(std::make_shared<const std::string>(std::move(bytes)), std::move(budget));
}

SharedReadSource HostReadSource(std::string path, std::shared_ptr<ReadBudget> budget) {
  return std::make_shared<HostSource>(std::move(path), std::move(budget));
}

absl::StatusOr<std::unique_ptr<ReadStream>> ReadSource::OpenAt(std::uint64_t offset) const {
  MBO_RETURN_IF_ERROR(Budget()->ConsumeReplay(offset));
  MBO_ASSIGN_OR_RETURN(auto stream, Open());
  while (offset != 0) {
    MBO_ASSIGN_OR_RETURN(const auto bytes, stream->Read(std::min<std::uint64_t>(offset, 64UZ * 1'024)));
    if (bytes.empty()) {
      break;
    }
    offset -= bytes.size();
  }
  return stream;
}

absl::StatusOr<std::uint64_t> ReadSource::Size() const {
  MBO_ASSIGN_OR_RETURN(auto stream, Open());
  std::uint64_t size = 0;
  while (true) {
    MBO_ASSIGN_OR_RETURN(const auto bytes, stream->Read(64UZ * 1'024));
    if (bytes.empty()) {
      return size;
    }
    MBO_RETURN_IF_ERROR(Budget()->ConsumeReplay(bytes.size()));
    size += bytes.size();
  }
}

absl::StatusOr<ReadBlock> ReadSourceRange(const ReadSource& source, std::uint64_t offset, std::size_t count) {
  if (count > 64UZ * 1'024 * 1'024) {
    return absl::ResourceExhaustedError("container range exceeds 64 MiB limit");
  }
  if (count > std::numeric_limits<std::uint64_t>::max() - offset) {
    return absl::OutOfRangeError("container range offset overflow");
  }
  MBO_ASSIGN_OR_RETURN(auto reservation, source.Budget()->Reserve(count));
  ReadBlock result{.reservation = std::move(reservation), .bytes = {}};
  if (count == 0) {
    return result;
  }
  MBO_ASSIGN_OR_RETURN(auto stream, source.OpenAt(offset));
  absl::StatusOr<std::size_t> transferred = 0;
  // XFF_ABI_POINTER: std::string::resize_and_overwrite requires this writable buffer callback.
  result.bytes.resize_and_overwrite(count, [&](char* bytes, std::size_t) noexcept -> std::size_t {
    transferred = ReadStreamFullyInto(*stream, std::span<char>(bytes, count));
    // Callback errors remain in transferred; no uninitialized bytes become string content.
    return transferred.value_or(0);
  });
  MBO_RETURN_IF_ERROR(transferred.status());
  return result;
}

absl::StatusOr<std::string> ReadSourceBytes(const ReadSource& source, std::size_t max_bytes) {
  MBO_ASSIGN_OR_RETURN(auto stream, source.Open());
  std::string result;
  const std::size_t limit = std::min(max_bytes, result.max_size());
  while (true) {
    if (result.size() == limit) {
      // A positive-sized read distinguishes exact EOF from overflow, including a zero limit.
      std::array<char, 1> probe;
      MBO_ASSIGN_OR_RETURN(const auto count, ReadStreamInto(*stream, probe, stream->SupportsReadInto()));
      if (count == 0) {
        return result;
      }
      return absl::ResourceExhaustedError("container exceeds materialization limit");
    }
    const std::size_t previous = result.size();
    if (!stream->SupportsReadInto()) {
      const std::size_t request = std::min(64UZ * 1'024, limit - previous);
      MBO_ASSIGN_OR_RETURN(auto bytes, stream->Read(request));
      if (bytes.size() > request) {
        return absl::DataLossError("read source returned more bytes than requested");
      }
      if (bytes.empty()) {
        return result;
      }
      if (previous == 0) {
        result = std::move(bytes);
        continue;
      }
      result += bytes;
      continue;
    }
    std::size_t request = std::min(64UZ * 1'024, limit - previous);
    if (previous < result.capacity()) {
      // Reuse spare storage after a short read, including SSO; probing EOF must not grow it.
      request = std::min(request, result.capacity() - previous);
    }
    absl::StatusOr<std::size_t> transferred = 0;
    // XFF_ABI_POINTER: std::string::resize_and_overwrite requires this writable buffer callback.
    result.resize_and_overwrite(previous + request, [&](char* bytes, std::size_t) noexcept -> std::size_t {
      transferred = ReadStreamInto(*stream, std::span<char>(bytes, previous + request).subspan(previous), true);
      // Preserve errors separately because this standard callback returns only the initialized size.
      return previous + transferred.value_or(0);
    });
    MBO_ASSIGN_OR_RETURN(const auto count, std::move(transferred));
    if (count == 0) {
      return result;
    }
  }
}

}  // namespace xff::vfs
