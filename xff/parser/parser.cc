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

#include "xff/parser/parser.h"

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "mbo/container/limited_set.h"
#include "mbo/status/status_macros.h"
#include "xff/matching/regex/regex.h"
#include "xff/parser/ast.h"
#include "xff/presentation/fields/fields.h"
#include "xff/registry/descriptor.h"
#include "xff/registry/registry.h"

namespace xff::parser {

namespace {

// A token begins the find expression if it is a single-dash word, or one of
// the operator/grouping tokens.
bool StartsExpression(std::string_view arg) {
  if (arg.empty()) {
    return false;
  }
  if (arg[0] == '-') {
    return true;
  }
  return arg == "(" || arg == ")" || arg == "!" || arg == ",";
}

// A whole-run global written with a double dash (`--summary=ext`, `--sort`, `--top=10`). Every
// expression primary and operator is single-dash, so a `--`-prefixed token is unambiguously a global
// wherever it appears (after the roots, in the expression, at the tail) -- never a primary. That lets
// the parser hoist it out of any primary/operator position (but NOT out of a primary's argument run,
// e.g. an -exec command or a -printf format, which the ExprParser consumes token-by-token). Bare `--`
// (exactly two dashes) is the end-of-options delimiter, not a global.
bool IsHoistableGlobal(std::string_view arg) {
  return arg.size() > 2 && arg[0] == '-' && arg[1] == '-';
}

// Meta flags are position-independent like double-dash globals, plus the GNU
// find-compatible single-dash spellings. Keeping this vocabulary in the parser
// is what prevents a lookalike inside `-exec`'s raw argument run from being
// mistaken for an xff request.
constexpr bool IsMetaFlag(std::string_view arg) {
  constexpr auto kMetaFlags = mbo::container::MakeLimitedSet<9>(std::initializer_list<std::string_view>{
      "--help",
      "-h",
      "-help",
      "--help-all",
      "--help-full",
      "--help-long",
      "--version",
      "-version",
      "--man",
  });
  return kMetaFlags.contains(arg) || absl::StartsWith(arg, "--help=");
}

bool IsOr(std::string_view token) {
  return token == "-o" || token == "-or";
}

bool IsAnd(std::string_view token) {
  return token == "-a" || token == "-and";
}

bool IsNot(std::string_view token) {
  return token == "!" || token == "-not";
}

// xff extensions: -nand binds at the AND tier, -nor at the OR tier, and -xor /
// -xnor form a new tier between them (NOT > AND > XOR > OR), matching the
// conventional boolean / C bitwise (& ^ |) precedence.
bool IsNand(std::string_view token) {
  return token == "-nand";
}

// Operators at the OR tier (the lowest binary tier): -o / -or / -nor.
bool IsOrTier(std::string_view token) {
  return IsOr(token) || token == "-nor";
}

// Operators at the XOR tier (between AND and OR): -xor / -xnor.
bool IsXorTier(std::string_view token) {
  return token == "-xor" || token == "-xnor";
}

// A binding primary's NAME must be an identifier: `[A-Za-z_][A-Za-z0-9_]*`. It is not cosmetic -
// a name is referenced as `{capture.NAME}`, so punctuation would be unreferenceable or ambiguous
// there - and enforcing it is what reserves every punctuation character for MODIFIERS, which is how
// `!` can mean "this node may reuse the name" without new syntax or a whole-run flag.
bool ValidLabelName(std::string_view name) {
  if (name.empty() || (!absl::ascii_isalpha(name.front()) && name.front() != '_')) {
    return false;
  }
  return absl::c_all_of(name, [](const char chr) { return absl::ascii_isalnum(chr) || chr == '_'; });
}

// Splits a leading `!` modifier off an attached name spec, reporting whether it was present.
//
// The `rest` view BORROWS from `spec`, so callers must pass a view over storage that outlives the
// result - `std::string_view(token).substr(...)`, never `token.substr(...)`, which materialises a
// temporary std::string and leaves `rest` dangling the moment the statement ends. That mistake was
// mine and ASan caught it across every test that parses.
struct LabelSpec {
  bool override_name = false;
  std::string_view rest;
};

LabelSpec SplitLabelModifier(std::string_view spec) {
  if (spec.starts_with('!')) {
    spec.remove_prefix(1);
    return LabelSpec{.override_name = true, .rest = spec};
  }
  return LabelSpec{.rest = spec};
}

// Returns a node's regex pattern when it has one:
// -regex/-iregex (whole-path) and -rxc/-irxc (file-content) match args[0];
// -capture/-capturedir (Binding::kLabelRegex) carry an optional extraction regex in
// args[1]. Case sensitivity comes from the descriptor's fold_case (so the i-variants
// are data, not a name check). Returns null when the node carries no regex, or the
std::optional<std::string_view> NodeRegexPattern(
    const registry::Descriptor& descriptor,
    const std::vector<std::string>& args) {
  std::string_view pattern;
  if ((descriptor.name == "-regex" || descriptor.name == "-iregex" || descriptor.name == "-rxc"
       || descriptor.name == "-irxc" || descriptor.name == "-grep")
      && !args.empty()) {
    pattern = args[0];
  } else if (descriptor.binding == registry::Binding::kLabelRegex && args.size() > 1 && !args[1].empty()) {
    pattern = args[1];  // the optional :NAME=REGEX extraction regex
  } else {
    return std::nullopt;
  }
  return pattern;
}

ExprPtr MakePredicate(const registry::Descriptor& descriptor, std::vector<std::string> args) {
  auto expr = std::make_unique<Expr>();
  expr->kind = Expr::Kind::kPredicate;
  expr->descriptor.set_ref(descriptor);
  expr->args = std::move(args);
  return expr;
}

ExprPtr MakeNot(ExprPtr operand) {
  auto expr = std::make_unique<Expr>();
  expr->kind = Expr::Kind::kNot;
  expr->lhs = std::move(operand);
  return expr;
}

ExprPtr MakeBinary(Expr::Kind kind, ExprPtr lhs, ExprPtr rhs) {
  auto expr = std::make_unique<Expr>();
  expr->kind = kind;
  expr->lhs = std::move(lhs);
  expr->rhs = std::move(rhs);
  return expr;
}

// Recursive-descent parser over the find expression tokens. Errors are
// accumulated in `status_`; node-returning methods return nullptr once failed.
//   list := or ( ',' or )*                          // comma: lowest precedence
//   or   := xor ( ('-o'|'-or'|'-nor') xor )*
//   xor  := and ( ('-xor'|'-xnor') and )*           // xff tier, between or and and
//   and  := unary ( ('-a'|'-and'|'-nand')? unary )* // implicit -a; -nand explicit
//   unary:= ('!'|'-not') unary | primary
//   prim := '(' list ')' | PREDICATE arg{arity}
class ExprParser {
 public:
  // `hoist_globals` = pull double-dash globals out of primary/operator positions into
  // HoistedGlobals() (off after an explicit `--` end-of-options delimiter).
  explicit ExprParser(const std::vector<std::string>& tokens, bool hoist_globals = true)
      : tokens_(tokens), hoist_globals_(hoist_globals) {}

  absl::StatusOr<ExprPtr> Parse() {
    SkipGlobals();
    if (AtEnd()) {
      return ExprPtr{};
    }
    ExprPtr expr = ParseComma();
    if (!status_.ok()) {
      return status_;
    }
    if (pos_ != tokens_.size()) {
      return absl::InvalidArgumentError(absl::StrCat("unexpected token: '", tokens_[pos_], "'"));
    }
    return expr;
  }

  // Double-dash globals lifted out of the expression (see IsHoistableGlobal), in appearance order,
  // for the caller to append to the command's global list.
  const std::vector<std::string>& HoistedGlobals() const { return hoisted_globals_; }

  const std::vector<std::string>& HoistedMetaFlags() const { return hoisted_meta_flags_; }

 private:
  bool AtEnd() const { return pos_ >= tokens_.size(); }

  const std::string& Peek() const { return tokens_[pos_]; }

  // At a primary/operator boundary, lift any run of double-dash globals into hoisted_globals_ so the
  // syntactic checks that follow see the next real primary/operator. Called only from those
  // boundaries, never while a primary is consuming its raw arguments (an -exec command, a -printf
  // format), so a `--flag` meant for a child command is left untouched.
  void SkipGlobals() {
    while (hoist_globals_ && !AtEnd() && (IsHoistableGlobal(tokens_[pos_]) || IsMetaFlag(tokens_[pos_]))) {
      if (IsMetaFlag(tokens_[pos_])) {
        hoisted_meta_flags_.push_back(tokens_[pos_]);
      } else {
        hoisted_globals_.push_back(tokens_[pos_]);
      }
      ++pos_;
    }
  }

  void Fail(std::string_view message) {
    if (status_.ok()) {
      status_ = absl::InvalidArgumentError(message);
    }
  }

  // Comma has the lowest precedence: a list of OR-expressions. Both sides are
  // always evaluated; the list's value is the right operand's (handled in eval).
  ExprPtr ParseComma() {
    ExprPtr lhs = ParseOr();
    while (status_.ok() && !AtEnd() && Peek() == ",") {
      ++pos_;
      ExprPtr rhs = ParseOr();
      lhs = MakeBinary(Expr::Kind::kComma, std::move(lhs), std::move(rhs));
    }
    return lhs;
  }

  ExprPtr ParseOr() {
    ExprPtr lhs = ParseXor();
    while (status_.ok() && !AtEnd() && IsOrTier(Peek())) {
      const Expr::Kind kind = Peek() == "-nor" ? Expr::Kind::kNor : Expr::Kind::kOr;
      ++pos_;
      ExprPtr rhs = ParseXor();
      lhs = MakeBinary(kind, std::move(lhs), std::move(rhs));
    }
    return lhs;
  }

  // The XOR tier sits between OR and AND (NOT > AND > XOR > OR). -xor / -xnor
  // (xff) bind here; both operands always evaluate (the result needs both).
  ExprPtr ParseXor() {
    ExprPtr lhs = ParseAnd();
    while (status_.ok() && !AtEnd() && IsXorTier(Peek())) {
      const Expr::Kind kind = Peek() == "-xnor" ? Expr::Kind::kXnor : Expr::Kind::kXor;
      ++pos_;
      ExprPtr rhs = ParseAnd();
      lhs = MakeBinary(kind, std::move(lhs), std::move(rhs));
    }
    return lhs;
  }

  ExprPtr ParseAnd() {
    ExprPtr lhs = ParseUnary();
    // ParseAnd is the tier directly over the operands, so skipping globals right after each operand
    // (before the operator check) leaves every higher tier looking at a real operator/terminator.
    SkipGlobals();
    while (status_.ok() && !AtEnd() && !IsOrTier(Peek()) && !IsXorTier(Peek()) && Peek() != ")" && Peek() != ",") {
      Expr::Kind kind = Expr::Kind::kAnd;
      if (IsNand(Peek())) {
        ++pos_;  // explicit -nand (xff)
        kind = Expr::Kind::kNand;
      } else if (IsAnd(Peek())) {
        ++pos_;  // optional explicit -a
      }
      ExprPtr rhs = ParseUnary();
      SkipGlobals();
      lhs = MakeBinary(kind, std::move(lhs), std::move(rhs));
    }
    return lhs;
  }

  ExprPtr ParseUnary() {
    SkipGlobals();  // a global leading this operand (start of expression, or after an operator)
    if (!AtEnd() && IsNot(Peek())) {
      ++pos_;
      return MakeNot(ParseUnary());
    }
    return ParsePrimary();
  }

  // NOLINTNEXTLINE(readability-function-cognitive-complexity): cohesive dispatch
  ExprPtr ParsePrimary() {
    if (AtEnd()) {
      Fail("expected a predicate or '('");
      return nullptr;
    }
    const std::string& token = Peek();
    if (token == "(") {
      ++pos_;
      ExprPtr inner = ParseComma();
      if (!status_.ok()) {
        return nullptr;
      }
      if (AtEnd() || Peek() != ")") {
        Fail("missing ')'");
        return nullptr;
      }
      ++pos_;
      return inner;
    }
    // Reject the pre-1.0 `=QUALIFIER` spelling with a focused migration diagnostic rather than
    // letting it fall through as an unknown primary. `=` belongs to whole-run globals; a primary's
    // attached per-node qualification begins with `:`.
    if (const std::string::size_type equals = token.find('='); equals != std::string::npos) {
      const std::string_view base = std::string_view(token).substr(0, equals);
      if (const auto descriptor = registry::Lookup(base);
          descriptor.has_value() && descriptor->binding != registry::Binding::kNone) {
        Fail(
            absl::StrCat(
                "'", token, "' uses the old primary qualifier spelling; use '", base, ":",
                std::string_view(token).substr(equals + 1), "'"));
        return nullptr;
      }
    }
    // A primary that declares Binding::kLabel (-collect) carries a plain attached :NAME and takes
    // nothing else: args = [NAME]. Unlike kLabelRegex there is no regex half and no command, and
    // the BARE form stays legal - the descriptor's own default name is the answer - so this branch
    // only has to reject `-collect:` with nothing after the ':'.
    if (const std::string::size_type colon = token.find(':'); colon != std::string::npos) {
      const std::string_view base = std::string_view(token).substr(0, colon);
      if (const auto descriptor = registry::Lookup(base);
          descriptor.has_value() && descriptor->binding == registry::Binding::kLabel) {
        const LabelSpec spec = SplitLabelModifier(std::string_view(token).substr(colon + 1));
        if (spec.rest.empty()) {
          Fail(absl::StrCat("'", base, ":' needs a NAME"));
          return nullptr;
        }
        if (!ValidLabelName(spec.rest)) {
          Fail(absl::StrCat("'", base, ":", spec.rest, "' is not a NAME; use an identifier ([A-Za-z_][A-Za-z0-9_]*)"));
          return nullptr;
        }
        ++pos_;
        ExprPtr node = MakePredicate(*descriptor, {std::string(spec.rest)});
        if (node != nullptr) {
          node->label_override = spec.override_name;
        }
        return node;
      }
    }
    // A primary that declares Binding::kLabelRegex (-capture/-capturedir) carries an
    // attached :NAME[=REGEX] on its own token, then collects a command like -exec:
    // args = [NAME, REGEX (may be empty), cmd...]. The grammar is read from the
    // registry, not a hardcoded name list.
    if (const std::string::size_type colon = token.find(':'); colon != std::string::npos) {
      const std::string_view base = std::string_view(token).substr(0, colon);
      if (const auto descriptor = registry::Lookup(base);
          descriptor.has_value() && descriptor->binding == registry::Binding::kLabelRegex) {
        // [!]NAME[=REGEX]: `!` allows this node to re-bind a NAME an earlier -capture already bound,
        // per instance rather than through a whole-run flag that would loosen every -capture at once.
        const LabelSpec modifier = SplitLabelModifier(std::string_view(token).substr(colon + 1));
        const std::string spec(modifier.rest);
        const std::string::size_type spec_eq = spec.find('=');
        std::string name = spec_eq == std::string::npos ? spec : spec.substr(0, spec_eq);
        std::string regex = spec_eq == std::string::npos ? std::string() : spec.substr(spec_eq + 1);
        if (name.empty()) {
          Fail(absl::StrCat("'", base, ":' needs a NAME"));
          return nullptr;
        }
        if (!ValidLabelName(name)) {
          Fail(absl::StrCat("'", base, ":", name, "' is not a NAME; use an identifier ([A-Za-z_][A-Za-z0-9_]*)"));
          return nullptr;
        }
        ++pos_;
        std::vector<std::string> command;
        while (!AtEnd() && Peek() != ";") {
          command.push_back(tokens_[pos_++]);
        }
        if (AtEnd()) {
          Fail(absl::StrCat("'", base, "' is missing a ';' terminator"));
          return nullptr;
        }
        if (command.empty()) {
          Fail(absl::StrCat("'", base, "' needs a command before ';'"));
          return nullptr;
        }
        ++pos_;  // consume ';'
        std::vector<std::string> args;
        args.reserve(command.size() + 2);
        args.push_back(std::move(name));
        args.push_back(std::move(regex));
        for (std::string& cmd_token : command) {
          args.push_back(std::move(cmd_token));
        }
        ExprPtr node = MakePredicate(*descriptor, std::move(args));
        if (node != nullptr) {
          node->label_override = modifier.override_name;
        }
        return node;
      }
      // A Binding::kFormat primary (-grep) carries an attached :FORMAT output
      // template on its own token; the whole payload after the first ':' is the
      // template (it may itself contain '='), then the arity operand (the pattern)
      // follows. The template is compiled once here and stored on the node.
      if (const auto descriptor = registry::Lookup(base);
          descriptor.has_value() && descriptor->binding == registry::Binding::kFormat) {
        const std::string format = token.substr(colon + 1);
        ++pos_;  // consume the `<name>:FORMAT` token
        std::vector<std::string> args;
        for (int i = 0; i < descriptor->arity; ++i) {
          if (AtEnd()) {
            Fail(absl::StrCat("predicate '", base, "' is missing an argument"));
            return nullptr;
          }
          args.push_back(tokens_[pos_++]);
        }
        ExprPtr node = MakePredicate(*descriptor, std::move(args));
        if (node != nullptr) {
          node->grep_template = std::make_shared<const fields::Template>(fields::Template::Compile(format));
        }
        return node;
      }
      // A Binding::kStyle primary (-diff) carries an attached :STYLE token (u3/c/n/y/none),
      // then the arity operand (the TARGET template). The style is stored raw on the node and
      // validated in the evaluator; a bare -diff (no ':') falls through to the default (u3).
      if (const auto descriptor = registry::Lookup(base);
          descriptor.has_value() && descriptor->binding == registry::Binding::kStyle) {
        const std::string style = token.substr(colon + 1);
        // Syntactic check: empty (default u3), "none", or a format letter u/c/n/y with an
        // optional context count (u3, c5, n, y). The evaluator maps it to the mbo output.
        bool valid_style = style.empty() || style == "none";
        if (!valid_style && (style[0] == 'u' || style[0] == 'c' || style[0] == 'n' || style[0] == 'y')) {
          valid_style = true;
          for (std::size_t i = 1; i < style.size(); ++i) {
            if (style[i] < '0' || style[i] > '9') {
              valid_style = false;
              break;
            }
          }
        }
        if (!valid_style) {
          Fail(absl::StrCat("'", base, ":", style, "': unknown style (use u[N] / c[N] / n / y[N] / none)"));
          return nullptr;
        }
        ++pos_;  // consume the `<name>:STYLE` token
        std::vector<std::string> args;
        for (int i = 0; i < descriptor->arity; ++i) {
          if (AtEnd()) {
            Fail(absl::StrCat("predicate '", base, "' is missing an argument"));
            return nullptr;
          }
          args.push_back(tokens_[pos_++]);
        }
        ExprPtr node = MakePredicate(*descriptor, std::move(args));
        if (node != nullptr) {
          node->diff_style = style;
        }
        return node;
      }
      // A Binding::kHash primary carries an attached :ALGO[/ENCODING] token, then its arity operands
      // (none for -hash, the EXPECTED template for -hasheq). The spec is stored raw and validated
      // before the walk (engine::ValidateHashArgs); a bare `<name>` (no ':') falls through to the
      // default (--hash-algorithm / --hash-encoding).
      if (const auto descriptor = registry::Lookup(base);
          descriptor.has_value() && descriptor->binding == registry::Binding::kHash) {
        const std::string spec = token.substr(colon + 1);
        ++pos_;  // consume the `<name>:SPEC` token
        std::vector<std::string> args;
        for (int i = 0; i < descriptor->arity; ++i) {
          if (AtEnd()) {
            Fail(absl::StrCat("predicate '", base, "' is missing an argument"));
            return nullptr;
          }
          args.push_back(tokens_[pos_++]);
        }
        ExprPtr node = MakePredicate(*descriptor, std::move(args));
        if (node != nullptr) {
          node->hash_spec = spec;
        }
        return node;
      }
      // A Binding::kText primary (-text) carries an attached :FLAVOR token (git/posix/windows/apple)
      // and takes no operand. The flavor is validated here and stored raw on the node; a bare -text
      // (no ':') is not this branch -- it falls through to the default (the git heuristic).
      if (const auto descriptor = registry::Lookup(base);
          descriptor.has_value() && descriptor->binding == registry::Binding::kText) {
        const std::string flavor = token.substr(colon + 1);
        if (flavor != "git" && flavor != "posix" && flavor != "windows" && flavor != "apple") {
          Fail(absl::StrCat("'", base, ":", flavor, "': unknown text flavor (use git / posix / windows / apple)"));
          return nullptr;
        }
        ++pos_;  // consume the `-text:FLAVOR` token
        ExprPtr node = MakePredicate(*descriptor, {});
        if (node != nullptr) {
          node->text_flavor = flavor;
        }
        return node;
      }
      // A fuzzy primary carries an optional model, optionally followed by a normalized quality
      // threshold: `:MODEL`, `:MODEL:PCT%`, or the default-model shorthand `:PCT%`.
      if (const auto descriptor = registry::Lookup(base);
          descriptor.has_value() && descriptor->binding == registry::Binding::kFuzzy) {
        std::string_view value = std::string_view(token).substr(colon + 1);
        FuzzyModel model = FuzzyModel::kFzf;
        std::optional<int> threshold;
        const auto parse_model = [&](std::string_view model_name) -> bool {
          if (model_name == "fzf") {
            model = FuzzyModel::kFzf;
          } else if (model_name == "sequence") {
            model = FuzzyModel::kSequence;
          } else if (model_name == "levenshtein" || model_name == "edit") {
            model = FuzzyModel::kLevenshtein;
          } else if (model_name == "shingles") {
            model = FuzzyModel::kShingles;
          } else {
            Fail(
                absl::StrCat(
                    "'", token, "': unknown fuzzy model '", model_name,
                    "' (use fzf / sequence / levenshtein / shingles; edit aliases levenshtein)"));
            return false;
          }
          return true;
        };
        if (const std::size_t colon = value.find(':'); colon != std::string_view::npos) {
          if (!parse_model(value.substr(0, colon))) {
            return nullptr;
          }
          value.remove_prefix(colon + 1);
          if (value.empty()) {
            Fail(absl::StrCat("'", token, "': fuzzy threshold must follow ':'"));
            return nullptr;
          }
        } else if (!value.ends_with('%')) {
          if (!parse_model(value)) {
            return nullptr;
          }
          value = {};
        }
        if (!value.empty()) {
          if (!value.ends_with('%')) {
            Fail(absl::StrCat("'", token, "': fuzzy threshold must be [MODEL:]PCT% (0% through 100%)"));
            return nullptr;
          }
          value.remove_suffix(1);
          int percent = 0;
          if (value.empty()) {
            Fail(absl::StrCat("'", token, "': fuzzy threshold must be [MODEL:]PCT% (0% through 100%)"));
            return nullptr;
          }
          for (const char digit : value) {
            if (digit < '0' || digit > '9') {
              Fail(absl::StrCat("'", token, "': fuzzy threshold must be [MODEL:]PCT% (0% through 100%)"));
              return nullptr;
            }
            percent = (percent * 10) + (digit - '0');
          }
          if (percent > 100) {
            Fail(absl::StrCat("'", token, "': fuzzy threshold must be between 0% and 100%"));
            return nullptr;
          }
          threshold = percent;
        }
        ++pos_;
        if (AtEnd()) {
          Fail(absl::StrCat("predicate '", base, "' is missing an argument"));
          return nullptr;
        }
        ExprPtr node = MakePredicate(*descriptor, {tokens_[pos_++]});
        if (node != nullptr) {
          node->fuzzy_threshold = threshold;
          node->fuzzy_model = model;
        }
        return node;
      }
      // Content similarity carries a word-shingle width and/or a Jaccard threshold:
      // `:WIDTH`, `:PCT%`, or `:WIDTH:PCT%`. The bare defaults are width 5 and 80%.
      if (const auto descriptor = registry::Lookup(base);
          descriptor.has_value() && descriptor->binding == registry::Binding::kSimilarity) {
        const std::string_view value = std::string_view(token).substr(colon + 1);
        std::size_t width = kDefaultSimilarityShingleWidth;
        int threshold = kDefaultSimilarityThresholdPercent;
        const auto parse_threshold = [&](std::string_view text) -> bool {
          if (!text.ends_with('%')) {
            return false;
          }
          text.remove_suffix(1);
          int percent = 0;
          if (text.empty() || !absl::SimpleAtoi(text, &percent) || percent < 0 || percent > 100) {
            return false;
          }
          threshold = percent;
          return true;
        };
        if (const std::size_t separator = value.find(':'); separator != std::string_view::npos) {
          if (!absl::SimpleAtoi(value.substr(0, separator), &width) || width == 0
              || !parse_threshold(value.substr(separator + 1))) {
            Fail(absl::StrCat("'", token, "': similarity must be WIDTH:PCT% with WIDTH > 0 and PCT 0 through 100"));
            return nullptr;
          }
        } else if (value.ends_with('%')) {
          if (!parse_threshold(value)) {
            Fail(absl::StrCat("'", token, "': similarity threshold must be 0% through 100%"));
            return nullptr;
          }
        } else if (!absl::SimpleAtoi(value, &width) || width == 0) {
          Fail(absl::StrCat("'", token, "': similarity shingle width must be greater than zero"));
          return nullptr;
        }
        ++pos_;
        if (AtEnd()) {
          Fail(absl::StrCat("predicate '", base, "' is missing an argument"));
          return nullptr;
        }
        ExprPtr node = MakePredicate(*descriptor, {tokens_[pos_++]});
        if (node != nullptr) {
          node->similarity_width = width;
          node->similarity_threshold = threshold;
        }
        return node;
      }
    }
    const auto descriptor = registry::Lookup(token);
    if (!descriptor.has_value()) {
      Fail(absl::StrCat("unknown predicate: '", token, "'"));
      return nullptr;
    }
    // A binding primary used bare (no :NAME), e.g. "-capture": the registry binding
    // lets us reject it instead of silently accepting a nameless action.
    if (descriptor->binding == registry::Binding::kLabelRegex) {
      Fail(absl::StrCat("'", token, "' needs a :NAME (e.g. ", token, ":NAME)"));
      return nullptr;
    }
    if (descriptor->kind == registry::Kind::kOperator) {
      Fail(absl::StrCat("unexpected operator: '", token, "'"));
      return nullptr;
    }
    ++pos_;
    // -exec/-execdir take a variable-length command terminated by ';'.
    if (descriptor->arity < 0) {
      std::vector<std::string> command;
      while (!AtEnd() && Peek() != ";" && Peek() != "+") {
        command.push_back(tokens_[pos_++]);
      }
      if (AtEnd()) {
        Fail(absl::StrCat("'", token, "' is missing a ';' or '+' terminator"));
        return nullptr;
      }
      const bool batch = Peek() == "+";
      if (command.empty()) {
        Fail(absl::StrCat("'", token, "' needs a command before '", Peek(), "'"));
        return nullptr;
      }
      if (batch && descriptor->name != "-exec" && descriptor->name != "-execdir") {
        // Only -exec/-execdir batch; the interactive -ok/-okdir never take '+'.
        Fail(absl::StrCat("'", token, " ... +' is not supported; use ';'"));
        return nullptr;
      }
      if (batch && command.back() != "{}") {
        Fail(absl::StrCat("'", token, " ... +' requires '{}' as the last argument before '+'"));
        return nullptr;
      }
      ++pos_;  // consume ';' or '+'
      ExprPtr node = MakePredicate(*descriptor, std::move(command));
      if (node != nullptr) {
        node->exec_batch = batch;
      }
      return node;
    }
    std::vector<std::string> args;
    for (int i = 0; i < descriptor->arity; ++i) {
      if (AtEnd()) {
        Fail(absl::StrCat("predicate '", token, "' is missing an argument"));
        return nullptr;
      }
      args.push_back(tokens_[pos_++]);
    }
    return MakePredicate(*descriptor, std::move(args));
  }

  const std::vector<std::string>& tokens_;
  bool hoist_globals_ = true;
  std::vector<std::string> hoisted_globals_;
  std::vector<std::string> hoisted_meta_flags_;
  std::size_t pos_ = 0;
  absl::Status status_ = absl::OkStatus();
};

// Returns the first expression primary (pre-order, left to right in evaluation
// order) whose descriptor is tagged as an xff extension, or empty if the tree
// has none. The strict find-style check uses it to name the offending primary.
mbo::types::OptionalRef<const registry::Descriptor> FirstXffExtension(const Expr& expr) {
  if (expr.kind == Expr::Kind::kPredicate) {
    if (expr.descriptor.has_value() && expr.descriptor->style == registry::Style::kXff) {
      return *expr.descriptor;
    }
    return std::nullopt;
  }
  if (expr.lhs) {
    if (const auto found = FirstXffExtension(*expr.lhs); found.has_value()) {
      return found;
    }
  }
  return expr.rhs ? FirstXffExtension(*expr.rhs) : std::nullopt;
}

// Returns the first day-time predicate (-mtime/-atime/-ctime, pre-order) whose
// argument is the xff duration form -- a space-bearing span like "-3 weeks 3
// hours" -- or empty. The bare day count and the BSD unit suffix (-1h) never
// contain a space, so they stay find-compatible; only the word/compound span is
// an xff extension the strict find style rejects. See docs/design-find-flavors.md.
mbo::types::OptionalRef<const Expr> FirstXffDurationValue(const Expr& expr) {
  if (expr.kind == Expr::Kind::kPredicate) {
    const auto descriptor = expr.descriptor;
    if (!descriptor.has_value()) {
      return std::nullopt;
    }
    const std::string_view name = descriptor->name;
    const bool day_time = name == "-mtime" || name == "-atime" || name == "-ctime";
    if (day_time && !expr.args.empty() && absl::StrContains(expr.args.front(), ' ')) {
      return expr;
    }
    return std::nullopt;
  }
  if (expr.lhs) {
    if (const auto found = FirstXffDurationValue(*expr.lhs); found.has_value()) {
      return found;
    }
  }
  return expr.rhs ? FirstXffDurationValue(*expr.rhs) : std::nullopt;
}

// Returns the canonical name of the first xff-only logical operator node
// (-xor/-nand/-nor/-xnor, pre-order), or empty if none. These are interior nodes
// with no descriptor, so FirstXffExtension (which inspects predicate descriptors)
// cannot see them; the strict find style rejects them through this instead.
std::string_view FirstXffOperator(const Expr& expr) {
  switch (expr.kind) {
    case Expr::Kind::kNand: return "-nand";
    case Expr::Kind::kNor: return "-nor";
    case Expr::Kind::kXnor: return "-xnor";
    case Expr::Kind::kXor: return "-xor";
    default: break;
  }
  if (expr.lhs) {
    if (const std::string_view found = FirstXffOperator(*expr.lhs); !found.empty()) {
      return found;
    }
  }
  return expr.rhs ? FirstXffOperator(*expr.rhs) : std::string_view{};
}

// Returns the first -printf / -fprintf (pre-order) whose FORMAT uses the xff `%{field}`
// escape, or empty. -printf / -fprintf are find-native actions, but %{...} -- the bridge
// from their % format into the brace field vocabulary -- is an xff extension the strict
// find style rejects. (-printfln / -fprintfln are xff extensions already, so they are
// caught by FirstXffExtension; -fprintf takes FILE then FORMAT, so its format is arg 1.)
mbo::types::OptionalRef<const Expr> FirstXffPrintfField(const Expr& expr) {
  if (expr.kind == Expr::Kind::kPredicate) {
    const auto descriptor = expr.descriptor;
    if (descriptor.has_value()) {
      const std::string_view name = descriptor->name;
      const bool is_fprintf = name == "-fprintf";
      const std::size_t fmt_index = is_fprintf ? 1 : 0;
      if ((name == "-printf" || is_fprintf) && expr.args.size() > fmt_index
          && absl::StrContains(expr.args[fmt_index], "%{")) {
        return expr;
      }
    }
    return std::nullopt;
  }
  if (expr.lhs) {
    if (const auto found = FirstXffPrintfField(*expr.lhs); found.has_value()) {
      return found;
    }
  }
  return expr.rhs ? FirstXffPrintfField(*expr.rhs) : std::nullopt;
}

// The regex grammar for the command's matchers, from `--regextype=` (last occurrence wins). EXACT
// selects the literal engine, FNMATCH the shell-wildcard engine, PCRE2 the Perl engine; RE2 (and the
// reserved MATCH placeholder) stay RE2. This is lenient by design: an unknown or PCRE2-not-built-in
// value is left as RE2 and rejected by run.cc's ValidateRegextype (the validating reader) before the
// walk, so it never reaches a matcher.
regex::Grammar GrammarFromGlobalsInternal(const std::vector<std::string>& globals) {
  constexpr std::string_view kPrefix = "--regextype=";
  regex::Grammar grammar = regex::Grammar::kRe2;
  for (const std::string& global : globals) {
    if (!global.starts_with(kPrefix)) {
      continue;
    }
    const std::string_view value = std::string_view(global).substr(kPrefix.size());
    if (value == "EXACT") {
      grammar = regex::Grammar::kExact;
    } else if (value == "FNMATCH") {
      grammar = regex::Grammar::kFnmatch;
    } else if (value == "GLOB") {
      grammar = regex::Grammar::kGlob;
    } else if (value == "SHGLOB") {
      grammar = regex::Grammar::kShglob;
    } else if (value == "PCRE2") {
      grammar = regex::Grammar::kPcre2;
    } else {
      grammar = regex::Grammar::kRe2;  // RE2 / MATCH / unknown -> RE2 (run.cc validates the value)
    }
  }
  return grammar;
}

bool ConsumeLeadingJobsGlobal(
    const std::vector<std::string>& args,
    std::size_t& idx,
    std::vector<std::string>& globals) {
  const std::string& arg = args[idx];
  if (arg == "-j") {
    if (++idx >= args.size()) {
      globals.emplace_back("--jobs=");
      --idx;
    } else {
      globals.push_back(absl::StrCat("--jobs=", args[idx]));
    }
    return true;
  }
  if (arg.starts_with("-j=")) {
    globals.push_back(absl::StrCat("--jobs=", std::string_view(arg).substr(3)));
    return true;
  }
  if (arg.starts_with("-j") && arg.size() > 2) {
    globals.push_back(absl::StrCat("--jobs=", std::string_view(arg).substr(2)));
    return true;
  }
  return false;
}

}  // namespace

regex::Grammar GrammarFromGlobals(const std::vector<std::string>& globals) {
  return GrammarFromGlobalsInternal(globals);
}

absl::StatusOr<Command> Parse(const std::vector<std::string>& args) {
  Command cmd;
  std::size_t idx = 0;
  bool options_ended = false;

  // Leading globals: '-'/'+' tokens before the first root; a bare '--' ends option parsing.
  for (; idx < args.size(); ++idx) {
    const std::string& arg = args[idx];
    if (arg == "--") {
      ++idx;
      options_ended = true;
      break;
    }
    if (ConsumeLeadingJobsGlobal(args, idx, cmd.globals)) {
      continue;
    } else if (IsMetaFlag(arg)) {
      cmd.meta_flags.push_back(arg);
    } else if (!arg.empty() && (arg[0] == '-' || arg[0] == '+')) {
      cmd.globals.push_back(arg);
    } else {
      break;
    }
  }

  // Roots: operands until the expression begins. A double-dash global among the roots is hoisted
  // (globals are position-independent), so `a --sort=tree b` keeps both roots; an explicit `--`
  // disables that, taking every following token literally.
  for (; idx < args.size(); ++idx) {
    if (!options_ended && (IsHoistableGlobal(args[idx]) || IsMetaFlag(args[idx]))) {
      if (IsMetaFlag(args[idx])) {
        cmd.meta_flags.push_back(args[idx]);
      } else {
        cmd.globals.push_back(args[idx]);
      }
      continue;
    }
    if (StartsExpression(args[idx])) {
      break;
    }
    cmd.roots.push_back(args[idx]);
  }

  // The regex grammar (from --regextype) compiles every matcher, so resolve it from the globals seen
  // so far -- a leading / among-roots --regextype applies. A --regextype inside the expression is
  // hoisted below but, sitting after the patterns it would govern, does not retro-recompile them; it
  // belongs before the expression.
  cmd.grammar = GrammarFromGlobals(cmd.globals);

  // Expression: the remaining tokens, parsed to a tree. The parser hoists any double-dash globals it
  // meets at a primary/operator boundary (unless `--` ended options) -- so `. -type f --summary=ext`
  // works -- and we fold them back into the command's globals, then refresh the grammar.
  const std::vector<std::string> expr_tokens(args.begin() + static_cast<std::ptrdiff_t>(idx), args.end());
  if (!expr_tokens.empty()) {
    ExprParser parser(expr_tokens, /*hoist_globals=*/!options_ended);
    MBO_ASSIGN_OR_RETURN(cmd.expression, parser.Parse());
    cmd.globals.insert(cmd.globals.end(), parser.HoistedGlobals().begin(), parser.HoistedGlobals().end());
    cmd.meta_flags.insert(cmd.meta_flags.end(), parser.HoistedMetaFlags().begin(), parser.HoistedMetaFlags().end());
    cmd.grammar = GrammarFromGlobals(cmd.globals);
  }
  return cmd;
}

absl::Status EnforceStyle(const Command& command, registry::Style style) {
  if (style != registry::Style::kFind) {
    return absl::OkStatus();  // the xff style accepts the full vocabulary
  }
  const auto expression = AsConstOptionalExpr(command.expression);
  if (!expression.has_value()) {
    return absl::OkStatus();
  }
  if (const auto ext = FirstXffExtension(*expression); ext.has_value()) {
    return absl::InvalidArgumentError(
        absl::StrCat(
            "'", ext->name,
            "' is an xff extension, not available under the find style (--config=find); use --config=xff"));
  }
  if (const std::string_view op = FirstXffOperator(*expression); !op.empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat(
            "'", op, "' is an xff extension, not available under the find style (--config=find); use --config=xff"));
  }
  if (const auto dur = FirstXffDurationValue(*expression); dur.has_value()) {
    return absl::InvalidArgumentError(
        absl::StrCat(
            "the duration form of '", dur->descriptor->name,
            "' (e.g. \"-3 weeks 3 hours\") is an xff extension, not available under the find style (--config=find); "
            "use a day count, a unit suffix like -1h, or --config=xff"));
  }
  if (const auto pf = FirstXffPrintfField(*expression); pf.has_value()) {
    return absl::InvalidArgumentError(
        absl::StrCat(
            "the '%{field}' escape in '", pf->descriptor->name,
            "' is an xff extension, not available under the find style (--config=find); use --config=xff"));
  }
  return absl::OkStatus();
}

CaseMode ResolveCaseMode(const std::vector<std::string>& globals, registry::Style style) {
  // The opinionated style (rg) defaults to smart-case; find/xff to sensitive.
  CaseMode mode = style == registry::Style::kRg ? CaseMode::kSmart : CaseMode::kSensitive;
  for (const std::string& global : globals) {
    if (global == "-i" || global == "--case=insensitive") {
      mode = CaseMode::kInsensitive;
    } else if (global == "-s" || global == "-s+" || global == "--case=smart") {
      mode = CaseMode::kSmart;
    } else if (global == "-s-" || global == "--case=sensitive") {
      mode = CaseMode::kSensitive;
    }
  }
  return mode;
}

namespace {

// A pattern is "cased" -- forcing case-sensitive under smart mode -- if it has an ASCII
// uppercase letter (the smart-case rule shared by rg / fd).
bool HasUpperAscii(std::string_view pattern) {
  return absl::c_any_of(pattern, [](char ch) { return ch >= 'A' && ch <= 'Z'; });
}

// Whether a case-sensitive matcher should fold under `mode` (the -i variants already fold
// and never reach here). kSmart folds only an all-lowercase pattern.
bool ShouldFold(CaseMode mode, std::string_view pattern) {
  switch (mode) {
    case CaseMode::kInsensitive: return true;
    case CaseMode::kSensitive: return false;
    case CaseMode::kSmart: return !HasUpperAscii(pattern);
  }
  return false;
}

// Applies final case semantics and compiles each regex exactly once, after the
// complete configuration/CLI stream has selected the grammar and case mode.
void BindMatchersToNode(Expr& expr, CaseMode mode, regex::Grammar grammar) {
  if (expr.kind == Expr::Kind::kPredicate && expr.descriptor.has_value()) {
    const std::string_view name = expr.descriptor->name;
    const bool glob_or_content = name == "-name" || name == "-path" || name == "-lname" || name == "-content"
                                 || name == "-fuzzy" || name == "-fuzzypath";
    if (glob_or_content && !expr.args.empty()) {
      expr.case_fold = ShouldFold(mode, expr.args.front());
    }
    if (const std::optional<std::string_view> pattern = NodeRegexPattern(*expr.descriptor, expr.args);
        pattern.has_value()) {
      const bool case_insensitive =
          expr.descriptor->fold_case
          || (expr.descriptor->binding != registry::Binding::kLabelRegex && ShouldFold(mode, *pattern));
      if (absl::StatusOr<regex::Matcher> matcher = regex::Matcher::Compile(*pattern, case_insensitive, grammar);
          matcher.ok()) {
        expr.matcher = std::make_shared<const regex::Matcher>(*std::move(matcher));
      }
    }
  }
  if (expr.lhs) {
    BindMatchersToNode(*expr.lhs, mode, grammar);
  }
  if (expr.rhs) {
    BindMatchersToNode(*expr.rhs, mode, grammar);
  }
}

}  // namespace

void BindMatchers(Command& command, regex::Grammar grammar, CaseMode mode) {
  command.grammar = grammar;
  if (auto expression = AsOptionalExpr(command.expression); expression.has_value()) {
    BindMatchersToNode(*expression, mode, grammar);
  }
}

namespace {

bool ExpressionTakesTerminal(const Expr& expr) {
  if (expr.descriptor.has_value() && expr.descriptor->terminal) {
    return true;
  }
  return (expr.lhs && ExpressionTakesTerminal(*expr.lhs)) || (expr.rhs && ExpressionTakesTerminal(*expr.rhs));
}

}  // namespace

bool TakesTerminal(const Command& command) {
  const auto expression = AsConstOptionalExpr(command.expression);
  return expression.has_value() && ExpressionTakesTerminal(*expression);
}

}  // namespace xff::parser
