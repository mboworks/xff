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
//
// These tests are environment-AGNOSTIC on purpose: whether a machine has libfuse3 is exactly what
// the loader reports, so a test may not assume either answer (Linux CI images tend to have it,
// macOS CI does not, developer machines vary). What is pinned instead are the loader's invariants
// in BOTH states.

#include "xff/fuse/fuse_loader.h"

#include <array>
#include <string_view>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xff::fuse {
namespace {

using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::IsNull;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::SizeIs;

struct FuseLoaderTest : ::testing::Test {};

TEST_F(FuseLoaderTest, ProbeReportsWhenNoCandidateCanBeOpened) {
  constexpr std::array kCandidates = std::to_array<std::string_view>({"missing.so"});
  constexpr std::array kSymbols = std::to_array<std::string_view>({"required"});
  const FuseProbeResult result = ProbeFuseLibraries(
      kCandidates, kSymbols, [](std::string_view) -> void* { return nullptr; },
      [](void*, std::string_view) -> void* { return nullptr; });
  EXPECT_THAT(result.handle, IsNull());
  EXPECT_THAT(result.library, IsEmpty());
  EXPECT_THAT(result.error, Not(IsEmpty()));
}

TEST_F(FuseLoaderTest, ProbeReportsTheMissingSymbolFromAnIncompleteCandidate) {
  constexpr std::array kCandidates = std::to_array<std::string_view>({"incomplete.so"});
  constexpr std::array kSymbols = std::to_array<std::string_view>({"present", "missing"});
  int library_token = 0;
  const FuseProbeResult result = ProbeFuseLibraries(
      kCandidates, kSymbols, [&](std::string_view) -> void* { return &library_token; },
      [&](void*, std::string_view symbol) {
        return symbol == "present" ? static_cast<void*>(&library_token) : nullptr;
      });
  EXPECT_THAT(result.handle, IsNull());
  EXPECT_THAT(result.library, IsEmpty());
  EXPECT_THAT(result.error, Eq("incomplete.so is present but lacks missing; not mountable"));
}

TEST_F(FuseLoaderTest, ProbeFallsBackFromAnIncompleteCandidateToACompleteOne) {
  constexpr std::array kCandidates = std::to_array<std::string_view>({"incomplete.so", "complete.so"});
  constexpr std::array kSymbols = std::to_array<std::string_view>({"first", "second"});
  int incomplete_token = 0;
  int complete_token = 0;
  const FuseProbeResult result = ProbeFuseLibraries(
      kCandidates, kSymbols,
      [&](std::string_view candidate) -> void* {
        return candidate == "complete.so" ? static_cast<void*>(&complete_token) : &incomplete_token;
      },
      [&](void* handle, std::string_view symbol) -> void* {
        return handle == &incomplete_token && symbol == "second" ? nullptr : handle;
      });
  EXPECT_THAT(result.handle, Eq(static_cast<void*>(&complete_token)));
  EXPECT_THAT(result.library, Eq("complete.so"));
  EXPECT_THAT(result.error, IsEmpty());
}

TEST_F(FuseLoaderTest, TheProbeIsStableAcrossCalls) {
  const FuseLoader& first = FuseLoader::Instance();
  const FuseLoader& second = FuseLoader::Instance();
  EXPECT_THAT(&second, Eq(&first));
  EXPECT_THAT(FuseAvailable(), Eq(first.Available()));
}

TEST_F(FuseLoaderTest, AvailableMeansEveryRequiredSymbolResolves) {
  const FuseLoader& loader = FuseLoader::Instance();
  if (!loader.Available()) {
    GTEST_SKIP() << "no FUSE3 on this machine: " << loader.Error();
  }
  EXPECT_THAT(loader.Library(), Not(IsEmpty()));
  EXPECT_THAT(loader.Error(), IsEmpty());
  for (const std::string_view symbol : RequiredSymbols()) {
    EXPECT_THAT(loader.Symbol(symbol), NotNull()) << symbol;
  }
}

TEST_F(FuseLoaderTest, UnavailableCarriesTheReasonAndResolvesNothing) {
  const FuseLoader& loader = FuseLoader::Instance();
  if (loader.Available()) {
    GTEST_SKIP() << "FUSE3 present (" << loader.Library() << "); the unavailable path is not reachable here";
  }
  EXPECT_THAT(loader.Library(), IsEmpty());
  EXPECT_THAT(loader.Error(), Not(IsEmpty()));
  EXPECT_THAT(loader.Symbol("fuse_session_new"), IsNull());
}

TEST_F(FuseLoaderTest, AnUnknownSymbolIsNullEvenWhenAvailable) {
  EXPECT_THAT(FuseLoader::Instance().Symbol("xff_definitely_not_a_fuse_symbol"), IsNull());
}

TEST_F(FuseLoaderTest, TheRequiredSetCoversTheServersCallSurface) {
  // The server needs the session lifecycle, the mount pair, the reply calls, the direntry builder,
  // readlink and the argv deallocator; a shrink of this list would let "available" lie. SizeIs guards accidental
  // deletion; the names themselves are the SOT in fuse_loader.cc.
  EXPECT_THAT(RequiredSymbols(), SizeIs(15));
}

}  // namespace
}  // namespace xff::fuse
