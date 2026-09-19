// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/engine/read_observer.h"

namespace xff::engine {
namespace {

absl::Status ReadOnly() {
  return absl::PermissionDeniedError("Read-accounting harness is read-only");
}

}  // namespace

ReadObservation ReadObserver::Counters::Snapshot() const {
  return {
      .attempts = attempts.load(std::memory_order_relaxed),
      .successes = successes.load(std::memory_order_relaxed),
      .bytes = bytes.load(std::memory_order_relaxed)};
}

absl::StatusOr<std::string> ReadObserver::Counters::Complete(absl::StatusOr<std::string> result) {
  if (result.ok()) {
    successes.fetch_add(1, std::memory_order_relaxed);
    bytes.fetch_add(result->size(), std::memory_order_relaxed);
  }
  return result;
}

ReadObservations ReadObserver::Snapshot() const {
  return {.whole = whole_.Snapshot(), .range = range_.Snapshot()};
}

absl::StatusOr<std::string> ReadObserver::ReadContent(std::string_view path) const {
  whole_.attempts.fetch_add(1, std::memory_order_relaxed);
  return whole_.Complete(backend_.ReadContent(path));
}

absl::StatusOr<std::string> ReadObserver::ReadContentRange(
    std::string_view path,
    std::uint64_t offset,
    std::size_t length) const {
  range_.attempts.fetch_add(1, std::memory_order_relaxed);
  return range_.Complete(backend_.ReadContentRange(path, offset, length));
}

absl::StatusOr<std::vector<vfs::Entry>> ReadObserver::ReadDir(std::string_view path) const {
  return backend_.ReadDir(path);
}

absl::StatusOr<vfs::Metadata> ReadObserver::Stat(std::string_view path, bool follow) const {
  return backend_.Stat(path, follow);
}

bool ReadObserver::Access(std::string_view path, vfs::AccessMode mode) const {
  return mode == vfs::AccessMode::kRead && backend_.Access(path, mode);
}

absl::StatusOr<std::string> ReadObserver::ReadLink(std::string_view path) const {
  return backend_.ReadLink(path);
}

absl::StatusOr<std::string> ReadObserver::FsType(std::string_view path) const {
  return backend_.FsType(path);
}

absl::StatusOr<bool> ReadObserver::IsCaseSensitive(std::string_view path) const {
  return backend_.IsCaseSensitive(path);
}

absl::Status ReadObserver::Remove(std::string_view) const {
  return ReadOnly();
}

absl::Status ReadObserver::WriteContent(std::string_view, std::string_view) const {
  return ReadOnly();
}

absl::StatusOr<std::unique_ptr<vfs::OutputFile>> ReadObserver::OpenOutput(std::string_view, bool) const {
  return ReadOnly();
}

absl::Status ReadObserver::RemoveControlled(std::string_view, const vfs::MutationPolicy&) const {
  return ReadOnly();
}

absl::StatusOr<std::unique_ptr<vfs::OutputFile>> ReadObserver::OpenControlledOutput(
    std::string_view,
    bool,
    const vfs::MutationPolicy&) const {
  return ReadOnly();
}

}  // namespace xff::engine
