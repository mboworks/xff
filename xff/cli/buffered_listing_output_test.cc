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

#include "xff/cli/buffered_listing_output.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <ostream>
#include <streambuf>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"

namespace xff::cli {
namespace {

using ::mbo::testing::EqualsText;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;

// An in-memory downstream buffer with observable writes and injectable short writes.
// No host handle or filesystem participates in these stream contract tests.
struct RecordingBuffer final : std::streambuf {
  std::string bytes;
  std::streamsize write_limit = std::numeric_limits<std::streamsize>::max();
  int writes = 0;
  int syncs = 0;
  int sync_result = 0;

 protected:
  // XFF_ABI_POINTER: std::streambuf requires this borrowed character-buffer override.
  std::streamsize xsputn(const char* data, std::streamsize count) override {
    ++writes;
    const auto accepted = std::min(count, write_limit);
    bytes.append(data, static_cast<std::size_t>(accepted));
    return accepted;
  }

  int sync() override {
    ++syncs;
    return sync_result;
  }
};

struct BufferedListingOutputTest : ::testing::Test {
  RecordingBuffer sink;
  std::ostream output{&sink};
};

TEST_F(BufferedListingOutputTest, SmallWritesAreHeldUntilExplicitFlush) {
  const auto buffer = std::make_unique<BufferedListingOutput>(output);
  output << "first" << "second";
  EXPECT_THAT(sink.bytes, IsEmpty());
  output.flush();
  EXPECT_THAT(sink.bytes, EqualsText("firstsecond"));
  EXPECT_THAT(sink.writes, Eq(1));
  EXPECT_THAT(sink.syncs, Eq(1));
  EXPECT_THAT(output.good(), IsTrue());
}

TEST_F(BufferedListingOutputTest, EmptyFlushDoesNotWriteButSynchronizesDownstream) {
  const auto buffer = std::make_unique<BufferedListingOutput>(output);
  output.flush();
  EXPECT_THAT(sink.bytes, IsEmpty());
  EXPECT_THAT(sink.writes, Eq(0));
  EXPECT_THAT(sink.syncs, Eq(1));
}

TEST_F(BufferedListingOutputTest, DestructionFlushesPendingBytesAndRestoresOriginalBuffer) {
  {
    const auto buffer = std::make_unique<BufferedListingOutput>(output);
    output << "pending";
    EXPECT_THAT(sink.bytes, IsEmpty());
  }
  EXPECT_THAT(sink.bytes, EqualsText("pending"));
  EXPECT_THAT(output.rdbuf(), Eq(&sink));
  EXPECT_THAT(output.good(), IsTrue());
  output << " restored";
  EXPECT_THAT(sink.bytes, EqualsText("pending restored"));
}

TEST_F(BufferedListingOutputTest, LargeWriteFlushesPendingBytesThenBypassesStorage) {
  const std::string large(BufferedListingOutput::kCapacity + 1, 'x');
  const auto buffer = std::make_unique<BufferedListingOutput>(output);
  output << "prefix" << large;
  EXPECT_THAT(sink.bytes, EqualsText("prefix" + large));
  EXPECT_THAT(sink.writes, Eq(2));
  EXPECT_THAT(output.good(), IsTrue());
}

TEST_F(BufferedListingOutputTest, ExactCapacityWriteAlsoBypassesStorage) {
  const std::string large(BufferedListingOutput::kCapacity, 'x');
  const auto buffer = std::make_unique<BufferedListingOutput>(output);
  output << large;
  EXPECT_THAT(sink.bytes, EqualsText(large));
  EXPECT_THAT(sink.writes, Eq(1));
}

TEST_F(BufferedListingOutputTest, SmallWriteCrossingCapacityFlushesWithoutLosingTail) {
  const std::string prefix(BufferedListingOutput::kCapacity - 3, 'a');
  {
    const auto buffer = std::make_unique<BufferedListingOutput>(output);
    output << prefix << "tail";
    EXPECT_THAT(sink.bytes, EqualsText(prefix));
    EXPECT_THAT(sink.writes, Eq(1));
  }
  EXPECT_THAT(sink.bytes, EqualsText(prefix + "tail"));
  EXPECT_THAT(output.good(), IsTrue());
}

TEST_F(BufferedListingOutputTest, CharacterOverflowFlushesFullStorageAndRetainsNewCharacter) {
  const std::string prefix(BufferedListingOutput::kCapacity - 1, 'a');
  {
    const auto buffer = std::make_unique<BufferedListingOutput>(output);
    output << prefix;
    output.put('b');
    EXPECT_THAT(sink.bytes, IsEmpty());
    output.put('c');
    EXPECT_THAT(sink.bytes, EqualsText(prefix + "b"));
    EXPECT_THAT(output.good(), IsTrue());
  }
  EXPECT_THAT(sink.bytes, EqualsText(prefix + "bc"));
}

TEST_F(BufferedListingOutputTest, NonpositiveWritesDoNotTouchDownstreamOrPendingBytes) {
  const auto buffer = std::make_unique<BufferedListingOutput>(output);
  output << "pending";
  EXPECT_THAT(buffer->sputn("", 0), Eq(0));
  EXPECT_THAT(buffer->sputn("", -1), Eq(0));
  EXPECT_THAT(sink.writes, Eq(0));
  output.flush();
  EXPECT_THAT(sink.bytes, EqualsText("pending"));
}

TEST_F(BufferedListingOutputTest, FailedFlushPreservesBadStateWhenRestoringOriginalBuffer) {
  sink.write_limit = 2;
  {
    const auto buffer = std::make_unique<BufferedListingOutput>(output);
    output << "pending";
    output.flush();
    EXPECT_THAT(output.bad(), IsTrue());
  }
  EXPECT_THAT(output.rdbuf(), Eq(&sink));
  EXPECT_THAT(output.bad(), IsTrue());
  EXPECT_THAT(sink.bytes, EqualsText("pe"));
  EXPECT_THAT(sink.writes, Eq(1));
  EXPECT_THAT(sink.syncs, Eq(0));
}

TEST_F(BufferedListingOutputTest, DestructionDetectsFailedFlush) {
  sink.write_limit = 0;
  {
    const auto buffer = std::make_unique<BufferedListingOutput>(output);
    output << "pending";
  }
  EXPECT_THAT(output.rdbuf(), Eq(&sink));
  EXPECT_THAT(output.bad(), IsTrue());
  EXPECT_THAT(sink.bytes, IsEmpty());
}

TEST_F(BufferedListingOutputTest, FailedFlushPreventsLargeBypassWrite) {
  const std::string large(BufferedListingOutput::kCapacity, 'x');
  sink.write_limit = 0;
  const auto buffer = std::make_unique<BufferedListingOutput>(output);
  output << "pending" << large;
  EXPECT_THAT(output.bad(), IsTrue());
  EXPECT_THAT(sink.bytes, IsEmpty());
  EXPECT_THAT(sink.writes, Eq(1));
}

TEST_F(BufferedListingOutputTest, ShortLargeBypassWriteSetsBadState) {
  const std::string large(BufferedListingOutput::kCapacity, 'x');
  sink.write_limit = 2;
  const auto buffer = std::make_unique<BufferedListingOutput>(output);
  output << large;
  EXPECT_THAT(output.bad(), IsTrue());
  EXPECT_THAT(sink.bytes, EqualsText("xx"));
}

TEST_F(BufferedListingOutputTest, FailedFlushPreventsSmallCrossingWrite) {
  const std::string prefix(BufferedListingOutput::kCapacity - 1, 'a');
  sink.write_limit = 0;
  const auto buffer = std::make_unique<BufferedListingOutput>(output);
  output << prefix << "tail";
  EXPECT_THAT(output.bad(), IsTrue());
  EXPECT_THAT(sink.bytes, IsEmpty());
  EXPECT_THAT(sink.writes, Eq(1));
}

TEST_F(BufferedListingOutputTest, FailedCharacterOverflowReturnsFailureToOstream) {
  const std::string prefix(BufferedListingOutput::kCapacity - 1, 'a');
  sink.write_limit = 0;
  const auto buffer = std::make_unique<BufferedListingOutput>(output);
  output << prefix;
  output.put('b');
  output.put('c');
  EXPECT_THAT(output.bad(), IsTrue());
  EXPECT_THAT(sink.bytes, IsEmpty());
  EXPECT_THAT(sink.writes, Eq(1));
}

TEST_F(BufferedListingOutputTest, DownstreamSynchronizationFailureSetsBadState) {
  sink.sync_result = -1;
  const auto buffer = std::make_unique<BufferedListingOutput>(output);
  output << "pending";
  output.flush();
  EXPECT_THAT(sink.bytes, EqualsText("pending"));
  EXPECT_THAT(sink.syncs, Eq(1));
  EXPECT_THAT(output.bad(), IsTrue());
}

TEST_F(BufferedListingOutputTest, TiedDiagnosticStreamFlushesListingFirst) {
  const auto buffer = std::make_unique<BufferedListingOutput>(output);
  std::ostream diagnostics(&sink);
  diagnostics.tie(&output);
  output << "listing";
  diagnostics << "diagnostic";
  EXPECT_THAT(sink.bytes, EqualsText("listingdiagnostic"));
  EXPECT_THAT(output.bad(), IsFalse());
}

}  // namespace
}  // namespace xff::cli
