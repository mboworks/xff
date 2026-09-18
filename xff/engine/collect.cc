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

#include "xff/engine/collect.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "xff/engine/walk.h"
#include "xff/parser/ast.h"

namespace xff::engine {
namespace {

// Walks the expression tree appending one site per `-collect` node, in AST order.
void AppendCollectSites(const parser::Expr& expr, std::vector<CollectSite>& sites) {
  if (expr.kind == parser::Expr::Kind::kPredicate) {
    if (expr.descriptor.has_value() && expr.descriptor->control == registry::Control::kCollect) {
      sites.push_back(
          CollectSite{
              .name = expr.args.empty() || expr.args.front().empty() ? kDefaultCollection : expr.args.front(),
              .override_name = expr.label_override,
          });
    }
    return;
  }
  if (expr.lhs != nullptr) {
    AppendCollectSites(*expr.lhs, sites);
  }
  if (expr.rhs != nullptr) {
    AppendCollectSites(*expr.rhs, sites);
  }
}

}  // namespace

Visit CollectedEntry::AsVisit() const {
  return Visit{
      .path = path,
      .name = name,
      .root = root,
      .depth = depth,
      .metadata = metadata,
      .fs = fs,
      .fs_owner = fs_owner,
      .root_index = root_index,
  };
}

bool Collections::Add(std::string_view name, const Visit& visit) {
  const std::size_t entry_bytes = visit.path.size() + visit.name.size() + visit.root.size();
  if ((budget_.rows != 0 && rows_ >= budget_.rows) || (budget_.bytes != 0 && bytes_ + entry_bytes > budget_.bytes)) {
    overflowed_ = true;
    return false;
  }
  rows_ += 1;
  bytes_ += entry_bytes;
  by_name_[std::string(name)].push_back(
      CollectedEntry{
          .path = std::string(visit.path),
          .name = std::string(visit.name),
          .root = std::string(visit.root),
          .depth = visit.depth,
          .metadata = visit.metadata,
          .fs = visit.fs,
          .fs_owner = visit.fs_owner,
          .root_index = visit.root_index,
      });
  return true;
}

const std::vector<CollectedEntry>& Collections::Entries(std::string_view name) const {
  static const std::vector<CollectedEntry> kEmpty;
  const auto it = by_name_.find(name);
  return it == by_name_.end() ? kEmpty : it->second;
}

std::vector<std::string_view> Collections::Names() const {
  std::vector<std::string_view> names;
  names.reserve(by_name_.size());
  for (const auto& [name, entries] : by_name_) {
    names.push_back(name);
  }
  return names;
}

std::size_t Collections::Size() const {
  std::size_t total = 0;
  for (const auto& [name, entries] : by_name_) {
    total += entries.size();
  }
  return total;
}

std::vector<CollectSite> CollectSites(const parser::Expr& expr) {
  std::vector<CollectSite> sites;
  AppendCollectSites(expr, sites);
  return sites;
}

}  // namespace xff::engine
