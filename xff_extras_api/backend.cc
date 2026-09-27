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

#include "xff/matching/regex/backend.h"

#include <memory>
#include <string_view>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace xff::regex {
namespace {

// The process-wide PCRE2 backend factory, empty when no PCRE2 backend is linked. Set once at
// static-init by the real backend's Pcre2Registrar (full build only); a Meyers static so the
// registrar in another translation unit can safely write it during static initialization.
Pcre2Factory& Pcre2FactorySlot() {
  static Pcre2Factory slot;
  return slot;
}

}  // namespace

void RegisterPcre2Backend(Pcre2Factory factory) {
  Pcre2FactorySlot() = std::move(factory);
}

bool Pcre2Available() {
  return static_cast<bool>(Pcre2FactorySlot());
}

absl::StatusOr<std::unique_ptr<const RegexBackend>> MakePcre2Backend(
    std::string_view pattern,
    bool case_insensitive,
    TextMode mode) {
  // No factory registered means the real backend was not linked (lean build): a distinct
  // Unimplemented state from an InvalidArgument bad pattern, and never a silent fallback to RE2.
  const Pcre2Factory& factory = Pcre2FactorySlot();
  if (!factory) {
    return absl::UnimplementedError("the PCRE2 regex grammar (--regextype=PCRE2) is not built into this binary");
  }
  return factory(pattern, case_insensitive, mode);
}

}  // namespace xff::regex
