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
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
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
#include "xff/engine/evaluate.h"
#include "xff/engine/output_credit.h"
#include "xff/engine/owned_visit.h"
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
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Ge;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Le;
using ::testing::Not;
using ::testing::Optional;
using ::testing::Property;
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
  std::string serial_entry;
  vfs::SharedReadSource source_override;
  std::function<void(std::string_view)> before_read;
  bool track_threads = false;
  std::size_t expected_threads = 2;
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
      read_condition.wait_for(
          lock, std::chrono::seconds(2), [this] { return read_threads.size() >= expected_threads; });
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
            .source = name == serial_entry ? vfs::Source::kArchiveMember : entry_source,
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
        .source = path == serial_entry ? vfs::Source::kArchiveMember : entry_source,
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
    if (before_read) {
      before_read(path);
    }
    TrackRead();
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

  std::vector<OwnedVisit> RepeatedEntries(std::size_t count) const {
    const vfs::Metadata metadata{.type = vfs::FileType::kRegular};
    std::vector<OwnedVisit> entries;
    entries.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      entries.emplace_back(Visit{.path = "tree/a", .name = "a", .root = "tree", .metadata = metadata, .fs = fs});
    }
    return entries;
  }

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

TEST_F(RgEngineTest, ResultBuffersReuseSmallOutputAndDiscardGiantAndTruncatedTails) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"tree", "-content", "hit"}));
  MBO_ASSERT_OK_AND_ASSIGN(const auto prepared, PrepareMatchOutput(*command.expression));
  ParallelMatch matcher(
      *command.expression, 4, false,
      ParallelContentOutput{
          .output = prepared,
          .grep = {.line_number = false, .filename = false},
      });
  fs.files["tree/a"] = std::string(1'024, 'x') + "hit\n";
  const auto& initial = matcher.Match(RepeatedEntries(64));
  EXPECT_THAT(initial, SizeIs(64));
  fs.files["tree/a"] = "hit\n";
  const auto& reused = matcher.Match(RepeatedEntries(64));
  for (const auto& result : reused) {
    EXPECT_THAT(
        result.content,
        Optional(AllOf(
            Field(&ContentResult::selected, IsOkAndHolds(IsTrue())),
            Field(&ContentResult::text, AllOf(EqualsText("hit\n"), Property(&std::string::capacity, Ge(1'024)))))));
  }
  fs.files["tree/a"] = std::string((ParallelMatch::kReusableOutputBytes / 64) + 100, 'x') + "hit\n";
  EXPECT_THAT(matcher.Match(RepeatedEntries(64)), SizeIs(64));
  fs.files["tree/a"] = "hit\n";
  for (const auto& result : matcher.Match(RepeatedEntries(64))) {
    EXPECT_THAT(
        result.content,
        Optional(Field(
            &ContentResult::text,
            AllOf(
                EqualsText("hit\n"), Property(&std::string::capacity, Le(ParallelMatch::kReusableOutputBytes / 64))))));
  }
  fs.files["tree/a"] = std::string(2 * ParallelMatch::kReusableRecordBytes, 'x') + "hit\n";
  EXPECT_THAT(matcher.Match(RepeatedEntries(64)), SizeIs(64));
  fs.files["tree/a"] = "hit\n";
  const auto& tail = matcher.Match(RepeatedEntries(1));
  ASSERT_THAT(tail, SizeIs(1));
  EXPECT_THAT(
      tail.front().content,
      Optional(Field(
          &ContentResult::text,
          AllOf(EqualsText("hit\n"), Property(&std::string::capacity, Le(ParallelMatch::kReusableRecordBytes))))));
  fs.files["tree/a"] = "miss\n";
  for (const auto& result : matcher.Match(RepeatedEntries(1))) {
    EXPECT_THAT(result.evaluation.matched, IsFalse());
    EXPECT_THAT(result.content, Eq(std::nullopt));
  }
  EXPECT_THAT(matcher.Match(RepeatedEntries(0)), IsEmpty());
  fs.files["tree/a"] = "hit\n";
  for (const auto& result : matcher.Match(RepeatedEntries(128))) {
    EXPECT_THAT(result.evaluation.matched, IsTrue());
    EXPECT_THAT(result.content, Optional(Field(&ContentResult::text, EqualsText("hit\n"))));
  }
}

TEST_F(RgEngineTest, ReusedRgResultSlotsClearSelectionErrorsAndOldOutput) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"--rg", "hit", "tree"}));
  MBO_ASSERT_OK_AND_ASSIGN(const auto prepared, PrepareRgOutput(command, fs, false, false));
  const auto search = command.rg.value_or(parser::RgSearch{});
  ParallelMatch matcher(
      {}, 4, false,
      ParallelContentOutput{
          .output = prepared,
          .rg = search,
          .grep = {.match_output = true, .line_number = false, .filename = false},
      });
  for (const bool fail : {true, false, true, false}) {
    fs.read_error = fail;
    fs.files["tree/a"] = "hit\n";
    const auto& results = matcher.Match(RepeatedEntries(64));
    for (const auto& result : results) {
      if (fail) {
        EXPECT_THAT(
            result.content,
            Optional(
                Field(&ContentResult::selected, StatusIs(absl::StatusCode::kPermissionDenied, HasSubstr("denied")))));
      } else {
        EXPECT_THAT(
            result.content, Optional(AllOf(
                                Field(&ContentResult::selected, IsOkAndHolds(IsTrue())),
                                Field(&ContentResult::text, EqualsText("hit\n")))));
      }
    }
  }
  fs.files["tree/a"] = "miss\n";
  for (const auto& result : matcher.Match(RepeatedEntries(1))) {
    EXPECT_THAT(
        result.content,
        Optional(
            AllOf(Field(&ContentResult::selected, IsOkAndHolds(IsFalse())), Field(&ContentResult::text, IsEmpty()))));
  }
}

TEST_F(RgEngineTest, NativeLineTransactionsKeepCallbackBoundariesAndContextSeparators) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"tree", "-content", "hit"}));
  MBO_ASSERT_OK_AND_ASSIGN(const auto prepared, PrepareMatchOutput(*command.expression));
  fs.files.at("tree/a") = "before\nhit\nafter\nskip\nskip\nhit\nlast\n";
  const auto entries = RepeatedEntries(1);
  std::vector<std::string> records;
  const auto sink = [&](std::string_view text) { records.emplace_back(text); };
  Control control;
  EvalContext context{
      .visit = entries.front().AsVisit(),
      .emit = sink,
      .fs = fs,
      .grep = {.line_number = false, .filename = false},
      .grep_before = 1,
      .grep_after = 1,
      .control = control,
  };
  EXPECT_THAT(EmitMatchOutput(prepared, context), IsTrue());
  EXPECT_THAT(
      records, ElementsAreArray({
                   EqualsText("before\n"),
                   EqualsText("hit\n"),
                   EqualsText("after\n"),
                   EqualsText("--\n"),
                   EqualsText("skip\n"),
                   EqualsText("hit\n"),
                   EqualsText("last\n"),
               }));
}

TEST_F(RgEngineTest, NativeLineTransactionsPublishNothingAfterLateFailure) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"tree", "-content", "hit"}));
  MBO_ASSERT_OK_AND_ASSIGN(const auto prepared, PrepareMatchOutput(*command.expression));
  const auto entries = RepeatedEntries(1);
  fs.streaming = true;
  fs.source_override = std::make_shared<FailingSource>(false);
  std::vector<std::string> records;
  const auto sink = [&](std::string_view text) { records.emplace_back(text); };
  Control control;
  EvalContext context{
      .visit = entries.front().AsVisit(),
      .emit = sink,
      .fs = fs,
      .grep = {.line_number = false, .filename = false},
      .control = control,
  };
  EXPECT_THAT(EmitMatchOutput(prepared, context), IsFalse());
  EXPECT_THAT(records, IsEmpty());
  context.emit("restored\n");
  EXPECT_THAT(records, ElementsAre(EqualsText("restored\n")));
}

TEST_F(RgEngineTest, NativeLineTransactionsKeepTheirSinkWhenCallbackReplacesIt) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"tree", "-content", "hit"}));
  MBO_ASSERT_OK_AND_ASSIGN(const auto prepared, PrepareMatchOutput(*command.expression));
  fs.files.at("tree/a") = "hit\nhit\n";
  const auto entries = RepeatedEntries(1);
  std::vector<std::string> records;
  std::vector<std::string> alternate_records;
  const auto alternate = [&](std::string_view text) { alternate_records.emplace_back(text); };
  std::function<void()> replace_sink;
  const auto sink = [&](std::string_view text) {
    records.emplace_back(text);
    replace_sink();
  };
  Control control;
  EvalContext context{
      .visit = entries.front().AsVisit(),
      .emit = sink,
      .fs = fs,
      .grep = {.line_number = false, .filename = false},
      .control = control,
  };
  replace_sink = [&] { context.emit = alternate; };
  EXPECT_THAT(EmitMatchOutput(prepared, context), IsTrue());
  EXPECT_THAT(records, ElementsAre(EqualsText("hit\n"), EqualsText("hit\n")));
  EXPECT_THAT(alternate_records, IsEmpty());
  context.emit("tail\n");
  EXPECT_THAT(alternate_records, ElementsAre(EqualsText("tail\n")));
}

TEST_F(RgEngineTest, OwnedRgRecordsRetainScratchAndRestoreHeadingAndSink) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"--rg", "hit", "tree"}));
  MBO_ASSERT_OK_AND_ASSIGN(const auto prepared, PrepareRgOutput(command, fs, false, false));
  const auto search = command.rg.value_or(parser::RgSearch{});
  const auto entries = RepeatedEntries(1);
  Control control;
  const auto sink = [&](std::string_view text) { output.append(text); };
  EvalContext context{
      .visit = entries.front().AsVisit(),
      .emit = sink,
      .fs = fs,
      .grep = {.heading = true, .line_number = false, .filename = true},
      .control = control,
  };
  std::string scratch(1'024, 'x');
  const auto capacity = scratch.capacity();
  MBO_ASSERT_OK_AND_ASSIGN(const auto rendered, RenderRgOutput(prepared, search, context, std::move(scratch)));
  EXPECT_THAT(rendered.selected, IsTrue());
  EXPECT_THAT(rendered.text, EqualsText("tree/a\nhit\n"));
  EXPECT_THAT(rendered.text, Property(&std::string::capacity, Ge(capacity)));
  EXPECT_THAT(context.grep.filename, IsTrue());
  EXPECT_THAT(output, IsEmpty());
  context.emit("restored\n");
  EXPECT_THAT(output, EqualsText("restored\n"));
}

TEST_F(RgEngineTest, OwnedRgRecordsDiscardLateFailureAndRestoreTheSink) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"--rg", "hit", "tree"}));
  MBO_ASSERT_OK_AND_ASSIGN(const auto prepared, PrepareRgOutput(command, fs, false, false));
  const auto search = command.rg.value_or(parser::RgSearch{});
  const auto entries = RepeatedEntries(1);
  fs.streaming = true;
  fs.source_override = std::make_shared<FailingSource>(false);
  Control control;
  const auto sink = [&](std::string_view text) { output.append(text); };
  EvalContext context{
      .visit = entries.front().AsVisit(),
      .emit = sink,
      .fs = fs,
      .grep = {.heading = true, .line_number = false, .filename = true},
      .control = control,
  };
  EXPECT_THAT(
      RenderRgOutput(prepared, search, context, "stale"),
      StatusIs(absl::StatusCode::kDataLoss, HasSubstr("late read failure")));
  EXPECT_THAT(output, IsEmpty());
  EXPECT_THAT(context.grep.filename, IsTrue());
  fs.source_override.reset();
  MBO_ASSERT_OK_AND_ASSIGN(const auto rendered, RenderRgOutput(prepared, search, context));
  EXPECT_THAT(rendered.text, EqualsText("tree/a\nhit\n"));
  EXPECT_THAT(output, IsEmpty());
  EXPECT_THAT(EmitRgOutput(prepared, search, context), IsOkAndHolds(IsTrue()));
  EXPECT_THAT(output, EqualsText("tree/a\nhit\n"));
}

TEST_F(RgEngineTest, OwnedRgRecordsDiscardBinaryAndNonmatchingScratch) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"--rg", "hit", "tree"}));
  MBO_ASSERT_OK_AND_ASSIGN(const auto prepared, PrepareRgOutput(command, fs, false, false));
  const auto search = command.rg.value_or(parser::RgSearch{});
  const auto entries = RepeatedEntries(1);
  fs.streaming = true;
  fs.files.at("tree/a") = "hit\n" + std::string(70'000, 'x') + '\0';
  Control control;
  const auto sink = [&](std::string_view text) { output.append(text); };
  EvalContext context{
      .visit = entries.front().AsVisit(),
      .emit = sink,
      .fs = fs,
      .grep = {.line_number = false, .filename = false},
      .control = control,
  };
  MBO_ASSERT_OK_AND_ASSIGN(const auto binary, RenderRgOutput(prepared, search, context, "stale"));
  EXPECT_THAT(binary.selected, IsFalse());
  EXPECT_THAT(binary.text, IsEmpty());
  fs.files.at("tree/a") = "miss\n";
  EXPECT_THAT(
      RenderRgOutput(prepared, search, context, "old output"),
      IsOkAndHolds(AllOf(
          Field("selected", &RenderedRgOutput::selected, IsFalse()),
          Field("text", &RenderedRgOutput::text, IsEmpty()))));
  EXPECT_THAT(output, IsEmpty());
}

TEST_F(RgEngineTest, RgResultBuffersReuseTransferredCapacityWithinExistingBounds) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"--rg", "hit", "tree"}));
  MBO_ASSERT_OK_AND_ASSIGN(const auto prepared, PrepareRgOutput(command, fs, false, false));
  const auto search = command.rg.value_or(parser::RgSearch{});
  ParallelMatch matcher(
      {}, 4, false,
      ParallelContentOutput{
          .output = prepared,
          .rg = search,
          .grep = {.match_output = true, .line_number = false, .filename = false},
      });
  fs.files.at("tree/a") = std::string(1'024, 'x') + "hit\n";
  EXPECT_THAT(matcher.Match(RepeatedEntries(64)), SizeIs(64));
  fs.files.at("tree/a") = "hit\n";
  for (const auto& result : matcher.Match(RepeatedEntries(64))) {
    EXPECT_THAT(
        result.content,
        Optional(Field(
            "text", &ContentResult::text, AllOf(EqualsText("hit\n"), Property(&std::string::capacity, Ge(1'024))))));
  }
  fs.files.at("tree/a") = std::string(2 * ParallelMatch::kReusableRecordBytes, 'x') + "hit\n";
  EXPECT_THAT(matcher.Match(RepeatedEntries(64)), SizeIs(64));
  fs.files.at("tree/a") = "hit\n";
  for (const auto& result : matcher.Match(RepeatedEntries(1))) {
    EXPECT_THAT(
        result.content,
        Optional(Field(
            "text", &ContentResult::text,
            AllOf(EqualsText("hit\n"), Property(&std::string::capacity, Le(ParallelMatch::kReusableRecordBytes))))));
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
  args.front() = "--jobs=4";
  EXPECT_THAT(Run(args, false).errors, Eq(1));
  EXPECT_THAT(output, EqualsText(expected));
  EXPECT_THAT(errors, Eq(expected_errors));
  EXPECT_THAT(fs.reads.load(), Eq(1'099));
  EXPECT_THAT(fs.read_threads.size(), Gt(1));
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
  EXPECT_THAT(Run({"-j4", "--archive=none", "hit", "tree"}).errors, 0);
  EXPECT_THAT(fs.read_threads.size(), Gt(1));
  EXPECT_THAT(fs.reads.load(), 300);
  EXPECT_THAT(output, HasSubstr("tree/0:hit 0\n"));
  EXPECT_THAT(output, HasSubstr("tree/299:hit 299\n"));
}

TEST_F(RgEngineTest, ParallelFieldRenderingReusesOnlyRequestedDerivedValues) {
  fs.files.clear();
  for (std::size_t index = 0; index < 128; ++index) {
    fs.files.emplace("tree/" + std::to_string(index), index % 4 == 0 ? "miss\n" : "hit\nsecond\n");
  }
  const std::string field = "--template={path}:{hash:md5}:{hash:md5}:{lines}:{lines}";
  EXPECT_THAT(Run({"--jobs=1", field, "tree", "-type", "f", "-content", "hit"}, false).errors, Eq(0));
  const auto serial = output;
  EXPECT_THAT(fs.reads.load(), Eq(128));
  fs.reads = 0;
  fs.track_threads = true;
  fs.expected_threads = 4;
  EXPECT_THAT(Run({"--jobs=4", field, "tree", "-type", "f", "-content", "hit"}, false).errors, Eq(0));
  EXPECT_THAT(output, EqualsText(serial));
  EXPECT_THAT(fs.reads.load(), Eq(128));
  EXPECT_THAT(fs.read_threads.size(), Gt(1));
  fs.reads = 0;
  EXPECT_THAT(
      Run({"--jobs=4", "--max-results=1", field, "tree", "-type", "f", "-content", "hit"}, false).errors, Eq(0));
  // A limit retains coordinator laziness instead of deriving hashes for unrendered matches.
  EXPECT_THAT(fs.reads.load(), Eq(129));
}

TEST_F(RgEngineTest, ParallelColumnsAndLineHistogramsReuseLoadedContent) {
  fs.files.clear();
  for (std::size_t index = 0; index < 128; ++index) {
    fs.files.emplace("tree/" + std::to_string(index), index % 2 == 0 ? "hit\nsecond\n" : "miss\n");
  }
  const std::vector<std::vector<std::string>> controls{
      {"--columns=path,lines", "--format=csv"}, {"--summary={lines}", "--format=jsonl"},
      {"--histogram=lines", "--format=jsonl"},  {"--histogram=ext:sum(lines)", "--format=jsonl"},
      {"--histogram=ext", "--format=jsonl"},
  };
  for (const auto& output_controls : controls) {
    auto arguments = output_controls;
    arguments.insert(arguments.end(), {"--jobs=1", "tree", "-type", "f", "-content", "hit"});
    fs.reads = 0;
    EXPECT_THAT(Run(arguments, false).errors, Eq(0));
    const auto serial = output;
    EXPECT_THAT(fs.reads.load(), Eq(128));
    arguments.at(output_controls.size()) = "--jobs=4";
    fs.reads = 0;
    EXPECT_THAT(Run(arguments, false).errors, Eq(0));
    EXPECT_THAT(output, EqualsText(serial));
    EXPECT_THAT(fs.reads.load(), Eq(128));
  }
}

TEST_F(RgEngineTest, DerivedSummariesRespectRgSelectionAndReadFailures) {
  fs.files.clear();
  for (std::size_t index = 0; index < 128; ++index) {
    fs.files.emplace("tree/" + std::to_string(index), index % 2 == 0 ? "hit needle\nsecond\n" : "hit other\n");
  }
  EXPECT_THAT(
      Run({"--jobs=1", "--summary={lines}", "--format=jsonl", "needle", "tree", "--xff", "-content", "hit"}).errors,
      Eq(0));
  const auto serial = output;
  EXPECT_THAT(
      Run({"--jobs=4", "--summary={lines}", "--format=jsonl", "needle", "tree", "--xff", "-content", "hit"}).errors,
      Eq(0));
  EXPECT_THAT(output, EqualsText(serial));
  fs.read_error_path = "tree/0";
  // Rg encounters the failure itself when no native content predicate reads first.
  const auto serial_failure = Run({"--jobs=1", "--summary={lines}", "--format=jsonl", "needle", "tree"});
  const auto failed_output = output;
  const auto serial_errors = errors;
  EXPECT_THAT(serial_failure.errors, Gt(0));
  const auto parallel_failure = Run({"--jobs=4", "--summary={lines}", "--format=jsonl", "needle", "tree"});
  EXPECT_THAT(parallel_failure.errors, Eq(serial_failure.errors));
  EXPECT_THAT(output, EqualsText(failed_output));
  EXPECT_THAT(errors, Eq(serial_errors));
}

TEST_F(RgEngineTest, DerivedPoolSlotsClearAcrossUnmatchedEmptyAndInlineBatches) {
  fs.files["tree/a"] = "hit\nsecond\nthird\n";
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"tree", "-content", "hit"}));
  ParallelMatch matcher(*command.expression, 4, false, std::nullopt, {}, {.lines = true});
  EXPECT_THAT(matcher.Match(RepeatedEntries(128)), SizeIs(128));
  const auto first = matcher.TakeDerived(0);
  EXPECT_THAT(first.Loaded(), IsFalse());
  EXPECT_THAT(first.Lines(fs, "tree/a"), Optional(3));
  EXPECT_THAT(fs.reads.load(), Eq(128));
  fs.files["tree/a"] = "miss\n";
  const auto& unmatched = matcher.Match(RepeatedEntries(128));
  EXPECT_THAT(unmatched.front().evaluation.matched, IsFalse());
  const auto second = matcher.TakeDerived(0);
  EXPECT_THAT(second.Lines(fs, "tree/a"), Optional(1));
  EXPECT_THAT(fs.reads.load(), Eq(257));
  EXPECT_THAT(matcher.Match({}), IsEmpty());
  fs.files["tree/a"] = "hit\nlast\n";
  EXPECT_THAT(matcher.Match(RepeatedEntries(1)), SizeIs(1));
  const auto last = matcher.TakeDerived(0);
  EXPECT_THAT(last.Loaded(), IsFalse());
  EXPECT_THAT(last.Lines(fs, "tree/a"), Optional(2));
  EXPECT_THAT(fs.reads.load(), Eq(258));
}

TEST_F(RgEngineTest, InputByteAdmissionPreservesLongPathsAcrossPartialBatches) {
  fs.files.clear();
  const std::string tail(5'000, 'x');
  for (std::size_t index = 0; index < 300; ++index) {
    fs.files.emplace("tree/" + std::to_string(index) + tail, "hit\n");
  }
  EXPECT_THAT(Run({"--jobs=1", "--archive=none", "tree", "-content", "hit"}, false).errors, Eq(0));
  const auto expected = output;
  EXPECT_THAT(Run({"--jobs=4", "--archive=none", "tree", "-content", "hit"}, false).errors, Eq(0));
  EXPECT_THAT(output, EqualsText(expected));
  fs.files.clear();
  fs.files.emplace("tree/" + std::string(1'100'000, 'x'), "hit\n");
  EXPECT_THAT(Run({"--jobs=4", "--archive=none", "tree", "-content", "hit"}, false).errors, Eq(0));
  EXPECT_THAT(output, EqualsText(fs.files.begin()->first + "\n"));
}

TEST_F(RgEngineTest, ObservedOutputSelectsOverlapAndReturnsToBatchPublication) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"--rg", "hit", "tree"}));
  MBO_ASSERT_OK_AND_ASSIGN(const auto prepared, PrepareRgOutput(command, fs, false, false));
  const auto search = command.rg.value_or(parser::RgSearch{});
  ParallelMatch matcher(
      {}, 4, false,
      ParallelContentOutput{
          .output = prepared,
          .rg = search,
          .grep =
              {
                  .line_number = false,
                  .filename = false,
              },
      });
  EXPECT_THAT(matcher.CollectNextBatch(), IsFalse());
  // Cross inline/worker and small/heavy boundaries on reused transactions.
  constexpr auto kBatches = std::to_array<std::pair<std::size_t, std::size_t>>({
      {128, 4},
      {10, 2'048},
      {64, 4},
      {128, 8'192},
      {7, 4},
  });
  for (const auto& [count, size] : kBatches) {
    fs.files.at("tree/a") = std::string(size - 4, 'x') + "hit\n";
    matcher.Start(RepeatedEntries(count), true);
    for (std::size_t index = 0; index < count; ++index) {
      const auto& result = matcher.AwaitResult(index);
      EXPECT_THAT(result.content, Optional(Property(&ContentResult::Text, EqualsText(fs.files.at("tree/a")))));
      matcher.ReleaseResult(index);
    }
    matcher.Wait();
    EXPECT_THAT(matcher.CollectNextBatch(), Eq(size >= 1'024));
    EXPECT_THAT(matcher.OutputUsage().excess, Eq(0));
  }
}

TEST_F(RgEngineTest, MixedOutputBatchesPreserveNativeAndRgRecordsAcrossSchedulingChanges) {
  fs.files.clear();
  for (std::size_t index = 0; index < 768; ++index) {
    const auto prefix = std::to_string(1'000 + index);
    fs.files.emplace("tree/" + prefix, index >= 256 && index < 512 ? std::string(8'192, 'x') + "hit\n" : "hit\n");
  }
  EXPECT_THAT(Run({"--jobs=1", "--archive=none", "-M", "tree", "-content", "hit"}, false).errors, Eq(0));
  const auto native = output;
  EXPECT_THAT(Run({"--jobs=4", "--archive=none", "-M", "tree", "-content", "hit"}, false).errors, Eq(0));
  EXPECT_THAT(output, EqualsText(native));
  EXPECT_THAT(Run({"--rg", "--jobs=1", "--archive=none", "hit", "tree"}, false).errors, Eq(0));
  const auto rg = output;
  EXPECT_THAT(Run({"--rg", "--jobs=4", "--archive=none", "hit", "tree"}, false).errors, Eq(0));
  EXPECT_THAT(output, EqualsText(rg));
}

TEST_F(RgEngineTest, AdaptivePipelineDrainsBeforeTraversalErrors) {
  fs.files.clear();
  for (std::size_t index = 0; index < 768; ++index) {
    fs.files.emplace("tree/" + std::to_string(1'000 + index), std::string(8'192, 'x') + "hit\n");
  }
  EXPECT_THAT(Run({"--jobs=1", "--archive=none", "hit", "tree", "missing"}).errors, Eq(1));
  const auto expected = output;
  bool emitted_after_error = false;
  after_emit = [&] { emitted_after_error |= !errors.empty(); };
  EXPECT_THAT(Run({"--jobs=4", "--archive=none", "hit", "tree", "missing"}).errors, Eq(1));
  EXPECT_THAT(output, EqualsText(expected));
  EXPECT_THAT(errors, SizeIs(1));
  EXPECT_THAT(emitted_after_error, IsFalse());
}

TEST_F(RgEngineTest, AdaptivePipelinePreservesRecordsAcrossSerialArchiveMemberBoundaries) {
  fs.files.clear();
  for (std::size_t index = 0; index < 768; ++index) {
    fs.files.emplace("tree/" + std::to_string(1'000 + index), std::string(8'192, 'x') + "hit\n");
  }
  // This source kind cannot enter the matcher pool. Force that real dispatch
  // boundary after the pipeline has observed enough output to overlap batches.
  fs.serial_entry = "tree/1512-serial";
  fs.files.emplace(fs.serial_entry, "serial hit\n");
  EXPECT_THAT(Run({"--jobs=1", "--archive=none", "hit", "tree"}).errors, Eq(0));
  const auto expected = output;
  EXPECT_THAT(Run({"--jobs=4", "--archive=none", "hit", "tree"}).errors, Eq(0));
  EXPECT_THAT(output, EqualsText(expected));
}

TEST_F(RgEngineTest, CreditedInlineEntriesReleaseEachTransactionBeforeReadingTheNext) {
  fs.files.at("tree/a") = std::string(2'097'152, 'x') + "hit\n";
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"--rg", "hit", "tree"}));
  MBO_ASSERT_OK_AND_ASSIGN(const auto prepared, PrepareRgOutput(command, fs, false, false));
  const auto search = command.rg.value_or(parser::RgSearch{});
  ParallelMatch matcher(
      {}, 4, false,
      ParallelContentOutput{
          .output = prepared,
          .rg = search,
          .grep =
              {
                  .line_number = false,
                  .filename = false,
              },
      });
  fs.track_threads = true;
  fs.expected_threads = 1;
  matcher.Start(RepeatedEntries(10), true);
  EXPECT_THAT(fs.reads.load(), Eq(0));
  for (std::size_t index = 0; index < 10; ++index) {
    const auto& result = matcher.AwaitResult(index);
    EXPECT_THAT(result.content, Optional(Property(&ContentResult::Text, EqualsText(fs.files.at("tree/a")))));
    EXPECT_THAT(fs.reads.load(), Eq(index + 1));
    matcher.ReleaseResult(index);
    EXPECT_THAT(matcher.OutputUsage().excess, Eq(0));
  }
  matcher.Wait();
  EXPECT_THAT(fs.read_threads, ElementsAre(std::this_thread::get_id()));
}

TEST_F(RgEngineTest, CreditedTransactionsProgressPastDelayedHeadAndFullLaterOutput) {
  fs.files.clear();
  const std::string ordinary = std::string(65'536, 'x') + "hit\n";
  const std::string giant = std::string(2'097'152, 'x') + "hit\n";
  std::vector<OwnedVisit> entries;
  static constexpr std::size_t kEntries = 256;
  entries.reserve(kEntries);
  for (std::size_t index = 0; index < kEntries; ++index) {
    const auto path = "tree/" + std::to_string(index);
    fs.files.emplace(path, index == 0 ? giant : ordinary);
    entries.emplace_back(Visit{.path = path, .metadata = {.type = vfs::FileType::kRegular}, .fs = fs});
  }
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"--rg", "hit", "tree"}));
  MBO_ASSERT_OK_AND_ASSIGN(const auto prepared, PrepareRgOutput(command, fs, false, false));
  const auto search = command.rg.value_or(parser::RgSearch{});
  ParallelMatch matcher(
      {}, 4, false,
      ParallelContentOutput{
          .output = prepared,
          .rg = search,
          .grep =
              {
                  .line_number = false,
                  .filename = false,
              },
      });
  std::mutex mutex;
  std::condition_variable condition;
  bool released = false;
  bool timed_out = false;
  fs.before_read = [&](std::string_view path) {
    if (path == "tree/0") {
      std::unique_lock lock(mutex);
      timed_out = !condition.wait_for(lock, std::chrono::seconds(5), [&] { return released; });
    }
  };
  matcher.Start(std::move(entries), true);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while ((matcher.OutputUsage().waiters == 0 || matcher.OutputUsage().excess < OutputCredit::kSharedBytes - 131'072)
         && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }
  EXPECT_THAT(matcher.OutputUsage().waiters, Gt(0));
  EXPECT_THAT(matcher.OutputUsage().excess, Le(OutputCredit::kSharedBytes));
  {
    const std::scoped_lock lock(mutex);
    released = true;
    condition.notify_all();
  }
  for (std::size_t index = 0; index < kEntries; ++index) {
    const auto& result = matcher.AwaitResult(index);
    EXPECT_THAT(
        result.content, Optional(AllOf(
                            Field("selected", &ContentResult::selected, IsOkAndHolds(IsTrue())),
                            Property(&ContentResult::Text, EqualsText(index == 0 ? giant : ordinary)))));
    matcher.ReleaseResult(index);
  }
  matcher.Wait();
  EXPECT_THAT(timed_out, IsFalse());
  EXPECT_THAT(matcher.OutputUsage().excess, Eq(0));
  EXPECT_THAT(matcher.OutputUsage().peak_excess, Gt(OutputCredit::kSharedBytes));
}

TEST_F(RgEngineTest, CancellationDrainsCreditWaitersAndUnstartedResultsBeforePoolReuse) {
  fs.files.at("tree/a") = std::string(262'144, 'x') + "hit\n";
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"--rg", "hit", "tree"}));
  MBO_ASSERT_OK_AND_ASSIGN(const auto prepared, PrepareRgOutput(command, fs, false, false));
  const auto search = command.rg.value_or(parser::RgSearch{});
  ParallelMatch matcher(
      {}, 4, false,
      ParallelContentOutput{
          .output = prepared,
          .rg = search,
          .grep =
              {
                  .line_number = false,
                  .filename = false,
              },
      });
  matcher.Start(RepeatedEntries(128), true);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (matcher.OutputUsage().waiters == 0 && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
  }
  EXPECT_THAT(matcher.OutputUsage().waiters, Gt(0));
  matcher.Cancel();
  EXPECT_THAT(matcher.AwaitResult(127).evaluation.matched, IsFalse());
  EXPECT_THAT(matcher.OutputUsage().waiters, Eq(0));
  fs.files.at("tree/a") = "hit\n";
  matcher.Start(RepeatedEntries(64), true);
  for (std::size_t index = 0; index < 64; ++index) {
    const auto& result = matcher.AwaitResult(index);
    EXPECT_THAT(result.content, Optional(Property(&ContentResult::Text, EqualsText("hit\n"))));
    matcher.ReleaseResult(index);
  }
  matcher.Wait();
  EXPECT_THAT(matcher.OutputUsage().excess, Eq(0));
}

TEST_F(RgEngineTest, AsyncDestructionDrainsWorkersBeforeReleasingFilesystemOwners) {
  auto owner = std::make_shared<SearchFs>();
  const std::weak_ptr<const vfs::FileSystem> weak = owner;
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"tree", "-content", "hit"}));
  {
    ParallelMatch matcher(*command.expression, 4, false);
    std::vector<OwnedVisit> entries;
    entries.reserve(128);
    for (std::size_t index = 0; index < 128; ++index) {
      entries.emplace_back(
          Visit{
              .path = "tree/a",
              .metadata = {.type = vfs::FileType::kRegular},
              .fs = *owner,
              .fs_owner = owner,
          });
    }
    matcher.Start(std::move(entries));
    owner.reset();
    EXPECT_THAT(weak.expired(), IsFalse());
  }
  EXPECT_THAT(weak.expired(), IsTrue());
}

TEST_F(RgEngineTest, AsyncStartReturnsWhileAnOwnedReadIsStillBlocked) {
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"tree", "-content", "hit"}));
  ParallelMatch matcher(*command.expression, 4, false);
  std::mutex mutex;
  std::condition_variable condition;
  bool started = false;
  bool released = false;
  bool timed_out = false;
  fs.before_read = [&](std::string_view) {
    std::unique_lock lock(mutex);
    started = true;
    condition.notify_all();
    timed_out |= !condition.wait_for(lock, std::chrono::seconds(2), [&] { return released; });
  };
  matcher.Start(RepeatedEntries(128));
  {
    std::unique_lock lock(mutex);
    EXPECT_THAT(condition.wait_for(lock, std::chrono::seconds(2), [&] { return started; }), IsTrue());
    released = true;
    condition.notify_all();
  }
  const auto& results = matcher.Wait();
  EXPECT_THAT(timed_out, IsFalse());
  EXPECT_THAT(results, SizeIs(128));
  for (const auto& result : results) {
    EXPECT_THAT(result.evaluation.matched, IsTrue());
  }
  EXPECT_THAT(fs.reads.load(), Eq(128));
}

TEST_F(RgEngineTest, MatcherPoolGrowsForLaterBatchesAndRunsShortTailsInline) {
  fs.files.clear();
  for (std::size_t index = 0; index < 256; ++index) {
    fs.files.emplace("tree/" + std::to_string(index), "hit\n");
  }
  const auto entries = [&](std::size_t count) {
    std::vector<OwnedVisit> result;
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      result.emplace_back(
          Visit{
              .path = "tree/" + std::to_string(index),
              .metadata = {.type = vfs::FileType::kRegular},
              .fs = fs,
          });
    }
    return result;
  };
  MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({"tree", "-content", "hit"}));
  ParallelMatch matcher(*command.expression, 12, false);
  fs.track_threads = true;
  // Keep twelve useful workers for sixty-four entries; only tails with fewer chunks
  // restrict participation. Inactive threads must rejoin later generations and stop cleanly.
  static constexpr auto kBatches = std::to_array<std::pair<std::size_t, std::size_t>>({
      {64, 4},
      {256, 12},
      {64, 12},
      {16, 4},
      {32, 8},
      {0, 0},
      {256, 12},
      {15, 1},
      {64, 12},
      {16, 4},
      {8, 1},
  });
  for (const auto& [count, workers] : kBatches) {
    fs.expected_threads = workers;
    fs.read_threads.clear();
    const auto& results = matcher.Match(entries(count));
    EXPECT_THAT(results.size(), Eq(count));
    EXPECT_THAT(fs.read_threads.size(), Eq(workers));
    for (const auto& result : results) {
      EXPECT_THAT(result.evaluation.matched, IsTrue());
    }
  }
  EXPECT_THAT(fs.read_threads, ElementsAre(std::this_thread::get_id()));
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
  EXPECT_THAT(fs.read_threads, ElementsAre(std::this_thread::get_id()));
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
  fs.files.clear();
  for (std::size_t index = 0; index < 300; ++index) {
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
    for (const bool rg : {false, true}) {
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
      fs.read_threads.clear();
      fs.track_threads = true;
      args.front() = "--jobs=4";
      EXPECT_THAT(Run(args, rg).errors, Eq(0));
      fs.track_threads = false;
      EXPECT_THAT(output, EqualsText(expected));
      EXPECT_THAT(fs.reads.load(), Eq(300));
      EXPECT_THAT(fs.read_threads.size(), Gt(1));
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
