// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#include "xff/config/safety.h"

#include <cstddef>

namespace xff::config {
namespace {
constexpr std::array<std::string_view, 9> kNames = {
    "file-deletion",
    "execution",
    "file-writing",
    "file-overwrite",
    "archive-content-deletion",
    "archive-writing",
    "archive-overwrite",
    "archive-content-writing",
    "archive-content-overwrite"};
}

bool SafetyPolicy::Blocks(Capability capability) const {
  const auto index = static_cast<std::size_t>(capability);
  return unconditional[index] || (safe && profile[index]);
}

vfs::MutationPolicy SafetyPolicy::FileMutations() const {
  return {
      .block_deletion = Blocks(Capability::kFileDeletion),
      .block_writing = Blocks(Capability::kFileWriting),
      .block_overwrite = Blocks(Capability::kFileOverwrite),
      .dry_run = dry_run};
}

vfs::MutationPolicy SafetyPolicy::ArchiveMutations() const {
  return {
      .block_deletion = Blocks(Capability::kArchiveContentDeletion),
      .block_writing = Blocks(Capability::kArchiveWriting),
      .block_overwrite = Blocks(Capability::kArchiveOverwrite),
      .dry_run = dry_run};
}

std::string_view CapabilityName(Capability capability) {
  return kNames[static_cast<std::size_t>(capability)];
}

std::vector<std::string> ExpandSafetyFlag(std::string_view flag, bool separate_archives) {
  if (flag.starts_with("--detailed-block-policy=")) {
    return {};
  }
  std::vector<std::string> result{std::string(flag)};
  if (separate_archives) {
    return result;
  }
  constexpr auto prefixes = std::to_array<std::string_view>({"--block-", "--safe-block-", "--no-safe-block-"});
  for (const std::string_view prefix : prefixes) {
    if (!flag.starts_with(prefix)) {
      continue;
    }
    const auto capability = flag.substr(prefix.size());
    const auto add = [&](std::string_view name) { result.push_back(std::string(prefix) + std::string(name)); };
    if (capability == "file-writing") {
      add("archive-writing");
      add("archive-content-writing");
    } else if (capability == "file-overwrite") {
      add("archive-overwrite");
      add("archive-content-overwrite");
    } else if (capability == "file-deletion") {
      add("archive-content-deletion");
    }
  }
  return result;
}

SafetyPolicy ResolveSafety(const std::vector<std::string>& globals, bool expanded) {
  SafetyPolicy result;
  for (const auto& original : globals) {
    for (const std::string_view flag : ExpandSafetyFlag(original, expanded)) {
      if (flag == "--safe" || flag == "--no-safe") {
        result.safe = flag == "--safe";
      } else if (flag == "--dry-run") {
        result.dry_run = true;
      } else {
        for (std::size_t index = 0; index < kNames.size(); ++index) {
          if (flag.starts_with("--block-") && flag.substr(8) == kNames[index]) {
            result.unconditional[index] = true;
          } else if (flag.starts_with("--safe-block-") && flag.substr(13) == kNames[index]) {
            result.profile[index] = true;
          } else if (flag.starts_with("--no-safe-block-") && flag.substr(16) == kNames[index]) {
            result.profile[index] = false;
          }
        }
      }
    }
  }
  return result;
}
}  // namespace xff::config
