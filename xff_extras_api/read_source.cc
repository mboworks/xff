// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/vfs/read_source.h"

#include <algorithm>
#include <cerrno>
#include <fstream>
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
  explicit MemoryStream(std::shared_ptr<const std::string> bytes) : bytes_(std::move(bytes)) {}

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
  explicit MemorySource(std::string bytes) : bytes_(std::make_shared<const std::string>(std::move(bytes))) {}

  absl::StatusOr<std::unique_ptr<ReadStream>> Open() const override { return std::make_unique<MemoryStream>(bytes_); }

 private:
  std::shared_ptr<const std::string> bytes_;
};

class HostStream final : public ReadStream {
 public:
  // XFF_HOST_IO: owns an input-only stream; cannot create or modify a file.
  explicit HostStream(std::ifstream file) : file_(std::move(file)) {}

  absl::StatusOr<std::string> Read(std::size_t max_bytes) override {
    const auto count = std::min(max_bytes, static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()));
    std::string result(count, '\0');
    file_.read(result.data(), static_cast<std::streamsize>(count));
    if (file_.bad()) {
      return absl::DataLossError("reading container source failed");
    }
    result.resize(static_cast<std::size_t>(file_.gcount()));
    return result;
  }

 private:
  // XFF_HOST_IO: owned read-only host cursor for container input.
  std::ifstream file_;
};

class HostSource final : public ReadSource {
 public:
  explicit HostSource(std::string path) : path_(std::move(path)) {}

  // XFF_HOST_IO: opens a read-only host handle; never creates or modifies files.
  absl::StatusOr<std::unique_ptr<ReadStream>> Open() const override {
    // XFF_HOST_IO: input-only open; never creates or truncates a file.
    std::ifstream file(path_, std::ios::binary);
    if (!file) {
      return absl::ErrnoToStatus(errno, absl::StrCat("cannot open container source: ", path_));
    }
    return std::make_unique<HostStream>(std::move(file));
  }

 private:
  std::string path_;
};
}  // namespace

SharedReadSource MemoryReadSource(std::string bytes) {
  return std::make_shared<MemorySource>(std::move(bytes));
}

SharedReadSource HostReadSource(std::string path) {
  return std::make_shared<HostSource>(std::move(path));
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
