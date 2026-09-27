// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"
#include "xff/engine/run.h"
#include "xff/parser/parser.h"
#include "xff/vfs/read_source.h"

namespace xff::engine {
namespace {
using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::Contains;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Not;

// A restarted cursor reports a failure after an otherwise selected line.
class FailingSource final : public vfs::ReadSource {
 public:
  explicit FailingSource(bool fail_open) : fail_open_(fail_open) {}

  absl::StatusOr<std::unique_ptr<vfs::ReadStream>> Open() const override {
    if (fail_open_) {
      return absl::PermissionDeniedError("cannot open stream");
    }
    return std::make_unique<Stream>();
  }

 private:
  class Stream final : public vfs::ReadStream {
   public:
    absl::StatusOr<std::string> Read(std::size_t max_bytes) override {
      EXPECT_THAT(max_bytes, Eq(65'536));
      if (std::exchange(emitted_, true)) {
        return absl::DataLossError("late read failure");
      }
      return "hit\n";
    }

   private:
    bool emitted_ = false;
  };

  bool fail_open_;
};

struct SearchFs final : vfs::FileSystem {
  std::map<std::string, std::string> files{{"tree/a", "hit\nmiss\n"}, {"tree/b", "miss\n"}, {"patterns", "hit\n"}};
  bool read_error = false;
  bool streaming = false;
  vfs::SharedReadSource source_override;
  bool track_threads = false;
  mutable std::mutex read_mutex;
  mutable std::condition_variable read_condition;
  mutable std::set<std::thread::id> read_threads;

  void TrackRead() const {
    if (!track_threads) {
      return;
    }
    std::unique_lock lock(read_mutex);
    if (read_threads.insert(std::this_thread::get_id()).second) {
      read_condition.notify_all();
      // Force overlap on the first read without hanging if scheduling regresses to serial.
      read_condition.wait_for(lock, std::chrono::seconds(2), [this] { return read_threads.size() > 1; });
    }
  }

  mutable std::atomic<std::size_t> sources = 0;
  mutable std::atomic<std::size_t> reads = 0;

  absl::StatusOr<std::vector<vfs::Entry>> ReadDir(std::string_view path) const override {
    if (path != "tree") {
      return absl::NotFoundError("not a directory");
    }
    std::vector<vfs::Entry> entries;
    entries.reserve(files.size());
    for (const auto& [name, data] : files) {
      if (name.starts_with("tree/")) {
        entries.push_back({.path = name, .name = name.substr(5), .type = vfs::FileType::kRegular});
      }
    }
    return entries;
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool) const override {
    if (path == "tree") {
      return vfs::Metadata{.type = vfs::FileType::kDirectory};
    }
    const auto found = files.find(std::string(path));
    if (found == files.end()) {
      return absl::NotFoundError("missing file");
    }
    return vfs::Metadata{.type = vfs::FileType::kRegular, .size = found->second.size()};
  }

  absl::Status Remove(std::string_view) const override {
    ADD_FAILURE() << "rg must never mutate the source";
    return absl::PermissionDeniedError("read-only");
  }

  bool Access(std::string_view, vfs::AccessMode mode) const override { return mode == vfs::AccessMode::kRead; }

  absl::StatusOr<std::string> ReadLink(std::string_view) const override {
    return absl::InvalidArgumentError("not a link");
  }

  absl::StatusOr<std::string> FsType(std::string_view) const override { return "memory"; }

  absl::StatusOr<bool> IsCaseSensitive(std::string_view) const override { return true; }

  absl::StatusOr<vfs::SharedReadSource> ContentSource(std::string_view path) const override {
    if (!streaming) {
      return FileSystem::ContentSource(path);
    }
    sources.fetch_add(1, std::memory_order_relaxed);
    if (source_override) {
      return source_override;
    }
    if (read_error) {
      return absl::PermissionDeniedError("denied stream");
    }
    const auto found = files.find(std::string(path));
    if (found == files.end()) {
      return absl::NotFoundError("missing stream");
    }
    return vfs::MemoryReadSource(found->second);
  }

  absl::StatusOr<std::string> ReadContent(std::string_view path) const override {
    TrackRead();
    reads.fetch_add(1, std::memory_order_relaxed);
    if (read_error) {
      return absl::PermissionDeniedError("denied content");
    }
    const auto found = files.find(std::string(path));
    if (found == files.end()) {
      return absl::NotFoundError("missing content");
    }
    return found->second;
  }
};

struct RgEngineTest : ::testing::Test {
  SearchFs fs;
  std::string output;
  std::vector<std::string> errors;

  RunResult Run(std::vector<std::string> args, bool rg = true) {
    // Keep record identity explicit here; CLI integration covers automatic filename prefixes.
    if (rg) {
      args.insert(args.begin(), {"--rg", "-H"});
    }
    auto parsed = parser::Parse(args);
    EXPECT_THAT(parsed, IsOk());
    if (!parsed.ok()) {
      return RunResult{.errors = 2};
    }
    auto command = *std::move(parsed);
    parser::BindMatchers(
        command, parser::GrammarFromGlobals(command.globals),
        parser::ResolveCaseMode(command.globals, registry::Style::kXff));
    output.clear();
    errors.clear();
    return RunFind(
        command, fs, [this](std::string_view text) { output += text; },
        [this](std::string_view, absl::Status status) { errors.emplace_back(status.message()); });
  }
};

TEST_F(RgEngineTest, NativeMatchOutputReusesContentWithinEachEntry) {
  for (const std::string_view jobs : {"1", "4"}) {
    fs.reads = 0;
    EXPECT_THAT(
        Run({"--jobs=" + std::string(jobs), "--archive=none", "--sort=none", "--exact", "-M", "tree", "-content", "hit",
             "-rxc", "hit"},
            false)
            .errors,
        0);
    EXPECT_THAT(output, EqualsText("tree/a:1:hit\n"));
    EXPECT_THAT(fs.reads.load(), 2);  // One read for each visited regular file, including the rejected file.
  }
}

TEST_F(RgEngineTest, NativeParallelBatchesReuseReadsWithoutSharingEntryContent) {
  fs.files.clear();
  for (std::size_t index = 0; index < 300; ++index) {
    fs.files.emplace("tree/" + std::to_string(index), "hit " + std::to_string(index) + "\n");
  }
  EXPECT_THAT(
      Run({"--jobs=4", "--archive=none", "--sort=none", "--exact", "-M", "tree", "-rxc", "hit"}, false).errors, 0);
  EXPECT_THAT(fs.reads.load(), 300);
  EXPECT_THAT(output, HasSubstr("tree/0:1:hit 0\n"));
  EXPECT_THAT(output, HasSubstr("tree/299:1:hit 299\n"));
}

TEST_F(RgEngineTest, RgFiltersAndLineSelectionReuseTheSameContent) {
  EXPECT_THAT(Run({"hit", "tree", "--xff", "-content", "hit"}).errors, 0);
  EXPECT_THAT(output, EqualsText("tree/a:hit\n"));
  EXPECT_THAT(fs.reads.load(), 2);
}

TEST_F(RgEngineTest, StatefulPrimariesInvalidateTheContentSnapshot) {
  EXPECT_THAT(Run({"--jobs=1", "-M", "tree/a", "-content", "hit", "-first", "1", "-rxc", "hit"}, false).errors, 0);
  EXPECT_THAT(output, EqualsText("tree/a:1:hit\n"));
  EXPECT_THAT(fs.reads.load(), 2);
}

TEST_F(RgEngineTest, SnapshotRetainsErrorsUntilInvalidated) {
  ContentSnapshot snapshot;
  fs.read_error = true;
  EXPECT_THAT(snapshot.Read(fs, "tree/a"), StatusIs(absl::StatusCode::kPermissionDenied));
  fs.read_error = false;
  EXPECT_THAT(snapshot.Read(fs, "tree/a"), StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(fs.reads.load(), 1);
  snapshot.Invalidate();
  EXPECT_THAT(snapshot.Read(fs, "tree/a"), IsOkAndHolds(EqualsText("hit\nmiss\n")));
  EXPECT_THAT(fs.reads.load(), 2);
}

TEST_F(RgEngineTest, FilenameCountAndQuietSelectionsUseStreams) {
  fs.streaming = true;
  const auto modes = std::to_array<std::string_view>({"-l", "--files-without-match", "-c", "--count-matches", "-q"});
  for (const auto mode : modes) {
    SCOPED_TRACE(mode);
    fs.sources = 0;
    EXPECT_THAT(Run({std::string(mode), "hit", "tree"}).errors, 0);
    EXPECT_THAT(fs.sources.load(), 2);
    EXPECT_THAT(fs.reads.load(), 0);
    if (mode == "-q") {
      EXPECT_THAT(output, IsEmpty());
    } else if (mode == "--files-without-match") {
      EXPECT_THAT(output, EqualsText("tree/b\n"));
    } else if (mode == "-l") {
      EXPECT_THAT(output, EqualsText("tree/a\n"));
    } else {
      EXPECT_THAT(output, EqualsText("tree/a:1\n"));
    }
  }
}

TEST_F(RgEngineTest, StreamedCountPreservesPortionsInversionAndEmptyMatches) {
  fs.streaming = true;
  fs.files.at("tree/a") = "hit hit\r\nmiss\n\n";
  EXPECT_THAT(Run({"--count-matches", "hit", "tree/a"}).errors, 0);
  EXPECT_THAT(output, EqualsText("tree/a:2\n"));
  EXPECT_THAT(Run({"--count-matches", "-v", "hit", "tree/a"}).errors, 0);
  EXPECT_THAT(output, EqualsText("tree/a:2\n"));
  EXPECT_THAT(Run({"--count-matches", "^", "tree/a"}).errors, 0);
  EXPECT_THAT(output, EqualsText("tree/a:3\n"));
  EXPECT_THAT(Run({"--count-matches", "-M", "tree/a", "-rxc", "hit"}, false).errors, 0);
  EXPECT_THAT(output, EqualsText("tree/a:2\n"));
  EXPECT_THAT(fs.sources.load(), 3);  // Native -M reuses its predicate's one buffered read.
  EXPECT_THAT(fs.reads.load(), 1);
}

TEST_F(RgEngineTest, LateBinaryBytesCannotTurnIntoFilenameOrQuietSuccess) {
  fs.streaming = true;
  fs.files.at("tree/a") = "hit\n" + std::string(70'000, 'x') + '\0';
  EXPECT_THAT(Run({"-l", "hit", "tree/a"}).any_match, IsFalse());
  EXPECT_THAT(output, IsEmpty());
  EXPECT_THAT(Run({"-q", "hit", "tree/a"}).any_match, IsFalse());
  EXPECT_THAT(Run({"--text", "-l", "hit", "tree/a"}).any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("tree/a\n"));
  EXPECT_THAT(Run({"--files-with-matches", "tree/a", "-grep", "hit"}, false).any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("tree/a\n"));  // Native binary detection is limited to its prefix.
}

TEST_F(RgEngineTest, StreamingErrorsRemainErrorsForQuietAndFilenameSelection) {
  fs.streaming = true;
  fs.read_error = true;
  EXPECT_THAT(Run({"-q", "hit", "tree"}).errors, 2);
  EXPECT_THAT(Run({"-l", "hit", "tree"}).errors, 2);
  EXPECT_THAT(output, IsEmpty());
}

TEST_F(RgEngineTest, StreamOpenAndLateReadFailuresOutrankEarlyQuietSelection) {
  fs.streaming = true;
  for (const bool fail_open : {true, false}) {
    fs.source_override = std::make_shared<FailingSource>(fail_open);
    const auto result = Run({"-q", "hit", "tree/a"});
    EXPECT_THAT(result.errors, 1);
    EXPECT_THAT(result.any_match, IsFalse());
    EXPECT_THAT(output, IsEmpty());
    EXPECT_THAT(errors, Contains(HasSubstr(fail_open ? "cannot open stream" : "late read failure")));
  }
}

TEST_F(RgEngineTest, OrdinaryRgSearchUsesContentWorkersWithoutNativeExpression) {
  fs.files.clear();
  for (std::size_t index = 0; index < 300; ++index) {
    fs.files.emplace("tree/" + std::to_string(index), "hit " + std::to_string(index) + "\n");
  }
  fs.track_threads = true;
  EXPECT_THAT(Run({"-j4", "--archive=none", "hit", "tree"}).errors, 0);
  EXPECT_THAT(fs.read_threads.size(), Gt(1));
  EXPECT_THAT(fs.reads.load(), 300);
  EXPECT_THAT(output, HasSubstr("tree/0:hit 0\n"));
  EXPECT_THAT(output, HasSubstr("tree/299:hit 299\n"));
}

TEST_F(RgEngineTest, ParallelRgMatchesSerialRecordsAcrossBatchesAndOutputModes) {
  fs.files.clear();
  for (std::size_t index = 0; index < 300; ++index) {
    fs.files.emplace("tree/" + std::to_string(index), index % 3 == 0 ? "before\nhit hit\nafter\n" : "miss\n");
  }
  const std::vector<std::vector<std::string>> modes{
      {},
      {"--format=jsonl"},
      {"-o"},
      {"-C1"},
      {"-c"},
      {"--count-matches"},
      {"-v"},
      {"-l"},
      {"--files-without-match"},
      {"-q"},
      {"--no-match-output"},
      {"--max-results=3"},
  };
  for (const auto& mode : modes) {
    auto args = mode;
    args.insert(args.end(), {"--archive=none", "--sort=none", "hit", "tree"});
    args.insert(args.begin(), "-j1");
    const auto serial = Run(args);
    EXPECT_THAT(serial.errors, 0);
    const auto expected = output;
    args.front() = "-j4";
    const auto parallel = Run(args);
    EXPECT_THAT(parallel.errors, 0);
    EXPECT_THAT(parallel.any_match, serial.any_match);
    EXPECT_THAT(output, EqualsText(expected));
  }
}

TEST_F(RgEngineTest, ParallelNativeFilterAndRgSelectionShareEachRead) {
  fs.files.clear();
  for (std::size_t index = 0; index < 300; ++index) {
    fs.files.emplace("tree/" + std::to_string(index), "hit\n");
  }
  EXPECT_THAT(Run({"-j4", "--archive=none", "hit", "tree", "--xff", "-content", "hit"}).errors, 0);
  EXPECT_THAT(fs.reads.load(), 300);
  fs.reads = 0;
  EXPECT_THAT(Run({"-j4", "--archive=none", "-g0", "hit", "tree"}).errors, 0);
  EXPECT_THAT(fs.reads.load(), 1);
  EXPECT_THAT(output, EqualsText("tree/0:hit\n"));
}

TEST_F(RgEngineTest, ParallelErrorsRemainErrorsAndProduceNoPartialRecords) {
  fs.files.clear();
  for (std::size_t index = 0; index < 100; ++index) {
    fs.files.emplace("tree/" + std::to_string(index), "hit\n");
  }
  fs.read_error = true;
  EXPECT_THAT(Run({"-j4", "--archive=none", "hit", "tree"}).errors, 100);
  EXPECT_THAT(output, IsEmpty());
  EXPECT_THAT(Run({"-j4", "--archive=none", "-q", "hit", "tree"}).errors, 100);
  EXPECT_THAT(output, IsEmpty());
}

TEST_F(RgEngineTest, StatefulNativeFiltersStayInCoordinator) {
  fs.files.clear();
  for (std::size_t index = 0; index < 100; ++index) {
    fs.files.emplace("tree/" + std::to_string(index), "hit\n");
  }
  EXPECT_THAT(Run({"-j4", "--archive=none", "hit", "tree", "--xff", "-type", "f", "-first", "1"}).errors, 0);
  EXPECT_THAT(fs.reads.load(), 1);
  EXPECT_THAT(output, EqualsText("tree/0:hit\n"));
}

TEST_F(RgEngineTest, ResourceInspectionExplainsBufferedAndStreamedSearches) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto lines, parser::Parse({"--rg", "hit", "tree"}));
  EXPECT_THAT(ExplainResources(lines), IsOkAndHolds(HasSubstr("line output materialized per file")));
  MBO_ASSERT_OK_AND_ASSIGN(const auto counts, parser::Parse({"--rg", "-c", "hit", "tree"}));
  EXPECT_THAT(ExplainResources(counts), IsOkAndHolds(HasSubstr("streamed line selection; longest line retained")));
}

TEST_F(RgEngineTest, PreparationRequiresAnRgSearch) {
  EXPECT_THAT(
      PrepareRgOutput({}, fs, false, false), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("not configured")));
}

TEST_F(RgEngineTest, VirtualPatternFilesAndEntriesUseTheSuppliedFilesystem) {
  const auto result = Run({"-f", "patterns", "tree", "--xff", "-name", "a"});
  EXPECT_THAT(result.errors, Eq(0));
  EXPECT_THAT(result.any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("tree/a:hit\n"));
}

TEST_F(RgEngineTest, ReadFailuresAreErrorsNotNoMatches) {
  fs.read_error = true;
  const auto result = Run({"hit", "tree"});
  EXPECT_THAT(result.errors, Eq(2));
  EXPECT_THAT(result.any_match, IsFalse());
  EXPECT_THAT(errors, Contains(HasSubstr("denied content")));
}

TEST_F(RgEngineTest, PatternReadFailuresStopBeforeTraversal) {
  fs.read_error = true;
  const auto result = Run({"-f", "patterns", "tree"});
  EXPECT_THAT(result.errors, Eq(2));
  EXPECT_THAT(result.any_match, IsFalse());
  EXPECT_THAT(errors.size(), Eq(1));
}

TEST_F(RgEngineTest, NonmatchingFilesAndDirectoriesDoNotSetSuccess) {
  const auto result = Run({"absent", "tree"});
  EXPECT_THAT(result.errors, Eq(0));
  EXPECT_THAT(result.any_match, IsFalse());
  EXPECT_THAT(output, IsEmpty());
}

TEST_F(RgEngineTest, EmptyPatternsAndInvertedPortionsFollowRg) {
  EXPECT_THAT(Run({"--count-matches", "^", "tree/a"}).any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("tree/a:2\n"));
  EXPECT_THAT(Run({"-vo", "hit", "tree/a"}).any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("tree/a:miss\n"));
}

TEST_F(RgEngineTest, MaximumColumnsMeasureTheEmittedMatchBeforePrefixes) {
  fs.files.insert_or_assign("tree/a", "hit and other text\n");
  EXPECT_THAT(Run({"-on", "-M3", "hit", "tree/a"}).any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("tree/a:1:hit\n"));
  EXPECT_THAT(Run({"-on", "-M3", "hit.*", "tree/a"}).any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("tree/a:1:[Omitted long matching line]\n"));
  EXPECT_THAT(Run({"-o", "-M3", "hit.*", "tree/a", "--format=jsonl"}).any_match, IsTrue());
  EXPECT_THAT(output, HasSubstr(R"("text":"[Omitted long matching line]")"));
  EXPECT_THAT(output, HasSubstr(R"("line":1)"));
  EXPECT_THAT(Run({"-o", "-M3", "hit", "tree/a", "--format=jsonl"}).any_match, IsTrue());
  EXPECT_THAT(output, HasSubstr(R"("text":"hit")"));
}

TEST_F(RgEngineTest, WholeLineMatchingUsesEachGrammarsFullMatchSemantics) {
  const std::vector<std::pair<std::string, std::string>> patterns{
      {"RE2", "h|hit"}, {"ERE", "h|hit"}, {"EXACT", "hit"}, {"FNMATCH", "h?t"}, {"GLOB", "h?t"}, {"SHGLOB", "{h,hit}"},
  };
  fs.files.insert_or_assign("tree/a", "hit\nshit\nhits\n");
  for (const auto& [grammar, pattern] : patterns) {
    EXPECT_THAT(Run({"--regextype=" + grammar, "-ox", pattern, "tree/a"}).errors, Eq(0));
    EXPECT_THAT(output, EqualsText("tree/a:hit\n"));
    EXPECT_THAT(Run({"--regextype=" + grammar, "-x", "--count-matches", pattern, "tree/a"}).any_match, IsTrue());
    EXPECT_THAT(output, EqualsText("tree/a:1\n"));
  }
}

TEST_F(RgEngineTest, WholeLineLiteralPatternsAndWordPrecedenceRemainLiteral) {
  fs.files.insert_or_assign("tree/a", "a.b\naXb\na.bc\n");
  EXPECT_THAT(Run({"-Fx", "a.b", "tree/a"}).any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("tree/a:a.b\n"));
  EXPECT_THAT(Run({"-Fx", "-e", "a", "-e", "a.b", "tree/a"}).any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("tree/a:a.b\n"));
  fs.files.insert_or_assign("tree/a", "@\n@x\n");
  EXPECT_THAT(Run({"-Fwx", "@", "tree/a"}).any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("tree/a:@\n"));
  fs.files.insert_or_assign("tree/a", "\nmiss\n");
  EXPECT_THAT(Run({"-x", "--count-matches", "", "tree/a"}).any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("tree/a:1\n"));
}

TEST_F(RgEngineTest, SmartCaseAppliesToTheWholePatternUnion) {
  EXPECT_THAT(Run({"-S", "-e", "HIT", "-e", "MISS", "tree"}).any_match, IsFalse());
  EXPECT_THAT(Run({"-S", "-e", "HIT", "-e", "miss", "tree/a"}).any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("tree/a:miss\n"));
}

TEST_F(RgEngineTest, WordMatchingChecksAdjacentCharactersIncludingPunctuationPatterns) {
  fs.files.insert_or_assign("tree/a", "@\nword@word\n @ \n");
  EXPECT_THAT(Run({"-ow", "@", "tree/a"}).any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("tree/a:@\ntree/a:@\n"));
}

TEST_F(RgEngineTest, SummariesCountSearchSelectedFiles) {
  EXPECT_THAT(Run({"hit", "tree", "--summary"}).any_match, IsTrue());
  EXPECT_THAT(output, HasSubstr("1"));
  EXPECT_THAT(output, Not(HasSubstr("tree/a:hit")));
}

TEST_F(RgEngineTest, ListingModeStillFiltersByTheRgSearch) {
  EXPECT_THAT(Run({"hit", "tree", "--no-match-output"}).any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("tree/a\n"));
}

TEST_F(RgEngineTest, OutputLimitDoesNotLeakExtraContent) {
  fs.files.insert_or_assign("tree/b", "hit\n");
  EXPECT_THAT(Run({"hit", "tree", "--sort=global", "--max-results=1"}).any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("tree/a:hit\n"));
}

TEST_F(RgEngineTest, InvalidFormatsAndConflictingModesAreRejected) {
  EXPECT_THAT(Run({"hit\nmiss", "tree"}).errors, Eq(2));
  EXPECT_THAT(Run({"hit", "tree", "--format=csv"}).errors, Eq(2));
  EXPECT_THAT(Run({"hit", "tree", "--columns=path"}).errors, Eq(2));
  EXPECT_THAT(Run({"hit", "tree", "--compare"}).errors, Eq(2));
  EXPECT_THAT(Run({"hit", "tree", "--xff", "-delete"}).errors, Eq(2));
  EXPECT_THAT(Run({"hit", "tree", "--xff", "-exec", "echo", "{}", ";"}).errors, Eq(2));
}

TEST_F(RgEngineTest, NonprintingActionsAreRejectedEvenInUnreachableBranches) {
  const std::vector<std::vector<std::string>> filters{
      {"-capture:x", "echo", "captured", ";"},
      {"-capturedir:x", "echo", "captured", ";"},
      {"-prune"},
      {"-false", "-a", "-capture:x", "echo", "captured", ";"},
      {"!", "(", "-true", "+", "-prune", ")"},
  };
  for (const auto& filter : filters) {
    std::vector<std::string> args{"hit", "tree", "--xff"};
    args.append_range(filter);
    EXPECT_THAT(Run(std::move(args)).errors, Eq(2));
    EXPECT_THAT(errors, Contains(HasSubstr("not actions")));
    EXPECT_THAT(output, IsEmpty());
  }
  EXPECT_THAT(Run({"hit", "tree", "--xff", "!", "(", "-false", "+", "-name", "b", ")"}).any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("tree/a:hit\n"));
}

TEST_F(RgEngineTest, OverlappingMatchesRespectInlineAndFilePatternOrder) {
  fs.files.insert_or_assign("patterns", "h\n");
  EXPECT_THAT(Run({"-o", "-f", "patterns", "-e", "hit", "tree/a"}).any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("tree/a:h\n"));
  EXPECT_THAT(Run({"-o", "-e", "hit", "-f", "patterns", "tree/a"}).any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("tree/a:hit\n"));
}

TEST_F(RgEngineTest, EmptyPatternFileJsonDoesNotInventAnEmptyPattern) {
  fs.files.insert_or_assign("patterns", "");
  EXPECT_THAT(Run({"-f", "patterns", "--files-without-match", "tree/a", "--format=jsonl"}).any_match, IsTrue());
  EXPECT_THAT(output, HasSubstr("\"patterns\":[]"));
}

TEST_F(RgEngineTest, ExplanationIdentifiesTheSeparateContentSearch) {
  ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"--rg", "hit", "tree"}));
  EXPECT_THAT(ExplainResources(command), IsOkAndHolds(HasSubstr("rg-search")));
}

TEST_F(RgEngineTest, JsonUsesTheExistingXffContentSchema) {
  EXPECT_THAT(Run({"hit", "tree", "--format=jsonl"}).any_match, IsTrue());
  EXPECT_THAT(output, HasSubstr("\"record\":\"grep\""));
  EXPECT_THAT(output, HasSubstr("\"path\":\"tree/a\""));
}
}  // namespace
}  // namespace xff::engine
