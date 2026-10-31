// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/status/status_macros.h"
#include "mbo/testing/status.h"
#include "xff/engine/run.h"
#include "xff/parser/parser.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/filesystem.h"

namespace xff::engine {
namespace {
using ::mbo::testing::StatusIs;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::SizeIs;

// Synthetic roots exercise file operands without host path resolution. All reads
// are observable, and mutations fail instead of reaching a host filesystem.
struct DiffPathsFs final : vfs::FileSystem {
  const std::map<std::string, std::string> files{
      {"before", "old\n"},
      {"after", "new\n"},
      {"tree/before", "old\n"},
      {"tree/after", "new\n"},
      {"root/one/file", "first\n"},
      {"root/two/file", "second\n"},
      {"mirror/one/file", "old\n"},
      {"mirror/two/file", "old\n"},
      {"binary/one/file", std::string("first\0", 6)},
      {"binary/two/file", std::string("second\0", 7)},
      {"binary-mirror/one/file", std::string("old\0", 4)},
      {"binary-mirror/two/file", std::string("old\0", 4)},
  };
  // Comparison walks read concurrently; this counter measures calls only.
  // The fixture data is immutable and never synchronized through this counter.
  mutable std::atomic<std::size_t> observations = 0;

  bool Directory(std::string_view path) const {
    const std::string prefix = std::string(path) + "/";
    return std::ranges::any_of(files, [&](const auto& file) { return file.first.starts_with(prefix); });
  }

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view path) const override {
    ++observations;
    if (!Directory(path)) {
      return absl::NotFoundError("unknown fixture directory");
    }
    const std::string prefix = std::string(path) + "/";
    std::map<std::string, vfs::Entry> entries;
    for (const auto& [name, content] : files) {
      if (!name.starts_with(prefix)) {
        continue;
      }
      const auto relative = std::string_view(name).substr(prefix.size());
      const auto slash = relative.find('/');
      const std::string child(relative.substr(0, slash));
      entries.try_emplace(
          child, vfs::Entry{
                     .path = prefix + child,
                     .name = child,
                     .type = slash == std::string_view::npos ? vfs::FileType::kRegular : vfs::FileType::kDirectory,
                 });
    }
    std::vector<vfs::Entry> result;
    result.reserve(entries.size());
    for (auto& [name, entry] : entries) {
      result.push_back(std::move(entry));
    }
    return result;
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool) const override {
    ++observations;
    if (const auto file = files.find(std::string(path)); file != files.end()) {
      return vfs::Metadata{
          .type = vfs::FileType::kRegular,
          .size = file->second.size(),
          .mode = 0644,
          .ino = std::hash<std::string_view>{}(path),
      };
    }
    if (Directory(path)) {
      // Distinct ancestors must have distinct inode identities for loop detection.
      return vfs::Metadata{.type = vfs::FileType::kDirectory, .ino = std::hash<std::string_view>{}(path)};
    }
    return absl::NotFoundError("unknown fixture entry");
  }

  absl::StatusOr<std::string> ReadContent(std::string_view path) const override {
    ++observations;
    if (const auto file = files.find(std::string(path)); file != files.end()) {
      return file->second;
    }
    return absl::NotFoundError("unknown fixture contents");
  }

  absl::Status Remove(std::string_view) const override {
    ADD_FAILURE() << "Patch rendering must not mutate its input";
    return absl::PermissionDeniedError("read-only fixture");
  }

  bool Access(std::string_view, vfs::AccessMode) const override { return true; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override { return absl::NotFoundError("not a link"); }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return "memory"; }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }
};

struct DiffOutput {
  RunResult result;
  std::vector<std::string> records;
  std::vector<absl::Status> diagnostics;
};

struct DiffPathsTest : ::testing::Test {
  absl::StatusOr<DiffOutput> Run(const std::vector<std::string>& args) {
    MBO_ASSIGN_OR_RETURN(auto command, parser::Parse(args));
    parser::BindMatchers(
        command, parser::GrammarFromGlobals(command.globals),
        parser::ResolveCaseMode(command.globals, registry::Style::kXff));
    filesystem.observations = 0;
    DiffOutput output;
    output.result = RunFind(
        command, filesystem, [&](std::string_view record) { output.records.emplace_back(record); },
        [&](std::string_view, absl::Status status) { output.diagnostics.push_back(std::move(status)); });
    return output;
  }

  DiffPathsFs filesystem;
};

TEST_F(DiffPathsTest, FileRootComparisonsUseBasenamesWithAndWithoutParentPaths) {
  for (const std::string_view prefix : {"", "tree/"}) {
    ASSERT_OK_AND_ASSIGN(
        const auto output, Run({"--compare=diff", std::string(prefix) + "before", std::string(prefix) + "after"}));
    EXPECT_THAT(output.result.errors, Eq(0));
    EXPECT_THAT(output.diagnostics, IsEmpty());
    EXPECT_THAT(
        output.records,
        ElementsAre(AllOf(HasSubstr("--- a/before"), HasSubstr("+++ b/after"), HasSubstr("-old"), HasSubstr("+new"))));
  }
}

TEST_F(DiffPathsTest, InvalidSettingsFailBeforeEitherComparisonWalkOrAnAction) {
  for (const std::string_view option : {"--diff-left-strip=-1", "--diff-right-rewrite=s|[|x|"}) {
    ASSERT_OK_AND_ASSIGN(const auto action, Run({std::string(option), "after", "-diff", "-o", "-print"}));
    EXPECT_THAT(action.result.errors, Eq(2));
    EXPECT_THAT(action.records, IsEmpty());
    EXPECT_THAT(action.diagnostics, ElementsAre(StatusIs(absl::StatusCode::kInvalidArgument)));
    EXPECT_THAT(filesystem.observations.load(), Eq(0));
    ASSERT_OK_AND_ASSIGN(const auto compared, Run({std::string(option), "--compare=diff", "before", "after"}));
    EXPECT_THAT(compared.result.errors, Eq(2));
    EXPECT_THAT(compared.records, IsEmpty());
    EXPECT_THAT(compared.diagnostics, ElementsAre(StatusIs(absl::StatusCode::kInvalidArgument)));
    EXPECT_THAT(filesystem.observations.load(), Eq(0));
  }
}

TEST_F(DiffPathsTest, InvalidTextAndBinaryLabelsStopSubsequentActionsDespiteSkipUnsupported) {
  for (const std::string_view primary : {"-diff", "-rdiff"}) {
    for (const std::string_view side : {"left", "right"}) {
      const std::string option = "--diff-" + std::string(side) + "-strip=1";
      ASSERT_OK_AND_ASSIGN(
          const auto text,
          Run({option, "--skip-unsupported", "after", std::string(primary), "before", "-o", "-print"}));
      EXPECT_THAT(text.result.errors, Eq(1));
      EXPECT_THAT(text.records, IsEmpty());
      EXPECT_THAT(text.diagnostics, ElementsAre(StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("strip"))));
      ASSERT_OK_AND_ASSIGN(
          const auto binary, Run(
                                 {option, "--skip-unsupported", "binary/one/file", std::string(primary),
                                  "binary-mirror/one/file", "-o", "-print"}));
      EXPECT_THAT(binary.result.errors, Eq(1));
      EXPECT_THAT(binary.records, IsEmpty());
      EXPECT_THAT(binary.diagnostics, ElementsAre(StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("strip"))));
    }
  }
}

TEST_F(DiffPathsTest, CreationAndDeletionCollisionsKeepOnlyTheEarlierCompletePatch) {
  for (const std::string_view primary : {"-diff", "-rdiff"}) {
    ASSERT_OK_AND_ASSIGN(
        const auto output, Run(
                               {"--sort=name", "--diff-left-strip=1", "--diff-right-strip=1", "--skip-unsupported",
                                "root", "-type", "f", std::string(primary)}));
    EXPECT_THAT(output.result.errors, Eq(1));
    EXPECT_THAT(output.records, ElementsAre(HasSubstr("diff --git \"a/file\" \"b/file\"")));
    EXPECT_THAT(output.diagnostics, ElementsAre(StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("collision"))));
  }
}

TEST_F(DiffPathsTest, ExplicitTextAndBinaryTargetCollisionsAreHardErrorsOnEitherSide) {
  for (const std::string_view primary : {"-diff", "-rdiff"}) {
    ASSERT_OK_AND_ASSIGN(
        const auto text, Run(
                             {"--sort=name", "--diff-left-strip=1", "--diff-right-strip=1", "--skip-unsupported",
                              "root", "-type", "f", std::string(primary), "mirror/{relpath}"}));
    EXPECT_THAT(text.result.errors, Eq(1));
    EXPECT_THAT(text.records, SizeIs(1));
    EXPECT_THAT(text.diagnostics, ElementsAre(StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("collision"))));
    ASSERT_OK_AND_ASSIGN(
        const auto binary, Run(
                               {"--sort=name", "--diff-left-strip=1", "--diff-right-strip=1", "--skip-unsupported",
                                "binary", "-type", "f", std::string(primary), "binary-mirror/{relpath}"}));
    EXPECT_THAT(binary.result.errors, Eq(1));
    EXPECT_THAT(binary.records, IsEmpty());
    EXPECT_THAT(binary.diagnostics, ElementsAre(StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("collision"))));
  }
}

TEST_F(DiffPathsTest, DeferredPatchErrorsStopReplayWithoutDryRunOrFollowingActionOutput) {
  ASSERT_OK_AND_ASSIGN(
      const auto output, Run(
                             {"--diff-right-strip=1", "--skip-unsupported", "after", "-fuzzy", "after", "-top", "1",
                              "-diff", "-o", "-print"}));
  EXPECT_THAT(output.result.errors, Eq(1));
  EXPECT_THAT(output.records, IsEmpty());
  EXPECT_THAT(output.diagnostics, ElementsAre(StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("strip"))));
}
}  // namespace
}  // namespace xff::engine
