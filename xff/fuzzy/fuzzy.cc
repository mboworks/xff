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

#include "xff/fuzzy/fuzzy.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/strings/ascii.h"
#include "absl/strings/strip.h"

namespace xff::fuzzy {

bool Matches(std::string_view pattern, std::string_view text, bool fold_case) {
  // A greedy left-to-right scan is enough: for a pure subsequence test, taking the EARLIEST match
  // for each pattern character can never lose a match a later choice would have found, so there is
  // nothing to backtrack. Ranking is what needs the alignment search - that is Score, below.
  std::string_view::size_type at = 0;
  for (const char want : pattern) {
    for (;;) {
      if (at == text.size()) {
        return false;
      }
      const char have = text[at++];
      if (have == want || (fold_case && absl::ascii_tolower(have) == absl::ascii_tolower(want))) {
        break;
      }
    }
  }
  return true;
}

namespace {

// The weights. Deliberately small integers with a wide gap between "a character matched" and the
// bonuses, so a longer match always beats a shorter one and the bonuses only order matches of the
// same length. Tuned by the cases in fuzzy_test.cc, which are the real specification.
constexpr int kMatch = 16;          // per matched character
constexpr int kWordStart = 8;       // the character begins a word
constexpr int kConsecutive = 8;     // it follows the previous match immediately
constexpr int kGap = 1;             // per character skipped before a match
constexpr int kUnmatched = -1'000;  // "no alignment reaches here", never a real score

bool IsSeparator(char chr) {
  return chr == '_' || chr == '-' || chr == '.' || chr == '/' || chr == ' ';
}

// Whether text[at] begins a word: the start, anything after a separator, or a camelCase hump.
bool IsWordStart(std::string_view text, std::string_view::size_type at) {
  if (at == 0) {
    return true;
  }
  const char before = text[at - 1];
  return IsSeparator(before)
         || (absl::ascii_islower(static_cast<unsigned char>(before))
             && absl::ascii_isupper(static_cast<unsigned char>(text[at])));
}

bool SameChar(char have, char want, bool fold_case) {
  return have == want || (fold_case && absl::ascii_tolower(have) == absl::ascii_tolower(want));
}

bool SameText(std::string_view lhs, std::string_view rhs, bool fold_case) {
  return lhs.size() == rhs.size()
         && std::equal(lhs.begin(), lhs.end(), rhs.begin(), [fold_case](char lhs_char, char rhs_char) {
              return SameChar(lhs_char, rhs_char, fold_case);
            });
}

std::optional<std::size_t> FindText(std::string_view needle, std::string_view haystack, bool fold_case) {
  if (needle.empty()) {
    return 0;
  }
  if (needle.size() > haystack.size()) {
    return std::nullopt;
  }
  for (std::size_t at = 0; at + needle.size() <= haystack.size(); ++at) {
    if (SameText(needle, haystack.substr(at, needle.size()), fold_case)) {
      return at;
    }
  }
  return std::nullopt;
}

enum class TermKind { kFuzzy, kExact, kBoundary, kPrefix, kSuffix, kEqual };

struct QueryTerm {
  std::string text;
  TermKind kind = TermKind::kFuzzy;
  bool inverse = false;
};

using OrGroup = std::vector<QueryTerm>;

std::vector<std::string> SplitQuery(std::string_view query) {
  std::vector<std::string> words;
  std::string word;
  for (std::size_t at = 0; at < query.size(); ++at) {
    if (query[at] == '\\' && at + 1 < query.size() && query[at + 1] == ' ') {
      word.push_back(' ');
      ++at;
    } else if (query[at] == ' ') {
      if (!word.empty()) {
        words.push_back(std::move(word));
        word.clear();
      }
    } else {
      word.push_back(query[at]);
    }
  }
  if (!word.empty()) {
    words.push_back(std::move(word));
  }
  return words;
}

QueryTerm ParseTerm(std::string word) {
  QueryTerm term;
  if (!word.empty() && word.front() == '!') {
    term.inverse = true;
    term.kind = TermKind::kExact;
    word.erase(word.begin());
  }
  // Keep this precedence in lockstep with fzf's parseTerms: suffix is recognized before quote,
  // and a leading quote is considered before prefix. Combinations such as `!'fuzzy`, `'word'`,
  // and `^equal$` otherwise acquire subtly different meanings.
  const bool suffix = word != "$" && !word.empty() && word.back() == '$';
  if (suffix) {
    word.pop_back();
    term.kind = TermKind::kSuffix;
  }
  if (word.size() > 2 && word.front() == '\'' && word.back() == '\'') {
    word = word.substr(1, word.size() - 2);
    term.kind = TermKind::kBoundary;
  } else if (!word.empty() && word.front() == '\'') {
    word.erase(word.begin());
    term.kind = term.inverse ? TermKind::kFuzzy : TermKind::kExact;
  } else if (!word.empty() && word.front() == '^') {
    word.erase(word.begin());
    term.kind = suffix ? TermKind::kEqual : TermKind::kPrefix;
  }
  term.text = std::move(word);
  return term;
}

std::vector<OrGroup> ParseQuery(std::string_view query) {
  const std::vector<std::string> words = SplitQuery(query);
  std::vector<OrGroup> groups;
  OrGroup group;
  bool starts_next_group = false;
  bool after_bar = false;
  for (const std::string& word : words) {
    if (!group.empty() && !after_bar && word == "|") {
      starts_next_group = false;
      after_bar = true;
      continue;
    }
    after_bar = false;
    QueryTerm term = ParseTerm(word);
    if (term.text.empty()) {
      continue;
    }
    if (starts_next_group) {
      groups.push_back(std::move(group));
      group.clear();
    }
    group.push_back(std::move(term));
    starts_next_group = true;
  }
  if (!group.empty()) {
    groups.push_back(std::move(group));
  }
  return groups;
}

bool IsBoundary(char chr) {
  return !absl::ascii_isalnum(static_cast<unsigned char>(chr));
}

std::string_view TrimAnchorWhitespace(std::string_view pattern, std::string_view text, bool leading, bool trailing) {
  if (leading && (pattern.empty() || !absl::ascii_isspace(static_cast<unsigned char>(pattern.front())))) {
    text = absl::StripLeadingAsciiWhitespace(text);
  }
  if (trailing && (pattern.empty() || !absl::ascii_isspace(static_cast<unsigned char>(pattern.back())))) {
    text = absl::StripTrailingAsciiWhitespace(text);
  }
  return text;
}

std::optional<int> PositiveTermPercent(const QueryTerm& term, std::string_view text, bool fold_case, bool score) {
  if (term.kind == TermKind::kFuzzy) {
    if (score) {
      return Percent(term.text, text, fold_case);
    }
    return Matches(term.text, text, fold_case) ? std::optional(100) : std::nullopt;
  }
  const std::optional<std::size_t> found = FindText(term.text, text, fold_case);
  bool matched = found.has_value();
  switch (term.kind) {
    case TermKind::kFuzzy:
    case TermKind::kExact: break;
    case TermKind::kBoundary: {
      matched = false;
      for (std::size_t at = 0; at + term.text.size() <= text.size(); ++at) {
        if (SameText(term.text, text.substr(at, term.text.size()), fold_case) && (at == 0 || IsBoundary(text[at - 1]))
            && (at + term.text.size() == text.size() || IsBoundary(text[at + term.text.size()]))) {
          matched = true;
          break;
        }
      }
      break;
    }
    case TermKind::kPrefix:
      text = TrimAnchorWhitespace(term.text, text, /*leading=*/true, /*trailing=*/false);
      matched = text.size() >= term.text.size() && SameText(term.text, text.substr(0, term.text.size()), fold_case);
      break;
    case TermKind::kSuffix:
      text = TrimAnchorWhitespace(term.text, text, /*leading=*/false, /*trailing=*/true);
      matched = text.size() >= term.text.size()
                && SameText(term.text, text.substr(text.size() - term.text.size()), fold_case);
      break;
    case TermKind::kEqual:
      text = TrimAnchorWhitespace(term.text, text, /*leading=*/true, /*trailing=*/true);
      matched = !term.text.empty() && SameText(term.text, text, fold_case);
      break;
  }
  if (!matched) {
    return std::nullopt;
  }
  return score ? Percent(term.text, text, fold_case).value_or(100) : 100;
}

// One row of the search: the best score for the pattern prefix whose LAST character is `want`, with
// that character placed at each position of `text`. `previous` is the same row for the prefix one
// character shorter (all kUnmatched when `want` is the first character), and the result lands in
// `best`. Split out of Score because the nesting, not the idea, is what made it hard to read.
void ScoreRow(
    std::string_view text,
    char want,
    bool fold_case,
    bool first,
    const std::vector<int>& previous,
    std::vector<int>& best) {
  // `reachable` carries the best score of any earlier alignment of the previous character, already
  // charged the gap penalty for the distance travelled. Updating it as the position advances is what
  // keeps this linear in the text rather than quadratic.
  int reachable = kUnmatched;
  for (std::string_view::size_type at = 0; at < text.size(); ++at) {
    if (at > 0) {
      reachable = std::max(reachable - kGap, previous[at - 1]);
    }
    if (!SameChar(text[at], want, fold_case)) {
      best[at] = kUnmatched;
      continue;
    }
    const int here = kMatch + (IsWordStart(text, at) ? kWordStart : 0);
    if (first) {
      // The first character may start anywhere, but the later it starts the worse it scores.
      best[at] = here - (kGap * static_cast<int>(at));
      continue;
    }
    const int consecutive = at > 0 && previous[at - 1] != kUnmatched ? previous[at - 1] + kConsecutive : kUnmatched;
    const int from = std::max(reachable, consecutive);
    best[at] = from == kUnmatched ? kUnmatched : from + here;
  }
}

}  // namespace

std::optional<int> Score(std::string_view pattern, std::string_view text, bool fold_case) {
  if (pattern.empty()) {
    return 0;  // matches, and says nothing about how well - see Matches
  }
  if (pattern.size() > text.size()) {
    return std::nullopt;
  }
  // Two rows are enough, because a pattern character can only follow the one before it.
  std::vector<int> previous(text.size(), kUnmatched);
  std::vector<int> best(text.size(), kUnmatched);
  for (std::string_view::size_type i = 0; i < pattern.size(); ++i) {
    ScoreRow(text, pattern[i], fold_case, /*first=*/i == 0, previous, best);
    previous.swap(best);
  }
  const auto found = absl::c_max_element(previous);
  if (found == previous.end() || *found == kUnmatched) {
    return std::nullopt;
  }
  return *found;
}

std::optional<int> Percent(std::string_view pattern, std::string_view text, bool fold_case) {
  const std::optional<int> score = Score(pattern, text, fold_case);
  if (!score.has_value()) {
    return std::nullopt;
  }
  if (pattern.empty()) {
    return 100;
  }
  const std::optional<int> ceiling = Score(pattern, pattern, fold_case);
  if (!ceiling.has_value() || *ceiling <= 0) {
    return std::nullopt;
  }
  const std::int64_t scaled = (static_cast<std::int64_t>(*score) * 100) / *ceiling;
  return static_cast<int>(std::clamp<std::int64_t>(scaled, 0, 100));
}

struct FzfQuery::Impl {
  std::vector<OrGroup> groups;
};

FzfQuery::FzfQuery(std::string_view query) : impl_(std::make_shared<const Impl>(Impl{.groups = ParseQuery(query)})) {}

bool FzfQuery::Matches(std::string_view text, bool fold_case) const {
  return absl::c_all_of(impl_->groups, [&](const OrGroup& group) {
    return absl::c_any_of(group, [&](const QueryTerm& term) {
      return term.inverse != PositiveTermPercent(term, text, fold_case, /*score=*/false).has_value();
    });
  });
}

std::optional<int> FzfPercent(std::string_view query, std::string_view text, bool fold_case) {
  return FzfQuery(query).Percent(text, fold_case);
}

std::optional<int> FzfQuery::Percent(std::string_view text, bool fold_case) const {
  int query_score = 100;
  for (const OrGroup& group : impl_->groups) {
    std::optional<int> group_score;
    bool matched = false;
    for (const QueryTerm& term : group) {
      const std::optional<int> positive = PositiveTermPercent(term, text, fold_case, /*score=*/true);
      const bool term_matches = term.inverse ? !positive.has_value() : positive.has_value();
      if (!term_matches) {
        continue;
      }
      matched = true;
      const int score = positive.value_or(100);
      group_score = std::max(group_score.value_or(0), score);
    }
    if (!matched) {
      return std::nullopt;
    }
    query_score = std::min(query_score, group_score.value_or(100));
  }
  return query_score;
}

std::optional<int> SequencePercent(std::string_view pattern, std::string_view text, bool fold_case) {
  if (!Matches(pattern, text, fold_case)) {
    return std::nullopt;
  }
  if (text.empty()) {
    return 100;
  }
  return static_cast<int>(((pattern.size() * 100) + (text.size() / 2)) / text.size());
}

int LevenshteinPercent(std::string_view pattern, std::string_view text, bool fold_case) {
  if (pattern.empty() && text.empty()) {
    return 100;
  }
  std::vector<std::size_t> previous(text.size() + 1);
  std::vector<std::size_t> current(text.size() + 1);
  for (std::size_t text_at = 0; text_at <= text.size(); ++text_at) {
    previous[text_at] = text_at;
  }
  for (std::size_t i = 1; i <= pattern.size(); ++i) {
    current[0] = i;
    for (std::size_t text_at = 1; text_at <= text.size(); ++text_at) {
      const std::size_t substitution =
          previous[text_at - 1] + (SameChar(pattern[i - 1], text[text_at - 1], fold_case) ? 0 : 1);
      current[text_at] = std::min({previous[text_at] + 1, current[text_at - 1] + 1, substitution});
    }
    previous.swap(current);
  }
  const std::size_t length = std::max(pattern.size(), text.size());
  const std::size_t same = length - previous.back();
  return static_cast<int>(((same * 100) + (length / 2)) / length);
}

int ShinglePercent(std::string_view pattern, std::string_view text, bool fold_case) {
  const auto shingles = [fold_case](std::string_view value) {
    std::vector<std::uint16_t> result;
    if (value.size() < 2) {
      return result;
    }
    result.reserve(value.size() - 1);
    for (std::size_t i = 1; i < value.size(); ++i) {
      const auto byte = [fold_case](char chr) {
        const char normalized = fold_case ? absl::ascii_tolower(chr) : chr;
        return static_cast<std::uint16_t>(static_cast<unsigned char>(normalized));
      };
      result.push_back(static_cast<std::uint16_t>((byte(value[i - 1]) * 256U) + byte(value[i])));
    }
    absl::c_sort(result);
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
  };
  if (pattern.size() < 2 || text.size() < 2) {
    if (pattern.size() != text.size()) {
      return 0;
    }
    return pattern.empty() || SameChar(pattern.front(), text.front(), fold_case) ? 100 : 0;
  }
  const std::vector<std::uint16_t> lhs = shingles(pattern);
  const std::vector<std::uint16_t> rhs = shingles(text);
  std::size_t intersection = 0;
  auto left = lhs.begin();
  auto right = rhs.begin();
  while (left != lhs.end() && right != rhs.end()) {
    if (*left < *right) {
      ++left;
    } else if (*right < *left) {
      ++right;
    } else {
      ++intersection;
      ++left;
      ++right;
    }
  }
  const std::size_t count = lhs.size() + rhs.size() - intersection;
  return static_cast<int>(((intersection * 100) + (count / 2)) / count);
}

}  // namespace xff::fuzzy
