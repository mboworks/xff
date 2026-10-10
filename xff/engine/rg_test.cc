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
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"
#include "xff/engine/parallel_match.h"
#include "xff/engine/run.h"
#include "xff/parser/parser.h"
#include "xff/vfs/read_source.h"

namespace xff::engine {
namespace {
using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::mbo::testing::WithDropIndent;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Not;
using ::testing::SizeIs;

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
  std::string stat_error_path;
  std::string read_error_path;
  bool streaming = false;
  vfs::Source entry_source = vfs::Source::kLocalFs;
  vfs::SharedReadSource source_override;
  bool track_threads = false;
  std::size_t expected_threads = 2;
  absl::Duration read_delay = absl::ZeroDuration();
  // Do not make the admission sample wait for workers that have not been admitted yet.
  std::size_t rendezvous_after_reads = 16;
  // Guards only observed reader identities/counts; fixture configuration stays
  // immutable during a run, and no lock spans the simulated per-read latency.
  mutable absl::Mutex read_mutex;
  mutable std::set<std::thread::id> read_threads ABSL_GUARDED_BY(read_mutex);
  mutable std::size_t observed_reads ABSL_GUARDED_BY(read_mutex) = 0;

  bool ReadersReady() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(read_mutex) {
    return read_threads.size() >= expected_threads;
  }

  std::set<std::thread::id> ReadThreads() const ABSL_LOCKS_EXCLUDED(read_mutex) {
    const absl::MutexLock lock(read_mutex);
    return read_threads;
  }

  void ClearReadThreads() ABSL_LOCKS_EXCLUDED(read_mutex) {
    const absl::MutexLock lock(read_mutex);
    read_threads.clear();
    observed_reads = 0;
  }

  void TrackRead() const ABSL_LOCKS_EXCLUDED(read_mutex) {
    if (!track_threads) {
      return;
    }
    const absl::MutexLock lock(read_mutex);
    read_threads.insert(std::this_thread::get_id());
    if (++observed_reads > rendezvous_after_reads) {
      // Bound the wait so failed scheduling produces an assertion rather than a hang.
      read_mutex.AwaitWithTimeout(absl::Condition(this, &SearchFs::ReadersReady), absl::Seconds(2));
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
        entries.push_back({
            .path = name,
            .name = name.substr(5),
            .type = vfs::FileType::kRegular,
            .source = entry_source,
        });
      }
    }
    return entries;
  }

  absl::StatusOr<vfs::Metadata> Stat(std::string_view path, bool) const override {
    if (path == stat_error_path) {
      return absl::PermissionDeniedError("denied metadata");
    }
    if (path == "tree") {
      return vfs::Metadata{.type = vfs::FileType::kDirectory};
    }
    const auto found = files.find(std::string(path));
    if (found == files.end()) {
      return absl::NotFoundError("missing file");
    }
    return vfs::Metadata{
        .type = vfs::FileType::kRegular,
        .source = entry_source,
        .size = found->second.size(),
    };
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
    absl::SleepFor(read_delay);
    reads.fetch_add(1, std::memory_order_relaxed);
    if (read_error || path == read_error_path) {
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
  std::function<void()> after_emit;

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
        command, fs,
        [this](std::string_view text) {
          output += text;
          if (after_emit) {
            after_emit();
          }
        },
        [this](std::string_view, absl::Status status) { errors.emplace_back(status.message()); });
  }
};

TEST_F(RgEngineTest, NativeContextIgnoresUnregisteredEngineGlobals) {
  // CLI validation rejects unknown options before RunFind. Direct engine callers can pass
  // unrelated globals; resolving context must ignore them without losing recognized aliases.
  fs.files["tree/a"] = "before\nhit\nafter\n";
  EXPECT_THAT(
      Run({"--unregistered-context=2", "--before-context=1", "--no-filename", "--no-line-number", "tree/a", "-grep",
           "hit"},
          false)
          .errors,
      Eq(0));
  EXPECT_THAT(errors, IsEmpty());
  EXPECT_THAT(output, WithDropIndent(EqualsText(R"out(
    before
    hit
    )out")));
}

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

TEST_F(RgEngineTest, NativeDecisionBatchesPreserveOutputAndErrorsAcrossLargeBoundaries) {
  fs.files.clear();
  for (std::size_t index = 0; index < 1'100; ++index) {
    fs.files.emplace("tree/" + std::to_string(index), index % 3 == 0 ? "hit\n" : "miss\n");
  }
  fs.stat_error_path = "tree/99";
  std::vector<std::string> args{"--jobs=1", "--archive=none", "--sort=none", "--summary=ext", "tree", "-rxc", "hit"};
  EXPECT_THAT(Run(args, false).errors, Eq(1));
  const auto expected = output;
  const auto expected_errors = errors;
  fs.reads = 0;
  fs.track_threads = true;
  fs.read_delay = absl::Milliseconds(2);
  args.front() = "--jobs=4";
  EXPECT_THAT(Run(args, false).errors, Eq(1));
  EXPECT_THAT(output, EqualsText(expected));
  EXPECT_THAT(errors, Eq(expected_errors));
  EXPECT_THAT(fs.reads.load(), Eq(1'099));
  EXPECT_THAT(fs.ReadThreads(), SizeIs(Gt(1)));
}

TEST_F(RgEngineTest, RgFiltersAndLineSelectionReuseTheSameContent) {
  EXPECT_THAT(Run({"hit", "tree", "--xff", "-content", "hit"}).errors, 0);
  EXPECT_THAT(output, EqualsText("tree/a:hit\n"));
  EXPECT_THAT(fs.reads.load(), 2);
}

TEST_F(RgEngineTest, ContentPredicatesAndRenderedFieldsReuseOneEntryRead) {
  const std::vector<std::vector<std::string>> outputs{
      {"--template={hash:md5}:{hash:md5}:{lines}:{lines}"},
      {"--columns=hash:md5,hash:md5,lines,lines", "--format=csv"},
      {"--summary=hash", "--summary={lines}", "--histogram=lines:sum(lines)"},
  };
  for (const auto& controls : outputs) {
    auto args = controls;
    args.insert(args.end(), {"--jobs=1", "--archive=none", "tree", "-text", "-eofnl", "!", "-binary", "-rxc", "hit"});
    fs.reads = 0;
    EXPECT_THAT(Run(args, false).errors, Eq(0));
    EXPECT_THAT(output, Not(IsEmpty()));
    EXPECT_THAT(fs.reads.load(), Eq(2));
  }
}

TEST_F(RgEngineTest, HashVerificationSharesBytesAndStillRecordsRejectedEntries) {
  EXPECT_THAT(
      Run({"--jobs=1", "--archive=none", "--summary=hash-verification", "tree", "-text", "-hasheq", "deadbeef"}, false)
          .errors,
      Eq(0));
  EXPECT_THAT(output, HasSubstr("failed"));
  EXPECT_THAT(fs.reads.load(), Eq(2));
}

TEST_F(RgEngineTest, ActionFieldReadsAreInvalidatedBeforeTheNextPredicate) {
  after_emit = [&] { fs.files.at("tree/a") = "changed\n"; };
  const auto result = Run(
      {"--jobs=1", "--archive=none", "tree/a", "-content", "hit", "-printf", "%{lines}", "-content", "changed"}, false);
  EXPECT_THAT(result.errors, Eq(0));
  EXPECT_THAT(result.any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("2"));
  EXPECT_THAT(fs.reads.load(), Eq(3));
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
  fs.read_delay = absl::Milliseconds(2);
  EXPECT_THAT(Run({"-j4", "--archive=none", "hit", "tree"}).errors, 0);
  EXPECT_THAT(fs.ReadThreads(), SizeIs(Gt(1)));
  EXPECT_THAT(fs.reads.load(), 300);
  EXPECT_THAT(output, HasSubstr("tree/0:hit 0\n"));
  EXPECT_THAT(output, HasSubstr("tree/299:hit 299\n"));
}

TEST_F(RgEngineTest, SmallSlowBatchCanUseRemainingWork) {
  fs.files.clear();
  std::vector<CollectedEntry> entries;
  for (std::size_t index = 0; index < 16; ++index) {
    const auto path = "tree/" + std::to_string(index);
    fs.files.emplace(path, "hit\n");
    entries.push_back({.path = path, .metadata = {.type = vfs::FileType::kRegular}, .fs = fs});
  }
  // Four three-entry observations leave four independent slow files. Their
  // remaining cost can repay startup even though the whole batch has only 16 entries.
  fs.track_threads = true;
  fs.read_delay = absl::Milliseconds(2);
  fs.expected_threads = 2;
  fs.rendezvous_after_reads = 12;
  ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"tree", "-content", "hit"}));
  RunExecutor executor(3);
  ParallelMatch matcher(*command.expression, 4, executor, false);
  const auto& results = matcher.Match(std::move(entries));
  EXPECT_THAT(results, SizeIs(16));
  for (const auto& result : results) {
    EXPECT_THAT(result.evaluation.matched, IsTrue());
  }
  EXPECT_THAT(executor.worker_count(), Eq(3));
  EXPECT_THAT(fs.reads.load(), Eq(16));
  EXPECT_THAT(fs.ReadThreads(), SizeIs(Gt(1)));
}

TEST_F(RgEngineTest, TinySlowBatchDoesNotStartIdleWorkers) {
  fs.files.clear();
  std::vector<CollectedEntry> entries;
  constexpr std::size_t kEntries = 5;
  for (std::size_t index = 0; index < kEntries; ++index) {
    const auto path = "tree/" + std::to_string(index);
    fs.files.emplace(path, "hit\n");
    entries.push_back({.path = path, .metadata = {.type = vfs::FileType::kRegular}, .fs = fs});
  }
  fs.track_threads = true;
  fs.read_delay = absl::Milliseconds(2);
  fs.expected_threads = 1;
  ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"tree", "-content", "hit"}));
  RunExecutor executor(3);
  ParallelMatch matcher(*command.expression, 4, executor, false);
  const auto& results = matcher.Match(std::move(entries));
  EXPECT_THAT(results, SizeIs(kEntries));
  for (const auto& result : results) {
    EXPECT_THAT(result.evaluation.matched, IsTrue());
  }
  EXPECT_THAT(executor.worker_count(), Eq(0));
  EXPECT_THAT(fs.reads.load(), Eq(kEntries));
  EXPECT_THAT(fs.ReadThreads(), ElementsAre(std::this_thread::get_id()));
}

TEST_F(RgEngineTest, MatcherPoolGrowsForLaterBatchesAndRunsShortTailsInline) {
  fs.files.clear();
  for (std::size_t index = 0; index < 256; ++index) {
    fs.files.emplace("tree/" + std::to_string(index), "hit\n");
  }
  const auto entries = [&](std::size_t count) {
    std::vector<CollectedEntry> result;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      result.push_back({
          .path = "tree/" + std::to_string(index),
          .metadata = {.type = vfs::FileType::kRegular},
          .fs = fs,
      });
    }
    return result;
  };
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"tree", "-content", "hit"}));
  RunExecutor executor(11);
  executor.Start(11);
  ParallelMatch matcher(*command.expression, 12, executor, false);
  fs.track_threads = true;
  fs.read_delay = absl::Milliseconds(2);
  constexpr std::array kBatches{std::pair{20UZ, 4UZ}, std::pair{256UZ, 12UZ}, std::pair{5UZ, 1UZ}};
  for (const auto& [count, workers] : kBatches) {
    fs.expected_threads = workers;
    fs.ClearReadThreads();
    const auto& results = matcher.Match(entries(count));
    EXPECT_THAT(results, SizeIs(count));
    EXPECT_THAT(fs.ReadThreads(), SizeIs(workers));
    for (const auto& result : results) {
      EXPECT_THAT(result.evaluation.matched, IsTrue());
    }
  }
  EXPECT_THAT(fs.ReadThreads(), ElementsAre(std::this_thread::get_id()));
}

TEST_F(RgEngineTest, ArchiveMembersKeepTheirSerialReadAdmissionWithMultipleWorkers) {
  fs.files.clear();
  for (std::size_t index = 0; index < 300; ++index) {
    fs.files.emplace("tree/" + std::to_string(index), "hit\n");
  }
  fs.entry_source = vfs::Source::kArchiveMember;
  EXPECT_THAT(Run({"-j1", "--archive=none", "hit", "tree"}).errors, Eq(0));
  const auto expected = output;
  fs.track_threads = true;
  fs.expected_threads = 1;
  EXPECT_THAT(Run({"-j4", "--archive=none", "hit", "tree"}).errors, Eq(0));
  EXPECT_THAT(output, EqualsText(expected));
  EXPECT_THAT(fs.ReadThreads(), ElementsAre(std::this_thread::get_id()));
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

TEST_F(RgEngineTest, MetadataConsumersKeepParallelFilteringAndOrderedResults) {
  // This controls filter eligibility with metadata consumers, not cold admission.
  // Eager metadata for at least 512 entries warms the shared executor before matching;
  // Uniform per-read latency supplies meaningful remaining content work. The
  // rendezvous starts after sampling, so the sample never waits for unadmitted workers.
  constexpr std::size_t kFiles = 600;
  fs.files.clear();
  for (std::size_t index = 0; index < kFiles; ++index) {
    fs.files.emplace("tree/" + std::to_string(index) + ".txt", index % 2 == 0 ? "hit\n" : "miss\n");
  }
  const std::vector<std::vector<std::string>> consumers{
      {"--summary=ext"},
      {"--summary=type", "--format=csv"},
      {"--no-match-output", "--columns=path,size", "--format=csv"},
      {"--no-match-output", "--template={path}:{size}"},
      {"--no-match-output", "--color=always"},
  };
  for (const auto& consumer : consumers) {
    SCOPED_TRACE(consumer.front());
    for (const bool rg : {false, true}) {
      SCOPED_TRACE(rg);
      auto args = consumer;
      args.insert(args.begin(), {"--jobs=1", "--archive=none", "--sort=none"});
      if (rg) {
        args.insert(args.end(), {"hit", "tree"});
      } else {
        args.insert(args.end(), {"tree", "-rxc", "hit"});
      }
      EXPECT_THAT(Run(args, rg).errors, Eq(0));
      const std::string expected = output;
      fs.reads = 0;
      fs.ClearReadThreads();
      fs.track_threads = true;
      fs.read_delay = absl::Milliseconds(2);
      args.front() = "--jobs=4";
      EXPECT_THAT(Run(args, rg).errors, Eq(0));
      fs.track_threads = false;
      fs.read_delay = absl::ZeroDuration();
      EXPECT_THAT(output, EqualsText(expected));
      EXPECT_THAT(fs.reads.load(), Eq(kFiles));
      EXPECT_THAT(fs.ReadThreads(), SizeIs(Gt(1)));
    }
  }
}

TEST_F(RgEngineTest, ParallelFilterRetainsFuzzyScoresForTemplates) {
  fs.files.clear();
  for (std::size_t index = 0; index < 300; ++index) {
    fs.files.emplace("tree/hit" + std::to_string(index), "hit\n");
  }
  std::vector<std::string> args{
      "--jobs=1", "--archive=none", "--sort=none", "--template={path}:{fuzzy}", "tree", "-rxc", "hit", "-fuzzy", "hit",
  };
  EXPECT_THAT(Run(args, false).errors, Eq(0));
  const auto expected = output;
  EXPECT_THAT(expected, Not(IsEmpty()));
  args.front() = "--jobs=4";
  EXPECT_THAT(Run(args, false).errors, Eq(0));
  EXPECT_THAT(output, EqualsText(expected));
}

TEST_F(RgEngineTest, MetadataConsumerKeepsTraversalAndContentErrorOrder) {
  fs.files = {{"tree/a", "hit\n"}, {"tree/b", "hit\n"}, {"tree/c", "hit\n"}};
  fs.read_error_path = "tree/a";
  fs.stat_error_path = "tree/b";
  EXPECT_THAT(Run({"-j1", "--summary=ext", "--sort=none", "hit", "tree"}).errors, Eq(2));
  const auto expected_errors = errors;
  const auto expected_output = output;
  EXPECT_THAT(Run({"-j4", "--summary=ext", "--sort=none", "hit", "tree"}).errors, Eq(2));
  EXPECT_THAT(errors, Eq(expected_errors));
  EXPECT_THAT(output, EqualsText(expected_output));
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

TEST_F(RgEngineTest, FullLineAndContextOutputUsesStreamsAndRetainsTransactionalErrors) {
  fs.streaming = true;
  fs.files["tree/a"] = "skip\npre\nhit\nhit\npost\nskip\nskip\npre\nhit\npost\n";
  EXPECT_THAT(Run({"-n", "-C1", "hit", "tree/a"}).errors, Eq(0));
  EXPECT_THAT(output, WithDropIndent(EqualsText(R"out(
      tree/a-2-pre
      tree/a:3:hit
      tree/a:4:hit
      tree/a-5-post
      --
      tree/a-8-pre
      tree/a:9:hit
      tree/a-10-post
)out")));
  EXPECT_THAT(fs.reads.load(), Eq(0));
  EXPECT_THAT(fs.sources.load(), Eq(1));
  fs.source_override = std::make_shared<FailingSource>(false);
  EXPECT_THAT(Run({"hit", "tree/a"}).errors, Eq(1));
  EXPECT_THAT(output, IsEmpty());
  fs.source_override = vfs::MemoryReadSource("hit\n" + std::string(70'000, 'x') + std::string(1, '\0'));
  EXPECT_THAT(Run({"hit", "tree/a"}).any_match, IsFalse());
  EXPECT_THAT(output, IsEmpty());
}

TEST_F(RgEngineTest, TypesFilterSearchButExplicitGlobsHavePrecedence) {
  fs.files = {{"tree/a.cc", "hit\n"}, {"tree/a.py", "hit\n"}, {"tree/other", "hit\n"}};
  EXPECT_THAT(Run({"-tcpp", "hit", "tree"}).errors, Eq(0));
  EXPECT_THAT(output, EqualsText("tree/a.cc:hit\n"));
  EXPECT_THAT(Run({"-tcpp", "-g*.py", "hit", "tree"}).errors, Eq(0));
  EXPECT_THAT(output, EqualsText("tree/a.py:hit\n"));
  EXPECT_THAT(Run({"-tcpp", "hit", "tree", "--xff", "-name", "*.py"}).any_match, IsFalse());
  EXPECT_THAT(output, IsEmpty());
  EXPECT_THAT(Run({"--type-list"}).errors, Eq(0));
  EXPECT_THAT(output, HasSubstr("cpp: "));
  EXPECT_THAT(output, HasSubstr("py: "));
  EXPECT_THAT(Run({"-tunknown", "hit", "tree"}).errors, Eq(2));
}

TEST_F(RgEngineTest, ExplicitFilesBypassDiscoveryFiltersButRespectNativePredicates) {
  fs.files = {{"tree/a.py", "hit\n"}};
  for (const std::string_view jobs : {"1", "4"}) {
    for (const std::string_view filter : {"-tcpp", "-Tpy", "-g*.cc", "-g!*.py"}) {
      SCOPED_TRACE(jobs);
      SCOPED_TRACE(filter);
      const std::vector<std::string> options{
          "--jobs=" + std::string(jobs), "--archive=none", "--sort=none", std::string(filter), "hit",
      };
      auto args = options;
      args.emplace_back("tree/a.py");
      EXPECT_THAT(Run(args).errors, Eq(0));
      EXPECT_THAT(output, EqualsText("tree/a.py:hit\n"));
      args.insert(args.end(), {"--xff", "-name", "*.cc"});
      EXPECT_THAT(Run(args).any_match, IsFalse());
      EXPECT_THAT(output, IsEmpty());
      args = options;
      args.emplace_back("tree");
      EXPECT_THAT(Run(args).any_match, IsFalse());
      EXPECT_THAT(output, IsEmpty());
    }
  }
}

TEST_F(RgEngineTest, SharedTypesComposeWithNativeLanguageFiltersAndDoNotDuplicateOutput) {
  fs.files = {{"tree/a.h", "hit header\n"}, {"tree/b.cc", "hit impl\n"}, {"tree/c.c", "hit C\n"}};
  EXPECT_THAT(Run({"-tc", "-tcpp", "hit", "tree", "--xff", "-lang", "C++"}).errors, Eq(0));
  EXPECT_THAT(output, WithDropIndent(EqualsText(R"out(
      tree/a.h:hit header
      tree/b.cc:hit impl
)out")));
  EXPECT_THAT(Run({"-tcpp", "-Tc", "hit", "tree"}).errors, Eq(0));
  EXPECT_THAT(output, EqualsText("tree/b.cc:hit impl\n"));
  EXPECT_THAT(Run({"-Tc", "-tcpp", "hit", "tree", "--xff", "-lang", "C"}).errors, Eq(0));
  EXPECT_THAT(output, EqualsText("tree/a.h:hit header\n"));
}

TEST_F(RgEngineTest, MultilineMatchesArePrintedByLineButCountedByOccurrence) {
  fs.files["tree/a"] = "zero\nalpha one\nbeta two\nend\nalpha\nbeta\n";
  EXPECT_THAT(Run({"-Un", "alpha[^\\n]*\\nbeta", "tree/a"}).errors, Eq(0));
  EXPECT_THAT(output, WithDropIndent(EqualsText(R"out(
      tree/a:2:alpha one
      tree/a:3:beta two
      tree/a:5:alpha
      tree/a:6:beta
)out")));
  EXPECT_THAT(Run({"-Uno", "alpha[^\\n]*\\nbeta", "tree/a"}).errors, Eq(0));
  EXPECT_THAT(output, WithDropIndent(EqualsText(R"out(
      tree/a:2:alpha one
      tree/a:3:beta
      tree/a:5:alpha
      tree/a:6:beta
)out")));
  EXPECT_THAT(Run({"-Uc", "alpha[^\\n]*\\nbeta", "tree/a"}).errors, Eq(0));
  EXPECT_THAT(output, EqualsText("tree/a:2\n"));
  EXPECT_THAT(Run({"-Unv", "alpha[^\\n]*\\nbeta", "tree/a"}).errors, Eq(0));
  EXPECT_THAT(output, WithDropIndent(EqualsText(R"out(
      tree/a:1:zero
      tree/a:4:end
)out")));
  EXPECT_THAT(Run({"-Unx", "alpha\\nbeta", "tree/a"}).errors, Eq(0));
  EXPECT_THAT(output, WithDropIndent(EqualsText(R"out(
      tree/a:5:alpha
      tree/a:6:beta
)out")));
}

TEST_F(RgEngineTest, MultilineDotallContextAndEmptyInputs) {
  fs.files["tree/a"] = "pre\nalpha\nbeta\npost\n";
  EXPECT_THAT(Run({"-U", "alpha.*beta", "tree/a"}).any_match, IsFalse());
  EXPECT_THAT(Run({"-Un", "--multiline-dotall", "-C1", "alpha.*beta", "tree/a"}).any_match, IsTrue());
  EXPECT_THAT(output, WithDropIndent(EqualsText(R"out(
      tree/a-1-pre
      tree/a:2:alpha
      tree/a:3:beta
      tree/a-4-post
)out")));
  EXPECT_THAT(Run({"-U", "alpha\nbeta", "tree/a"}).any_match, IsTrue());
  EXPECT_THAT(Run({"alpha\nbeta", "tree/a"}).errors, Eq(2));
  fs.files["tree/a"] = "";
  EXPECT_THAT(Run({"-U", "alpha", "tree/a"}).any_match, IsFalse());
}

TEST_F(RgEngineTest, HeadingsColumnsAndOverridesPreserveFileBoundaries) {
  fs.files["tree/b"] = "miss hit\n";
  EXPECT_THAT(Run({"--heading", "--column", "--sort=dir", "hit", "tree"}).errors, Eq(0));
  EXPECT_THAT(output, WithDropIndent(EqualsText(R"out(
      tree/a
      1:1:hit

      tree/b
      1:6:miss hit
)out")));
  EXPECT_THAT(Run({"--heading", "--no-heading", "--column", "-o", "hit", "tree/b"}).errors, Eq(0));
  EXPECT_THAT(output, EqualsText("tree/b:1:6:hit\n"));
  EXPECT_THAT(Run({"--heading", "-I", "hit", "tree/b"}).errors, Eq(0));
  EXPECT_THAT(output, EqualsText("miss hit\n"));
  EXPECT_THAT(Run({"--heading", "-c", "hit", "tree/b"}).errors, Eq(0));
  EXPECT_THAT(output, EqualsText("tree/b:1\n"));
  EXPECT_THAT(Run({"--heading", "--format=jsonl", "hit", "tree/b"}).errors, Eq(0));
  EXPECT_THAT(output, HasSubstr("\"record\":\"grep\""));
  EXPECT_THAT(output, Not(HasSubstr("tree/b\n")));
}

TEST_F(RgEngineTest, MultilineEmptyAndNewlinePortionsHaveNoPhantomFinalLine) {
  fs.files["tree/a"] = "abc";
  EXPECT_THAT(Run({"-Un", "$", "tree/a"}).any_match, IsTrue());
  EXPECT_THAT(output, EqualsText("tree/a:1:abc\n"));
  fs.files["tree/a"] = "abc\n";
  EXPECT_THAT(Run({"-Uno", "a*", "tree/a"}).errors, Eq(0));
  EXPECT_THAT(output, WithDropIndent(EqualsText(R"out(
      tree/a:1:a
      tree/a:1:
      tree/a:1:
)out")));
  EXPECT_THAT(Run({"-Uno", "\\n", "tree/a"}).any_match, IsTrue());
  EXPECT_THAT(output, IsEmpty());
  EXPECT_THAT(Run({"--column", "-C1", "b", "tree/a"}).errors, Eq(0));
  EXPECT_THAT(output, EqualsText("tree/a:1:2:abc\n"));
  fs.files["tree/a"] = "pre\nabc\npost\n";
  EXPECT_THAT(Run({"--column", "-C1", "b", "tree/a"}).errors, Eq(0));
  EXPECT_THAT(output, WithDropIndent(EqualsText(R"out(
      tree/a-1-pre
      tree/a:2:2:abc
      tree/a-3-post
)out")));
  fs.files["tree/a"] = "";
  EXPECT_THAT(Run({"-Uc", "", "tree/a"}).any_match, IsFalse());
  EXPECT_THAT(output, IsEmpty());
}

TEST_F(RgEngineTest, MultilineFixedStringsAndPrettyRendering) {
  fs.files["tree/a"] = "prealpha\nbeta\nalpha\nbeta\n";
  EXPECT_THAT(Run({"-UFxn", "alpha\nbeta", "tree/a"}).errors, Eq(0));
  EXPECT_THAT(output, WithDropIndent(EqualsText(R"out(
      tree/a:3:alpha
      tree/a:4:beta
)out")));
  EXPECT_THAT(Run({"-pU", "alpha\\nbeta", "tree/a"}).any_match, IsTrue());
  EXPECT_THAT(output, HasSubstr("\x1b[35mtree/a\x1b[0m\n"));
  EXPECT_THAT(output, HasSubstr("\x1b[1;31malpha\x1b[0m"));
  EXPECT_THAT(output, HasSubstr("\x1b[1;31mbeta\x1b[0m"));
  EXPECT_THAT(Run({"-U", "--regextype=ERE", "alpha", "tree/a"}).errors, Eq(2));
  EXPECT_THAT(Run({"--unicode", "--regextype=ERE", "alpha", "tree/a"}).errors, Eq(2));
}

}  // namespace
}  // namespace xff::engine
