// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#include "xff/config/safety.h"

#include <cstddef>
#include <optional>
#include <utility>

#include "mbo/status/status_macros.h"

namespace xff::config {
namespace {
constexpr std::array<std::string_view, SafetyPolicy::kCapabilities> kNames = {
    "file-deletion",
    "execution",
    "file-writing",
    "file-overwrite",
    "archive-content-deletion",
    "archive-writing",
    "archive-overwrite",
    "archive-content-writing",
    "archive-content-overwrite",
    "directory-creation",
    "directory-deletion",
    "temp-file-writing",
    "temp-file-overwrite",
    "temp-file-deletion",
    "temp-directory-creation",
    "temp-directory-deletion",
    "output-file-writing",
    "output-file-overwrite",
    "output-file-deletion",
    "output-directory-creation",
    "output-directory-deletion"};
}  // namespace

bool SafetyPolicy::Blocks(Capability capability) const {
  const auto index = static_cast<std::size_t>(capability);
  return unconditional.at(index) || (safe && profile.at(index));
}

vfs::MutationPolicy SafetyPolicy::FileMutations() const {
  return {
      .block_deletion = Blocks(Capability::kFileDeletion),
      .block_writing = Blocks(Capability::kFileWriting),
      .block_overwrite = Blocks(Capability::kFileOverwrite),
      .dry_run = dry_run,
      .block_directory_creation = Blocks(Capability::kDirectoryCreation),
      .block_directory_deletion = Blocks(Capability::kDirectoryDeletion),
      .directories = directories,
      .temporary_root = temp_root};
}

vfs::MutationPolicy SafetyPolicy::ArchiveMutations() const {
  return {
      .block_deletion = Blocks(Capability::kArchiveContentDeletion),
      .block_writing = Blocks(Capability::kArchiveWriting),
      .block_overwrite = Blocks(Capability::kArchiveOverwrite),
      .dry_run = dry_run,
      .archive = true,
      .directories = directories,
      .temporary_root = temp_root,
      .outside_directory_blocks = directories ? std::make_optional(
                                                    std::array{
                                                        Blocks(Capability::kFileWriting),
                                                        Blocks(Capability::kFileOverwrite), false, false, false})
                                              : std::nullopt};
}

absl::StatusOr<SafetyPolicy> SafetyPolicy::PrepareDirectories() const {
  SafetyPolicy result = *this;
  std::vector<vfs::DirectoryRule> rules;
  if (!temp_root.empty()) {
    rules.push_back(
        {.root = temp_root,
         .blocks = {
             Blocks(Capability::kTempFileWriting), Blocks(Capability::kTempFileOverwrite),
             Blocks(Capability::kTempFileDeletion), Blocks(Capability::kTempDirectoryCreation),
             Blocks(Capability::kTempDirectoryDeletion)}});
  }
  if (!output_root.empty()) {
    rules.push_back(
        {.root = output_root,
         .blocks = {
             Blocks(Capability::kOutputFileWriting), Blocks(Capability::kOutputFileOverwrite),
             Blocks(Capability::kOutputFileDeletion), Blocks(Capability::kOutputDirectoryCreation),
             Blocks(Capability::kOutputDirectoryDeletion)}});
  }
  if (!rules.empty()) {
    MBO_ASSIGN_OR_RETURN(result.directories, vfs::DirectoryPolicy::Create(std::move(rules)));
  }
  return result;
}

std::string_view CapabilityName(Capability capability) {
  return kNames.at(static_cast<std::size_t>(capability));
}

std::vector<std::string> ExpandSafetyFlag(std::string_view flag, DetailedPolicy detailed) {
  if (flag.starts_with("--block-policy-categories=")) {
    return {};
  }
  std::vector<std::string> result{std::string(flag)};
  constexpr auto kPrefixes = std::to_array<std::string_view>({"--block-", "--safe-block-", "--no-safe-block-"});
  for (const std::string_view prefix : kPrefixes) {
    if (!flag.starts_with(prefix)) {
      continue;
    }
    const auto capability = flag.substr(prefix.size());
    const auto add = [&](std::string_view name) { result.push_back(std::string(prefix) + std::string(name)); };
    if (!detailed.temp && (capability.starts_with("file-") || capability.starts_with("directory-"))) {
      add(std::string("temp-") + std::string(capability));
    }
    if (!detailed.output && (capability.starts_with("file-") || capability.starts_with("directory-"))) {
      add(std::string("output-") + std::string(capability));
    }
    if (detailed.archive) {
      continue;
    }
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
    for (const std::string_view flag :
         ExpandSafetyFlag(original, {.archive = expanded, .temp = expanded, .output = expanded})) {
      if (flag == "--safe" || flag == "--no-safe") {
        result.safe = flag == "--safe";
      } else if (flag.starts_with("--temp-root=") && result.temp_root.empty()) {
        result.temp_root = flag.substr(12);
      } else if (flag.starts_with("--output-root=") && result.output_root.empty()) {
        result.output_root = flag.substr(14);
      } else if (flag == "--dry-run") {
        result.dry_run = true;
      } else {
        for (std::size_t index = 0; index < kNames.size(); ++index) {
          if (flag.starts_with("--block-") && flag.substr(8) == kNames.at(index)) {
            result.unconditional.at(index) = true;
          } else if (flag.starts_with("--safe-block-") && flag.substr(13) == kNames.at(index)) {
            result.profile.at(index) = true;
          } else if (flag.starts_with("--no-safe-block-") && flag.substr(16) == kNames.at(index)) {
            result.profile.at(index) = false;
          }
        }
      }
    }
  }
  return result;
}
}  // namespace xff::config
