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

#ifndef XFF_PCRE2_BACKEND_INTERNAL_H_
#define XFF_PCRE2_BACKEND_INTERNAL_H_

#include <functional>
#include <memory>
#include <string_view>

#include "absl/status/statusor.h"
#include "xff/matching/regex/backend.h"

namespace xff::regex::internal {

// Module-private seam for verifying actual JIT dispatch. The observer must be
// safe for concurrent calls when the returned backend is shared between threads.
// Production registration leaves it empty, so no match-time callback is installed.
absl::StatusOr<std::unique_ptr<const RegexBackend>> CompilePcre2(
    std::string_view pattern,
    bool case_insensitive,
    std::function<void()> jit_observer = {});

}  // namespace xff::regex::internal

#endif  // XFF_PCRE2_BACKEND_INTERNAL_H_
