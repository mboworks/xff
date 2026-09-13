// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#ifndef XFF_CONFIG_SAFETY_H_
#define XFF_CONFIG_SAFETY_H_

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "xff/vfs/mutations.h"

namespace xff::config {

enum class Capability {
  kFileDeletion,
  kExecution,
  kFileWriting,
  kFileOverwrite,
  kArchiveContentDeletion,
  kArchiveWriting,
  kArchiveOverwrite,
  kArchiveContentWriting,
  kArchiveContentOverwrite
};

struct SafetyPolicy {
  std::array<bool, 9> unconditional = {};
  std::array<bool, 9> profile = {true, true, true, true, true, true, true, true, true};
  bool safe = false;
  bool dry_run = false;

  bool Blocks(Capability capability) const;
  vfs::MutationPolicy FileMutations() const;
  vfs::MutationPolicy ArchiveMutations() const;
};

// Globals must be in application order, excluding primary arguments. Unconditional blocks
// accumulate; activation and individual profile settings are last-value-wins.
// Config resolution expands each file's policy before combining flags. Raw CLI flags use file scope.
SafetyPolicy ResolveSafety(const std::vector<std::string>& globals, bool expanded = false);
std::vector<std::string> ExpandSafetyFlag(std::string_view flag, bool separate_archives);
std::string_view CapabilityName(Capability capability);

}  // namespace xff::config
#endif  // XFF_CONFIG_SAFETY_H_
