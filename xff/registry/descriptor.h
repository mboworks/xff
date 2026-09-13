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

#ifndef XFF_REGISTRY_DESCRIPTOR_H_
#define XFF_REGISTRY_DESCRIPTOR_H_

#include <string_view>

namespace xff::registry {

// Where a token lives on the command line (design.md "CLI grammar & parser").
enum class Region { kGlobal, kExpression };

// What an expression token is.
enum class Kind { kTest, kAction, kOperator };

// Safety classification, surfaced in --help / --explain (design.md "Security & safety").
enum class Safety { kNone, kSafety, kSecurity };

// Cost tier driving the advisory ordering warning (design.md "Evaluation").
enum class Cost { kCheap, kMeta, kExpensive };

// How a primary carries an attached ':' qualification on its own token, so the parser
// reads the grammar from the registry instead of hardcoding names (design #68):
// kNone for most; kLabelRegex for -capture/-capturedir (-capture:NAME[=REGEX]);
// kFormat for -grep (-grep:FORMAT, an attached output template); kStyle for -diff
// (-diff:STYLE, an attached output-style token like u3 / c / n / y / none); kHash for
// -hash (-hash:ALGO[/ENCODING], the digest algorithm and hex/base64 rendering); kText for
// -text (-text:git|posix|windows|apple, the text-definition / line-ending flavor);
// kFuzzy for fuzzy predicates (-fuzzy:MODEL[:PCT%] PATTERN, model + optional quality threshold);
// kSimilarity for -similar[:WIDTH[:PCT%]] TARGET (word-shingle width + Jaccard threshold).
enum class Binding { kNone, kLabel, kLabelRegex, kFormat, kStyle, kHash, kText, kFuzzy, kSimilarity };

// The active command style, and (for kFind/kXff) a primary's origin. As a primary
// tag: kFind = find-native, kXff = an xff extension; the strict find style
// (--config=find) rejects xff extensions, the xff style accepts all. The default is
// kFind, so only xff-native primaries need tagging. kXff is find-evolved but
// conservative (find-like visibility: shows hidden, ignore files off). kRg
// (--config=rg) is a config-only style, never a descriptor tag: it uses the full
// xff vocabulary but swaps in opinionated defaults (respect .gitignore/.ignore,
// skip hidden, smart case). It is the single opinionated style. Vocabulary is accepted exactly
// like kXff (everything but kFind accepts all).
enum class Style { kFind, kXff, kRg };

// One option / predicate / action description. The registry is the single
// source of truth from which the parser, --help, completions, --explain, and
// the cost-warning are all derived.
struct Descriptor {
  std::string_view name;
  // Optional alternate spelling resolved to this SAME descriptor. Keeping aliases on the canonical
  // entry makes parser behavior, generated help, style/cost/safety metadata, and dispatch identical
  // by construction rather than duplicating a near-identical descriptor.
  std::string_view alias;
  // One-line synopsis for `xff help <name>` and the generated --help listing: a
  // short phrase, normally lower-case (proper nouns such as GNU/BSD aside), with no
  // trailing period (e.g. "match the basename against a shell glob"). Every
  // descriptor carries one; registry_test enforces the shape.
  std::string_view summary;
  // Optional multi-sentence explanation shown by the detailed tiers (`--help=NAME` for this
  // primary, and `--help=full`); empty falls back to the summary alone. Full prose: arguments, how
  // it behaves, and ideally a short example. The counterpart of cli::GlobalFlag::details, so the
  // detailed reference is generated from the registry SOT and cannot drift. A trivial primary whose
  // summary already says everything (e.g. -true, -a) may leave this empty.
  std::string_view details;
  Kind kind = Kind::kTest;
  Region region = Region::kExpression;
  int arity = 0;  // trailing tokens consumed as arguments (-1 = variadic until ';')
  // Optional closed vocabulary for each operand, comma-delimited. Empty means unrestricted.
  std::string_view argument_choices;
  bool argument_choice_list = false;  // an operand may be a non-empty comma list of choices
  Binding binding = Binding::kNone;   // attached ':' qualification carried on the token itself
  // The case-insensitive variant of a matcher (-iname/-ipath/-iregex): folds case
  // when matching (FNM_CASEFOLD for the glob tests, RE2 case-insensitive for the
  // regex). Lets the parser/evaluator read case from the registry instead of
  // keying off the leading 'i' in the primary's name.
  bool fold_case = false;
  Safety safety = Safety::kNone;
  Style style = Style::kFind;  // find-native by default; set kXff to mark an xff extension
  Cost cost = Cost::kCheap;
  bool pure = true;  // side-effect-free (reorderable within a conjunction)
  // The help topic (--help=TOPIC) this primary belongs to, or empty for none. The counterpart of
  // cli::GlobalFlag::topic: a topic page pulls its family from this tag, so the list cannot drift.
  std::string_view topic;
  // The primary may take the TERMINAL for itself: it prompts and reads a reply (-ok / -okdir), or it
  // hands our stdin / stdout to a child that might (an editor under -exec / -execdir). Read by the
  // CLI to suppress the listing pager, which would otherwise sit between that primary and the user.
  bool terminal = false;
};

template<typename Sink>
void AbslStringify(Sink& sink, const Descriptor& descriptor) {
  sink.Append(descriptor.name);
}

}  // namespace xff::registry

#endif  // XFF_REGISTRY_DESCRIPTOR_H_
