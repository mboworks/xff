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

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"
#include "xff/fuse/fuse_api.h"
#include "xff/fuse/fuse_loader.h"

// GNU ld binds only this executable's dlopen references to the unavailable-library adapter.
#if defined(__linux__)
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
extern "C" void* __wrap_dlopen(const char*, int) {  // XFF_ABI_POINTER: dlopen's C ABI for GNU ld --wrap.
  return nullptr;
}
#endif

namespace xff::fuse {
namespace {

#if defined(__linux__)
using ::mbo::testing::EqualsText;
using ::mbo::testing::StatusIs;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::Not;
#endif

struct FuseApiUnavailableTest : ::testing::Test {};

TEST_F(FuseApiUnavailableTest, MissingLibraryReturnsTheLoadersDiagnostic) {
#if defined(__linux__)
  EXPECT_THAT(FuseAvailable(), IsFalse());
  const FuseLoader& loader = FuseLoader::Instance();
  EXPECT_THAT(loader.Library(), IsEmpty());
  EXPECT_THAT(loader.Error(), Not(IsEmpty()));
  EXPECT_THAT(FuseApi::Resolve(), StatusIs(absl::StatusCode::kUnavailable, EqualsText(loader.Error())));
#else
  // NOLINTNEXTLINE(readability-implicit-bool-conversion): originates in GTEST_SKIP.
  GTEST_SKIP() << "The deterministic unavailable-library adapter requires GNU linker wrapping";
#endif
}

}  // namespace
}  // namespace xff::fuse
