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

// FNM_CASEFOLD and POSIX fnmatch() are hidden by glibc under the strict `-std=c++23` we build with;
// request them explicitly (the kFnmatch backend needs them). No effect on macOS.
#if defined(__linux__) && !defined(_GNU_SOURCE)
# define _GNU_SOURCE 1
#endif

#include "xff/matching/regex/regex.h"

#include <fnmatch.h>
#include <regex.h>

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "mbo/file/glob.h"
#include "mbo/status/status_macros.h"
#include "re2/re2.h"
#include "xff/matching/regex/backend.h"

namespace xff::regex {
namespace {

// The default grammar: RE2 (linear-time, no catastrophic backtracking). Holds the compiled RE2 and
// forwards each Matcher operation to it.
class Re2Backend final : public RegexBackend {
 public:
  explicit Re2Backend(std::unique_ptr<RE2> re) : re_(std::move(re)) {}

  bool FullMatch(std::string_view text) const override { return RE2::FullMatch(text, *re_); }

  bool PartialMatch(std::string_view text) const override { return RE2::PartialMatch(text, *re_); }

  std::optional<std::pair<std::size_t, std::size_t>> FindFirst(std::string_view text) const override {
    std::string_view match;  // submatch[0] = the whole match; its data() points into `text`
    if (!re_->Match(text, 0, text.size(), RE2::UNANCHORED, &match, 1)) {
      return std::nullopt;
    }
    return std::make_pair(static_cast<std::size_t>(match.data() - text.data()), match.size());
  }

  std::optional<std::vector<std::string>> FullMatchCaptures(std::string_view text) const override {
    const int groups = re_->NumberOfCapturingGroups();  // parenthesised groups, excluding the whole match
    const int nsubmatch = groups + 1;                   // index 0 holds the whole match
    std::vector<std::string_view> submatches(static_cast<std::size_t>(nsubmatch));
    if (!re_->Match(text, 0, text.size(), RE2::ANCHOR_BOTH, submatches.data(), nsubmatch)) {
      return std::nullopt;
    }
    std::vector<std::string> captures;
    captures.reserve(submatches.size());
    for (const std::string_view submatch : submatches) {  // [0] = whole match, [1..] = groups
      captures.emplace_back(submatch);
    }
    return captures;
  }

  std::string Rewrite(std::string_view text, std::string_view replacement, bool global) const override {
    std::string out(text);
    if (global) {
      RE2::GlobalReplace(&out, *re_, replacement);
    } else {
      RE2::Replace(&out, *re_, replacement);
    }
    return out;
  }

 private:
  std::unique_ptr<RE2> re_;
};

class EreBackend final : public RegexBackend {
 public:
  EreBackend(const EreBackend&) = delete;
  EreBackend& operator=(const EreBackend&) = delete;
  EreBackend(EreBackend&&) = delete;
  EreBackend& operator=(EreBackend&&) = delete;

  static absl::StatusOr<std::unique_ptr<EreBackend>> Compile(std::string_view pattern, bool case_insensitive) {
    if (ContainsNul(pattern)) {
      return absl::InvalidArgumentError("POSIX ERE patterns cannot contain NUL bytes");
    }
    auto backend = std::unique_ptr<EreBackend>(new EreBackend());
    const int result =
        regcomp(&backend->compiled_, std::string(pattern).c_str(), REG_EXTENDED | (case_insensitive ? REG_ICASE : 0));
    if (result == 0) {
      backend->compiled_ok_ = true;
      return backend;
    }
    std::array<char, 256> message{};
    regerror(result, &backend->compiled_, message.data(), message.size());
    return absl::InvalidArgumentError(absl::StrCat("invalid POSIX ERE: ", message.data()));
  }

  ~EreBackend() override {
    if (compiled_ok_) {
      regfree(&compiled_);
    }
  }

  bool FullMatch(std::string_view text) const override {
    if (ContainsNul(text)) {
      return false;
    }
    const std::string input(text);
    regmatch_t match{};
    return regexec(&compiled_, input.c_str(), 1, &match, 0) == 0 && match.rm_so == 0
           && static_cast<std::size_t>(match.rm_eo) == input.size();
  }

  bool PartialMatch(std::string_view text) const override { return FindFirst(text).has_value(); }

  std::optional<std::pair<std::size_t, std::size_t>> FindFirst(std::string_view text) const override {
    if (ContainsNul(text)) {
      return std::nullopt;
    }
    const std::string input(text);
    regmatch_t match{};
    if (regexec(&compiled_, input.c_str(), 1, &match, 0) != 0) {
      return std::nullopt;
    }
    return std::make_pair(static_cast<std::size_t>(match.rm_so), static_cast<std::size_t>(match.rm_eo - match.rm_so));
  }

  std::optional<std::vector<std::string>> FullMatchCaptures(std::string_view text) const override {
    if (ContainsNul(text)) {
      return std::nullopt;
    }
    const std::string input(text);
    std::vector<regmatch_t> matches(compiled_.re_nsub + 1);
    if (regexec(&compiled_, input.c_str(), matches.size(), matches.data(), 0) != 0 || matches[0].rm_so != 0
        || static_cast<std::size_t>(matches[0].rm_eo) != input.size()) {
      return std::nullopt;
    }
    std::vector<std::string> captures;
    captures.reserve(matches.size());
    for (const regmatch_t match : matches) {
      if (match.rm_so < 0) {
        captures.emplace_back();
      } else {
        captures.emplace_back(
            input.substr(static_cast<std::size_t>(match.rm_so), static_cast<std::size_t>(match.rm_eo - match.rm_so)));
      }
    }
    return captures;
  }

  std::string Rewrite(std::string_view text, std::string_view replacement, bool global) const override {
    if (ContainsNul(text)) {
      return std::string(text);
    }
    const std::string input(text);
    std::string out;
    std::size_t offset = 0;
    while (true) {
      std::vector<regmatch_t> matches(compiled_.re_nsub + 1);
      const int flags = offset == 0 ? 0 : REG_NOTBOL;
      if (regexec(&compiled_, SuffixCString(input, offset), matches.size(), matches.data(), flags) != 0) {
        break;
      }
      const std::size_t begin = offset + static_cast<std::size_t>(matches[0].rm_so);
      const std::size_t end = offset + static_cast<std::size_t>(matches[0].rm_eo);
      out.append(input, offset, begin - offset);
      AppendReplacement(out, input, offset, matches, replacement);
      offset = end;
      if (end == begin) {
        if (offset >= input.size()) {
          break;
        }
        out.push_back(input[offset++]);
      }
      if (!global) {
        break;
      }
    }
    out.append(input.substr(offset));
    return out;
  }

 private:
  EreBackend() = default;

  static bool ContainsNul(std::string_view text) { return absl::StrContains(text, std::string_view("\0", 1)); }

  // XFF_ABI_POINTER: regexec requires a NUL-terminated pointer to the current subject suffix.
  static const char* SuffixCString(const std::string& input, std::size_t offset) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic): narrow POSIX regex adapter.
    return input.c_str() + offset;
  }

  static void AppendReplacement(
      std::string& out,
      const std::string& input,
      std::size_t offset,
      const std::vector<regmatch_t>& matches,
      std::string_view replacement) {
    for (std::size_t index = 0; index < replacement.size(); ++index) {
      if (replacement[index] != '\\' || index + 1 >= replacement.size()) {
        out.push_back(replacement[index]);
        continue;
      }
      const char escaped = replacement[++index];
      if (escaped < '0' || escaped > '9') {
        out.push_back(escaped);
        continue;
      }
      const auto capture = static_cast<std::size_t>(escaped - '0');
      if (capture < matches.size() && matches[capture].rm_so >= 0) {
        out.append(
            input, offset + static_cast<std::size_t>(matches[capture].rm_so),
            static_cast<std::size_t>(matches[capture].rm_eo - matches[capture].rm_so));
      }
    }
  }

  regex_t compiled_{};
  bool compiled_ok_ = false;
};

// The kExact grammar: a literal string match, no metacharacters. A core engine (always linked, no
// dependency), so --regextype=EXACT is always available. FullMatch is equality, PartialMatch a
// substring test, FindFirst the first occurrence, Rewrite a literal find/replace (no
// backreferences); `case_insensitive` folds ASCII case on both sides. There is no pattern to
// compile, so Compile(kExact) never fails.
class ExactBackend final : public RegexBackend {
 public:
  ExactBackend(std::string pattern, bool case_insensitive)
      : pattern_(std::move(pattern)),
        case_insensitive_(case_insensitive),
        needle_(case_insensitive_ ? absl::AsciiStrToLower(pattern_) : pattern_) {}

  bool FullMatch(std::string_view text) const override {
    return case_insensitive_ ? absl::EqualsIgnoreCase(text, pattern_) : text == pattern_;
  }

  bool PartialMatch(std::string_view text) const override { return FindFirst(text).has_value(); }

  std::optional<std::pair<std::size_t, std::size_t>> FindFirst(std::string_view text) const override {
    if (pattern_.empty()) {
      return std::make_pair(std::size_t{0}, std::size_t{0});  // empty needle matches at the start
    }
    // ASCII case-folding preserves byte positions, so the offset found in the lowered copy maps back
    // to `text` unchanged (the reported length is the pattern's).
    const std::size_t pos = case_insensitive_ ? absl::AsciiStrToLower(text).find(needle_) : text.find(needle_);
    if (pos == std::string::npos) {
      return std::nullopt;
    }
    return std::make_pair(pos, pattern_.size());
  }

  std::optional<std::vector<std::string>> FullMatchCaptures(std::string_view text) const override {
    if (!FullMatch(text)) {
      return std::nullopt;
    }
    return std::vector<std::string>{std::string(text)};  // index 0 = the whole match; no groups
  }

  std::string Rewrite(std::string_view text, std::string_view replacement, bool global) const override {
    if (pattern_.empty()) {
      return std::string(text);  // an empty needle rewrites nothing (avoids an infinite loop)
    }
    const std::string haystack = case_insensitive_ ? absl::AsciiStrToLower(text) : std::string(text);
    std::string out;
    std::size_t pos = 0;
    while (true) {
      const std::size_t hit = haystack.find(needle_, pos);
      if (hit == std::string::npos) {
        break;
      }
      out.append(text, pos, hit - pos);  // copy from the original text (preserves case)
      out.append(replacement);
      pos = hit + needle_.size();
      if (!global) {
        break;
      }
    }
    out.append(text.substr(pos));
    return out;
  }

 private:
  std::string pattern_;
  bool case_insensitive_;
  std::string needle_;  // == pattern_, ASCII-lowered when case_insensitive_
};

// The kFnmatch grammar: a flat shell wildcard via POSIX fnmatch (`*`/`?`/`[…]`, `*` matching any
// character including `/` - no FNM_PATHNAME, matching find's -name/-path). A core engine (no
// dependency). fnmatch is a whole-string test: FullMatch runs it directly (anchored), PartialMatch
// wraps the pattern in `*…*` so it matches anywhere (`**` collapses to `*` in POSIX fnmatch, so the
// always-wrap is safe - see #85). fnmatch yields no span or groups, so FindFirst reports the whole
// text as the match, FullMatchCaptures is the whole match only, and Rewrite is a no-op.
// `case_insensitive` sets FNM_CASEFOLD. There is no pattern to compile, so Compile never fails.
class FnmatchBackend final : public RegexBackend {
 public:
  FnmatchBackend(std::string pattern, bool case_insensitive)
      : pattern_(std::move(pattern)),
        partial_pattern_(absl::StrCat("*", pattern_, "*")),
        flags_(case_insensitive ? FNM_CASEFOLD : 0) {}

  bool FullMatch(std::string_view text) const override { return Fnmatch(pattern_, text); }

  bool PartialMatch(std::string_view text) const override { return Fnmatch(partial_pattern_, text); }

  std::optional<std::pair<std::size_t, std::size_t>> FindFirst(std::string_view text) const override {
    // fnmatch is a whole-string test, not a span search: when the unanchored pattern matches, the
    // match is the whole text (so -grep:FORMAT's {match} is the line, {column} is 1).
    if (!PartialMatch(text)) {
      return std::nullopt;
    }
    return std::make_pair(std::size_t{0}, text.size());
  }

  std::optional<std::vector<std::string>> FullMatchCaptures(std::string_view text) const override {
    if (!FullMatch(text)) {
      return std::nullopt;
    }
    return std::vector<std::string>{std::string(text)};  // index 0 = the whole match; no groups
  }

  std::string Rewrite(std::string_view text, std::string_view /*replacement*/, bool /*global*/) const override {
    return std::string(text);  // a shell glob has no rewrite / backreference semantics
  }

 private:
  bool Fnmatch(const std::string& pattern, std::string_view text) const {
    // fnmatch needs NUL-terminated C strings; `text` may not be, so materialize it (per-entry cost,
    // matching the evaluator's own -name/-path helper).
    return ::fnmatch(pattern.c_str(), std::string(text).c_str(), flags_) == 0;
  }

  std::string pattern_;
  std::string partial_pattern_;  // "*" + pattern_ + "*", for the unanchored PartialMatch
  int flags_;
};

}  // namespace

absl::StatusOr<Matcher> Matcher::Compile(std::string_view pattern, bool case_insensitive, Grammar grammar) {
  // Shared RE2 compilation: kRe2 uses the pattern verbatim, kGlob its glob-to-RE2 translation. A
  // lambda in this member function reaches Matcher's private constructor.
  const auto compile_re2 = [case_insensitive](std::string_view re_pattern) -> absl::StatusOr<Matcher> {
    RE2::Options options;
    options.set_case_sensitive(!case_insensitive);
    options.set_log_errors(false);  // surface failures via Status, not stderr
    auto re = std::make_unique<RE2>(re_pattern, options);
    if (!re->ok()) {
      return absl::InvalidArgumentError(absl::StrCat("invalid regular expression: ", re->error()));
    }
    return Matcher(std::make_unique<Re2Backend>(std::move(re)));
  };
  switch (grammar) {
    case Grammar::kRe2: return compile_re2(pattern);
    case Grammar::kExact:
      // A literal match: no pattern to compile, so this never fails.
      return Matcher(std::make_unique<ExactBackend>(std::string(pattern), case_insensitive));
    case Grammar::kFnmatch:
      // A shell wildcard: fnmatch validates lazily per call, so this never fails either.
      return Matcher(std::make_unique<FnmatchBackend>(std::string(pattern), case_insensitive));
    case Grammar::kGlob: {
      // A path-aware shell glob translated by mbo, then RE2 provides every operation.
      MBO_ASSIGN_OR_RETURN(const std::string translated, mbo::file::Glob2Re2Expression(pattern));
      return compile_re2(translated);
    }
    case Grammar::kShglob: {
      // SHGLOB selects the same path semantics plus nested brace alternatives.
      MBO_ASSIGN_OR_RETURN(
          const std::string translated,
          mbo::file::Glob2Re2Expression(pattern, {.syntax = mbo::file::GlobSyntax::kShGlob}));
      return compile_re2(translated);
    }
    case Grammar::kEre: {
      MBO_ASSIGN_OR_RETURN(std::unique_ptr<EreBackend> compiled, EreBackend::Compile(pattern, case_insensitive));
      return Matcher(std::move(compiled));
    }
    case Grammar::kPcre2: {
      // PCRE2 is a build-time extra: the real backend (extra_modules/pcre2) self-registers a factory
      // in the xff_extras_api slot. MakePcre2Backend invokes it, or returns Unimplemented when no
      // PCRE2 backend is linked (lean build) -- a distinct state from an InvalidArgument bad pattern,
      // and never a silent fallback to RE2.
      MBO_ASSIGN_OR_RETURN(std::unique_ptr<const RegexBackend> backend, MakePcre2Backend(pattern, case_insensitive));
      return Matcher(std::move(backend));
    }
  }
  return absl::InternalError("unknown regex grammar");  // unreachable: the enum is exhaustive
}

Matcher::Matcher(std::unique_ptr<const RegexBackend> backend) : backend_(std::move(backend)) {}

Matcher::~Matcher() = default;
Matcher::Matcher(Matcher&&) noexcept = default;
Matcher& Matcher::operator=(Matcher&&) noexcept = default;

bool Matcher::FullMatch(std::string_view text) const {
  return backend_->FullMatch(text);
}

bool Matcher::PartialMatch(std::string_view text) const {
  return backend_->PartialMatch(text);
}

std::optional<std::pair<std::size_t, std::size_t>> Matcher::FindFirst(std::string_view text) const {
  return backend_->FindFirst(text);
}

std::optional<std::vector<std::string>> Matcher::FullMatchCaptures(std::string_view text) const {
  return backend_->FullMatchCaptures(text);
}

std::string Matcher::Rewrite(std::string_view text, std::string_view replacement, bool global) const {
  return backend_->Rewrite(text, replacement, global);
}

absl::Span<const std::pair<std::string_view, std::string_view>> GrammarDocs() {
  static constexpr auto kDocs = std::to_array<std::pair<std::string_view, std::string_view>>({
      {"RE2",
       "the default. Google RE2 regular expressions - linear-time, no catastrophic backtracking. "
       "Full syntax: https://github.com/google/re2/wiki/Syntax ."},
      {"EXACT",
       "a literal string; every character matches itself, no metacharacters. -regex is whole-string "
       "equality, -rxc / -grep a substring test."},
      {"FNMATCH",
       "a flat shell wildcard via the platform's fnmatch(3): * matches any run of characters "
       "(including /), ? one character, [...] a class. Whole-string, like find -name / -path (no "
       "/-awareness); -i uses FNM_CASEFOLD. Provided by libc, so class / collation details vary by "
       "system."},
      {"GLOB",
       "xff's path-aware, locale-independent shell glob (compiled to RE2 - NOT POSIX glob(7)): * "
       "and ? stay within one path component; a complete-component ** crosses components (middle "
       "foo/**/bar permits zero or more, while trailing foo/** requires a descendant); embedded star "
       "runs reduce to *. [...] supports literals, ascending ranges, leading ! negation, and RE2 ASCII "
       "named classes, always excluding / except the compatibility spelling [/]. Malformed ranges, "
       "descending ranges, unsupported named classes, collation/equivalence, and negative extglob are "
       "errors. Braces are literal. Because it compiles to RE2, -grep / -rxc partial matching and match "
       "spans work."},
      {"SHGLOB",
       "GLOB plus brace alternation: {a,b,c} matches any one alternative, so *.{cc,h} matches either. "
       "Integer and ASCII-letter sequences expand in either direction (`{1..9}`, `{09..01}`, `{a..z}`); a "
       "leading zero preserves integer width, and expansion above 10,000 terms is rejected. Alternatives "
       "and sequences may nest; alternatives may be empty. Escaped braces and commas, braces inside a "
       "[...] class, and comma-less braces that are not a sequence are literal. The optional shell "
       "increment form (`{1..9..2}`) is not supported and remains literal. Everything else is exactly GLOB."},
      {"ERE",
       "POSIX extended regular expressions through the platform regcomp(3) implementation. This provides "
       "traditional find -E syntax, captures, partial matching, and rewrites, but locale details and some "
       "edge-case behavior follow the host C library rather than RE2's cross-platform semantics or linear-time "
       "guarantee. Patterns containing NUL are errors; subjects containing NUL do not match because the POSIX API "
       "uses C strings."},
      {"PCRE2",
       "Perl-Compatible Regular Expressions (lookaround, backreferences, ...). A build-time extra: "
       "present only in a full build - run `xff --help=extras` to see whether THIS binary has it. Full "
       "syntax: pcre2pattern(3)."},
  });
  return kDocs;
}

}  // namespace xff::regex
