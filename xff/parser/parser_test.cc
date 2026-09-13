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

#include <array>
#include <memory>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/status.h"
#include "xff/matching/regex/regex.h"
#include "xff/parser/ast.h"
#include "xff/registry/descriptor.h"

namespace xff::parser {
namespace {

using ::mbo::testing::IsOk;
using ::mbo::testing::StatusIs;
using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsFalse;
using ::testing::IsNull;
using ::testing::IsTrue;
using ::testing::NotNull;
using ::testing::Optional;

struct ParserTest : ::testing::Test {};

struct OptionalExprTest : ::testing::Test {};

TEST_F(OptionalExprTest, MutableProjectionPreservesThePointee) {
  const std::unique_ptr<Expr> expr = std::make_unique<Expr>();

  mbo::types::OptionalRef<Expr> ref = AsOptionalExpr(expr);
  ref->case_fold = true;

  EXPECT_THAT(expr->case_fold, IsTrue());
}

TEST_F(OptionalExprTest, ConstProjectionAcceptsMutableAndConstPointees) {
  const std::unique_ptr<Expr> mutable_expr = std::make_unique<Expr>();
  std::unique_ptr<Expr> owned_const_expr = std::make_unique<Expr>();
  owned_const_expr->case_fold = true;
  const std::unique_ptr<const Expr> const_expr = std::move(owned_const_expr);

  const mbo::types::OptionalRef<const Expr> mutable_view = AsConstOptionalExpr(mutable_expr);
  const mbo::types::OptionalRef<const Expr> const_view = AsConstOptionalExpr(const_expr);

  EXPECT_THAT(mutable_view->case_fold, IsFalse());
  EXPECT_THAT(const_view->case_fold, IsTrue());
}

TEST_F(OptionalExprTest, EmptyOwnersProduceEmptyReferences) {
  const std::unique_ptr<Expr> mutable_expr;
  const std::unique_ptr<const Expr> const_expr;

  EXPECT_THAT(AsOptionalExpr(mutable_expr) == std::nullopt, IsTrue());
  EXPECT_THAT(AsConstOptionalExpr(mutable_expr) == std::nullopt, IsTrue());
  EXPECT_THAT(AsConstOptionalExpr(const_expr) == std::nullopt, IsTrue());
}

TEST_F(ParserTest, GlobalsRootsExpression) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({"--color", ".", "-type", "f"}));
  EXPECT_THAT(cmd.globals, ElementsAre("--color"));
  EXPECT_THAT(cmd.roots, ElementsAre("."));
  ASSERT_THAT(cmd.expression, NotNull());
  EXPECT_THAT(cmd.expression->kind, Expr::Kind::kPredicate);
  ASSERT_THAT(cmd.expression->descriptor, Optional(_));
  EXPECT_THAT(cmd.expression->descriptor->name, "-type");
  EXPECT_THAT(cmd.expression->args, ElementsAre("f"));
}

TEST_F(ParserTest, JobsShortFormsCanonicalizeToTheLongGlobal) {
  ASSERT_OK_AND_ASSIGN(const Command spaced, Parse({"-j", "4", "."}));
  EXPECT_THAT(spaced.globals, ElementsAre("--jobs=4"));
  ASSERT_OK_AND_ASSIGN(const Command equals, Parse({"-j=4", "."}));
  EXPECT_THAT(equals.globals, ElementsAre("--jobs=4"));
  ASSERT_OK_AND_ASSIGN(const Command attached, Parse({"-j4", "."}));
  EXPECT_THAT(attached.globals, ElementsAre("--jobs=4"));
  ASSERT_OK_AND_ASSIGN(const Command attached_all, Parse({"-jall", "."}));
  EXPECT_THAT(attached_all.globals, ElementsAre("--jobs=all"));
  ASSERT_OK_AND_ASSIGN(const Command missing, Parse({"-j"}));
  EXPECT_THAT(missing.globals, ElementsAre("--jobs="));
}

TEST_F(ParserTest, DoubleDashGlobalsHoistFromAfterRootsAndTheExpression) {
  // A `--` global is unambiguous (every primary/operator is single-dash), so it is hoisted to
  // globals wherever it appears: after the roots, at the tail, and interspersed among operators.
  ASSERT_OK_AND_ASSIGN(const Command after, Parse({".", "--summary=ext", "-type", "f"}));
  EXPECT_THAT(after.globals, ElementsAre("--summary=ext"));
  EXPECT_THAT(after.roots, ElementsAre("."));
  EXPECT_THAT(after.expression->descriptor->name, "-type");

  ASSERT_OK_AND_ASSIGN(const Command tail, Parse({".", "-type", "f", "--summary=ext"}));
  EXPECT_THAT(tail.globals, ElementsAre("--summary=ext"));
  EXPECT_THAT(tail.expression->descriptor->name, "-type");

  ASSERT_OK_AND_ASSIGN(const Command mid, Parse({".", "-type", "f", "--sort=tree", "-o", "-name", "x"}));
  EXPECT_THAT(mid.globals, ElementsAre("--sort=tree"));
  EXPECT_THAT(mid.expression->kind, Expr::Kind::kOr);
}

TEST_F(ParserTest, DoubleDashGlobalHoistsFromBetweenRoots) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({"a", "--sort=tree", "b", "-type", "f"}));
  EXPECT_THAT(cmd.globals, ElementsAre("--sort=tree"));
  EXPECT_THAT(cmd.roots, ElementsAre("a", "b"));
}

TEST_F(ParserTest, DoubleDashInsideAnExecCommandIsNotHoisted) {
  // A --flag between -exec and its ';' belongs to the child command, not xff: it stays an argument.
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-type", "f", "-exec", "echo", "--summary=x", "{}", ";"}));
  EXPECT_THAT(cmd.globals, ElementsAre());  // nothing hoisted out of the exec args
  ASSERT_THAT(cmd.expression, NotNull());
  EXPECT_THAT(cmd.expression->kind, Expr::Kind::kAnd);  // -type f AND -exec ...
}

TEST_F(ParserTest, MetaFlagsHoistOnlyAtParserBoundaries) {
  ASSERT_OK_AND_ASSIGN(const Command leading, Parse({"--help=archive"}));
  EXPECT_THAT(leading.meta_flags, ElementsAre("--help=archive"));
  EXPECT_THAT(leading.expression, IsNull());

  ASSERT_OK_AND_ASSIGN(const Command tail, Parse({".", "-type", "f", "-h"}));
  EXPECT_THAT(tail.meta_flags, ElementsAre("-h"));
  ASSERT_THAT(tail.expression, NotNull());
  EXPECT_THAT(tail.expression->descriptor->name, "-type");

  ASSERT_OK_AND_ASSIGN(const Command exec, Parse({".", "-exec", "echo", "--help", "-version", "{}", ";"}));
  EXPECT_THAT(exec.meta_flags, ElementsAre());
  EXPECT_THAT(exec.expression->args, ElementsAre("echo", "--help", "-version", "{}"));

  ASSERT_OK_AND_ASSIGN(const Command html, Parse({".", "-type", "f", "--help=full:html"}));
  EXPECT_THAT(html.meta_flags, ElementsAre("--help=full:html"));
  ASSERT_THAT(html.expression, NotNull());
}

TEST_F(ParserTest, LeadingEndOfOptionsDisablesGlobalHoisting) {
  // A bare `--` stops option parsing, so a later --summary is taken literally (an unknown predicate),
  // not hoisted -- if it had been hoisted the parse would have succeeded.
  EXPECT_THAT(Parse({"--", ".", "-type", "f", "--summary=ext"}), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ParserTest, NoExpressionIsNull) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({"."}));
  EXPECT_THAT(cmd.roots, ElementsAre("."));
  EXPECT_THAT(cmd.expression, IsNull());
}

TEST_F(ParserTest, ShortNameAndPathAliasesResolveToCanonicalPrimaries) {
  ASSERT_OK_AND_ASSIGN(const Command command, Parse({".", "-n", "*.cc", "-a", "-p", "src/*"}));
  const Expr& root = *command.expression;
  ASSERT_THAT(root.kind, Expr::Kind::kAnd);
  ASSERT_THAT(root.lhs->kind, Expr::Kind::kPredicate);
  EXPECT_THAT(root.lhs->descriptor->name, Eq("-name"));
  EXPECT_THAT(root.lhs->args, ElementsAre("*.cc"));
  ASSERT_THAT(root.rhs->kind, Expr::Kind::kPredicate);
  EXPECT_THAT(root.rhs->descriptor->name, Eq("-path"));
  EXPECT_THAT(root.rhs->args, ElementsAre("src/*"));
}

TEST_F(ParserTest, OrIsLowerThanImplicitAnd) {
  // `-type f -name x -o -name y` => Or( And(-type f, -name x), -name y )
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-type", "f", "-name", "x", "-o", "-name", "y"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Expr::Kind::kOr);
  EXPECT_THAT(root.lhs->kind, Expr::Kind::kAnd);
  ASSERT_THAT(root.rhs->kind, Expr::Kind::kPredicate);
  EXPECT_THAT(root.rhs->descriptor->name, "-name");
}

TEST_F(ParserTest, NotBindsTightest) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "!", "-type", "d"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Expr::Kind::kNot);
  ASSERT_THAT(root.lhs->kind, Expr::Kind::kPredicate);
  EXPECT_THAT(root.lhs->descriptor->name, "-type");
}

TEST_F(ParserTest, ParensGroup) {
  // `( -type f -o -type d ) -print` => And( Or(...), -print )
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "(", "-type", "f", "-o", "-type", "d", ")", "-print"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Expr::Kind::kAnd);
  EXPECT_THAT(root.lhs->kind, Expr::Kind::kOr);
  ASSERT_THAT(root.rhs->kind, Expr::Kind::kPredicate);
  EXPECT_THAT(root.rhs->descriptor->name, "-print");
}

TEST_F(ParserTest, CommaIsLowestPrecedence) {
  // `-type f -o -type d , -name x` => Comma( Or(-type f, -type d), -name x )
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-type", "f", "-o", "-type", "d", ",", "-name", "x"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Expr::Kind::kComma);
  EXPECT_THAT(root.lhs->kind, Expr::Kind::kOr);
  ASSERT_THAT(root.rhs->kind, Expr::Kind::kPredicate);
  EXPECT_THAT(root.rhs->descriptor->name, "-name");
}

TEST_F(ParserTest, ExecCollectsCommandUntilSemicolon) {
  // `-exec echo {} ; -print` => And( -exec[echo, {}], -print ); the ';' is consumed.
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-exec", "echo", "{}", ";", "-print"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Expr::Kind::kAnd);
  ASSERT_THAT(root.lhs->kind, Expr::Kind::kPredicate);
  EXPECT_THAT(root.lhs->descriptor->name, "-exec");
  EXPECT_THAT(root.lhs->args, ElementsAre("echo", "{}"));
  EXPECT_THAT(root.rhs->descriptor->name, "-print");
}

TEST_F(ParserTest, ExecWithoutTerminatorErrors) {
  EXPECT_THAT(Parse({".", "-exec", "echo", "{}"}), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ParserTest, ExecPlusMarksBatchAndKeepsCommand) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-exec", "echo", "{}", "+"}));
  const Expr& root = *cmd.expression;
  EXPECT_THAT(root.descriptor->name, "-exec");
  EXPECT_THAT(root.exec_batch, IsTrue());
  EXPECT_THAT(root.args, ElementsAre("echo", "{}"));
}

TEST_F(ParserTest, ExecSemicolonIsNotBatch) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-exec", "echo", "{}", ";"}));
  EXPECT_THAT(cmd.expression->exec_batch, IsFalse());
}

TEST_F(ParserTest, ExecPlusRequiresTrailingBrace) {
  EXPECT_THAT(Parse({".", "-exec", "echo", "+"}), StatusIs(absl::StatusCode::kInvalidArgument));  // no {}
  EXPECT_THAT(
      Parse({".", "-exec", "echo", "{}", "x", "+"}), StatusIs(absl::StatusCode::kInvalidArgument));  // {} not last
}

TEST_F(ParserTest, ExecdirPlusMarksBatch) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-execdir", "echo", "{}", "+"}));
  EXPECT_THAT(cmd.expression->descriptor->name, "-execdir");
  EXPECT_THAT(cmd.expression->exec_batch, IsTrue());
}

TEST_F(ParserTest, OkPlusNotSupported) {
  // The interactive -ok/-okdir never take the '+' batch form.
  EXPECT_THAT(Parse({".", "-ok", "echo", "{}", "+"}), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ParserTest, CaptureCollectsNameRegexAndCommand) {
  // -capture:NAME[=REGEX] cmd... ; => args = [NAME, REGEX (may be empty), cmd...].
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-capture:lines", "wc", "-l", "{}", ";", "-print"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Expr::Kind::kAnd);
  ASSERT_THAT(root.lhs->kind, Expr::Kind::kPredicate);
  EXPECT_THAT(root.lhs->descriptor->name, "-capture");
  EXPECT_THAT(root.lhs->args, ElementsAre("lines", "", "wc", "-l", "{}"));  // empty regex slot
  EXPECT_THAT(root.rhs->descriptor->name, "-print");
}

TEST_F(ParserTest, CaptureExtractionRegexInSpec) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-capture:n=([0-9]+)", "wc", ";"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Expr::Kind::kPredicate);
  EXPECT_THAT(root.descriptor->name, "-capture");
  EXPECT_THAT(root.args, ElementsAre("n", "([0-9]+)", "wc"));  // NAME, REGEX, command
}

TEST_F(ParserTest, CaptureErrors) {
  using ::absl::StatusCode;
  EXPECT_THAT(Parse({".", "-capture:x", "echo"}), StatusIs(StatusCode::kInvalidArgument));      // no ';'
  EXPECT_THAT(Parse({".", "-capture:", "echo", ";"}), StatusIs(StatusCode::kInvalidArgument));  // no NAME
  EXPECT_THAT(Parse({".", "-capture:x", ";"}), StatusIs(StatusCode::kInvalidArgument));         // no command
  EXPECT_THAT(Parse({".", "-capture", "echo", ";"}), StatusIs(StatusCode::kInvalidArgument));   // bare, missing :NAME
}

TEST_F(ParserTest, OldEqualsQualifierSpellingsPointToColon) {
  constexpr std::array kOldSpellings = {
      "-text=posix", "-collect=all", "-fuzzy=fzf",  "-fuzzypath=80%",      "-ifuzzy=sequence", "-ifuzzypath=edit",
      "-diff=u3",    "-hash=sha256", "-hasheq=md5", "-grep={line}:{text}", "-capture=name",    "-capturedir=name",
  };
  for (const std::string_view spelling : kOldSpellings) {
    const std::string expected = absl::StrCat(
        "use '", spelling.substr(0, spelling.find('=')), ":", spelling.substr(spelling.find('=') + 1), "'");
    EXPECT_THAT(Parse({".", std::string(spelling)}), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr(expected)));
  }
}

TEST_F(ParserTest, TypeChoicesRejectUnknownAndEmptyListMembers) {
  static constexpr std::array kInvalid = std::to_array<std::string_view>({
      "garbage",
      "z",
      "",
      "f,",
      ",f",
      "f,,d",
      "f,z",
      "fd",
  });
  static constexpr std::array kFlags = std::to_array<std::string_view>({"-type", "-xtype"});
  for (const std::string_view flag : kFlags) {
    for (const std::string_view value : kInvalid) {
      SCOPED_TRACE(absl::StrCat(flag, " ", value));
      EXPECT_THAT(
          Parse({".", std::string(flag), std::string(value)}),
          StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("accepted: b,c,d,f,l,p,s")));
    }
    EXPECT_THAT(Parse({".", std::string(flag), "b,c,d,f,l,p,s"}), IsOk());
  }
}

TEST_F(ParserTest, Errors) {
  using ::absl::StatusCode;
  EXPECT_THAT(Parse({".", "-bogus"}), StatusIs(StatusCode::kInvalidArgument));              // unknown predicate
  EXPECT_THAT(Parse({".", "-name"}), StatusIs(StatusCode::kInvalidArgument));               // missing argument
  EXPECT_THAT(Parse({".", "(", "-type", "f"}), StatusIs(StatusCode::kInvalidArgument));     // unbalanced '('
  EXPECT_THAT(Parse({".", "-o", "-type", "f"}), StatusIs(StatusCode::kInvalidArgument));    // leading operator
  EXPECT_THAT(Parse({".", "-xor", "-name", "x"}), StatusIs(StatusCode::kInvalidArgument));  // leading xff operator
  EXPECT_THAT(Parse({".", "-name", "x", "-nor"}), StatusIs(StatusCode::kInvalidArgument));  // operator missing rhs
}

TEST_F(ParserTest, ResolveCaseModeDefaultsAndFlags) {
  // Style defaults: find/xff sensitive, the opinionated style (rg) smart.
  EXPECT_THAT(ResolveCaseMode({}, registry::Style::kFind), CaseMode::kSensitive);
  EXPECT_THAT(ResolveCaseMode({}, registry::Style::kXff), CaseMode::kSensitive);
  EXPECT_THAT(ResolveCaseMode({}, registry::Style::kRg), CaseMode::kSmart);
  // Flags override the default; last occurrence wins.
  EXPECT_THAT(ResolveCaseMode({"-i"}, registry::Style::kFind), CaseMode::kInsensitive);
  EXPECT_THAT(ResolveCaseMode({"-s"}, registry::Style::kFind), CaseMode::kSmart);
  EXPECT_THAT(ResolveCaseMode({"-s+"}, registry::Style::kFind), CaseMode::kSmart);
  EXPECT_THAT(ResolveCaseMode({"-s-"}, registry::Style::kRg), CaseMode::kSensitive);
  EXPECT_THAT(ResolveCaseMode({"--case=insensitive"}, registry::Style::kFind), CaseMode::kInsensitive);
  EXPECT_THAT(ResolveCaseMode({"--case=smart"}, registry::Style::kFind), CaseMode::kSmart);
  EXPECT_THAT(ResolveCaseMode({"--case=sensitive"}, registry::Style::kRg), CaseMode::kSensitive);
  EXPECT_THAT(ResolveCaseMode({"-i", "-s-"}, registry::Style::kFind), CaseMode::kSensitive);  // last wins
}

TEST_F(ParserTest, BindMatchersFoldsSensitiveMatchers) {
  // smart: an all-lowercase glob folds; an uppercase-bearing pattern stays exact.
  ASSERT_OK_AND_ASSIGN(Command lower, Parse({".", "-name", "readme"}));
  BindMatchers(lower, lower.grammar, CaseMode::kSmart);
  EXPECT_THAT(lower.expression->case_fold, IsTrue());
  ASSERT_OK_AND_ASSIGN(Command upper, Parse({".", "-name", "README"}));
  BindMatchers(upper, upper.grammar, CaseMode::kSmart);
  EXPECT_THAT(upper.expression->case_fold, IsFalse());
  // insensitive: folds regardless of pattern case.
  ASSERT_OK_AND_ASSIGN(Command ins, Parse({".", "-name", "README"}));
  BindMatchers(ins, ins.grammar, CaseMode::kInsensitive);
  EXPECT_THAT(ins.expression->case_fold, IsTrue());
  // The -i variant already folds (descriptor.fold_case), so it is left untouched.
  ASSERT_OK_AND_ASSIGN(Command iname, Parse({".", "-iname", "README"}));
  BindMatchers(iname, iname.grammar, CaseMode::kInsensitive);
  EXPECT_THAT(iname.expression->case_fold, IsFalse());
  // sensitive is a no-op.
  ASSERT_OK_AND_ASSIGN(Command sens, Parse({".", "-name", "readme"}));
  BindMatchers(sens, sens.grammar, CaseMode::kSensitive);
  EXPECT_THAT(sens.expression->case_fold, IsFalse());

  ASSERT_OK_AND_ASSIGN(Command fuzzy, Parse({".", "-fuzzy", "readme"}));
  BindMatchers(fuzzy, fuzzy.grammar, CaseMode::kSmart);
  EXPECT_THAT(fuzzy.expression->case_fold, IsTrue());
}

TEST_F(ParserTest, BindMatchersCompilesRegexOnceWithFinalCaseMode) {
  // Parsing records the pattern but does not bind an engine. The final smart-case mode compiles
  // the matcher case-insensitively once, so it matches an uppercase path.
  ASSERT_OK_AND_ASSIGN(Command cmd, Parse({".", "-regex", ".*readme.*"}));
  EXPECT_THAT(cmd.expression->matcher, IsNull());
  BindMatchers(cmd, cmd.grammar, CaseMode::kSmart);
  ASSERT_THAT(cmd.expression->matcher, NotNull());
  EXPECT_THAT(cmd.expression->matcher->PartialMatch("/x/README.txt"), IsTrue());  // folded after
}

TEST_F(ParserTest, BindMatchersCompilesOptionalCaptureRegexWithoutApplyingGlobalCaseMode) {
  ASSERT_OK_AND_ASSIGN(Command capture, Parse({".", "-capture:n=([a-z]+)", "printf", ";"}));
  BindMatchers(capture, regex::Grammar::kRe2, CaseMode::kInsensitive);
  ASSERT_THAT(capture.expression->matcher, NotNull());
  EXPECT_THAT(capture.expression->matcher->FullMatch("lower"), IsTrue());
  EXPECT_THAT(capture.expression->matcher->FullMatch("UPPER"), IsFalse());

  ASSERT_OK_AND_ASSIGN(Command without_regex, Parse({".", "-capture:n", "printf", ";"}));
  BindMatchers(without_regex, regex::Grammar::kRe2, CaseMode::kInsensitive);
  EXPECT_THAT(without_regex.expression->matcher, IsNull());
}

TEST_F(ParserTest, EnforceStyleRejectsXffExtensionUnderFind) {
  // The strict find style (--config=find) refuses an xff-only primary, naming it
  // and pointing at the escape hatch.
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-println"}));
  const absl::Status status = EnforceStyle(cmd, registry::Style::kFind);
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(status.message(), HasSubstr("-println"));
  EXPECT_THAT(status.message(), HasSubstr("--config=xff"));
}

TEST_F(ParserTest, EnforceStyleRejectsFileWritingLineEndingActionsUnderFind) {
  // -fprintln / -fprintfln are the file-writing counterparts of -println / -printfln,
  // so they are xff extensions the find style rejects (their bases -fprint / -fprintf
  // stay find-native).
  ASSERT_OK_AND_ASSIGN(const Command ln, Parse({".", "-fprintln", "out"}));
  EXPECT_THAT(EnforceStyle(ln, registry::Style::kFind), StatusIs(absl::StatusCode::kInvalidArgument));
  ASSERT_OK_AND_ASSIGN(const Command fln, Parse({".", "-fprintfln", "out", "%p"}));
  EXPECT_THAT(EnforceStyle(fln, registry::Style::kFind), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(EnforceStyle(ln, registry::Style::kXff), IsOk());
  EXPECT_THAT(EnforceStyle(fln, registry::Style::kXff), IsOk());
}

TEST_F(ParserTest, EnforceStyleRejectsPrintfFieldEscapeUnderFind) {
  // -printf / -fprintf are find-native, but their xff `%{field}` escape is not: the strict
  // find style rejects a format that uses it (while a plain % format stays fine). -fprintf
  // takes FILE then FORMAT, so the escape is checked in its second argument.
  ASSERT_OK_AND_ASSIGN(const Command pf, Parse({".", "-printf", "%{relpath}\n"}));
  EXPECT_THAT(EnforceStyle(pf, registry::Style::kFind), StatusIs(absl::StatusCode::kInvalidArgument));
  ASSERT_OK_AND_ASSIGN(const Command fpf, Parse({".", "-fprintf", "out", "%{name}"}));
  EXPECT_THAT(EnforceStyle(fpf, registry::Style::kFind), StatusIs(absl::StatusCode::kInvalidArgument));
  ASSERT_OK_AND_ASSIGN(const Command plain, Parse({".", "-printf", "%p\n"}));
  EXPECT_THAT(EnforceStyle(plain, registry::Style::kFind), IsOk());
  EXPECT_THAT(EnforceStyle(pf, registry::Style::kXff), IsOk());
}

TEST_F(ParserTest, EnforceStyleWalksTheWholeTree) {
  // A -capture buried under operators is still found (the check is a full walk).
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-type", "f", "-o", "-capture:n", "wc", ";"}));
  const absl::Status status = EnforceStyle(cmd, registry::Style::kFind);
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(status.message(), HasSubstr("-capture"));
}

TEST_F(ParserTest, EnforceStyleFindsEveryExtensionKindInALeftSubtree) {
  // Each specialized pre-order walk must return a match found below the left
  // child rather than falling through to the right child.
  ASSERT_OK_AND_ASSIGN(const Command primary, Parse({".", "-capture:n", "wc", ";", "-o", "-name", "plain"}));
  EXPECT_THAT(EnforceStyle(primary, registry::Style::kFind), StatusIs(absl::StatusCode::kInvalidArgument));

  ASSERT_OK_AND_ASSIGN(const Command duration, Parse({".", "-mtime", "-3 weeks", "-o", "-name", "plain"}));
  EXPECT_THAT(EnforceStyle(duration, registry::Style::kFind), StatusIs(absl::StatusCode::kInvalidArgument));

  ASSERT_OK_AND_ASSIGN(const Command logical, Parse({".", "-name", "a", "-xor", "-name", "b", "-o", "-name", "plain"}));
  EXPECT_THAT(EnforceStyle(logical, registry::Style::kFind), StatusIs(absl::StatusCode::kInvalidArgument));

  ASSERT_OK_AND_ASSIGN(const Command field, Parse({".", "-printf", "%{name}", "-o", "-name", "plain"}));
  EXPECT_THAT(EnforceStyle(field, registry::Style::kFind), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ParserTest, EnforceStyleToleratesAPredicateWithoutADescriptor) {
  Command command;
  command.expression = std::make_unique<Expr>();
  command.expression->kind = Expr::Kind::kPredicate;

  EXPECT_THAT(EnforceStyle(command, registry::Style::kFind), IsOk());
}

TEST_F(ParserTest, EnforceStyleAcceptsFindVocabularyUnderFind) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-type", "f", "-o", "-name", "x"}));
  EXPECT_THAT(EnforceStyle(cmd, registry::Style::kFind), IsOk());
}

TEST_F(ParserTest, EnforceStyleAcceptsXffExtensionsUnderXff) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-println"}));
  EXPECT_THAT(EnforceStyle(cmd, registry::Style::kXff), IsOk());
}

TEST_F(ParserTest, EnforceStyleAllowsAnEmptyExpression) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({"."}));  // roots only, no expression
  EXPECT_THAT(EnforceStyle(cmd, registry::Style::kFind), IsOk());
}

TEST_F(ParserTest, EnforceStyleRejectsTimeDurationValueUnderFind) {
  // The xff word/compound duration value of a day-time predicate is refused by the
  // strict find style (the bare day count and BSD unit suffix stay allowed below).
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-mtime", "-3 weeks 3 hours"}));
  const absl::Status status = EnforceStyle(cmd, registry::Style::kFind);
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(status.message(), HasSubstr("-mtime"));
  EXPECT_THAT(status.message(), HasSubstr("--config=xff"));
}

TEST_F(ParserTest, EnforceStyleAcceptsTimeDurationValueUnderXff) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-mtime", "-3 weeks 3 hours"}));
  EXPECT_THAT(EnforceStyle(cmd, registry::Style::kXff), IsOk());
}

TEST_F(ParserTest, EnforceStyleAcceptsBareAndSuffixTimeUnderFind) {
  // A bare day count (POSIX/GNU) and a BSD unit suffix carry no space, so both
  // stay find-compatible under --config=find.
  ASSERT_OK_AND_ASSIGN(const Command bare, Parse({".", "-mtime", "+2"}));
  EXPECT_THAT(EnforceStyle(bare, registry::Style::kFind), IsOk());
  ASSERT_OK_AND_ASSIGN(const Command suffix, Parse({".", "-mtime", "-1h"}));
  EXPECT_THAT(EnforceStyle(suffix, registry::Style::kFind), IsOk());
}

TEST_F(ParserTest, RegexPredicatesCompileWhenFinallyBound) {
  ASSERT_OK_AND_ASSIGN(Command cmd, Parse({".", "-regex", ".*\\.txt"}));
  ASSERT_THAT(cmd.expression, NotNull());
  EXPECT_THAT(cmd.expression->matcher, IsNull());
  BindMatchers(cmd, regex::Grammar::kRe2, CaseMode::kSensitive);
  ASSERT_THAT(cmd.expression->matcher, NotNull());
  EXPECT_THAT(cmd.expression->matcher->FullMatch("a/b.txt"), IsTrue());
  EXPECT_THAT(cmd.expression->matcher->FullMatch("a/b.md"), IsFalse());
}

TEST_F(ParserTest, IregexMatcherFoldsCaseFromTheDescriptor) {
  // -iregex's case-insensitivity comes from the descriptor's fold_case, not a name check.
  ASSERT_OK_AND_ASSIGN(Command cmd, Parse({".", "-iregex", ".*readme"}));
  BindMatchers(cmd, regex::Grammar::kRe2, CaseMode::kSensitive);
  ASSERT_THAT(cmd.expression->matcher, NotNull());
  EXPECT_THAT(cmd.expression->matcher->FullMatch("docs/README"), IsTrue());
}

TEST_F(ParserTest, NonRegexAndUncompilablePatternsLeaveMatcherNull) {
  ASSERT_OK_AND_ASSIGN(const Command name, Parse({".", "-name", "x"}));
  EXPECT_THAT(name.expression->matcher, IsNull());  // not a regex predicate
  ASSERT_OK_AND_ASSIGN(Command bad, Parse({".", "-regex", "a("}));
  BindMatchers(bad, regex::Grammar::kRe2, CaseMode::kSensitive);
  EXPECT_THAT(bad.expression->matcher, IsNull());  // uncompilable: null (evaluated as no-match), no parse error
}

TEST_F(ParserTest, FuzzyThresholdIsAnAttachedPercentage) {
  ASSERT_OK_AND_ASSIGN(const Command command, Parse({".", "-fuzzy:80%", "foo"}));
  ASSERT_THAT(command.expression, NotNull());
  EXPECT_THAT(command.expression->descriptor->name, "-fuzzy");
  EXPECT_THAT(command.expression->args, ElementsAre("foo"));
  EXPECT_THAT(command.expression->fuzzy_threshold, Optional(Eq(80)));
  EXPECT_THAT(command.expression->fuzzy_model, FuzzyModel::kFzf);
}

TEST_F(ParserTest, FuzzyThresholdMaySelectItsModel) {
  ASSERT_OK_AND_ASSIGN(const Command command, Parse({".", "-ifuzzypath:levenshtein:30%", "foo"}));
  ASSERT_THAT(command.expression, NotNull());
  EXPECT_THAT(command.expression->descriptor->name, "-ifuzzypath");
  EXPECT_THAT(command.expression->args, ElementsAre("foo"));
  EXPECT_THAT(command.expression->fuzzy_threshold, Optional(Eq(30)));
  EXPECT_THAT(command.expression->fuzzy_model, FuzzyModel::kLevenshtein);
}

TEST_F(ParserTest, FuzzyModelMayBeSelectedWithoutAThresholdAndAliasesNormalize) {
  ASSERT_OK_AND_ASSIGN(const Command fzf, Parse({".", "-fuzzy:fzf", "foo"}));
  ASSERT_THAT(fzf.expression, NotNull());
  EXPECT_THAT(fzf.expression->fuzzy_model, FuzzyModel::kFzf);
  EXPECT_THAT(fzf.expression->fuzzy_threshold, Eq(std::nullopt));

  ASSERT_OK_AND_ASSIGN(const Command edit, Parse({".", "-fuzzy:edit:30%", "foo"}));
  ASSERT_THAT(edit.expression, NotNull());
  EXPECT_THAT(edit.expression->fuzzy_model, FuzzyModel::kLevenshtein);
  EXPECT_THAT(edit.expression->fuzzy_threshold, Optional(Eq(30)));

  ASSERT_OK_AND_ASSIGN(const Command shingles, Parse({".", "-fuzzy:shingles", "foo"}));
  ASSERT_THAT(shingles.expression, NotNull());
  EXPECT_THAT(shingles.expression->fuzzy_model, FuzzyModel::kShingles);
  EXPECT_THAT(shingles.expression->fuzzy_threshold, Eq(std::nullopt));
}

TEST_F(ParserTest, FuzzyThresholdRejectsMalformedPercentages) {
  EXPECT_THAT(Parse({".", "-fuzzy:80", "foo"}), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(Parse({".", "-fuzzy:101%", "foo"}), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(Parse({".", "-fuzzy:oops%", "foo"}), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(Parse({".", "-fuzzy:unknown:80%", "foo"}), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(Parse({".", "-fuzzy:fzf:", "foo"}), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ParserTest, SimilarityDefaultsToFiveWordShinglesAndEightyPercent) {
  ASSERT_OK_AND_ASSIGN(const Command command, Parse({".", "-similar", "reference.txt"}));
  ASSERT_THAT(command.expression, NotNull());
  EXPECT_THAT(command.expression->descriptor->name, "-similar");
  EXPECT_THAT(command.expression->args, ElementsAre("reference.txt"));
  EXPECT_THAT(command.expression->similarity_width, Eq(5));
  EXPECT_THAT(command.expression->similarity_threshold, Eq(kDefaultSimilarityThresholdPercent));
}

TEST_F(ParserTest, SimilarityQualifierCanOverrideThresholdWidthOrBoth) {
  ASSERT_OK_AND_ASSIGN(const Command threshold, Parse({".", "-similar:65%", "reference.txt"}));
  EXPECT_THAT(threshold.expression->similarity_width, Eq(5));
  EXPECT_THAT(threshold.expression->similarity_threshold, Eq(65));

  ASSERT_OK_AND_ASSIGN(const Command width, Parse({".", "-similar:7", "reference.txt"}));
  EXPECT_THAT(width.expression->similarity_width, Eq(7));
  EXPECT_THAT(width.expression->similarity_threshold, Eq(kDefaultSimilarityThresholdPercent));

  ASSERT_OK_AND_ASSIGN(const Command both, Parse({".", "-similar:7:65%", "reference.txt"}));
  EXPECT_THAT(both.expression->similarity_width, Eq(7));
  EXPECT_THAT(both.expression->similarity_threshold, Eq(65));
}

TEST_F(ParserTest, SimilarityQualifierRejectsInvalidWidthsAndThresholds) {
  EXPECT_THAT(Parse({".", "-similar:0", "reference.txt"}), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(Parse({".", "-similar:101%", "reference.txt"}), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(Parse({".", "-similar:5:101%", "reference.txt"}), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(Parse({".", "-similar:words", "reference.txt"}), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(Parse({".", "-similar:5:", "reference.txt"}), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ParserTest, XorBindsTighterThanOr) {
  // `-name a -xor -name b -o -name c` => Or( Xor(a, b), c ): XOR is above OR.
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-name", "a", "-xor", "-name", "b", "-o", "-name", "c"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Expr::Kind::kOr);
  EXPECT_THAT(root.lhs->kind, Expr::Kind::kXor);
  ASSERT_THAT(root.rhs->kind, Expr::Kind::kPredicate);
  EXPECT_THAT(root.rhs->descriptor->name, "-name");
}

TEST_F(ParserTest, XorBindsLooserThanAnd) {
  // `-name a -xor -name b -name c` => Xor( a, And(b, c) ): implicit AND is above XOR.
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-name", "a", "-xor", "-name", "b", "-name", "c"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Expr::Kind::kXor);
  EXPECT_THAT(root.lhs->kind, Expr::Kind::kPredicate);
  EXPECT_THAT(root.rhs->kind, Expr::Kind::kAnd);
}

TEST_F(ParserTest, NandBindsAtTheAndTier) {
  // `-name a -nand -name b -o -name c` => Or( Nand(a, b), c ).
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-name", "a", "-nand", "-name", "b", "-o", "-name", "c"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Expr::Kind::kOr);
  EXPECT_THAT(root.lhs->kind, Expr::Kind::kNand);
}

TEST_F(ParserTest, NorBindsAtTheOrTier) {
  // `-name a -o -name b -nor -name c` => Nor( Or(a, b), c ): left-associative at the OR tier.
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-name", "a", "-o", "-name", "b", "-nor", "-name", "c"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Expr::Kind::kNor);
  EXPECT_THAT(root.lhs->kind, Expr::Kind::kOr);
}

TEST_F(ParserTest, XnorParsesAsItsOwnNode) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-name", "a", "-xnor", "-name", "b"}));
  const Expr& root = *cmd.expression;
  ASSERT_THAT(root.kind, Expr::Kind::kXnor);
  EXPECT_THAT(root.lhs->kind, Expr::Kind::kPredicate);
  EXPECT_THAT(root.rhs->kind, Expr::Kind::kPredicate);
}

TEST_F(ParserTest, EnforceStyleRejectsXffOperatorsUnderFind) {
  // The new logical operators are xff extensions; the strict find style refuses
  // them even though they are interior nodes with no descriptor.
  static constexpr std::array kXffOperators = std::to_array<std::string_view>({
      "-xor",
      "-nand",
      "-nor",
      "-xnor",
  });
  for (const std::string_view op : kXffOperators) {
    ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-name", "a", std::string(op), "-name", "b"}));
    const absl::Status status = EnforceStyle(cmd, registry::Style::kFind);
    EXPECT_THAT(status, StatusIs(absl::StatusCode::kInvalidArgument)) << op;
    EXPECT_THAT(status.message(), HasSubstr(op)) << op;
    EXPECT_THAT(status.message(), HasSubstr("--config=xff")) << op;
  }
}

TEST_F(ParserTest, EnforceStyleAcceptsXffOperatorsUnderXff) {
  ASSERT_OK_AND_ASSIGN(const Command cmd, Parse({".", "-name", "a", "-xor", "-name", "b"}));
  EXPECT_THAT(EnforceStyle(cmd, registry::Style::kXff), IsOk());
}

TEST_F(ParserTest, RegextypeSelectsTheMatcherGrammar) {
  // The grammar is resolved once from --regextype and stored on the Command, so every matcher (and
  // final BindMatchers call uses it. RE2 is the default.
  ASSERT_OK_AND_ASSIGN(const Command def, Parse({".", "-regex", ".*"}));
  EXPECT_THAT(def.grammar, regex::Grammar::kRe2);  // no --regextype -> RE2

  ASSERT_OK_AND_ASSIGN(const Command re2, Parse({"--regextype=RE2", ".", "-regex", ".*"}));
  EXPECT_THAT(re2.grammar, regex::Grammar::kRe2);

  ASSERT_OK_AND_ASSIGN(const Command ere, Parse({"--regextype=ERE", ".", "-regex", ".*"}));
  EXPECT_THAT(ere.grammar, regex::Grammar::kEre);

  ASSERT_OK_AND_ASSIGN(const Command pcre2, Parse({"--regextype=PCRE2", ".", "-regex", ".*"}));
  EXPECT_THAT(pcre2.grammar, regex::Grammar::kPcre2);

  // EXACT selects the literal engine (a core grammar, applies to every pattern predicate).
  ASSERT_OK_AND_ASSIGN(const Command exact, Parse({"--regextype=EXACT", ".", "-grep", "x"}));
  EXPECT_THAT(exact.grammar, regex::Grammar::kExact);

  // FNMATCH selects the shell-wildcard engine (also core).
  ASSERT_OK_AND_ASSIGN(const Command fnmatch, Parse({"--regextype=FNMATCH", ".", "-rxc", "x*"}));
  EXPECT_THAT(fnmatch.grammar, regex::Grammar::kFnmatch);

  // GLOB selects the path-aware shell-glob engine (also core).
  ASSERT_OK_AND_ASSIGN(const Command glob, Parse({"--regextype=GLOB", ".", "-regex", "src/*.txt"}));
  EXPECT_THAT(glob.grammar, regex::Grammar::kGlob);

  // Last occurrence wins (mirrors run.cc's ResolveGrepLiteral).
  ASSERT_OK_AND_ASSIGN(const Command last, Parse({"--regextype=PCRE2", "--regextype=RE2", ".", "-regex", ".*"}));
  EXPECT_THAT(last.grammar, regex::Grammar::kRe2);
}

TEST_F(ParserTest, ErgonomicRegexSelectorsParticipateInLastWinsOrder) {
  EXPECT_THAT(GrammarFromGlobals({"--regextype=EXACT", "--re2"}), regex::Grammar::kRe2);
  EXPECT_THAT(GrammarFromGlobals({"--re2", "--pcre"}), regex::Grammar::kPcre2);
  EXPECT_THAT(GrammarFromGlobals({"--pcre", "--regextype=GLOB"}), regex::Grammar::kGlob);
  EXPECT_THAT(GrammarFromGlobals({"--regextype=EXACT", "-E"}), regex::Grammar::kExact);
}

struct TakesTerminalTest : ::testing::Test {};

TEST_F(TakesTerminalTest, TheExecAndPromptFamilyTakesTheTerminal) {
  // -ok / -okdir prompt and read a reply; -exec / -execdir can hand the terminal to a child.
  static constexpr std::array kTerminalPrimaries =
      std::to_array<std::string_view>({"-exec", "-execdir", "-ok", "-okdir"});
  for (const std::string_view primary : kTerminalPrimaries) {
    SCOPED_TRACE(primary);
    MBO_ASSERT_OK_AND_ASSIGN(const Command command, Parse({".", std::string(primary), "echo", "{}", ";"}));
    EXPECT_THAT(TakesTerminal(command), IsTrue()) << primary;
  }
}

TEST_F(TakesTerminalTest, AnOrdinaryExpressionDoesNot) {
  MBO_ASSERT_OK_AND_ASSIGN(const Command command, Parse({".", "-type", "f", "-o", "-name", "*.cc"}));
  EXPECT_THAT(TakesTerminal(command), IsFalse());
  EXPECT_THAT(TakesTerminal(Command{}), IsFalse());
}

TEST_F(TakesTerminalTest, ItFindsThePrimaryAnywhereInTheTree) {
  // The walk has to reach both operands and through a negation, or a nested -ok would slip past.
  MBO_ASSERT_OK_AND_ASSIGN(const Command command, Parse({".", "!", "-name", "x", "-o", "-ok", "rm", "{}", ";"}));
  EXPECT_THAT(TakesTerminal(command), IsTrue());
}

}  // namespace
}  // namespace xff::parser
