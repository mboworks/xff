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

#ifndef XFF_REGISTRY_COMPATIBILITY_H_
#define XFF_REGISTRY_COMPATIBILITY_H_

#include <span>
#include <string_view>

#include "mbo/types/optional_ref.h"
#include "xff/registry/mode.h"

namespace xff::registry {
// Shared grammar and help metadata. A nonempty argument names the required value.
struct CompatibilityOption {
  enum class Effect {
    kGlobal,
    kPattern,
    kFile,
    kWord,
    kLine,
    kText,
    kColumns,
    kGlob,
    kRoot,
    kThreads,
    kTypeList,
    kMultiline,
    kNoMultiline,
    kDotall,
    kNoDotall,
    kHeading,
    kNoHeading,
    kColumn,
    kNoColumn,
    kPretty,
    kUnicode,
    kNoUnicode
  };
  std::string_view name;
  std::string_view alias;
  std::string_view replacement;
  std::string_view argument;
  Effect effect = Effect::kGlobal;
  std::string_view summary;
  Modes modes = Modes::kRg;
};

template<typename Sink>
void AbslStringify(Sink& sink, const CompatibilityOption& option) {
  sink.Append(option.name);
}

std::span<const CompatibilityOption> CompatibilityOptions();
mbo::types::OptionalRef<const CompatibilityOption> LookupCompatibilityOption(std::string_view name, Mode mode);

}  // namespace xff::registry
#endif  // XFF_REGISTRY_COMPATIBILITY_H_
