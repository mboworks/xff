// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "benchmark/benchmark.h"
#include "xff/cli/globals.h"

namespace {

struct TrieNode {
  char value = 0;
  std::size_t child = 0;
  std::size_t sibling = 0;
  bool terminal = false;
};

struct DfaNode {
  std::array<std::uint32_t, 128> next{};
  bool terminal = false;
};

struct HashSlot {
  std::string_view name;
  std::uint64_t hash = 0;
};

std::uint64_t Hash(std::string_view name) {
  std::uint64_t hash = 14'695'981'039'346'656'037ULL;
  for (const char value : name) {
    hash ^= static_cast<unsigned char>(value);
    hash *= 1'099'511'628'211ULL;
  }
  return hash;
}

// Experimental tables are constructed before timing. Each mode has its own
// vocabulary, and every implementation checks exact spelling, including misses.
struct LookupCases {
  explicit LookupCases(xff::registry::Mode selected) : mode(selected) {
    names.reserve(xff::cli::AllGlobals().size() * 2);
    for (const auto& flag : xff::cli::AllGlobals()) {
      if (xff::registry::Supports(flag.modes, mode)) {
        names.push_back(flag.name);
        if (!flag.alias.empty() && xff::registry::Supports(flag.alias_modes, mode)) {
          names.push_back(flag.alias);
        }
      }
    }
    queries.reserve(names.size() * 2);
    slots.resize(std::bit_ceil(names.size() * 2));
    for (const auto name : names) {
      InsertTrie(name);
      InsertDfa(name);
      const auto hash = Hash(name);
      auto slot = hash & (slots.size() - 1);
      while (!slots.at(slot).name.empty()) {
        slot = (slot + 1) & (slots.size() - 1);
      }
      slots.at(slot) = {.name = name, .hash = hash};
      queries.emplace_back(name);
      queries.emplace_back(std::string(name) + "-unknown");
    }
  }

  void InsertTrie(std::string_view name) {
    std::size_t node = 0;
    for (const char value : name) {
      auto child = nodes.at(node).child;
      while (child != 0 && nodes.at(child).value != value) {
        child = nodes.at(child).sibling;
      }
      if (child == 0) {
        child = nodes.size();
        const auto sibling = nodes.at(node).child;
        nodes.push_back({.value = value, .sibling = sibling});
        nodes.at(node).child = child;
      }
      node = child;
    }
    nodes.at(node).terminal = true;
  }

  void InsertDfa(std::string_view name) {
    std::size_t node = 0;
    for (const char value : name) {
      const auto byte = static_cast<unsigned char>(value);
      auto next = dfa.at(node).next.at(byte);
      if (next == 0) {
        next = static_cast<std::uint32_t>(dfa.size());
        dfa.emplace_back();
        dfa.at(node).next.at(byte) = next;
      }
      node = next;
    }
    dfa.at(node).terminal = true;
  }

  bool DfaContains(std::string_view name) const {
    std::size_t node = 0;
    for (const char value : name) {
      const auto byte = static_cast<unsigned char>(value);
      if (byte >= 128) {
        return false;
      }
      node = dfa.at(node).next.at(byte);
      if (node == 0) {
        return false;
      }
    }
    return dfa.at(node).terminal;
  }

  bool TrieContains(std::string_view name) const {
    std::size_t node = 0;
    for (const char value : name) {
      auto child = nodes.at(node).child;
      while (child != 0 && nodes.at(child).value != value) {
        child = nodes.at(child).sibling;
      }
      if (child == 0) {
        return false;
      }
      node = child;
    }
    return nodes.at(node).terminal;
  }

  bool HashContains(std::string_view name) const {
    const auto hash = Hash(name);
    auto index = hash & (slots.size() - 1);
    while (!slots.at(index).name.empty()) {
      const auto& slot = slots.at(index);
      if (slot.hash == hash && slot.name == name) {
        return true;
      }
      index = (index + 1) & (slots.size() - 1);
    }
    return false;
  }

  bool LinearContains(std::string_view name) const {
    return std::ranges::any_of(xff::cli::AllGlobals(), [&](const auto& flag) {
      return xff::registry::Supports(flag.modes, mode)
             && (flag.name == name
                 || (xff::registry::Supports(flag.alias_modes, mode) && !flag.alias.empty() && flag.alias == name));
    });
  }

  xff::registry::Mode mode;
  std::vector<std::string_view> names;
  std::vector<std::string> queries;
  std::vector<TrieNode> nodes{TrieNode{}};
  std::vector<HashSlot> slots;
  std::vector<DfaNode> dfa{DfaNode{}};
};

enum class Method { kLinear, kSorted, kHash, kTrie, kDfa };

template<Method Lookup>
void Measure(benchmark::State& state, const LookupCases& cases) {
  for (auto iteration : state) {
    benchmark::DoNotOptimize(iteration);
    for (const auto& query : cases.queries) {
      bool found = false;
      if constexpr (Lookup == Method::kLinear) {
        found = cases.LinearContains(query);
      } else if constexpr (Lookup == Method::kSorted) {
        found = xff::cli::LookupGlobal(query, cases.mode).has_value();
      } else if constexpr (Lookup == Method::kHash) {
        found = cases.HashContains(query);
      } else if constexpr (Lookup == Method::kTrie) {
        found = cases.TrieContains(query);
      } else {
        found = cases.DfaContains(query);
      }
      benchmark::DoNotOptimize(found);
    }
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(cases.queries.size()));
}

}  // namespace

// XFF_ABI_POINTER: Google Benchmark's process entry point uses the native argv ABI.
int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  const std::array cases{LookupCases(xff::registry::Mode::kXff), LookupCases(xff::registry::Mode::kRg)};
  for (const auto& mode : cases) {
    for (const auto& query : mode.queries) {
      const bool expected = xff::cli::LookupGlobal(query, mode.mode).has_value();
      if (mode.LinearContains(query) != expected || mode.HashContains(query) != expected
          || mode.TrieContains(query) != expected || mode.DfaContains(query) != expected) {
        return 1;
      }
    }
    const std::string prefix(xff::registry::ModeName(mode.mode));
    benchmark::RegisterBenchmark(
        prefix + "/linear", [&mode](benchmark::State& state) { Measure<Method::kLinear>(state, mode); });
    benchmark::RegisterBenchmark(
        prefix + "/sorted", [&mode](benchmark::State& state) { Measure<Method::kSorted>(state, mode); });
    benchmark::RegisterBenchmark(
        prefix + "/hash", [&mode](benchmark::State& state) { Measure<Method::kHash>(state, mode); });
    benchmark::RegisterBenchmark(
        prefix + "/dfa", [&mode](benchmark::State& state) { Measure<Method::kDfa>(state, mode); });
    benchmark::RegisterBenchmark(
        prefix + "/trie", [&mode](benchmark::State& state) { Measure<Method::kTrie>(state, mode); });
  }
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
