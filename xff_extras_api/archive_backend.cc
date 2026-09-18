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

#include "xff/archive/archive_backend.h"

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/types/span.h"
#include "mbo/status/status_macros.h"
#include "xff/archive/member_path.h"
#include "xff/vfs/filesystem.h"

namespace xff::archive {
namespace {

// A temporary override used by focused tests. Production extras compose in ContainerReadersSlot.
ContainerOpener& ContainerOpenerSlot() {
  static ContainerOpener slot;
  return slot;
}

struct ContainerReader {
  std::string name;
  ContainerOpener opener;
  std::vector<ReadFormatInfo> formats;
};

std::vector<ContainerReader>& ContainerReadersSlot() {
  static std::vector<ContainerReader> slot;
  return slot;
}

// The override opener's formats. Kept separately so tests can exercise the name gate without
// installing a reader; production readers carry formats atomically in ContainerReader.
std::vector<ReadFormatInfo>& ReadFormatsSlot() {
  static std::vector<ReadFormatInfo> slot;
  return slot;
}

absl::flat_hash_set<std::string>& GateSuffixesSlot() {
  static absl::flat_hash_set<std::string> slot;
  return slot;
}

std::vector<ReadFormatInfo> AllReadFormats() {
  std::vector<ReadFormatInfo> formats = ReadFormatsSlot();
  for (const ContainerReader& reader : ContainerReadersSlot()) {
    formats.insert(formats.end(), reader.formats.begin(), reader.formats.end());
  }
  return formats;
}

void RebuildGate() {
  absl::flat_hash_set<std::string>& gate = GateSuffixesSlot();
  gate.clear();
  for (const ReadFormatInfo& format : AllReadFormats()) {
    for (const std::string& suffix : format.suffixes) {
      const std::string::size_type dot = suffix.rfind('.');
      gate.insert(absl::AsciiStrToLower(dot == 0 ? suffix : suffix.substr(dot)));
    }
  }
}

// The process-wide member remover, empty when no backend registered one - which is the answer for a
// build without archive support AND for a backend that can only read.
ContainerMemberRemover& ContainerMemberRemoverSlot() {
  static ContainerMemberRemover slot;
  return slot;
}

// The process-wide packer and the formats it accepts, both empty when no backend registered one.
ContainerPacker& ContainerPackerSlot() {
  static ContainerPacker slot;
  return slot;
}

std::vector<std::string>& ContainerPackFormatsSlot() {
  static std::vector<std::string> slot;
  return slot;
}

std::vector<PackOptionInfo>& ContainerPackVocabularySlot() {
  static std::vector<PackOptionInfo> slot;
  return slot;
}

}  // namespace

void RegisterContainerOpener(ContainerOpener opener) {
  ContainerOpenerSlot() = std::move(opener);
}

void RegisterContainerReader(std::string name, ContainerOpener opener, std::vector<ReadFormatInfo> formats) {
  std::vector<ContainerReader>& readers = ContainerReadersSlot();
  const auto found = std::ranges::find(readers, name, &ContainerReader::name);
  if (!opener) {
    if (found != readers.end()) {
      readers.erase(found);
    }
  } else if (found == readers.end()) {
    readers.push_back({.name = std::move(name), .opener = std::move(opener), .formats = std::move(formats)});
  } else {
    found->opener = std::move(opener);
    found->formats = std::move(formats);
  }
  std::ranges::sort(readers, {}, &ContainerReader::name);
  RebuildGate();
}

void RegisterContainerMemberRemover(ContainerMemberRemover remover) {
  ContainerMemberRemoverSlot() = std::move(remover);
}

bool ContainerRemovalAvailable() {
  return static_cast<bool>(ContainerMemberRemoverSlot());
}

absl::Status RemoveContainerMembers(
    std::string_view container,
    const std::vector<std::string>& members,
    const vfs::MutationPolicy& policy) {
  MBO_RETURN_IF_ERROR(policy.Delete());
  MBO_RETURN_IF_ERROR(policy.Write(true));
  if (!ContainerRemovalAvailable()) {
    return absl::UnimplementedError("this binary was built without archive support");
  }
  return ContainerMemberRemoverSlot()(container, members, policy);
}

void RegisterContainerPacker(
    ContainerPacker packer,
    std::vector<std::string> formats,
    std::vector<PackOptionInfo> vocabulary) {
  ContainerPackerSlot() = std::move(packer);
  ContainerPackFormatsSlot() = std::move(formats);
  ContainerPackVocabularySlot() = std::move(vocabulary);
}

bool ContainerPackingAvailable() {
  return static_cast<bool>(ContainerPackerSlot());
}

std::vector<std::string> ContainerPackFormats() {
  return ContainerPackFormatsSlot();
}

std::vector<PackOptionInfo> ContainerPackVocabulary() {
  return ContainerPackVocabularySlot();
}

std::string ContainerPackFormatFor(std::string_view path) {
  const std::string lower = absl::AsciiStrToLower(path);
  std::string best;
  for (const std::string& format : ContainerPackFormatsSlot()) {
    if (format.size() > best.size() && std::string_view(lower).ends_with(absl::StrCat(".", format))) {
      best = format;
    }
  }
  return best;
}

namespace {

absl::StatusOr<std::string> PackDestination(const PackFile& file) {
  if (file.name.empty() || file.name.starts_with('/') || file.name.contains('\0')) {
    return absl::InvalidArgumentError(absl::StrCat("invalid archive member name from '", file.source, "'"));
  }
  std::string name;
  for (const std::string_view component : absl::StrSplit(file.name, '/')) {
    if (component == "..") {
      return absl::InvalidArgumentError(
          absl::StrCat("archive member '", file.name, "' from '", file.source, "' contains parent traversal"));
    }
    if (component.empty() || component == ".") {
      continue;
    }
    if (!name.empty()) {
      name.push_back('/');
    }
    name.append(component);
  }
  if (name.empty()) {
    name = ".";
  }
  return name;
}

std::optional<std::size_t> ConflictingPackDestination(
    const PackFile& file,
    const std::map<std::string, std::size_t>& destinations,
    const std::vector<PackFile>& planned) {
  if (const auto exact = destinations.find(file.name); exact != destinations.end()) {
    return exact->second;
  }
  if (const auto root = destinations.find("."); root != destinations.end() && !planned.at(root->second).is_directory) {
    return root->second;
  }
  for (auto slash = file.name.find('/'); slash != std::string::npos; slash = file.name.find('/', slash + 1)) {
    const auto parent = destinations.find(file.name.substr(0, slash));
    if (parent != destinations.end() && !planned.at(parent->second).is_directory) {
      return parent->second;
    }
  }
  if (file.is_directory || destinations.empty()) {
    return std::nullopt;
  }
  if (file.name == ".") {
    return destinations.begin()->second;
  }
  const std::string prefix = absl::StrCat(file.name, "/");
  const auto child = destinations.lower_bound(prefix);
  if (child != destinations.end() && child->first.starts_with(prefix)) {
    return child->second;
  }
  return std::nullopt;
}

}  // namespace

absl::StatusOr<std::vector<PackFile>> PlanPackFiles(
    const std::vector<PackFile>& files,
    PackDuplicatePolicy duplicates) {
  std::vector<PackFile> planned;
  planned.reserve(files.size());
  std::map<std::string, std::size_t> destinations;
  for (const auto& file : files) {
    MBO_ASSIGN_OR_RETURN(std::string name, PackDestination(file));
    PackFile normalized{.source = file.source, .name = std::move(name), .is_directory = file.is_directory};
    const auto conflict = ConflictingPackDestination(normalized, destinations, planned);
    if (conflict.has_value()) {
      if (duplicates == PackDuplicatePolicy::kFirst) {
        continue;
      }
      return absl::AlreadyExistsError(
          absl::StrCat(
              "duplicate archive member '", normalized.name, "' conflicts with '", planned.at(*conflict).name,
              "' from '", planned.at(*conflict).source, "' and '", file.source, "'"));
    }
    destinations.emplace(normalized.name, planned.size());
    planned.push_back(std::move(normalized));
  }
  return planned;
}

absl::Status PackContainer(std::string_view path, const std::vector<PackFile>& files, const PackOptions& options) {
  MBO_RETURN_IF_ERROR(options.mutations.Write());
  if (!ContainerPackingAvailable()) {
    return absl::UnimplementedError("this binary was built without archive support");
  }
  MBO_ASSIGN_OR_RETURN(const auto planned, PlanPackFiles(files, options.duplicates));
  return ContainerPackerSlot()(path, planned, options);
}

void RegisterContainerReadFormats(std::vector<ReadFormatInfo> formats) {
  ReadFormatsSlot() = std::move(formats);
  RebuildGate();
}

std::vector<ReadFormatInfo> ContainerReadFormats() {
  return AllReadFormats();
}

bool ContainerSupportAvailable() {
  return static_cast<bool>(ContainerOpenerSlot()) || !ContainerReadersSlot().empty();
}

bool LooksLikeContainerName(std::string_view name) {
  const std::string::size_type dot = name.rfind('.');
  if (dot == std::string_view::npos || dot == 0 || dot + 1 == name.size()) {
    // No suffix at all (a Makefile, a compiled binary), a trailing dot, or a name that IS the suffix:
    // `.gz` is a dotfile, not a gzip called something.
    return false;
  }
  return GateSuffixesSlot().contains(absl::AsciiStrToLower(name.substr(dot)));
}

absl::StatusOr<std::unique_ptr<vfs::FileSystem>> OpenContainer(std::string_view container, MemberPathOptions options) {
  if (!ContainerSupportAvailable()) {
    // Unimplemented, not InvalidArgument: nothing is wrong with the path, this binary simply cannot
    // look inside it. The CLI turns this into the "not built into this binary" message.
    return absl::UnimplementedError("this binary was built without archive support");
  }
  if (ContainerOpenerSlot()) {
    return ContainerOpenerSlot()(container, std::nullopt, options);
  }
  absl::Status not_a_container = absl::InvalidArgumentError("not a readable container");
  for (const ContainerReader& reader : ContainerReadersSlot()) {
    absl::StatusOr<std::unique_ptr<vfs::FileSystem>> opened = reader.opener(container, std::nullopt, options);
    if (opened.ok() || !absl::IsInvalidArgument(opened.status())) {
      return opened;
    }
    not_a_container = opened.status();
  }
  return not_a_container;
}

absl::StatusOr<std::unique_ptr<vfs::FileSystem>> OpenContainerBytes(
    std::string_view container,
    std::string_view bytes,
    MemberPathOptions options) {
  if (!ContainerSupportAvailable()) {
    return absl::UnimplementedError("this binary was built without archive support");
  }
  if (ContainerOpenerSlot()) {
    return ContainerOpenerSlot()(container, bytes, options);
  }
  absl::Status not_a_container = absl::InvalidArgumentError("not a readable container");
  for (const ContainerReader& reader : ContainerReadersSlot()) {
    absl::StatusOr<std::unique_ptr<vfs::FileSystem>> opened = reader.opener(container, bytes, options);
    if (opened.ok() || !absl::IsInvalidArgument(opened.status())) {
      return opened;
    }
    not_a_container = opened.status();
  }
  return not_a_container;
}

}  // namespace xff::archive
