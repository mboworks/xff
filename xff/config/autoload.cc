// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <set>
#include <utility>

#include "absl/strings/str_cat.h"
#include "mbo/status/status_macros.h"
#include "xff/config/loader.h"
#include "xff/config/policy.h"

namespace xff::config {
namespace {
class RcDiscovery {
 public:
  RcDiscovery(ConfigInputs inputs, const vfs::FileSystem& filesystem)
      : inputs_(std::move(inputs)), filesystem_(filesystem) {}

  absl::StatusOr<ConfigInputs> Discover(const std::vector<std::string>& roots) {
    MBO_RETURN_IF_ERROR(ValidateConfigSkips(inputs_));
    if (inputs_.rc_mode == RcMode::kOff) {
      return std::move(inputs_);
    }
    std::vector<std::string> pending(roots.rbegin(), roots.rend());
    if (pending.empty()) {
      pending.emplace_back(".");
    }
    while (!pending.empty()) {
      const std::string directory = std::move(pending.back());
      pending.pop_back();
      MBO_ASSIGN_OR_RETURN(const auto children, Visit(directory));
      pending.insert(pending.end(), children.rbegin(), children.rend());
    }
    files_.insert(
        files_.end(), std::make_move_iterator(inputs_.xffrc.begin()), std::make_move_iterator(inputs_.xffrc.end()));
    inputs_.xffrc = std::move(files_);
    return std::move(inputs_);
  }

 private:
  absl::StatusOr<std::vector<std::string>> Visit(const std::string& directory) {
    const auto metadata = filesystem_.Stat(directory, false);
    if (!metadata.ok()) {
      return metadata.status();
    }
    if (metadata->type != vfs::FileType::kDirectory || !NewDirectory(directory, *metadata)) {
      return std::vector<std::string>{};
    }
    MBO_RETURN_IF_ERROR(ReadConfig(directory));
    std::vector<std::string> children;
    if (inputs_.rc_mode == RcMode::kRecursive) {
      MBO_ASSIGN_OR_RETURN(const auto entries, filesystem_.ReadDir(directory));
      for (const auto& entry : entries) {
        if (entry.type == vfs::FileType::kDirectory || entry.type == vfs::FileType::kUnknown) {
          children.push_back(entry.path);
        }
      }
      std::ranges::sort(children);
    }
    return children;
  }

  bool NewDirectory(const std::string& path, const vfs::Metadata& metadata) {
    if (metadata.ino != 0) {
      return identities_.emplace(metadata.dev, metadata.ino).second;
    }
    return paths_.insert(path).second;
  }

  absl::Status ReadConfig(const std::string& directory) {
    const std::string path = absl::StrCat(directory, directory.ends_with('/') ? "" : "/", ".xffrc");
    const auto metadata = filesystem_.Stat(path, false);
    if (!metadata.ok() && metadata.status().code() != absl::StatusCode::kNotFound) {
      return metadata.status();
    }
    inputs_.sources.push_back({.path = path, .layer = Source::kXffrc, .found = metadata.ok()});
    if (!metadata.ok()) {
      return absl::OkStatus();
    }
    if (metadata->type != vfs::FileType::kRegular) {
      return absl::InvalidArgumentError(absl::StrCat("autoloaded config must be a regular file: ", path));
    }
    MBO_ASSIGN_OR_RETURN(const auto text, filesystem_.ReadContent(path));
    auto config = ParseIni(text);
    if (!RcGlobalsAllowed(inputs_) && (!config.globals.empty() || !config.global_lines.empty())) {
      return absl::PermissionDeniedError(absl::StrCat("autoloaded config globals require --allow-rc-globals: ", path));
    }
    files_.push_back({.path = path, .config = std::move(config), .automatic = true});
    return absl::OkStatus();
  }

  ConfigInputs inputs_;
  const vfs::FileSystem& filesystem_;
  std::vector<ExplicitConfig> files_;
  std::set<std::pair<std::uint64_t, std::uint64_t>> identities_;
  std::set<std::string> paths_;
};
}  // namespace

RcMode ResolveRcMode(
    const ConfigInputs& inputs,
    const std::vector<std::string>& cli_globals,
    std::string_view invocation_selector) {
  if (inputs.no_config) {
    return RcMode::kOff;
  }
  auto trusted = inputs;
  trusted.xffrc.clear();
  RcMode mode = RcMode::kOff;
  for (const auto& flag : ResolveConfigInOrder(trusted, cli_globals, invocation_selector)) {
    if (flag.is_argument) {
      continue;
    }
    if (flag.flag == "--rc") {
      mode = RcMode::kRoots;
    } else if (flag.flag == "--rc+") {
      mode = RcMode::kRecursive;
    } else if (flag.flag == "--rc-") {
      mode = RcMode::kOff;
    }
  }
  return mode;
}

std::string_view RcModeName(RcMode mode) {
  switch (mode) {
    case RcMode::kOff: return "off";
    case RcMode::kRoots: return "roots";
    case RcMode::kRecursive: return "recursive";
  }
  return "off";
}

absl::StatusOr<ConfigInputs> DiscoverRc(
    ConfigInputs inputs,
    const std::vector<std::string>& roots,
    const vfs::FileSystem& filesystem) {
  return RcDiscovery(std::move(inputs), filesystem).Discover(roots);
}
}  // namespace xff::config
