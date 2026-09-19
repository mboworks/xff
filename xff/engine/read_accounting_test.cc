// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/engine/run.h"
#include "xff/parser/parser.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {
namespace {

using ::testing::Eq;
using ::testing::Gt;

constexpr std::size_t kFiles = 256;
constexpr std::size_t kBytes = 1'024;
constexpr std::size_t kLevels = 16;

// Reports successful bytes delivered through the VFS seam, not storage traffic.
// All entries and contents are synthetic; mutation sinks stay disconnected.
class ReadAccountingFs final : public vfs::FileSystem {
 public:
  explicit ReadAccountingFs(bool deep) {
    AddRoot("/left", deep);
    AddRoot("/right", deep);
  }

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view dir) const override {
    const auto found = directories_.find(dir);
    if (found == directories_.end()) {
      return absl::NotFoundError("No such synthetic directory");
    }
    return found->second;
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool) const override {
    const auto found = metadata_.find(path);
    if (found == metadata_.end()) {
      return absl::NotFoundError("No such synthetic entry");
    }
    return found->second;
  }

  absl::Status Remove(std::string_view) const override {
    ADD_FAILURE() << "Read-accounting workload attempted deletion";
    return absl::PermissionDeniedError("Read-only measurement fixture");
  }

  absl::Status WriteContent(std::string_view, std::string_view) const override {
    ADD_FAILURE() << "Read-accounting workload attempted a write";
    return absl::PermissionDeniedError("Read-only measurement fixture");
  }

  absl::StatusOr<std::unique_ptr<vfs::OutputFile>> OpenOutput(std::string_view, bool) const override {
    ADD_FAILURE() << "Read-accounting workload attempted output creation";
    return absl::PermissionDeniedError("Read-only measurement fixture");
  }

  bool Access(std::string_view path, vfs::AccessMode mode) const override {
    return mode == vfs::AccessMode::kRead && metadata_.contains(path);
  }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override {
    return absl::InvalidArgumentError("No synthetic symlinks");
  }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return std::string("memory"); }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

  absl::StatusOr<std::string> ReadContent(std::string_view path) const override {
    attempts_.fetch_add(1, std::memory_order_relaxed);
    const auto found = metadata_.find(path);
    if (found == metadata_.end() || found->second.type != vfs::FileType::kRegular) {
      return absl::NotFoundError("No such synthetic file");
    }
    calls_.fetch_add(1, std::memory_order_relaxed);
    bytes_.fetch_add(kBytes, std::memory_order_relaxed);
    return std::string(kBytes, 'x');
  }

  std::uint64_t Attempts() const { return attempts_.load(std::memory_order_relaxed); }

  std::uint64_t Calls() const { return calls_.load(std::memory_order_relaxed); }

  std::uint64_t Bytes() const { return bytes_.load(std::memory_order_relaxed); }

 private:
  void AddRoot(std::string current, bool deep) {
    directories_.try_emplace(current);
    metadata_.emplace(current, vfs::Metadata{.type = vfs::FileType::kDirectory, .ino = metadata_.size() + 1, .dev = 1});
    for (std::size_t index = 0; index < kFiles; ++index) {
      if (deep && index % (kFiles / kLevels) == 0) {
        const std::string child = absl::StrCat(current, "/d");
        directories_.at(current).push_back(vfs::Entry{.path = child, .name = "d", .type = vfs::FileType::kDirectory});
        directories_.try_emplace(child);
        metadata_.emplace(
            child, vfs::Metadata{.type = vfs::FileType::kDirectory, .ino = metadata_.size() + 1, .dev = 1});
        current = child;
      }
      const std::string name = absl::StrCat("file", index, ".txt");
      const std::string path = absl::StrCat(current, "/", name);
      directories_.at(current).push_back(vfs::Entry{.path = path, .name = name, .type = vfs::FileType::kRegular});
      metadata_.emplace(
          path, vfs::Metadata{.type = vfs::FileType::kRegular, .size = kBytes, .ino = metadata_.size() + 1, .dev = 1});
    }
  }

  std::map<std::string, std::vector<vfs::Entry>, std::less<>> directories_;
  std::map<std::string, vfs::Metadata, std::less<>> metadata_;
  mutable std::atomic<std::uint64_t> attempts_ = 0;
  mutable std::atomic<std::uint64_t> calls_ = 0;
  mutable std::atomic<std::uint64_t> bytes_ = 0;
};

struct ReadAccountingTest : ::testing::Test {
  static void Measure(const std::vector<std::string>& arguments, bool compare = false) {
    RecordProperty("files_per_root", std::to_string(kFiles));
    RecordProperty("bytes_per_file", std::to_string(kBytes));
    RecordProperty("deep_levels", std::to_string(kLevels));
    RecordProperty("root_count", compare ? 2 : 1);
    for (const bool deep : std::to_array<bool>({false, true})) {
      const ReadAccountingFs fs(deep);
      std::vector<std::string> argv = {"--safe", "--jobs=1", "/left"};
      if (compare) {
        argv.emplace_back("/right");
      }
      argv.insert(argv.end(), {"-type", "f"});
      argv.insert(argv.end(), arguments.begin(), arguments.end());
      ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse(argv));
      std::atomic<std::size_t> output_bytes = 0;
      const RunResult result = RunFind(
          command, fs, [&](std::string_view text) { output_bytes.fetch_add(text.size(), std::memory_order_relaxed); },
          [](std::string_view path, absl::Status status) { ADD_FAILURE() << path << ": " << status; });
      EXPECT_THAT(result.errors, Eq(0));
      EXPECT_THAT(output_bytes.load(), Gt(0U));
      const std::string_view shape = deep ? "deep" : "broad";
      RecordProperty(absl::StrCat(shape, "_read_attempts"), std::to_string(fs.Attempts()));
      RecordProperty(absl::StrCat(shape, "_successful_reads"), std::to_string(fs.Calls()));
      RecordProperty(absl::StrCat(shape, "_logical_bytes"), std::to_string(fs.Bytes()));
    }
  }
};

// Read counts are observations, deliberately not frozen as pass/fail performance gates.
TEST_F(ReadAccountingTest, Listing) {
  Measure({});
}

TEST_F(ReadAccountingTest, Summary) {
  Measure({"--summary=ext"});
}

TEST_F(ReadAccountingTest, Hash) {
  Measure({"--template={hash}"});
}

TEST_F(ReadAccountingTest, HashTwice) {
  Measure({"--template={hash} {hash}"});
}

TEST_F(ReadAccountingTest, HashAndLines) {
  Measure({"--template={hash} {lines}"});
}

TEST_F(ReadAccountingTest, Compare) {
  Measure({"--compare=summary"}, true);
}

}  // namespace
}  // namespace xff::engine
