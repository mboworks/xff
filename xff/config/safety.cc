// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#include "xff/config/safety.h"

#include <cstddef>
#include <optional>
#include <utility>

#include "absl/algorithm/container.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "mbo/status/status_macros.h"
#include "xff/config/config.h"

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
      .temporary_root = temp_root,
  };
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
                                              : std::nullopt,
  };
}

absl::StatusOr<SafetyPolicy> SafetyPolicy::PrepareDirectories() const {
  SafetyPolicy result = *this;
  std::vector<vfs::DirectoryRule> rules;
  if (!temp_root.empty()) {
    rules.push_back({
        .root = temp_root,
        .blocks =
            {Blocks(Capability::kTempFileWriting), Blocks(Capability::kTempFileOverwrite),
             Blocks(Capability::kTempFileDeletion), Blocks(Capability::kTempDirectoryCreation),
             Blocks(Capability::kTempDirectoryDeletion)},
    });
  }
  if (!output_root.empty()) {
    rules.push_back({
        .root = output_root,
        .blocks =
            {Blocks(Capability::kOutputFileWriting), Blocks(Capability::kOutputFileOverwrite),
             Blocks(Capability::kOutputFileDeletion), Blocks(Capability::kOutputDirectoryCreation),
             Blocks(Capability::kOutputDirectoryDeletion)},
    });
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

namespace {

// The same state transitions serve execution and inspection. Origins are indices into the
// explanation's application stream; execution leaves them unset.
struct SafetyResolution {
  SafetyPolicy policy;
  std::array<std::optional<std::size_t>, SafetyPolicy::kCapabilities> mandatory_source_indices = {};
  std::array<std::optional<std::size_t>, SafetyPolicy::kCapabilities> profile_source_indices = {};
  std::optional<std::size_t> activation_source_index;
  std::optional<std::size_t> dry_run_source_index;
  std::optional<std::size_t> temp_root_source_index;
  std::optional<std::size_t> output_root_source_index;

  void ApplyCapability(std::string_view flag, std::optional<std::size_t> source) {
    for (std::size_t index = 0; index < kNames.size(); ++index) {
      if (flag.starts_with("--block-") && flag.substr(8) == kNames.at(index)) {
        if (!policy.unconditional.at(index)) {
          mandatory_source_indices.at(index) = source;
        }
        policy.unconditional.at(index) = true;
      } else if (flag.starts_with("--safe-block-") && flag.substr(13) == kNames.at(index)) {
        policy.profile.at(index) = true;
        profile_source_indices.at(index) = source;
      } else if (flag.starts_with("--no-safe-block-") && flag.substr(16) == kNames.at(index)) {
        policy.profile.at(index) = false;
        profile_source_indices.at(index) = source;
      }
    }
  }

  void Apply(std::string_view flag, std::optional<std::size_t> source = std::nullopt) {
    if (flag == "--safe" || flag == "--no-safe") {
      policy.safe = flag == "--safe";
      activation_source_index = source;
    } else if (flag.starts_with("--temp-root=") && policy.temp_root.empty()) {
      policy.temp_root = flag.substr(12);
      temp_root_source_index = source;
    } else if (flag.starts_with("--output-root=") && policy.output_root.empty()) {
      policy.output_root = flag.substr(14);
      output_root_source_index = source;
    } else if (flag == "--dry-run") {
      policy.dry_run = true;
      dry_run_source_index = source;
    } else {
      ApplyCapability(flag, source);
    }
  }
};

std::string ExplainOrigin(const std::vector<ResolvedFlag>& application, std::optional<std::size_t> index) {
  if (!index.has_value()) {
    return "default";
  }
  const auto& setting = application.at(*index);
  std::string result(SourceName(setting.source));
  if (!setting.origin.path.empty()) {
    absl::StrAppend(&result, " ", absl::CEscape(setting.origin.path));
  }
  if (setting.origin.line != 0) {
    absl::StrAppend(&result, ":", setting.origin.line);
  }
  if (!setting.origin.section.empty()) {
    absl::StrAppend(&result, " [", absl::CEscape(setting.origin.section), "]");
  }
  absl::StrAppend(&result, " (", absl::CEscape(setting.flag), ")");
  return result;
}

std::string DetailedPolicyNames(DetailedPolicy policy) {
  std::vector<std::string_view> names;
  if (policy.archive) {
    names.emplace_back("archive");
  }
  if (policy.temp) {
    names.emplace_back("temp");
  }
  if (policy.output) {
    names.emplace_back("output");
  }
  return names.empty() ? "none (file controls throughout)" : absl::StrJoin(names, ",");
}

std::string ExplainCapability(
    const SafetyResolution& resolved,
    const std::vector<ResolvedFlag>& application,
    std::size_t index) {
  const auto& policy = resolved.policy;
  std::string_view reason = "inactive profile";
  auto source = resolved.activation_source_index;
  if (policy.unconditional.at(index)) {
    reason = "unconditional block";
    source = resolved.mandatory_source_indices.at(index);
  } else if (policy.safe) {
    reason = policy.profile.at(index) ? "active profile block" : "profile permits";
    source = resolved.profile_source_indices.at(index);
  }
  return absl::StrCat(
      "safety\t", kNames.at(index), "\t", policy.Blocks(static_cast<Capability>(index)) ? "block" : "allow", "\t",
      policy.unconditional.at(index) ? "block" : "none", "\t", policy.profile.at(index) ? "block" : "allow", "\t",
      reason, "\t", ExplainOrigin(application, source), "\t",
      ExplainOrigin(application, resolved.profile_source_indices.at(index)), "\n");
}

}  // namespace

DetailedPolicy ResolveDetailedPolicy(const std::vector<std::string>& globals) {
  DetailedPolicy result;
  for (const auto token : DirectiveTokens(globals)) {
    constexpr std::string_view kPrefix = "--block-policy-categories=";
    if (!token.starts_with(kPrefix)) {
      continue;
    }
    const auto categories = absl::StrSplit(token.substr(kPrefix.size()), ',');
    result.archive = absl::c_contains(categories, "archive");
    result.temp = absl::c_contains(categories, "temp");
    result.output = absl::c_contains(categories, "output");
  }
  return result;
}

SafetyPolicy ResolveSafety(const std::vector<std::string>& globals, bool expanded) {
  SafetyResolution result;
  for (const auto& original : globals) {
    for (const std::string_view flag :
         ExpandSafetyFlag(original, {.archive = expanded, .temp = expanded, .output = expanded})) {
      result.Apply(flag);
    }
  }
  return std::move(result.policy);
}

std::string ExplainSafety(const std::vector<ResolvedFlag>& application, const ConfigInputs& inputs) {
  SafetyResolution resolved;
  for (std::size_t index = 0; index < application.size(); ++index) {
    if (!application.at(index).is_argument) {
      resolved.Apply(application.at(index).flag, index);
    }
  }
  const auto& policy = resolved.policy;
  const auto active_inputs = ApplyConfigSkips(inputs);
  std::string out =
      "\n# effective safety policy (operation blocks; config arming and filesystem permissions also apply)\n";
  absl::StrAppend(
      &out, "safe-mode\t", policy.safe ? "on" : "off", "\t",
      ExplainOrigin(application, resolved.activation_source_index), "\n", "dry-run\t", policy.dry_run ? "on" : "off",
      "\t", ExplainOrigin(application, resolved.dry_run_source_index), "\n",
      "# selected detailed categories per file; other categories inherit that file's ordinary controls\n",
      "policy\tsystem\t", DetailedPolicyNames(ResolveDetailedPolicy(active_inputs.system.globals)), "\n",
      "policy\tuser\t", DetailedPolicyNames(ResolveDetailedPolicy(active_inputs.user.globals)), "\n",
      "policy\tcli/xffrc\tnone (file controls throughout)\n",
      "# row\tcapability\tdecision\tunconditional\tprofile definition\treason\tdecision origin\tprofile origin\n");
  for (std::size_t index = 0; index < SafetyPolicy::kCapabilities; ++index) {
    absl::StrAppend(&out, ExplainCapability(resolved, application, index));
  }
  absl::StrAppend(
      &out, "# directory scopes apply recursively; overlapping scope restrictions combine\n", "root\ttemp\t",
      policy.temp_root.empty() ? "(unset)" : absl::CEscape(policy.temp_root), "\t",
      ExplainOrigin(application, resolved.temp_root_source_index), "\n", "root\toutput\t",
      policy.output_root.empty() ? "(unset)" : absl::CEscape(policy.output_root), "\t",
      ExplainOrigin(application, resolved.output_root_source_index), "\n",
      "# temp/output capabilities apply beneath configured roots; root validity is checked before actions\n",
      "# an operation must satisfy every applicable capability; see --help=safety\n");
  return out;
}
}  // namespace xff::config
