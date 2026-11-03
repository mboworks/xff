// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

// Compare archive input callbacks with caller-owned versus legacy owned storage.
// Fixture creation and source ownership are outside the timed iteration loop.

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "benchmark/benchmark.h"
#include "xff/archive/archive_reader.h"
#include "xff/vfs/read_source.h"

namespace xff::archive {
namespace {

class MemoryCursor final : public vfs::ReadStream {
 public:
  explicit MemoryCursor(std::shared_ptr<const std::string> bytes) : bytes_(std::move(bytes)) {}

  absl::StatusOr<std::string> Read(std::size_t max_bytes) override {
    auto result = bytes_->substr(offset_, std::min(max_bytes, bytes_->size() - offset_));
    offset_ += result.size();
    return result;
  }

  bool SupportsReadInto() const noexcept override { return true; }

  absl::StatusOr<std::size_t> ReadInto(std::span<char> buffer) override {
    const std::size_t count = std::min(buffer.size(), bytes_->size() - offset_);
    std::ranges::copy(std::string_view(*bytes_).substr(offset_, count), buffer.begin());
    offset_ += count;
    return count;
  }

 private:
  std::shared_ptr<const std::string> bytes_;
  std::size_t offset_ = 0;
};

class CallerOwnedSource final : public vfs::ReadSource {
 public:
  explicit CallerOwnedSource(std::string bytes) : bytes_(std::make_shared<const std::string>(std::move(bytes))) {}

  absl::StatusOr<std::unique_ptr<vfs::ReadStream>> Open() const override {
    return std::make_unique<MemoryCursor>(bytes_);
  }

 private:
  std::shared_ptr<const std::string> bytes_;
};

absl::StatusOr<std::string> MakeTar(std::size_t members, std::size_t payload) {
  // XFF_ABI_POINTER: libarchive's C writer handle is released before this function returns.
  struct ::archive* writer = ::archive_write_new();
  ::archive_write_set_format_pax_restricted(writer);
  const std::string content(payload, 'x');
  std::string bytes((members * (payload + 1'024)) + 16'384, '\0');
  std::size_t used = 0;
  ::archive_write_open_memory(writer, bytes.data(), bytes.size(), &used);
  bool valid = true;
  for (std::size_t index = 0; index < members; ++index) {
    // XFF_ABI_POINTER: libarchive's C entry is released after each fixture record.
    struct ::archive_entry* entry = ::archive_entry_new();
    const std::string name = absl::StrCat("file-", index);
    ::archive_entry_set_pathname(entry, name.c_str());
    ::archive_entry_set_size(entry, static_cast<la_int64_t>(payload));
    ::archive_entry_set_filetype(entry, AE_IFREG);
    ::archive_entry_set_perm(entry, 0644);
    valid &= ::archive_write_header(writer, entry) == ARCHIVE_OK;
    valid &= ::archive_write_data(writer, content.data(), content.size()) == static_cast<la_ssize_t>(payload);
    ::archive_entry_free(entry);
  }
  valid &= ::archive_write_close(writer) == ARCHIVE_OK;
  ::archive_write_free(writer);
  if (!valid) {
    return absl::InternalError("cannot create archive benchmark fixture");
  }
  bytes.resize(used);
  return bytes;
}

void Measure(benchmark::State& state, bool caller_owned, bool read_last) {
  const auto members = static_cast<std::size_t>(state.range(0));
  const auto payload = static_cast<std::size_t>(state.range(1));
  auto bytes = MakeTar(members, payload);
  if (!bytes.ok()) {
    state.SkipWithError(bytes.status().ToString());
    return;
  }
  const vfs::SharedReadSource source =
      caller_owned ? std::make_shared<CallerOwnedSource>(std::move(*bytes)) : vfs::MemoryReadSource(std::move(*bytes));
  const auto member = MemberReadSource(source, absl::StrCat("file-", members - 1));
  const std::string expected(payload, 'x');
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores): benchmark loop controls the measured iterations.
  for (const auto unused : state) {
    if (read_last) {
      auto result = vfs::ReadSourceBytes(*member, payload);
      if (!result.ok() || *result != expected) {
        state.SkipWithError("archive member content differs from fixture");
        return;
      }
      benchmark::DoNotOptimize(result);
    } else {
      auto result = ListMembersOfSource(source);
      if (!result.ok() || result->size() != members || std::cmp_not_equal(result->back().size, payload)) {
        state.SkipWithError("archive listing differs from fixture");
        return;
      }
      benchmark::DoNotOptimize(result);
    }
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(members));
}

void RegisterAll() {
  constexpr auto kCounts = std::to_array<std::int64_t>({10, 1'000, 10'000});
  constexpr auto kPayloads = std::to_array<std::int64_t>({128, 4'096});
  for (const bool caller_owned : {false, true}) {
    for (const bool read_last : {false, true}) {
      const std::string name =
          absl::StrCat("archive/", caller_owned ? "caller-owned/" : "legacy/", read_last ? "last" : "list");
      // XFF_ABI_POINTER: google-benchmark returns a borrowed mutable registration object.
      auto* registration = benchmark::RegisterBenchmark(name, Measure, caller_owned, read_last);
      for (const auto count : kCounts) {
        for (const auto payload : kPayloads) {
          registration->Args({count, payload});
        }
      }
      registration->Unit(benchmark::kMicrosecond)->UseRealTime();
    }
  }
}

}  // namespace
}  // namespace xff::archive

int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  xff::archive::RegisterAll();
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
