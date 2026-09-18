// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#ifndef XFF_CONFIG_SAFETY_H_
#define XFF_CONFIG_SAFETY_H_

#include <array>
#include <cstddef>
#include <memory>
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
  kArchiveContentOverwrite,
  kDirectoryCreation,
  kDirectoryDeletion,
  kTempFileWriting,
  kTempFileOverwrite,
  kTempFileDeletion,
  kTempDirectoryCreation,
  kTempDirectoryDeletion,
  kOutputFileWriting,
  kOutputFileOverwrite,
  kOutputFileDeletion,
  kOutputDirectoryCreation,
  kOutputDirectoryDeletion,
  kCount
};

struct DetailedPolicy {
  bool archive = false;
  bool temp = false;
  bool output = false;
};

struct SafetyPolicy {
  static constexpr std::size_t kCapabilities = static_cast<std::size_t>(Capability::kCount);
  std::array<bool, kCapabilities> unconditional = {};
  std::array<bool, kCapabilities> profile = [] {
    std::array<bool, kCapabilities> values{};
    values.fill(true);
    return values;
  }();
  bool safe = false;
  bool dry_run = false;
  std::string temp_root;
  std::string output_root;
  std::shared_ptr<const vfs::DirectoryPolicy> directories;

  absl::StatusOr<SafetyPolicy> PrepareDirectories() const;

  bool Blocks(Capability capability) const;
  vfs::MutationPolicy FileMutations() const;
  vfs::MutationPolicy ArchiveMutations() const;
};

// Globals must be in application order, excluding primary arguments. Unconditional blocks
// accumulate; activation and individual profile settings are last-value-wins.
// Config resolution expands each file's policy before combining flags. Raw CLI flags use file scope.
SafetyPolicy ResolveSafety(const std::vector<std::string>& globals, bool expanded = false);
DetailedPolicy ResolveDetailedPolicy(const std::vector<std::string>& globals);
std::vector<std::string> ExpandSafetyFlag(std::string_view flag, DetailedPolicy detailed);
std::string_view CapabilityName(Capability capability);

}  // namespace xff::config
#endif  // XFF_CONFIG_SAFETY_H_
