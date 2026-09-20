// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/vfs/read_source.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "mbo/status/status_macros.h"

namespace xff::vfs {
namespace {
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

 private:
  std::shared_ptr<const std::string> bytes_;
  std::size_t offset_ = 0;
};

class MemorySource final : public ReadSource {
 public:
  MemorySource(std::string bytes, std::shared_ptr<ReadBudget> budget)
      : ReadSource(std::move(budget)), bytes_(std::make_shared<const std::string>(std::move(bytes))) {}

  absl::StatusOr<std::uint64_t> Size() const override { return bytes_->size(); }

  absl::StatusOr<std::unique_ptr<ReadStream>> Open() const override { return OpenAt(0); }

  absl::StatusOr<std::unique_ptr<ReadStream>> OpenAt(std::uint64_t offset) const override {
    return std::make_unique<MemoryStream>(bytes_, std::min<std::uint64_t>(offset, bytes_->size()));
  }

 private:
  std::shared_ptr<const std::string> bytes_;
};

class HostStream final : public ReadStream {
 public:
  explicit HostStream(int fd) : fd_(fd) {}

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
    return result;
  }

 private:
  int fd_;
};

class HostSource final : public ReadSource {
 public:
  HostSource(std::string path, std::shared_ptr<ReadBudget> budget)
      : ReadSource(std::move(budget)), path_(std::move(path)) {}

  // XFF_HOST_IO: obtains the read source size without opening a writable handle.
  absl::StatusOr<std::uint64_t> Size() const override {
    // XFF_HOST_IO: local metadata record for this read-only host adapter.
    struct stat metadata{};
    // XFF_HOST_IO: query metadata for the read-only source path.
    if (::stat(path_.c_str(), &metadata) != 0) {
      return absl::ErrnoToStatus(errno, "cannot stat container source");
    }
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
    auto stream = std::make_unique<HostStream>(fd);
    if (offset != 0 && ::lseek(fd, static_cast<off_t>(offset), SEEK_SET) < 0) {
      return absl::ErrnoToStatus(errno, "cannot seek container source");
    }
    return stream;
  }

 private:
  std::string path_;
};
}  // namespace

SharedReadSource MemoryReadSource(std::string bytes, std::shared_ptr<ReadBudget> budget) {
  return std::make_shared<MemorySource>(std::move(bytes), std::move(budget));
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
  result.bytes.reserve(count);
  while (result.bytes.size() < count) {
    MBO_ASSIGN_OR_RETURN(const auto bytes, stream->Read(count - result.bytes.size()));
    if (bytes.empty()) {
      break;
    }
    result.bytes += bytes;
  }
  return result;
}

absl::StatusOr<std::string> ReadSourceBytes(const ReadSource& source, std::size_t max_bytes) {
  MBO_ASSIGN_OR_RETURN(auto stream, source.Open());
  std::string result;
  while (true) {
    MBO_ASSIGN_OR_RETURN(const std::string block, stream->Read(64UZ * 1'024));
    if (block.empty()) {
      return result;
    }
    if (block.size() > max_bytes - result.size()) {
      return absl::ResourceExhaustedError("container exceeds materialization limit");
    }
    result += block;
  }
}
}  // namespace xff::vfs
