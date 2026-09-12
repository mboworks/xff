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

#include "xff/engine/evaluate.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "mbo/testing/status.h"
#include "xff/engine/walk.h"
#include "xff/fuzzy/fuzzy.h"
#include "xff/parser/parser.h"
#include "xff/vfs/entry.h"
#include "xff/vfs/local_fs.h"

namespace xff::engine {
namespace {

using ::mbo::testing::EqualsText;
using ::mbo::testing::IsOk;
using ::mbo::testing::IsOkAndHolds;
using ::mbo::testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::Optional;
using ::testing::Pair;
using ::testing::SizeIs;

struct EvaluateTest : ::testing::Test {
  // Parses `. <expr...>` and evaluates the expression against `visit`, capturing
  // any action output in `emitted_`.
  bool Match(const std::vector<std::string>& expr, const Visit& visit) {
    emitted_.clear();
    file_emitted_.clear();
    control_ = {};
    std::vector<std::string> argv;
    argv.reserve(expr.size() + 2);
    if (!regextype_.empty()) {
      argv.push_back(absl::StrCat("--regextype=", regextype_));  // a leading global, before the root
    }
    argv.emplace_back(".");
    argv.insert(argv.end(), expr.begin(), expr.end());
    auto command = parser::Parse(argv);
    EXPECT_THAT(command, IsOk());
    if (!command.ok() || command->expression == nullptr) {
      return false;
    }
    parser::BindMatchers(
        *command, parser::GrammarFromGlobals(command->globals),
        parser::ResolveCaseMode(command->globals, registry::Style::kXff));
    return MatchExpression(*command->expression, visit);
  }

  // Evaluates an already parsed expression, preserving its node identity across calls.
  bool MatchExpression(const parser::Expr& expression, const Visit& visit) {
    const auto sink = [this](std::string_view record) { emitted_ += record; };
    captures_.clear();
    outputs_.clear();
    fuzzy_score_.reset();
    hash_verification_.reset();
    const auto file_sink = [this](std::string_view file, std::string_view record) {
      file_emitted_[std::string(file)] += record;
    };
    EvalContext context{
        .visit = visit,
        .emit = sink,
        .emit_file = file_sink,
        .fs = fs_,
        .now = now_,
        .tz = tz_,
        .fold_name_case = fold_name_case_,
        .fuzzy_score = collect_fuzzy_score_ ? mbo::types::OptionalRef{fuzzy_score_}
                                            : mbo::types::OptionalRef<std::optional<int>>{},
        .hash_verification = collect_hash_verification_ ? mbo::types::OptionalRef{hash_verification_}
                                                        : mbo::types::OptionalRef<std::optional<bool>>{},
        .grep_count = grep_count_,
        .control = control_,
        .exec_fields = exec_fields_,
        .captures = exec_fields_ ? mbo::types::OptionalRef{captures_} : std::nullopt,
        .outputs = capture_outputs_ ? mbo::types::OptionalRef{outputs_}
                                    : mbo::types::OptionalRef<std::map<std::string, std::string>>{},
        .first_counts =
            provide_first_counts_ ? mbo::types::OptionalRef{first_counts_} : mbo::types::OptionalRef<FirstCounts>{},
        .collections =
            provide_collections_ ? mbo::types::OptionalRef{collections_} : mbo::types::OptionalRef<Collections>{},
        .confirm =
            [this](std::string_view prompt) {
              last_prompt_ = std::string(prompt);
              return confirm_reply_;
            },
        .exec_batches = provide_exec_batches_ ? mbo::types::OptionalRef{exec_batches_}
                                              : mbo::types::OptionalRef<decltype(exec_batches_)>{}};
    return Evaluate(expression, context);
  }

  // A Visit of `type`, with `path`/`name` backed by the caller and metadata by
  // `storage` (both must outlive the returned Visit).
  static Visit MakeVisit(std::string_view path, std::string_view name, vfs::FileType type, vfs::Metadata& storage) {
    storage.type = type;
    return Visit{.path = path, .name = name, .depth = 1, .metadata = storage};
  }

  // Writes `bytes` (which may contain a NUL, for the binary-skip case) to a temp
  // file and returns its path, tracked for TearDown cleanup. The content predicates
  // read a real file via LocalFs::ReadContent, so they need one on disk.
  std::string WriteContentFile(std::string_view tag, std::string_view bytes) {
    namespace stdfs = ::std::filesystem;
    const stdfs::path path = stdfs::temp_directory_path() / (std::string("xff_content_") + std::string(tag));
    {
      std::ofstream out(path, std::ios::binary);
      out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    content_files_.push_back(path.string());
    return content_files_.back();
  }

  void TearDown() override {
    namespace stdfs = ::std::filesystem;
    for (const std::string& file : content_files_) {
      std::error_code ec;
      stdfs::remove(file, ec);
    }
  }

  std::string emitted_;
  std::map<std::string, std::string> file_emitted_;  // -fprint/-fprintf/... output, keyed by filename
  bool confirm_reply_ = false;                       // scripted reply for the -ok confirmer
  std::string last_prompt_;                          // captures the prompt -ok passed to confirm()
  vfs::LocalFs fs_;
  // A fixed reference instant for age-test (-mtime/-mmin) cases; the entry's
  // mtime is set relative to this so the assertions are clock-independent.
  const absl::Time now_ = absl::FromUnixSeconds(1'700'000'000);
  absl::TimeZone tz_ = absl::LocalTimeZone();   // zone Match feeds to EvalContext::tz (varied by -newermt cases)
  Control control_;                             // set by Match from the most recent evaluation (-prune/-quit)
  bool exec_fields_ = false;                    // when true, Match enables --exec-fields token substitution
  bool fold_name_case_ = false;                 // when true, Match sets EvalContext::fold_name_case (FS-native fold)
  std::string regextype_;                       // when set (e.g. "EXACT"), Match prepends --regextype=<v> as a global
  bool grep_count_ = false;                     // when true, Match sets EvalContext::grep_count (-grep --count mode)
  bool capture_outputs_ = true;                 // when false, Match leaves the -capture output sink unwired
  bool collect_fuzzy_score_ = true;             // when false, Match evaluates without a fuzzy-score consumer
  bool collect_hash_verification_ = false;      // when true, Match records the reached -hasheq verdict
  bool provide_first_counts_ = false;           // when true, Match wires the stateful -first counter map
  bool provide_collections_ = false;            // when true, Match wires the -collect store
  bool provide_exec_batches_ = false;           // when true, Match wires -exec/-execdir ... + batches
  std::optional<int> fuzzy_score_;              // normalized score composed by the most recent Match
  std::optional<bool> hash_verification_;       // verdict from the most recent reached -hasheq
  std::vector<std::string> captures_;           // -regex groups captured during the most recent (gated) Match
  std::map<std::string, std::string> outputs_;  // -capture results from the most recent Match
  FirstCounts first_counts_;
  Collections collections_;
  ExecBatches exec_batches_;
  std::vector<std::string> content_files_;  // temp files written by WriteContentFile, removed in TearDown
};

TEST_F(EvaluateTest, ExpressionIdentityTracksNodesRatherThanTheirContents) {
  const parser::Expr first{.kind = parser::Expr::Kind::kPredicate};
  const parser::Expr second{.kind = parser::Expr::Kind::kPredicate};
  const ExprIdentity first_identity{first};
  const ExprIdentity same_identity{first};
  const ExprIdentity second_identity{second};

  EXPECT_THAT(first_identity, Eq(same_identity));
  EXPECT_THAT(first_identity, Not(Eq(second_identity)));
  std::map<ExprIdentity, int> values{{first_identity, 1}, {second_identity, 2}};
  EXPECT_THAT(values, SizeIs(2));
  EXPECT_THAT(values.at(same_identity), Eq(1));
}

TEST_F(EvaluateTest, TrueAndFalse) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo", "foo", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-true"}, visit), IsTrue());
  EXPECT_THAT(Match({"-false"}, visit), IsFalse());
}

TEST_F(EvaluateTest, HashVerificationSideChannelRecordsEveryDefensiveFailure) {
  collect_hash_verification_ = true;
  vfs::Metadata regular_md;
  const std::string path = WriteContentFile("hasheq_verdict", "abc");
  const Visit regular = MakeVisit(path, "hasheq_verdict", vfs::FileType::kRegular, regular_md);

  EXPECT_THAT(Match({"-hasheq", "{def.MISSING}"}, regular), IsFalse());
  EXPECT_THAT(hash_verification_, Optional(IsFalse()));

  vfs::Metadata directory_md;
  const Visit directory = MakeVisit(".", ".", vfs::FileType::kDirectory, directory_md);
  EXPECT_THAT(Match({"-hasheq", "deadbeef"}, directory), IsFalse());
  EXPECT_THAT(hash_verification_, Optional(IsFalse()));

  ASSERT_OK_AND_ASSIGN(auto missing_argument, parser::Parse({".", "-hasheq", "deadbeef"}));
  ASSERT_THAT(missing_argument.expression, NotNull());
  missing_argument.expression->args.clear();
  EXPECT_THAT(MatchExpression(*missing_argument.expression, regular), IsFalse());
  EXPECT_THAT(hash_verification_, Optional(IsFalse()));

  ASSERT_OK_AND_ASSIGN(auto bad_spec, parser::Parse({".", "-hasheq", "deadbeef"}));
  ASSERT_THAT(bad_spec.expression, NotNull());
  bad_spec.expression->hash_spec = "crc32";
  EXPECT_THAT(MatchExpression(*bad_spec.expression, regular), IsFalse());
  EXPECT_THAT(hash_verification_, Optional(IsFalse()));
}

TEST_F(EvaluateTest, XorMatchesExactlyOneSide) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo", "foo", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-true", "-xor", "-true"}, visit), IsFalse());
  EXPECT_THAT(Match({"-true", "-xor", "-false"}, visit), IsTrue());
  EXPECT_THAT(Match({"-false", "-xor", "-true"}, visit), IsTrue());
  EXPECT_THAT(Match({"-false", "-xor", "-false"}, visit), IsFalse());
}

TEST_F(EvaluateTest, XnorMatchesWhenBothSidesAgree) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo", "foo", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-true", "-xnor", "-true"}, visit), IsTrue());
  EXPECT_THAT(Match({"-true", "-xnor", "-false"}, visit), IsFalse());
  EXPECT_THAT(Match({"-false", "-xnor", "-true"}, visit), IsFalse());
  EXPECT_THAT(Match({"-false", "-xnor", "-false"}, visit), IsTrue());
}

TEST_F(EvaluateTest, NandIsTheNegationOfAnd) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo", "foo", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-true", "-nand", "-true"}, visit), IsFalse());
  EXPECT_THAT(Match({"-true", "-nand", "-false"}, visit), IsTrue());
  EXPECT_THAT(Match({"-false", "-nand", "-true"}, visit), IsTrue());
  EXPECT_THAT(Match({"-false", "-nand", "-false"}, visit), IsTrue());
}

TEST_F(EvaluateTest, NorIsTheNegationOfOr) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo", "foo", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-true", "-nor", "-true"}, visit), IsFalse());
  EXPECT_THAT(Match({"-true", "-nor", "-false"}, visit), IsFalse());
  EXPECT_THAT(Match({"-false", "-nor", "-true"}, visit), IsFalse());
  EXPECT_THAT(Match({"-false", "-nor", "-false"}, visit), IsTrue());
}

TEST_F(EvaluateTest, DaystartIsAPositionalNoOp) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo", "foo", vfs::FileType::kRegular, md);
  // -daystart is consumed by the driver (it shifts the age-test reference to local
  // midnight); as a predicate it is a no-op that matches, so it never filters.
  EXPECT_THAT(Match({"-daystart"}, visit), IsTrue());
  EXPECT_THAT(Match({"-daystart", "-name", "foo"}, visit), IsTrue());
}

TEST_F(EvaluateTest, NameGlobsBasename) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo.txt", "foo.txt", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-name", "*.txt"}, visit), IsTrue());
  EXPECT_THAT(Match({"-name", "foo.*"}, visit), IsTrue());
  EXPECT_THAT(Match({"-name", "*.md"}, visit), IsFalse());
  EXPECT_THAT(Match({"-name", "dir/*"}, visit), IsFalse()) << "-name matches the basename, not the path";
}

TEST_F(EvaluateTest, INameFoldsCase) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/Foo.TXT", "Foo.TXT", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-iname", "foo.txt"}, visit), IsTrue());
  EXPECT_THAT(Match({"-name", "foo.txt"}, visit), IsFalse());
}

TEST_F(EvaluateTest, FsNativeFoldsNameOnCaseFoldingVolume) {
  // fold_name_case (set by the driver for an entry on a case-folding volume, xff
  // style, no --exact) makes the case-sensitive -name fold like -iname, so it
  // matches the way the filesystem itself resolves the name.
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/Foo.TXT", "Foo.TXT", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-name", "foo.txt"}, visit), IsFalse())
      << "byte-exact by default (case-sensitive volume / --exact)";
  fold_name_case_ = true;
  EXPECT_THAT(Match({"-name", "foo.txt"}, visit), IsTrue()) << "FS-native fold on a case-folding volume";
  EXPECT_THAT(Match({"-name", "Foo.TXT"}, visit), IsTrue()) << "the exact-case name still matches when folding";
}

TEST_F(EvaluateTest, FsNativeFoldsPathOnCaseFoldingVolume) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("Dir/Foo.TXT", "Foo.TXT", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-path", "dir/foo.txt"}, visit), IsFalse());
  fold_name_case_ = true;
  EXPECT_THAT(Match({"-path", "dir/foo.txt"}, visit), IsTrue()) << "-path folds too under FS-native matching";
}

TEST_F(EvaluateTest, PathGlobsWholePath) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("a/b/c.txt", "c.txt", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-path", "a/*/c.txt"}, visit), IsTrue());
  EXPECT_THAT(Match({"-path", "*/c.txt"}, visit), IsTrue()) << "* spans slashes for -path";
  EXPECT_THAT(Match({"-path", "c.txt"}, visit), IsFalse());
  EXPECT_THAT(Match({"-ipath", "A/B/*"}, visit), IsTrue());
}

TEST_F(EvaluateTest, FuzzyPercentThresholdGatesTheMatch) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/far_out_of", "far_out_of", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-fuzzy:0%", "foo"}, visit), IsTrue());
  EXPECT_THAT(Match({"-fuzzy:100%", "foo"}, visit), IsFalse());
  EXPECT_THAT(Match({"-fuzzy:100%", "far_out_of"}, visit), IsTrue());
  EXPECT_THAT(fuzzy_score_, Optional(Eq(100)));
}

TEST_F(EvaluateTest, FuzzyWithoutScoreConsumerEvaluatesCompoundExpression) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo", "foo", vfs::FileType::kRegular, md);
  collect_fuzzy_score_ = false;

  EXPECT_THAT(Match({"-fuzzy", "foo", "-a", "-true"}, visit), IsTrue());
  EXPECT_THAT(fuzzy_score_, Eq(std::nullopt));
}

TEST_F(EvaluateTest, FuzzyModelsHaveConcreteThresholdSemantics) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo", "foo", vfs::FileType::kRegular, md);

  EXPECT_THAT(Match({"-fuzzy:sequence:67%", "fo"}, visit), IsTrue());
  EXPECT_THAT(fuzzy_score_, Optional(Eq(67)));
  EXPECT_THAT(Match({"-fuzzy:sequence:68%", "fo"}, visit), IsFalse());

  EXPECT_THAT(Match({"-fuzzy:levenshtein:67%", "fof"}, visit), IsTrue());
  EXPECT_THAT(fuzzy_score_, Optional(Eq(67)));
  EXPECT_THAT(Match({"-fuzzy:levenshtein:68%", "fof"}, visit), IsFalse());

  EXPECT_THAT(Match({"-fuzzy:shingles:33%", "fof"}, visit), IsTrue());
  EXPECT_THAT(fuzzy_score_, Optional(Eq(33)));
  EXPECT_THAT(Match({"-fuzzy:shingles:34%", "fof"}, visit), IsFalse());
}

TEST_F(EvaluateTest, FuzzyAndUsesTheWeakestNormalizedScore) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/far_out_of", "far_out_of", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-fuzzy", "foo", "-fuzzy", "far_out_of"}, visit), IsTrue());
  const std::optional<int> weak = fuzzy::Percent("foo", "far_out_of", false);
  EXPECT_THAT(fuzzy_score_, Eq(weak));
}

TEST_F(EvaluateTest, PureFuzzyOrEvaluatesBothAlternativesAndUsesTheBestScore) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/far_out_of", "far_out_of", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-fuzzy", "foo", "-o", "-fuzzy", "far_out_of"}, visit), IsTrue());
  EXPECT_THAT(fuzzy_score_, Optional(Eq(100)));
}

TEST_F(EvaluateTest, WholenameIsSynonymForPath) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("a/b/c.txt", "c.txt", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-wholename", "a/*/c.txt"}, visit), IsTrue());  // -wholename == -path
  EXPECT_THAT(Match({"-wholename", "c.txt"}, visit), IsFalse());     // matches the whole path, not the basename
  EXPECT_THAT(Match({"-iwholename", "A/B/*"}, visit), IsTrue());     // -iwholename folds case, like -ipath
  EXPECT_THAT(Match({"-wholename", "A/B/*"}, visit), IsFalse());     // -wholename is case-sensitive
}

TEST_F(EvaluateTest, TypeMatchesFileType) {
  vfs::Metadata file_md;
  const Visit file = MakeVisit("x", "x", vfs::FileType::kRegular, file_md);
  vfs::Metadata dir_md;
  const Visit dir = MakeVisit("d", "d", vfs::FileType::kDirectory, dir_md);
  EXPECT_THAT(Match({"-type", "f"}, file), IsTrue());
  EXPECT_THAT(Match({"-type", "d"}, file), IsFalse());
  EXPECT_THAT(Match({"-type", "d"}, dir), IsTrue());
  EXPECT_THAT(Match({"-type", "f"}, dir), IsFalse());
}

TEST_F(EvaluateTest, MimeGlobsTheExtensionDerivedMediaType) {
  vfs::Metadata md;
  const Visit png = MakeVisit("dir/photo.png", "photo.png", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-mime", "image/*"}, png), IsTrue());    // image/png matches the glob
  EXPECT_THAT(Match({"-mime", "image/png"}, png), IsTrue());  // and the exact type
  EXPECT_THAT(Match({"-mime", "text/*"}, png), IsFalse());
  // MIME type/subtype names are case-insensitive (RFC 2045/6838): an upper- or mixed-case pattern
  // matches the (canonical lowercase) type just the same.
  EXPECT_THAT(Match({"-mime", "IMAGE/*"}, png), IsTrue());
  EXPECT_THAT(Match({"-mime", "Image/PNG"}, png), IsTrue());
  // An unknown / absent extension is application/octet-stream (the file(1) fallback).
  vfs::Metadata bin_md;
  const Visit bin = MakeVisit("dir/README", "README", vfs::FileType::kRegular, bin_md);
  EXPECT_THAT(Match({"-mime", "application/octet-stream"}, bin), IsTrue());
  EXPECT_THAT(Match({"-mime", "text/*"}, bin), IsFalse());
}

TEST_F(EvaluateTest, LangGlobsTheExtensionOrFilenameLanguage) {
  vfs::Metadata md;
  const Visit cpp = MakeVisit("src/main.cc", "main.cc", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-lang", "C*"}, cpp), IsTrue());   // "C++" matches the glob
  EXPECT_THAT(Match({"-lang", "c++"}, cpp), IsTrue());  // and case-insensitively (canonical is "C++")
  EXPECT_THAT(Match({"-lang", "Python"}, cpp), IsFalse());
  // Filename-classified languages match too.
  const Visit mk = MakeVisit("proj/Makefile", "Makefile", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-lang", "Makefile"}, mk), IsTrue());
  // An unrecognized name has no language (""), matched only by a `*` / empty pattern.
  const Visit bin = MakeVisit("dir/photo.jpg", "photo.jpg", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-lang", "C*"}, bin), IsFalse());
  EXPECT_THAT(Match({"-lang", "*"}, bin), IsTrue());
}

TEST_F(EvaluateTest, TypeListMatchesAnyListedType) {
  vfs::Metadata file_md;
  const Visit file = MakeVisit("x", "x", vfs::FileType::kRegular, file_md);
  vfs::Metadata dir_md;
  const Visit dir = MakeVisit("d", "d", vfs::FileType::kDirectory, dir_md);
  vfs::Metadata link_md;
  const Visit link = MakeVisit("l", "l", vfs::FileType::kSymlink, link_md);
  // GNU's "-type f,d": matches if the entry is any of the listed types.
  EXPECT_THAT(Match({"-type", "f,d"}, file), IsTrue());
  EXPECT_THAT(Match({"-type", "f,d"}, dir), IsTrue());
  EXPECT_THAT(Match({"-type", "f,d"}, link), IsFalse());
  EXPECT_THAT(Match({"-type", "l,p,f"}, file), IsTrue());  // order does not matter
  // Malformed lists are rejected by the shared parser before evaluation.
}

TEST_F(EvaluateTest, AndOrNotShortCircuit) {
  vfs::Metadata md;
  const Visit txt = MakeVisit("dir/foo.txt", "foo.txt", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-type", "f", "-name", "*.txt"}, txt), IsTrue());        // implicit -a
  EXPECT_THAT(Match({"-type", "d", "-name", "*.txt"}, txt), IsFalse());       // -a, first fails
  EXPECT_THAT(Match({"-type", "d", "-o", "-name", "*.txt"}, txt), IsTrue());  // -o
  EXPECT_THAT(Match({"!", "-type", "d"}, txt), IsTrue());                     // -not
  EXPECT_THAT(Match({"!", "-name", "*.txt"}, txt), IsFalse());
}

TEST_F(EvaluateTest, PrintActionsEmit) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo.txt", "foo.txt", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-print"}, visit), IsTrue());
  EXPECT_THAT(emitted_, "dir/foo.txt\n");
  EXPECT_THAT(Match({"-print0"}, visit), IsTrue());
  EXPECT_THAT(emitted_, std::string("dir/foo.txt\0", 12));
}

TEST_F(EvaluateTest, FileActionsWriteRecordsToNamedFiles) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo.txt", "foo.txt", vfs::FileType::kRegular, md);
  // Each f-action mirrors its stdout counterpart's bytes, routed to the FILE arg
  // (Match clears file_emitted_ each call, so each map holds just that action's output).
  EXPECT_THAT(Match({"-fprint", "out"}, visit), IsTrue());
  EXPECT_THAT(file_emitted_, ElementsAre(Pair("out", "dir/foo.txt\n")));
  EXPECT_THAT(Match({"-fprint0", "out"}, visit), IsTrue());
  EXPECT_THAT(file_emitted_, ElementsAre(Pair("out", std::string("dir/foo.txt\0", 12))));
  EXPECT_THAT(Match({"-fprintf", "out", "%p:%f\n"}, visit), IsTrue());
  EXPECT_THAT(file_emitted_, ElementsAre(Pair("out", "dir/foo.txt:foo.txt\n")));
  EXPECT_THAT(Match({"-fls", "log"}, visit), IsTrue());
  EXPECT_THAT(file_emitted_, ElementsAre(Pair("log", HasSubstr("dir/foo.txt"))));
}

TEST_F(EvaluateTest, ShortCircuitSkipsAction) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo.txt", "foo.txt", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-type", "f", "-print"}, visit), IsTrue());
  EXPECT_THAT(emitted_, "dir/foo.txt\n");
  EXPECT_THAT(Match({"-type", "d", "-print"}, visit), IsFalse()) << "-type d fails; -a short-circuits before -print";
  EXPECT_THAT(emitted_, IsEmpty());
}

TEST_F(EvaluateTest, SizeMatchesBytesAndUnits) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.size = 5;  // bytes
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-size", "5c"}, visit), IsTrue());
  EXPECT_THAT(Match({"-size", "4c"}, visit), IsFalse());
  EXPECT_THAT(Match({"-size", "+4c"}, visit), IsTrue());
  EXPECT_THAT(Match({"-size", "-6c"}, visit), IsTrue());
  EXPECT_THAT(Match({"-size", "+5c"}, visit), IsFalse());
  EXPECT_THAT(Match({"-size", "1"}, visit), IsTrue()) << "5 bytes rounds up to one 512-byte block";
  EXPECT_THAT(Match({"-size", "1k"}, visit), IsTrue()) << "5 bytes rounds up to one 1k unit";
}

TEST_F(EvaluateTest, SizeMatchesLargeUnits) {
  // T/P/E continue the k/M/G binary scale (2^40/2^50/2^60). Exact multiples make the
  // round-up-to-unit arithmetic land on a clean count.
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.size = 3ULL * 1'024 * 1'024 * 1'024 * 1'024;  // 3 TiB
  const Visit tib{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-size", "3T"}, tib), IsTrue());
  EXPECT_THAT(Match({"-size", "+2T"}, tib), IsTrue());
  EXPECT_THAT(Match({"-size", "2T"}, tib), IsFalse());
  md.size = 2ULL * 1'024 * 1'024 * 1'024 * 1'024 * 1'024;  // 2 PiB
  const Visit pib{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-size", "2P"}, pib), IsTrue());
  md.size = 1ULL * 1'024 * 1'024 * 1'024 * 1'024 * 1'024 * 1'024;  // 1 EiB (2^60)
  const Visit eib{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-size", "1E"}, eib), IsTrue());
}

TEST_F(EvaluateTest, SizeExplicitUnitsKeepDecimalAndBinaryMeaningsDistinct) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.size = 1'000'001;
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-size", "2MB"}, visit), IsTrue()) << "SI MB rounds 1,000,001 bytes up to two units";
  EXPECT_THAT(Match({"-size", "1MiB"}, visit), IsTrue()) << "the same length fits in one IEC MiB";
  EXPECT_THAT(Match({"-size", "1M"}, visit), IsTrue()) << "legacy M remains the binary MiB scale";
  EXPECT_THAT(Match({"-size", "1MB"}, visit), IsFalse());
  EXPECT_THAT(Match({"-size", "1000001B"}, visit), IsTrue());
}

TEST_F(EvaluateTest, ValidateSizeArgsRejectsBadUnits) {
  // Valid units (incl. the T/P/E continuation) pass; an over-64-bit unit (Z/Y/...)
  // or an unknown unit is rejected with a self-documenting message, so the driver
  // fails before traversing rather than silently matching nothing.
  static constexpr std::array kValidSizeArgs = std::to_array<std::string_view>({
      "+1T",
      "2P",
      "-3E",
      "5c",
      "1k",
      "1B",
      "2MB",
      "3MiB",
      "4EB",
      "5EiB",
  });
  for (const std::string_view good : kValidSizeArgs) {
    MBO_ASSERT_OK_AND_ASSIGN(const auto command, parser::Parse({".", "-size", std::string(good)}));
    EXPECT_THAT(ValidateSizeArgs(*command.expression), IsOk()) << good;
  }
  MBO_ASSERT_OK_AND_ASSIGN(const auto blocks, parser::Parse({".", "-blocks", "8k"}));
  EXPECT_THAT(ValidateSizeArgs(*blocks.expression), IsOk());
  MBO_ASSERT_OK_AND_ASSIGN(const auto zetta, parser::Parse({".", "-size", "+1Z"}));
  EXPECT_THAT(
      ValidateSizeArgs(*zetta.expression), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("largest units")));
  MBO_ASSERT_OK_AND_ASSIGN(const auto unknown, parser::Parse({".", "-size", "1q"}));
  EXPECT_THAT(
      ValidateSizeArgs(*unknown.expression),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("unknown size unit")));
}

TEST_F(EvaluateTest, ParseBlockSizeAcceptsBytesAndUnits) {
  // --block-size value: a bare number is bytes (unlike -size, where bare = blocks);
  // the binary-multiple suffixes scale it. 'b' (circular), zero, and a missing/
  // non-numeric value are rejected.
  EXPECT_THAT(ParseBlockSize("512"), IsOkAndHolds(Eq(512U)));
  EXPECT_THAT(ParseBlockSize("4096"), IsOkAndHolds(Eq(4'096U)));
  EXPECT_THAT(ParseBlockSize("4k"), IsOkAndHolds(Eq(4'096U)));
  EXPECT_THAT(ParseBlockSize("1M"), IsOkAndHolds(Eq(1'024U * 1'024)));
  EXPECT_THAT(ParseBlockSize("4B"), IsOkAndHolds(Eq(4U)));
  EXPECT_THAT(ParseBlockSize("4kB"), IsOkAndHolds(Eq(4'000U)));
  EXPECT_THAT(ParseBlockSize("4KiB"), IsOkAndHolds(Eq(4'096U)));
  EXPECT_THAT(ParseBlockSize("2PB"), IsOkAndHolds(Eq(2'000'000'000'000'000U)));
  EXPECT_THAT(ParseBlockSize("2PiB"), IsOkAndHolds(Eq(2ULL << 50U)));
  EXPECT_THAT(ParseBlockSize("0"), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("positive")));
  EXPECT_THAT(ParseBlockSize("4b"), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("use B for bytes")));
  EXPECT_THAT(ParseBlockSize("19EB"), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("overflows")));
  EXPECT_THAT(ParseBlockSize("9Z"), StatusIs(absl::StatusCode::kInvalidArgument));  // over-64-bit unit, not in the map
  EXPECT_THAT(ParseBlockSize(""), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(EvaluateTest, BlocksMatchesAllocatedSpaceNotApparentSize) {
  // -blocks applies -size's grammar to allocated space (st_blocks * 512), so it is
  // independent of the apparent size: a 1-byte file occupying 16 512-blocks (8 KiB
  // on disk) matches -blocks but not -size of the same magnitude.
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.size = 1;     // 1 apparent byte
  md.blocks = 16;  // 16 * 512 = 8 KiB allocated
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-blocks", "16"}, visit), IsTrue());  // 8192 bytes / 512-byte block = 16
  EXPECT_THAT(Match({"-blocks", "+8"}, visit), IsTrue());  // more than 8 blocks
  EXPECT_THAT(Match({"-blocks", "8k"}, visit), IsTrue());  // exactly 8 KiB allocated
  EXPECT_THAT(Match({"-blocks", "+8k"}, visit), IsFalse());
  EXPECT_THAT(Match({"-blocks", "1c"}, visit), IsFalse());  // not 1 byte allocated
  EXPECT_THAT(Match({"-size", "1c"}, visit), IsTrue());     // but -size sees the 1 apparent byte
}

TEST_F(EvaluateTest, BlocksZeroForUnallocatedEntry) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;  // blocks defaults to 0 (nothing allocated)
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-blocks", "+0"}, visit), IsFalse());  // occupies no disk
  EXPECT_THAT(Match({"-blocks", "0"}, visit), IsTrue());
}

TEST_F(EvaluateTest, PermMatchesOctalModes) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.mode = 0644;  // rw-r--r--
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-perm", "644"}, visit), IsTrue());  // exact
  EXPECT_THAT(Match({"-perm", "640"}, visit), IsFalse());
  EXPECT_THAT(Match({"-perm", "-200"}, visit), IsTrue());   // -MODE: owner-write bit set
  EXPECT_THAT(Match({"-perm", "-044"}, visit), IsTrue());   // group + other read set
  EXPECT_THAT(Match({"-perm", "-022"}, visit), IsFalse());  // group/other write NOT set
  EXPECT_THAT(Match({"-perm", "/040"}, visit), IsTrue());   // /MODE: any bit (group read) set
  EXPECT_THAT(Match({"-perm", "/022"}, visit), IsFalse());  // none of group/other write set
  EXPECT_THAT(Match({"-perm", "+040"}, visit), IsTrue());   // BSD +MODE: any-of, like /MODE
  EXPECT_THAT(Match({"-perm", "+022"}, visit), IsFalse());  // none of group/other write set
}

TEST_F(EvaluateTest, PermMatchesSymbolicModes) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.mode = 0644;  // rw-r--r--
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  // Exact: the symbolic mode resolves (from zero) to exactly the file's bits.
  EXPECT_THAT(Match({"-perm", "u=rw,go=r"}, visit), IsTrue());  // == 0644
  EXPECT_THAT(Match({"-perm", "u=rw,g=r"}, visit), IsFalse());  // == 0640, file is 0644
  EXPECT_THAT(Match({"-perm", "u+w"}, visit), IsFalse());       // == 0200 exactly; file has more
  // -MODE: all requested bits present.
  EXPECT_THAT(Match({"-perm", "-u+w"}, visit), IsTrue());       // owner write set
  EXPECT_THAT(Match({"-perm", "-u+r,go+r"}, visit), IsTrue());  // all of owner/group/other read set
  EXPECT_THAT(Match({"-perm", "-g+w"}, visit), IsFalse());      // group write NOT set
  EXPECT_THAT(Match({"-perm", "-a+x"}, visit), IsFalse());      // no execute bits set
  // /MODE: any requested bit present.
  EXPECT_THAT(Match({"-perm", "/u+x,g+r"}, visit), IsTrue());  // group read is set
  EXPECT_THAT(Match({"-perm", "/a+x"}, visit), IsFalse());     // no execute bits at all
  // A malformed symbolic mode never matches.
  EXPECT_THAT(Match({"-perm", "u?w"}, visit), IsFalse());
  EXPECT_THAT(Match({"-perm", "u"}, visit), IsFalse());
  EXPECT_THAT(Match({"-perm", "u+q"}, visit), IsFalse());
  EXPECT_THAT(Match({"-perm", "u+w,u-w,go+r"}, visit), IsFalse());

  // An omitted "who" behaves as 'a' (all classes): "+r" resolves to 0444, not 0400.
  vfs::Metadata r_md;
  r_md.type = vfs::FileType::kRegular;
  r_md.mode = 0444;  // r--r--r--
  const Visit r_all{.path = "r", .name = "r", .depth = 1, .metadata = r_md};
  EXPECT_THAT(Match({"-perm", "+r"}, r_all), IsTrue());    // exact: omitted who == a, so 0444
  EXPECT_THAT(Match({"-perm", "u+r"}, r_all), IsFalse());  // exact: explicit u+r == 0400, file is 0444
}

TEST_F(EvaluateTest, OptionalStatefulCapabilitiesAreExplicit) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/file", "file", vfs::FileType::kRegular, md);

  EXPECT_THAT(Match({"-first", "1"}, visit), IsFalse());
  provide_first_counts_ = true;
  MBO_ASSERT_OK_AND_ASSIGN(const auto first, parser::Parse({".", "-first", "1"}));
  ASSERT_THAT(first.expression, NotNull());
  EXPECT_THAT(MatchExpression(*first.expression, visit), IsTrue());
  EXPECT_THAT(MatchExpression(*first.expression, visit), IsFalse());

  EXPECT_THAT(Match({"-collect"}, visit), IsTrue());
  provide_collections_ = true;
  EXPECT_THAT(Match({"-collect:kept"}, visit), IsTrue());
  EXPECT_THAT(collections_.Entries("kept"), SizeIs(1));

  EXPECT_THAT(Match({"-exec", "echo", "{}", "+"}, visit), IsTrue());
  EXPECT_THAT(Match({"-execdir", "echo", "{}", "+"}, visit), IsTrue());
  provide_exec_batches_ = true;
  EXPECT_THAT(Match({"-exec", "echo", "{}", "+"}, visit), IsTrue());
  EXPECT_THAT(exec_batches_, SizeIs(1));
  exec_batches_.clear();
  EXPECT_THAT(Match({"-execdir", "echo", "{}", "+"}, visit), IsTrue());
  EXPECT_THAT(exec_batches_, SizeIs(1));
}

TEST_F(EvaluateTest, ResultSetPredicatesWithoutDeferredEvaluationAreFalse) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/file", "file", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-top", "1"}, visit), IsFalse());
  EXPECT_THAT(Match({"-shard-status", "complete"}, visit), IsFalse());
}

TEST_F(EvaluateTest, PermSymbolicSpecialBits) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.mode = 04755;  // setuid + rwxr-xr-x
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-perm", "-u+s"}, visit), IsTrue());      // setuid bit set
  EXPECT_THAT(Match({"-perm", "-g+s"}, visit), IsFalse());     // setgid NOT set
  EXPECT_THAT(Match({"-perm", "-u+s,a+x"}, visit), IsTrue());  // setuid + all execute set
}

TEST_F(EvaluateTest, EmptyMatchesZeroByteFilesNotOthers) {
  vfs::Metadata empty_file;
  empty_file.type = vfs::FileType::kRegular;
  empty_file.size = 0;
  EXPECT_THAT(Match({"-empty"}, Visit{.path = "f", .name = "f", .depth = 1, .metadata = empty_file}), IsTrue());

  vfs::Metadata nonempty_file;
  nonempty_file.type = vfs::FileType::kRegular;
  nonempty_file.size = 5;
  EXPECT_THAT(Match({"-empty"}, Visit{.path = "f", .name = "f", .depth = 1, .metadata = nonempty_file}), IsFalse());

  vfs::Metadata link;
  link.type = vfs::FileType::kSymlink;
  EXPECT_THAT(Match({"-empty"}, Visit{.path = "l", .name = "l", .depth = 1, .metadata = link}), IsFalse())
      << "-empty matches only regular files and directories";
}

TEST_F(EvaluateTest, SparseMatchesFilesWithHoles) {
  vfs::Metadata sparse;
  sparse.type = vfs::FileType::kRegular;
  sparse.size = 1'000'000;
  sparse.blocks = 8;  // 8 * 512 = 4096, far less than the 1 MB apparent size -> sparse
  EXPECT_THAT(Match({"-sparse"}, Visit{.path = "s", .name = "s", .depth = 1, .metadata = sparse}), IsTrue());

  vfs::Metadata dense;
  dense.type = vfs::FileType::kRegular;
  dense.size = 4'096;
  dense.blocks = 8;  // 4096 fully allocated -> not sparse
  EXPECT_THAT(Match({"-sparse"}, Visit{.path = "d", .name = "d", .depth = 1, .metadata = dense}), IsFalse());

  vfs::Metadata empty;
  empty.type = vfs::FileType::kRegular;  // zero-size is not sparse
  EXPECT_THAT(Match({"-sparse"}, Visit{.path = "e", .name = "e", .depth = 1, .metadata = empty}), IsFalse());
}

TEST_F(EvaluateTest, LinksMatchesHardLinkCount) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.nlink = 1;
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-links", "1"}, visit), IsTrue());
  EXPECT_THAT(Match({"-links", "2"}, visit), IsFalse());
  EXPECT_THAT(Match({"-links", "-2"}, visit), IsTrue());
  EXPECT_THAT(Match({"-links", "+0"}, visit), IsTrue());
  EXPECT_THAT(Match({"-links", "+1"}, visit), IsFalse());
}

TEST_F(EvaluateTest, NewerFalseWhenReferenceMissing) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  // The reference cannot be stat'd, so -newer is false (real comparisons are
  // covered by the conformance test against actual files).
  EXPECT_THAT(Match({"-newer", "/no/such/reference/file"}, visit), IsFalse());
}

TEST_F(EvaluateTest, InumMatchesInodeNumber) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.ino = 4'242;
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-inum", "4242"}, visit), IsTrue());
  EXPECT_THAT(Match({"-inum", "4243"}, visit), IsFalse());
  EXPECT_THAT(Match({"-inum", "-5000"}, visit), IsTrue());   // inode < 5000
  EXPECT_THAT(Match({"-inum", "+4242"}, visit), IsFalse());  // not strictly greater than itself
}

TEST_F(EvaluateTest, UsedMatchesWholeDaysBetweenAtimeAndCtime) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.ctime = absl::FromUnixSeconds(1'600'000'000);
  md.atime = md.ctime + absl::Hours(24 * 5) + absl::Hours(3);  // 5 days 3h later -> floor 5
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-used", "5"}, visit), IsTrue());  // exactly 5 whole days
  EXPECT_THAT(Match({"-used", "6"}, visit), IsFalse());
  EXPECT_THAT(Match({"-used", "+4"}, visit), IsTrue());  // more than 4
  EXPECT_THAT(Match({"-used", "+5"}, visit), IsFalse());
  EXPECT_THAT(Match({"-used", "-6"}, visit), IsTrue());  // fewer than 6
  EXPECT_THAT(Match({"-used", "-5"}, visit), IsFalse());

  // atime before ctime yields a negative day delta (accessed before status change).
  vfs::Metadata before;
  before.type = vfs::FileType::kRegular;
  before.ctime = absl::FromUnixSeconds(1'600'000'000);
  before.atime = before.ctime - absl::Hours(24 * 2);  // 2 days earlier -> -2
  const Visit earlier{.path = "g", .name = "g", .depth = 1, .metadata = before};
  EXPECT_THAT(Match({"-used", "0"}, earlier), IsFalse());  // -2 is not 0
  EXPECT_THAT(Match({"-used", "-1"}, earlier), IsTrue());  // -2 < 1
}

TEST_F(EvaluateTest, SamefileFalseWhenReferenceMissing) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  // No reference to stat, so -samefile is false (the real same-inode match is
  // covered by the conformance test against an actual hard link).
  EXPECT_THAT(Match({"-samefile", "/no/such/reference/file"}, visit), IsFalse());
}

TEST_F(EvaluateTest, AccessReadableWritableExecutable) {
  namespace stdfs = ::std::filesystem;
  const stdfs::path tmp = stdfs::temp_directory_path() / "xff_access_probe.tmp";
  { std::ofstream(tmp) << "x"; }
  stdfs::permissions(tmp, stdfs::perms::owner_read | stdfs::perms::owner_write);  // rw, no execute bit
  const std::string path = tmp.string();
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  const Visit visit{.path = path, .name = "xff_access_probe.tmp", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-readable"}, visit), IsTrue());
  EXPECT_THAT(Match({"-writable"}, visit), IsTrue());
  EXPECT_THAT(Match({"-executable"}, visit), IsFalse());  // no execute bit (X_OK is not bypassed, even for root)
  stdfs::remove(tmp);
  EXPECT_THAT(Match({"-readable"}, visit), IsFalse());  // gone -> not accessible
}

TEST_F(EvaluateTest, FstypeMatchesTheHostingFilesystem) {
  namespace stdfs = ::std::filesystem;
  const stdfs::path tmp = stdfs::temp_directory_path() / "xff_fstype_probe.tmp";
  { std::ofstream(tmp) << "x"; }
  const std::string path = tmp.string();
  // The recognised names are platform-specific (apfs, ext2/ext3, tmpfs, ...), so
  // learn the temp filesystem's actual type through the same VFS the predicate
  // uses, then assert the match is by exact name -- and that an unrelated name
  // does not match.
  const absl::StatusOr<std::string> actual = fs_.FsType(path);
  ASSERT_THAT(actual, IsOkAndHolds(Not(IsEmpty())));
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  const Visit visit{.path = path, .name = "xff_fstype_probe.tmp", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-fstype", *actual}, visit), IsTrue());
  EXPECT_THAT(Match({"-fstype", "xff_no_such_fstype"}, visit), IsFalse());
  stdfs::remove(tmp);
  EXPECT_THAT(Match({"-fstype", *actual}, visit), IsFalse());  // gone -> statfs fails -> never matches
}

TEST_F(EvaluateTest, ContentMatchesLiteralSubstring) {
  const std::string path = WriteContentFile("literal.txt", "the quick brown fox\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "literal.txt", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-content", "quick brown"}, visit), IsTrue());
  EXPECT_THAT(Match({"-content", "fox"}, visit), IsTrue());
  EXPECT_THAT(Match({"-content", "QUICK"}, visit), IsFalse());     // -content is case-sensitive
  EXPECT_THAT(Match({"-content", "lazy dog"}, visit), IsFalse());  // absent
  // '.' is a literal character, not a regex wildcard: "q.ick" matches "quick" as a
  // regex but not as the literal substring -content searches for.
  EXPECT_THAT(Match({"-content", "q.ick"}, visit), IsFalse());
}

TEST_F(EvaluateTest, DiffTreatsUnreadableSourceAsDifferent) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("missing-source", "missing-source", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-diff:none", "missing-target"}, visit), IsFalse());
}

TEST_F(EvaluateTest, IcontentFoldsCase) {
  const std::string path = WriteContentFile("fold.txt", "Hello World");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "fold.txt", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-icontent", "hello"}, visit), IsTrue());
  EXPECT_THAT(Match({"-icontent", "WORLD"}, visit), IsTrue());
  EXPECT_THAT(Match({"-icontent", "goodbye"}, visit), IsFalse());
}

TEST_F(EvaluateTest, RxcMatchesRegexAnywhere) {
  const std::string path = WriteContentFile("rx.txt", "id=12345 name=foo\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "rx.txt", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-rxc", "id=[0-9]+"}, visit), IsTrue());  // unanchored: matches a substring
  EXPECT_THAT(Match({"-rxc", "name=[a-z]+"}, visit), IsTrue());
  EXPECT_THAT(Match({"-rxc", "id=[a-z]+"}, visit), IsFalse());  // digits are not letters
  EXPECT_THAT(Match({"-rxc", "^name="}, visit), IsFalse());     // 'name=' is not at the start of the content
}

TEST_F(EvaluateTest, RxcUnderExactMatchesLiterally) {
  // --regextype=EXACT reaches -rxc too (not just -grep): the content predicate matches the argument
  // as a literal substring, so metacharacters are plain text.
  const std::string path = WriteContentFile("rx_exact.txt", "value = a[0-9]b here\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "rx_exact.txt", vfs::FileType::kRegular, md);
  regextype_ = "EXACT";
  EXPECT_THAT(Match({"-rxc", "a[0-9]b"}, visit), IsTrue());  // the literal bracket text is present
  EXPECT_THAT(Match({"-rxc", "a5b"}, visit), IsFalse());     // the regex interpretation is gone under EXACT
}

TEST_F(EvaluateTest, RxcUnderFnmatchUsesShellWildcards) {
  // --regextype=FNMATCH treats the -rxc argument as a shell glob: `*` matches any run, `?` one char.
  const std::string path = WriteContentFile("rx_glob.txt", "the quick brown fox\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "rx_glob.txt", vfs::FileType::kRegular, md);
  regextype_ = "FNMATCH";
  EXPECT_THAT(Match({"-rxc", "quick*fox"}, visit), IsTrue());   // glob spans the middle
  EXPECT_THAT(Match({"-rxc", "q?ick"}, visit), IsTrue());       // '?' matches one char
  EXPECT_THAT(Match({"-rxc", "quick.fox"}, visit), IsFalse());  // '.' is literal in a glob, no dot here
}

TEST_F(EvaluateTest, RxcUnderGlobUsesPathAwareGlob) {
  // --regextype=GLOB reaches -rxc: a path-aware shell glob (`*` stops at '/', `**` crosses).
  const std::string path = WriteContentFile("rx_pathglob.txt", "path is src/app/main.cc\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "rx_pathglob.txt", vfs::FileType::kRegular, md);
  regextype_ = "GLOB";
  EXPECT_THAT(Match({"-rxc", "src/**/main.cc"}, visit), IsTrue());  // `**` crosses the 'app' directory
  EXPECT_THAT(Match({"-rxc", "src/*/main.cc"}, visit), IsTrue());   // one segment
  EXPECT_THAT(Match({"-rxc", "src/main.cc"}, visit), IsFalse());  // `*`/segment does not cross '/', 'app' is in the way
}

TEST_F(EvaluateTest, IrxcFoldsCase) {
  const std::string path = WriteContentFile("irx.txt", "STATUS: OK");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "irx.txt", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-irxc", "status: ok"}, visit), IsTrue());
  EXPECT_THAT(Match({"-rxc", "status: ok"}, visit), IsFalse());  // the case-sensitive form does not match
}

TEST_F(EvaluateTest, ContentSkipsBinaryFiles) {
  // A NUL byte in the sniffed prefix marks the file binary; content search skips it,
  // so even a literal substring that is present does not match (grep/rg behaviour).
  const std::string path = WriteContentFile("bin", std::string("ELF\0needle here", 15));
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "bin", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-content", "needle"}, visit), IsFalse());
  EXPECT_THAT(Match({"-rxc", "needle"}, visit), IsFalse());
}

TEST_F(EvaluateTest, TextFlavorsEnforceTheirDocumentedLineEndings) {
  const auto matches = [this](std::string_view tag, std::string_view content, std::string_view flavor) {
    const std::string path = WriteContentFile(tag, content);
    vfs::Metadata md;
    const Visit visit = MakeVisit(path, tag, vfs::FileType::kRegular, md);
    return Match({absl::StrCat("-text:", flavor)}, visit);
  };

  EXPECT_THAT(matches("empty", "", "posix"), IsTrue());
  EXPECT_THAT(matches("posix", "one\ntwo\n", "posix"), IsTrue());
  EXPECT_THAT(matches("posix_no_eol", "one", "posix"), IsFalse());
  EXPECT_THAT(matches("posix_cr", "one\r\n", "posix"), IsFalse());
  EXPECT_THAT(matches("windows", "one\r\ntwo\r\n", "windows"), IsTrue());
  EXPECT_THAT(matches("windows_bare_lf", "one\n", "windows"), IsFalse());
  EXPECT_THAT(matches("windows_bare_cr", "one\rX\r\n", "windows"), IsFalse());
  EXPECT_THAT(matches("windows_no_eol", "one", "windows"), IsFalse());
  EXPECT_THAT(matches("apple", "one\rtwo\r", "apple"), IsTrue());
  EXPECT_THAT(matches("apple_lf", "one\n", "apple"), IsFalse());
  EXPECT_THAT(matches("apple_no_eol", "one", "apple"), IsFalse());
  EXPECT_THAT(matches("strict_nul", std::string_view("one\0two\n", 8), "posix"), IsFalse());
}

TEST_F(EvaluateTest, TextFlavorsIgnoreOneLeadingUtf8Bom) {
  const auto matches = [this](std::string_view tag, std::string_view content, std::string_view flavor) {
    const std::string path = WriteContentFile(tag, content);
    vfs::Metadata md;
    const Visit visit = MakeVisit(path, tag, vfs::FileType::kRegular, md);
    return Match({absl::StrCat("-text:", flavor)}, visit);
  };

  EXPECT_THAT(matches("bom_git", "\xef\xbb\xbftext", "git"), IsTrue());
  EXPECT_THAT(matches("bom_posix", "\xef\xbb\xbftext\n", "posix"), IsTrue());
  EXPECT_THAT(matches("bom_windows", "\xef\xbb\xbftext\r\n", "windows"), IsTrue());
  EXPECT_THAT(matches("bom_apple", "\xef\xbb\xbftext\r", "apple"), IsTrue());
  EXPECT_THAT(matches("bom_only_posix", "\xef\xbb\xbf", "posix"), IsTrue());
  EXPECT_THAT(matches("bom_only_windows", "\xef\xbb\xbf", "windows"), IsTrue());
  EXPECT_THAT(matches("bom_only_apple", "\xef\xbb\xbf", "apple"), IsTrue());
  EXPECT_THAT(matches("two_boms", "\xef\xbb\xbf\xef\xbb\xbf", "posix"), IsFalse());
  EXPECT_THAT(matches("utf16", std::string_view("\xff\xfe\0t\0e\0x\0t", 10), "posix"), IsFalse());
}

TEST_F(EvaluateTest, GitTextAndBinaryUseOnlyTheLeadingNulSniffWindow) {
  vfs::Metadata md;
  std::string content(8'001, 'x');
  content.back() = '\0';
  const std::string late_nul = WriteContentFile("late_nul", content);
  const Visit late = MakeVisit(late_nul, "late_nul", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-text"}, late), IsTrue());
  EXPECT_THAT(Match({"-binary"}, late), IsFalse());

  const std::string early_nul = WriteContentFile("early_nul", std::string_view("x\0y", 3));
  const Visit early = MakeVisit(early_nul, "early_nul", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-text:git"}, early), IsFalse());
  EXPECT_THAT(Match({"-binary"}, early), IsTrue());

  vfs::Metadata dir_md;
  const Visit directory = MakeVisit("directory", "directory", vfs::FileType::kDirectory, dir_md);
  EXPECT_THAT(Match({"-text"}, directory), IsFalse());
  EXPECT_THAT(Match({"-binary"}, directory), IsFalse());
}

TEST_F(EvaluateTest, GrepEmitsMatchingLinesAsPathLineText) {
  const std::string path = WriteContentFile("grep.txt", "first TODO line\nsecond line\nanother TODO here\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "grep.txt", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-grep", "TODO"}, visit), IsTrue());  // returns true because a line matched
  EXPECT_THAT(emitted_, EqualsText(absl::StrCat(path, ":1:first TODO line\n", path, ":3:another TODO here\n")));
}

TEST_F(EvaluateTest, GrepUsesRegexNotLiteral) {
  const std::string path = WriteContentFile("grep_rx.txt", "id=12345\nname=foo\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "grep_rx.txt", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-grep", "id=[0-9]+"}, visit), IsTrue());
  EXPECT_THAT(emitted_, EqualsText(absl::StrCat(path, ":1:id=12345\n")));
}

TEST_F(EvaluateTest, GrepWithNoMatchingLineIsFalseAndSilent) {
  const std::string path = WriteContentFile("grep_none.txt", "nothing to see\nhere at all\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "grep_none.txt", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-grep", "TODO"}, visit), IsFalse());
  EXPECT_THAT(emitted_, IsEmpty());
}

TEST_F(EvaluateTest, GrepSkipsBinaryFiles) {
  const std::string path = WriteContentFile("grep_bin", std::string("ELF\0TODO here\n", 14));
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "grep_bin", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-grep", "TODO"}, visit), IsFalse());
  EXPECT_THAT(emitted_, IsEmpty());
}

TEST_F(EvaluateTest, GrepExactModeMatchesLiterally) {
  // grep_literal (--regextype=EXACT) treats the pattern as a literal substring, so
  // '.' is a literal dot, not a regex wildcard.
  const std::string path = WriteContentFile("grep_exact.txt", "price 3.50\nprice 3X50\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "grep_exact.txt", vfs::FileType::kRegular, md);
  regextype_ = "EXACT";
  EXPECT_THAT(Match({"-grep", "3.50"}, visit), IsTrue());
  EXPECT_THAT(emitted_, EqualsText(absl::StrCat(path, ":1:price 3.50\n")));  // only the literal 3.50, not 3X50
}

TEST_F(EvaluateTest, GrepExactModeAcceptsRegexMetacharactersAsLiterals) {
  // A pattern that is not a valid regex is still a fine literal under EXACT.
  const std::string path = WriteContentFile("grep_lit.txt", "call foo(bar) now\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "grep_lit.txt", vfs::FileType::kRegular, md);
  regextype_ = "EXACT";
  EXPECT_THAT(Match({"-grep", "foo(bar"}, visit), IsTrue());
  EXPECT_THAT(emitted_, EqualsText(absl::StrCat(path, ":1:call foo(bar) now\n")));
}

TEST_F(EvaluateTest, GrepFormatRendersTemplatePerMatchLine) {
  // -grep:FORMAT overrides the default path:line:text; {line}/{text} bind per line.
  const std::string path = WriteContentFile("grep_fmt.txt", "alpha\nhit one\nbeta\nhit two\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "grep_fmt.txt", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-grep:{line}: {text}", "hit"}, visit), IsTrue());
  EXPECT_THAT(emitted_, EqualsText("2: hit one\n4: hit two\n"));
}

TEST_F(EvaluateTest, GrepFormatCanReferenceEntryFields) {
  // The template sees the entry's field vocabulary too, not just {line}/{text}.
  const std::string path = WriteContentFile("grep_fmt2.txt", "x hit y\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "grep_fmt2.txt", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-grep:{path}#{line}", "hit"}, visit), IsTrue());
  EXPECT_THAT(emitted_, EqualsText(absl::StrCat(path, "#1\n")));
}

TEST_F(EvaluateTest, GrepFormatMatchAndColumnExtractTheMatch) {
  // {match} is the matched substring (grep -o), {column} its 1-based start.
  const std::string path = WriteContentFile("grep_o.txt", "code E42 here\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "grep_o.txt", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-grep:{column}:{match}", "E[0-9]+"}, visit), IsTrue());
  EXPECT_THAT(emitted_, EqualsText("6:E42\n"));  // E42 starts at column 6 (after "code ")
}

TEST_F(EvaluateTest, GrepFormatMatchInExactModeUsesTheLiteralSpan) {
  const std::string path = WriteContentFile("grep_o2.txt", "aXbXc\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "grep_o2.txt", vfs::FileType::kRegular, md);
  regextype_ = "EXACT";
  EXPECT_THAT(Match({"-grep:{column} {match}", "X"}, visit), IsTrue());
  EXPECT_THAT(emitted_, EqualsText("2 X\n"));  // first literal X at column 2
}

TEST_F(EvaluateTest, GrepCountEmitsPerFileMatchLineCount) {
  // --count: one path:count (matching lines) instead of the lines; supersedes FORMAT.
  const std::string path = WriteContentFile("grep_c.txt", "TODO a\nx\nTODO b\nTODO c\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "grep_c.txt", vfs::FileType::kRegular, md);
  grep_count_ = true;
  EXPECT_THAT(Match({"-grep:{line}", "TODO"}, visit), IsTrue());  // FORMAT is superseded by --count
  EXPECT_THAT(emitted_, EqualsText(absl::StrCat(path, ":3\n")));
}

TEST_F(EvaluateTest, GrepCountEmitsNothingWhenNoLineMatches) {
  const std::string path = WriteContentFile("grep_c0.txt", "nothing here\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "grep_c0.txt", vfs::FileType::kRegular, md);
  grep_count_ = true;
  EXPECT_THAT(Match({"-grep", "TODO"}, visit), IsFalse());  // no match -> false, and no count line
  EXPECT_THAT(emitted_, IsEmpty());
}

TEST_F(EvaluateTest, ContentNonRegularOrMissingDoesNotMatch) {
  // A directory has no searchable content and a missing path is unreadable: both are
  // a clean no-match, not an error.
  vfs::Metadata dir_md;
  const Visit dir = MakeVisit(".", ".", vfs::FileType::kDirectory, dir_md);
  EXPECT_THAT(Match({"-content", "anything"}, dir), IsFalse());
  vfs::Metadata md;
  const Visit missing = MakeVisit("/no/such/xff/file", "file", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-content", "anything"}, missing), IsFalse());
  EXPECT_THAT(Match({"-rxc", "anything"}, missing), IsFalse());
}

TEST_F(EvaluateTest, LnameGlobsSymlinkTarget) {
  namespace stdfs = ::std::filesystem;
  const stdfs::path link = stdfs::temp_directory_path() / "xff_lname_probe.link";
  stdfs::remove(link);  // clear any leftover from a previous run
  stdfs::create_symlink("/some/Where/target.txt", link);
  const std::string path = link.string();
  vfs::Metadata md;
  md.type = vfs::FileType::kSymlink;
  const Visit visit{.path = path, .name = "xff_lname_probe.link", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-lname", "*/target.txt"}, visit), IsTrue());   // glob against the (unresolved) target text
  EXPECT_THAT(Match({"-lname", "*/TARGET.txt"}, visit), IsFalse());  // -lname is case-sensitive
  EXPECT_THAT(Match({"-ilname", "*/TARGET.txt"}, visit), IsTrue());  // -ilname folds case
  // A non-symlink never matches, even when the target glob otherwise would.
  vfs::Metadata reg;
  reg.type = vfs::FileType::kRegular;
  const Visit regular{.path = path, .name = "xff_lname_probe.link", .depth = 1, .metadata = reg};
  EXPECT_THAT(Match({"-lname", "*"}, regular), IsFalse());
  stdfs::remove(link);
}

TEST_F(EvaluateTest, XtypeFollowsSymlinkTarget) {
  namespace stdfs = ::std::filesystem;
  const stdfs::path dir = stdfs::temp_directory_path() / "xff_xtype_probe.d";
  stdfs::remove_all(dir);
  ASSERT_THAT(stdfs::create_directories(dir), IsTrue());
  const stdfs::path file = dir / "target.txt";
  { std::ofstream(file) << "x"; }
  stdfs::create_symlink(file, dir / "to_file");
  stdfs::create_symlink(dir, dir / "to_dir");
  stdfs::create_symlink(dir / "missing", dir / "broken");  // dangling

  const std::string to_file = (dir / "to_file").string();
  const std::string to_dir = (dir / "to_dir").string();
  const std::string broken = (dir / "broken").string();
  const std::string file_s = file.string();
  vfs::Metadata link_md;
  link_md.type = vfs::FileType::kSymlink;
  const Visit v_file{.path = to_file, .name = "to_file", .depth = 1, .metadata = link_md};
  const Visit v_dir{.path = to_dir, .name = "to_dir", .depth = 1, .metadata = link_md};
  const Visit v_broken{.path = broken, .name = "broken", .depth = 1, .metadata = link_md};
  EXPECT_THAT(Match({"-xtype", "f"}, v_file), IsTrue());  // link -> regular file
  EXPECT_THAT(Match({"-xtype", "d"}, v_file), IsFalse());
  EXPECT_THAT(Match({"-xtype", "d"}, v_dir), IsTrue());     // link -> directory
  EXPECT_THAT(Match({"-xtype", "l"}, v_broken), IsTrue());  // broken link reports type l
  EXPECT_THAT(Match({"-xtype", "f"}, v_broken), IsFalse());
  // A non-symlink behaves exactly like -type.
  vfs::Metadata reg_md;
  reg_md.type = vfs::FileType::kRegular;
  const Visit v_reg{.path = file_s, .name = "target.txt", .depth = 1, .metadata = reg_md};
  EXPECT_THAT(Match({"-xtype", "f"}, v_reg), IsTrue());
  EXPECT_THAT(Match({"-xtype", "l"}, v_reg), IsFalse());
  stdfs::remove_all(dir);
}

TEST_F(EvaluateTest, MTimeMatchesWholeDaysAgo) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.mtime = now_ - absl::Hours(60);  // 2.5 days ago -> floor to 2 whole days
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-mtime", "2"}, visit), IsTrue());  // floor(2.5) == 2
  EXPECT_THAT(Match({"-mtime", "3"}, visit), IsFalse());
  EXPECT_THAT(Match({"-mtime", "+1"}, visit), IsTrue());   // strictly older than 1 day
  EXPECT_THAT(Match({"-mtime", "+2"}, visit), IsFalse());  // not strictly older than 2
  EXPECT_THAT(Match({"-mtime", "-3"}, visit), IsTrue());   // strictly younger than 3 days
}

TEST_F(EvaluateTest, MMinMatchesWholeMinutesAgo) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.mtime = now_ - absl::Minutes(150);  // 150 minutes ago
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-mmin", "150"}, visit), IsTrue());
  EXPECT_THAT(Match({"-mmin", "+100"}, visit), IsTrue());
  EXPECT_THAT(Match({"-mmin", "+150"}, visit), IsFalse());
  EXPECT_THAT(Match({"-mmin", "-200"}, visit), IsTrue());
}

TEST_F(EvaluateTest, BTimeMatchesWholeDaysSinceBirth) {
  // BSD -Btime: the -mtime of the birth time. Same whole-day floor / +N older /
  // -N younger semantics, measured against `btime`.
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.btime = now_ - absl::Hours(60);  // born 2.5 days ago -> floor to 2 whole days
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-Btime", "2"}, visit), IsTrue());  // floor(2.5) == 2
  EXPECT_THAT(Match({"-Btime", "3"}, visit), IsFalse());
  EXPECT_THAT(Match({"-Btime", "+1"}, visit), IsTrue());   // strictly older than 1 day
  EXPECT_THAT(Match({"-Btime", "+2"}, visit), IsFalse());  // not strictly older than 2
  EXPECT_THAT(Match({"-Btime", "-3"}, visit), IsTrue());   // strictly younger than 3 days
}

TEST_F(EvaluateTest, BMinMatchesWholeMinutesSinceBirth) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.btime = now_ - absl::Minutes(150);  // born 150 minutes ago
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-Bmin", "150"}, visit), IsTrue());
  EXPECT_THAT(Match({"-Bmin", "+100"}, visit), IsTrue());
  EXPECT_THAT(Match({"-Bmin", "+150"}, visit), IsFalse());
  EXPECT_THAT(Match({"-Bmin", "-200"}, visit), IsTrue());
}

TEST_F(EvaluateTest, BirthtimePredicatesNeverMatchWhenBtimeUnrecorded) {
  // btime is optional: when the kernel/FS did not record it, -Btime/-Bmin match
  // nothing -- there is no value to compare -- and must not borrow another stamp.
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;  // md.btime deliberately left empty
  md.mtime = now_ - absl::Hours(60);  // a present mtime must not be used as a fallback
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-Btime", "2"}, visit), IsFalse());
  EXPECT_THAT(Match({"-Btime", "-9999"}, visit), IsFalse());  // even a wide window: no btime -> no match
  EXPECT_THAT(Match({"-Bmin", "-9999"}, visit), IsFalse());
}

TEST_F(EvaluateTest, BirthtimePredicateFlagsUnsupportedWhenBtimeUnrecorded) {
  // An unrecorded birth time is an impossible task: besides not matching, the
  // predicate raises the control side-channel so the driver can fail (or, under
  // --skip-unsupported, warn and skip). Covers -Btime/-Bmin and the X=B -newerXY.
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;  // no btime
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-Btime", "2"}, visit), IsFalse());
  EXPECT_THAT(control_.unsupported, Not(IsEmpty()));
  EXPECT_THAT(Match({"-Bmin", "2"}, visit), IsFalse());
  EXPECT_THAT(control_.unsupported, Not(IsEmpty()));
  EXPECT_THAT(Match({"-newerBt", "@1"}, visit), IsFalse());
  EXPECT_THAT(control_.unsupported, Not(IsEmpty()));
}

TEST_F(EvaluateTest, BirthtimePredicateDoesNotFlagUnsupportedWhenBtimePresent) {
  // With a recorded birth time the predicate evaluates normally and leaves the
  // control side-channel clear (no impossible-task signal).
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.btime = now_ - absl::Hours(60);
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-Btime", "2"}, visit), IsTrue());
  EXPECT_THAT(control_.unsupported, IsEmpty());
}

TEST_F(EvaluateTest, MTimeAcceptsBsdUnitSuffix) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.mtime = now_ - absl::Hours(3);  // 3 hours ago
  md.atime = now_ - absl::Hours(3);
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  // A BSD s/m/h/d/w suffix overrides the day default (here, hours).
  EXPECT_THAT(Match({"-mtime", "3h"}, visit), IsTrue());     // floor(3h / 1h) == 3
  EXPECT_THAT(Match({"-mtime", "+2h"}, visit), IsTrue());    // older than 2 hours
  EXPECT_THAT(Match({"-mtime", "+3h"}, visit), IsFalse());   // not older than 3
  EXPECT_THAT(Match({"-mtime", "-4h"}, visit), IsTrue());    // younger than 4 hours
  EXPECT_THAT(Match({"-mtime", "-3h"}, visit), IsFalse());   // not younger than 3
  EXPECT_THAT(Match({"-mtime", "+179m"}, visit), IsTrue());  // 180 min old, older than 179 minutes
  EXPECT_THAT(Match({"-mtime", "-1d"}, visit), IsTrue());    // younger than 1 day
  EXPECT_THAT(Match({"-mtime", "+1s"}, visit), IsTrue());    // older than 1 second
  EXPECT_THAT(Match({"-atime", "+2h"}, visit), IsTrue());    // suffix works on -atime too
  EXPECT_THAT(Match({"-mtime", "3x"}, visit), IsFalse());    // unrecognised suffix -> no match
}

TEST_F(EvaluateTest, MMinRejectsUnitSuffix) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.mtime = now_ - absl::Minutes(150);
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  // -mmin is GNU integer-minutes only; a BSD-style suffix is not its vocabulary.
  EXPECT_THAT(Match({"-mmin", "150m"}, visit), IsFalse());
  EXPECT_THAT(Match({"-mmin", "-3h"}, visit), IsFalse());
}

TEST_F(EvaluateTest, MTimeAcceptsXffWordDuration) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.mtime = now_ - absl::Hours(24 * 7 * 3) - absl::Hours(3);  // 3 weeks 3 hours ago
  md.atime = md.mtime;
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  // "+span" = older than the span; "-span" = younger than it (the span reaches
  // back through ParseTimeString, so compound terms work).
  EXPECT_THAT(Match({"-mtime", "+3 weeks"}, visit), IsTrue());           // 3w3h old is older than 3 weeks
  EXPECT_THAT(Match({"-mtime", "+3 weeks 4 hours"}, visit), IsFalse());  // but not older than 3w4h
  EXPECT_THAT(Match({"-mtime", "-3 weeks 4 hours"}, visit), IsTrue());   // it is younger than 3w4h
  EXPECT_THAT(Match({"-mtime", "-3 weeks"}, visit), IsFalse());          // not younger than 3 weeks
  EXPECT_THAT(Match({"-atime", "+1 day"}, visit), IsTrue());             // works on -atime too
  EXPECT_THAT(Match({"-mtime", "3 weeks"}, visit), IsFalse());           // the word form requires an explicit sign
}

TEST_F(EvaluateTest, UidAndGidMatchNumericOwner) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.uid = 501;
  md.gid = 20;
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-uid", "501"}, visit), IsTrue());
  EXPECT_THAT(Match({"-uid", "500"}, visit), IsFalse());
  EXPECT_THAT(Match({"-uid", "+500"}, visit), IsTrue());  // uid strictly greater than 500
  EXPECT_THAT(Match({"-gid", "20"}, visit), IsTrue());
  EXPECT_THAT(Match({"-gid", "21"}, visit), IsFalse());
  EXPECT_THAT(Match({"-gid", "-21"}, visit), IsTrue());  // gid strictly less than 21
}

TEST_F(EvaluateTest, AccessAndChangeTimeFamily) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.atime = now_ - absl::Hours(60);     // 2.5 days / 3600 minutes ago
  md.ctime = now_ - absl::Minutes(150);  // 2.5 hours / 150 minutes ago
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-atime", "2"}, visit), IsTrue());  // -atime/-amin read atime: floor(2.5 days) == 2
  EXPECT_THAT(Match({"-atime", "+1"}, visit), IsTrue());
  EXPECT_THAT(Match({"-amin", "3600"}, visit), IsTrue());
  EXPECT_THAT(Match({"-amin", "+3000"}, visit), IsTrue());
  EXPECT_THAT(Match({"-ctime", "0"}, visit), IsTrue());  // -ctime/-cmin read ctime: 2.5 hours floors to 0 days
  EXPECT_THAT(Match({"-cmin", "150"}, visit), IsTrue());
  EXPECT_THAT(Match({"-cmin", "+150"}, visit), IsFalse());
}

TEST_F(EvaluateTest, UserGroupNumericFallback) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.uid = 501;
  md.gid = 20;
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  // A token that is not a known user/group but is all-digits is taken as a
  // literal id (matching find); real-name resolution is covered by the
  // conformance test against the current user/group.
  EXPECT_THAT(Match({"-user", "501"}, visit), IsTrue());
  EXPECT_THAT(Match({"-user", "500"}, visit), IsFalse());
  EXPECT_THAT(Match({"-group", "20"}, visit), IsTrue());
  EXPECT_THAT(Match({"-group", "21"}, visit), IsFalse());
}

TEST_F(EvaluateTest, NouserNogroupMatchOrphanedIds) {
  vfs::Metadata owned_md;
  owned_md.type = vfs::FileType::kRegular;
  owned_md.uid = 0;  // root: present in passwd
  owned_md.gid = 0;  // present in group
  const Visit owned{.path = "a", .name = "a", .depth = 1, .metadata = owned_md};
  EXPECT_THAT(Match({"-nouser"}, owned), IsFalse());
  EXPECT_THAT(Match({"-nogroup"}, owned), IsFalse());
  vfs::Metadata orphan_md;
  orphan_md.type = vfs::FileType::kRegular;
  orphan_md.uid = 2'000'000'001U;  // unassigned on Linux/macOS runners (avoids macOS nobody == -2)
  orphan_md.gid = 2'000'000'001U;
  const Visit orphan{.path = "b", .name = "b", .depth = 1, .metadata = orphan_md};
  EXPECT_THAT(Match({"-nouser"}, orphan), IsTrue());
  EXPECT_THAT(Match({"-nogroup"}, orphan), IsTrue());
}

TEST_F(EvaluateTest, CommaEvaluatesBothValueIsRight) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  // The comma list evaluates both operands; its value is the right operand's.
  EXPECT_THAT(Match({"-true", ",", "-false"}, visit), IsFalse());
  EXPECT_THAT(Match({"-false", ",", "-true"}, visit), IsTrue());
  // Side effects on both sides still occur: two -print actions emit two records.
  EXPECT_THAT(Match({"-print", ",", "-print"}, visit), IsTrue());
  EXPECT_THAT(emitted_, "f\nf\n");
}

TEST_F(EvaluateTest, WriteActionsRefuseAVirtualEntryInsteadOfActingOnIt) {
  // An archive member exists only inside its container: there is no path to unlink and none a child
  // process could open. Before this, -delete silently did nothing (exit 0, no message) and -exec handed
  // the child `a.tar!x`. Each write action must instead report an impossible task, which the driver
  // turns into a hard error naming the path - or, under --skip-unsupported, a warning that skips it.
  vfs::Metadata member;
  member.type = vfs::FileType::kRegular;
  member.source = vfs::Source::kArchiveMember;
  // A named list rather than an inline braced-init-list (STYLE_CPP), and `static` rather than
  // `constexpr` because a vector of strings is not a constant expression.
  static const std::array<std::vector<std::string>, 4> kWriteActions{
      std::vector<std::string>{"-delete"},
      std::vector<std::string>{"-exec", "true", ";"},
      std::vector<std::string>{"-exec", "true", "{}", "+"},
      std::vector<std::string>{"-execdir", "true", ";"},
  };
  for (const std::vector<std::string>& action : kWriteActions) {
    const std::string label = action.front();
    EXPECT_THAT(Match(action, Visit{.path = "a.tar!x", .name = "x", .depth = 1, .metadata = member}), IsFalse())
        << label;
    EXPECT_THAT(control_.unsupported, HasSubstr("archive member")) << label;
  }
}

TEST_F(EvaluateTest, WriteActionsStillActOnARealFile) {
  // The guard keys on the entry's SOURCE, so an ordinary file is untouched by it: -exec still runs and
  // reports true, and nothing is recorded as unsupported.
  vfs::Metadata real;
  real.type = vfs::FileType::kRegular;
  EXPECT_THAT(Match({"-exec", "true", ";"}, Visit{.path = "f", .name = "f", .depth = 1, .metadata = real}), IsTrue());
  EXPECT_THAT(control_.unsupported, IsEmpty());
}

TEST_F(EvaluateTest, PruneAndQuitSetControl) {
  vfs::Metadata md;
  md.type = vfs::FileType::kDirectory;
  const Visit visit{.path = "d", .name = "d", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-prune"}, visit), IsTrue());
  EXPECT_THAT(control_.prune, IsTrue());
  EXPECT_THAT(control_.quit, IsFalse());
  EXPECT_THAT(Match({"-quit"}, visit), IsTrue());
  EXPECT_THAT(control_.quit, IsTrue());
  EXPECT_THAT(control_.prune, IsFalse());  // control is reset per Match
  // A short-circuited -prune does not fire: `-type f -prune` on a directory.
  EXPECT_THAT(Match({"-type", "f", "-prune"}, visit), IsFalse());
  EXPECT_THAT(control_.prune, IsFalse());
}

TEST_F(EvaluateTest, RegexMatchesWholePath) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("a/b/c.txt", "c.txt", vfs::FileType::kRegular, md);
  // -regex matches the whole path (not just the basename); -iregex folds case.
  EXPECT_THAT(Match({"-regex", ".*\\.txt"}, visit), IsTrue());
  EXPECT_THAT(Match({"-regex", ".*\\.md"}, visit), IsFalse());
  EXPECT_THAT(Match({"-regex", "c\\.txt"}, visit), IsFalse());  // must match the whole path, not the basename
  EXPECT_THAT(Match({"-iregex", ".*\\.TXT"}, visit), IsTrue());
}

TEST_F(EvaluateTest, NewerXYFalseWhenReferenceMissing) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("f", "f", vfs::FileType::kRegular, md);
  // -newerXY with an unreadable reference is false; real field comparisons are
  // covered by the conformance test against actual files.
  EXPECT_THAT(Match({"-newermm", "/no/such/reference"}, visit), IsFalse());
  EXPECT_THAT(Match({"-newerac", "/no/such/reference"}, visit), IsFalse());
}

TEST_F(EvaluateTest, AnewerCnewerCompareEntryTimeToReferenceMtime) {
  // -anewer/-cnewer are the classic spellings of -neweram/-newercm: the entry's
  // atime/ctime vs the reference file's mtime. False when the reference is gone.
  vfs::Metadata missing_md;
  const Visit missing = MakeVisit("f", "f", vfs::FileType::kRegular, missing_md);
  EXPECT_THAT(Match({"-anewer", "/no/such/reference"}, missing), IsFalse());
  EXPECT_THAT(Match({"-cnewer", "/no/such/reference"}, missing), IsFalse());

  namespace stdfs = ::std::filesystem;
  const stdfs::path ref = stdfs::temp_directory_path() / "xff_anewer_ref.tmp";
  { std::ofstream(ref) << "r"; }  // reference mtime is ~now (between the two fixed times below)
  const std::string ref_path = ref.string();
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.atime = absl::FromUnixSeconds(2'000'000'000);  // 2033, after the reference's mtime
  md.ctime = absl::FromUnixSeconds(1'000'000'000);  // 2001, before the reference's mtime
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-anewer", ref_path}, visit), IsTrue());   // atime newer than the reference mtime
  EXPECT_THAT(Match({"-cnewer", ref_path}, visit), IsFalse());  // ctime older than the reference mtime
  stdfs::remove(ref);
}

TEST_F(EvaluateTest, NewerBtComparesBirthTimeToTimeString) {
  // -newerBt (X=B, Y=t): the entry's birth time vs a time string. now_ == 1.7e9.
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.btime = now_;
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-newerBt", "@1500000000"}, visit), IsTrue());   // born after an earlier instant
  EXPECT_THAT(Match({"-newerBt", "@1700000001"}, visit), IsFalse());  // not after a later instant
}

TEST_F(EvaluateTest, NewerBCombosAreFalseWhenBirthTimeUnrecorded) {
  // Every -newerXY touching B is false when that birth time is unrecorded -- here
  // the entry's (X=B). Both the time-string and file-reference forms short-circuit.
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;  // no btime
  md.mtime = now_;
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-newerBt", "@1"}, visit), IsFalse());                  // X=B, no btime
  EXPECT_THAT(Match({"-newerBm", "/no/such/reference"}, visit), IsFalse());  // X=B (also a missing ref)
}

TEST_F(EvaluateTest, NewerBmComparesBirthTimeToReferenceMtime) {
  // -newerBm: the entry's birth time vs the reference FILE's mtime (X=B, file-ref).
  namespace stdfs = ::std::filesystem;
  const stdfs::path ref = stdfs::temp_directory_path() / "xff_newerbm_ref.tmp";
  { std::ofstream(ref) << "r"; }  // reference mtime is ~now (between the two fixed times below)
  const std::string ref_path = ref.string();
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.btime = absl::FromUnixSeconds(2'000'000'000);  // 2033, after the reference's mtime
  const Visit younger{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-newerBm", ref_path}, younger), IsTrue());
  md.btime = absl::FromUnixSeconds(1'000'000'000);  // 2001, before the reference's mtime
  const Visit older{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-newerBm", ref_path}, older), IsFalse());
  stdfs::remove(ref);
}

TEST_F(EvaluateTest, NewerMbComparesMtimeToReferenceBirthTime) {
  // -newermB (Y=B): the entry's mtime vs the reference FILE's birth time. Only
  // assert when the test filesystem records birth time -- otherwise the reference
  // btime is absent and the comparison is always false (mirroring -Btime), which
  // is not what this case means to exercise.
  namespace stdfs = ::std::filesystem;
  const stdfs::path ref = stdfs::temp_directory_path() / "xff_newermb_ref.tmp";
  { std::ofstream(ref) << "r"; }  // birth time is ~now (between the two fixed times below)
  const std::string ref_path = ref.string();
  MBO_ASSERT_OK_AND_ASSIGN(const vfs::Metadata ref_md, fs_.Stat(ref_path, /*follow_symlinks=*/true));
  if (ref_md.btime.has_value()) {
    vfs::Metadata md;
    md.type = vfs::FileType::kRegular;
    md.mtime = absl::FromUnixSeconds(2'000'000'000);  // 2033, after the reference's birth time
    const Visit younger{.path = "f", .name = "f", .depth = 1, .metadata = md};
    EXPECT_THAT(Match({"-newermB", ref_path}, younger), IsTrue());
    md.mtime = absl::FromUnixSeconds(1'000'000'000);  // 2001, before the reference's birth time
    const Visit older{.path = "f", .name = "f", .depth = 1, .metadata = md};
    EXPECT_THAT(Match({"-newermB", ref_path}, older), IsFalse());
  }
  stdfs::remove(ref);
}

TEST_F(EvaluateTest, PrintfExpandsDirectivesAndEscapes) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.size = 42;
  md.mode = 0644;
  md.nlink = 3;
  const Visit visit{.path = "a/b/c.txt", .name = "c.txt", .depth = 2, .metadata = md};
  EXPECT_THAT(Match({"-printf", "%p|%f|%h|%s|%m|%d|%y\\n"}, visit), IsTrue());
  EXPECT_THAT(emitted_, "a/b/c.txt|c.txt|a/b|42|644|2|f\n");
  EXPECT_THAT(Match({"-printf", "%%\\t%n"}, visit), IsTrue());  // literal %, tab escape, link count
  EXPECT_THAT(emitted_, "%\t3");
}

TEST_F(EvaluateTest, PrintfTypeDirectiveCoversEveryFilesystemType) {
  static constexpr auto kCases = std::to_array<std::pair<vfs::FileType, char>>({
      {vfs::FileType::kBlockDevice, 'b'},
      {vfs::FileType::kCharDevice, 'c'},
      {vfs::FileType::kDirectory, 'd'},
      {vfs::FileType::kFifo, 'p'},
      {vfs::FileType::kRegular, 'f'},
      {vfs::FileType::kSocket, 's'},
      {vfs::FileType::kSymlink, 'l'},
      {vfs::FileType::kUnknown, 'U'},
  });
  for (const auto& [type, letter] : kCases) {
    SCOPED_TRACE(letter);
    vfs::Metadata md;
    const Visit visit = MakeVisit("entry", "entry", type, md);
    EXPECT_THAT(Match({"-printf", "%y"}, visit), IsTrue());
    EXPECT_THAT(emitted_, std::string(1, letter));
  }
}

TEST_F(EvaluateTest, PrintfHandlesRootAndBasenameDirectoriesAndZeroPermissions) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  const Visit root_child{.path = "/entry", .name = "entry", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-printf", "%h|%m"}, root_child), IsTrue());
  EXPECT_THAT(emitted_, "/|0");

  const Visit basename{.path = "entry", .name = "entry", .depth = 0, .metadata = md};
  EXPECT_THAT(Match({"-printf", "%h"}, basename), IsTrue());
  EXPECT_THAT(emitted_, ".");
}

TEST_F(EvaluateTest, PrintfPreservesUnknownEscapesAndDirectives) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.ino = 42;
  const Visit visit{.path = "entry", .name = "entry", .depth = 0, .metadata = md};
  EXPECT_THAT(Match({"-printf", "%i|\\q|%Q|trailing\\|trailing%"}, visit), IsTrue());
  EXPECT_THAT(emitted_, "42|\\q|%Q|trailing\\|trailing%");
}

TEST_F(EvaluateTest, PrintfOwnerDirectives) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.uid = 1'234'567;  // no passwd entry -> %u falls back to the numeric id
  md.gid = 7'654'321;  // no group entry -> %g falls back to the numeric id
  const Visit visit{.path = "d/f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_THAT(Match({"-printf", "%u|%U|%g|%G"}, visit), IsTrue());
  EXPECT_THAT(emitted_, "1234567|1234567|7654321|7654321");  // %U/%G numeric; %u/%g fall back to the id
}

TEST_F(EvaluateTest, PrintfTimeDirectives) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.mtime = absl::FromUnixSeconds(1'600'000'000);  // 2020-09-13 12:26:40 UTC (a Sunday)
  md.atime = absl::FromUnixSeconds(0);              // 1970-01-01 UTC
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  tz_ = absl::UTCTimeZone();  // render times in UTC for a deterministic assertion
  // %t/%a are the asctime form of mtime/atime; %Tk/%Ak are strftime conversion k.
  EXPECT_THAT(Match({"-printf", "%t|%TY-%Tm-%Td %TH:%TM:%TS|%AY"}, visit), IsTrue());
  EXPECT_THAT(emitted_, "Sun Sep 13 12:26:40 2020|2020-09-13 12:26:40|1970");
  // The rendering zone honors EvalContext::tz (--timezone): 12:26 UTC is 13:26 in UTC+1.
  tz_ = absl::FixedTimeZone(3'600);
  EXPECT_THAT(Match({"-printf", "%TH"}, visit), IsTrue());
  EXPECT_THAT(emitted_, "13");

  md.ctime = absl::FromUnixSeconds(86'400);
  const Visit changed{.path = "f", .name = "f", .depth = 1, .metadata = md};
  tz_ = absl::UTCTimeZone();
  EXPECT_THAT(Match({"-printf", "%c|%CY"}, changed), IsTrue());
  EXPECT_THAT(emitted_, "Fri Jan  2 00:00:00 1970|1970");
}

TEST_F(EvaluateTest, PrintlnAndPrintflnAppendOsLineEnding) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.size = 42;
  const Visit visit{.path = "a/b/c.txt", .name = "c.txt", .depth = 2, .metadata = md};
  // -println: -print with the OS line ending (LF on this platform).
  EXPECT_THAT(Match({"-println"}, visit), IsTrue());
  EXPECT_THAT(emitted_, "a/b/c.txt\n");
  // -printfln: -printf plus a trailing OS line ending the format need not carry.
  EXPECT_THAT(Match({"-printfln", "%f|%s"}, visit), IsTrue());
  EXPECT_THAT(emitted_, "c.txt|42\n");
}

TEST_F(EvaluateTest, LsEmitsAnLsStyleLine) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.ino = 42;
  md.blocks = 8;   // 8 * 512B -> 4 KiB blocks
  md.mode = 0644;  // -rw-r--r--
  md.nlink = 1;
  md.uid = 1'234'567;  // no passwd/group entry -> numeric owner/group
  md.gid = 7'654'321;
  md.size = 4'096;
  md.mtime = absl::FromUnixSeconds(1'600'000'000);  // 2020-09-13 UTC, >6mo before now_ -> year form
  const Visit visit{.path = "dir/f", .name = "f", .depth = 1, .metadata = md};
  tz_ = absl::UTCTimeZone();
  EXPECT_THAT(Match({"-ls"}, visit), IsTrue());
  EXPECT_THAT(emitted_, "42 4 -rw-r--r-- 1 1234567 7654321 4096 Sep 13  2020 dir/f\n");
}

TEST_F(EvaluateTest, LsRendersEveryTypeAndSpecialPermissionState) {
  static constexpr auto kCases = std::to_array<std::tuple<vfs::FileType, std::uint32_t, std::string_view>>({
      {vfs::FileType::kBlockDevice, 04700, "brws------"},
      {vfs::FileType::kCharDevice, 04600, "crwS------"},
      {vfs::FileType::kDirectory, 02070, "d---rws---"},
      {vfs::FileType::kFifo, 02060, "p---rwS---"},
      {vfs::FileType::kRegular, 01007, "-------rwt"},
      {vfs::FileType::kSocket, 01006, "s------rwT"},
      {vfs::FileType::kSymlink, 0777, "lrwxrwxrwx"},
      {vfs::FileType::kUnknown, 0, "?---------"},
  });
  for (const auto& [type, mode, permissions] : kCases) {
    SCOPED_TRACE(permissions);
    vfs::Metadata md;
    md.type = type;
    md.mode = mode;
    const Visit visit{.path = "entry", .name = "entry", .depth = 0, .metadata = md};
    EXPECT_THAT(Match({"-ls"}, visit), IsTrue());
    EXPECT_THAT(emitted_, HasSubstr(absl::StrCat(" ", permissions, " ")));
  }
}

TEST_F(EvaluateTest, OkPromptsWithSubstitutionAndRunsOnlyWhenConfirmed) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo.txt", "foo.txt", vfs::FileType::kRegular, md);
  // Declined: the command is not run, -ok is false; the prompt shows {} -> the path.
  confirm_reply_ = false;
  EXPECT_THAT(Match({"-ok", "/bin/echo", "{}", ";"}, visit), IsFalse());
  EXPECT_THAT(last_prompt_, "/bin/echo dir/foo.txt? ");
  // Affirmative: the command runs and -ok mirrors its exit status.
  confirm_reply_ = true;
  EXPECT_THAT(Match({"-ok", "/bin/sh", "-c", "exit 0", ";"}, visit), IsTrue());
  EXPECT_THAT(Match({"-ok", "/bin/sh", "-c", "exit 1", ";"}, visit), IsFalse());
}

TEST_F(EvaluateTest, NewerMtComparesEntryTimeToTimeString) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.mtime = absl::FromUnixSeconds(1'600'000'000);  // 2020-09-13, a fixed mtime
  const Visit visit = MakeVisit("f", "f", vfs::FileType::kRegular, md);
  // @epoch reference: mtime is newer than an earlier instant, not a later one.
  EXPECT_THAT(Match({"-newermt", "@1500000000"}, visit), IsTrue());
  EXPECT_THAT(Match({"-newermt", "@1700000000"}, visit), IsFalse());
  // ISO date form (local zone); the coarse year gap is timezone-independent.
  EXPECT_THAT(Match({"-newermt", "2001-01-01"}, visit), IsTrue());
  EXPECT_THAT(Match({"-newermt", "2099-01-01"}, visit), IsFalse());
  // An unparseable time string never matches.
  EXPECT_THAT(Match({"-newermt", "yesterday"}, visit), IsFalse());
}

TEST_F(EvaluateTest, NewerMtAcceptsRelativeTimeStrings) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.mtime = now_ - absl::Hours(48);  // modified two days before the reference clock
  const Visit visit = MakeVisit("f", "f", vfs::FileType::kRegular, md);
  EXPECT_THAT(Match({"-newermt", "3 days ago"}, visit), IsTrue());  // within the last three days
  EXPECT_THAT(Match({"-newermt", "-3 days"}, visit), IsTrue());     // same, sign form
  EXPECT_THAT(Match({"-newermt", "1 day ago"}, visit), IsFalse());  // not within the last day
  EXPECT_THAT(Match({"-newermt", "now"}, visit), IsFalse());        // older than now
}

TEST_F(EvaluateTest, NewerMtInterpretsTheTimeStringInTheContextZone) {
  // A file modified at 2020-01-01 23:30 UTC straddles the 2020-01-02 boundary:
  // that midnight is 00:00 UTC but 23:00 UTC the previous day in UTC+1, so
  // -newermt 2020-01-02 flips with the context zone (EvalContext::tz, --timezone).
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.mtime = absl::FromCivil(absl::CivilSecond(2'020, 1, 1, 23, 30, 0), absl::UTCTimeZone());
  const Visit visit = MakeVisit("f", "f", vfs::FileType::kRegular, md);

  tz_ = absl::UTCTimeZone();  // ref = 2020-01-02 00:00 UTC; mtime is 30 min earlier -> not newer
  EXPECT_THAT(Match({"-newermt", "2020-01-02"}, visit), IsFalse());
  tz_ = absl::FixedTimeZone(3'600);  // UTC+1: ref = 2020-01-01 23:00 UTC; mtime is 30 min later -> newer
  EXPECT_THAT(Match({"-newermt", "2020-01-02"}, visit), IsTrue());
}

TEST_F(EvaluateTest, ExecFieldsGatesNamedPlaceholderSubstitution) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("a/b/f.txt", "f.txt", vfs::FileType::kRegular, md);
  // Default (find-exact): {name} is not a placeholder, so the child compares the
  // literal "{name}" against "f.txt" -> not equal -> child exits 1 -> false.
  exec_fields_ = false;
  EXPECT_THAT(Match({"-exec", "/bin/sh", "-c", "test \"{name}\" = f.txt", ";"}, visit), IsFalse());
  // With --exec-fields, {name} renders the basename -> equal -> child exits 0 -> true.
  exec_fields_ = true;
  EXPECT_THAT(Match({"-exec", "/bin/sh", "-c", "test \"{name}\" = f.txt", ";"}, visit), IsTrue());
}

TEST_F(EvaluateTest, ExecdirRunsChildInEntryDirectoryWithDotSlashBasename) {
  vfs::Metadata md;
  // Entry "/x.txt" -> -execdir runs the child in "/" with {} expanded to "./x.txt"
  // ("/" always exists, so the chdir succeeds under the test sandbox).
  const Visit visit = MakeVisit("/x.txt", "x.txt", vfs::FileType::kRegular, md);
  exec_fields_ = false;
  EXPECT_THAT(Match({"-execdir", "/bin/sh", "-c", "test \"$(pwd -P)\" = /", ";"}, visit), IsTrue());  // ran in "/"
  EXPECT_THAT(Match({"-execdir", "/bin/sh", "-c", "test \"{}\" = ./x.txt", ";"}, visit), IsTrue());  // {} -> ./basename
  EXPECT_THAT(Match({"-execdir", "/bin/sh", "-c", "test \"{}\" = ./nope", ";"}, visit), IsFalse());
}

TEST_F(EvaluateTest, ExecdirHonorsExecFields) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("/f.txt", "f.txt", vfs::FileType::kRegular, md);
  // With --exec-fields, {name} renders the basename (the vocabulary still sees the
  // full path); without it, {name} stays literal and the comparison fails.
  exec_fields_ = true;
  EXPECT_THAT(Match({"-execdir", "/bin/sh", "-c", "test \"{name}\" = f.txt", ";"}, visit), IsTrue());
  exec_fields_ = false;
  EXPECT_THAT(Match({"-execdir", "/bin/sh", "-c", "test \"{name}\" = f.txt", ";"}, visit), IsFalse());
}

TEST_F(EvaluateTest, OkdirPromptsWithDotSlashBasenameThenRunsInEntryDirOnYes) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("/x.txt", "x.txt", vfs::FileType::kRegular, md);
  confirm_reply_ = true;  // affirmative: -okdir runs the command, like -execdir
  EXPECT_THAT(Match({"-okdir", "/bin/sh", "-c", "test \"$(pwd -P)\" = /", ";"}, visit), IsTrue());  // ran in "/"
  EXPECT_THAT(Match({"-okdir", "/bin/sh", "-c", "test \"{}\" = ./x.txt", ";"}, visit), IsTrue());   // {} -> ./basename
  // The prompt substitutes {} -> ./basename and ends with "? ".
  EXPECT_THAT(Match({"-okdir", "/bin/echo", "{}", ";"}, visit), IsTrue());
  EXPECT_THAT(last_prompt_, "/bin/echo ./x.txt? ");  // tokens joined, then "? " (no space before, like -ok)
}

TEST_F(EvaluateTest, OkdirDeclinedDoesNotRunAndIsFalse) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("/x.txt", "x.txt", vfs::FileType::kRegular, md);
  confirm_reply_ = false;  // declined: not run, -okdir is false (the command would exit 0)
  EXPECT_THAT(Match({"-okdir", "/bin/sh", "-c", "exit 0", ";"}, visit), IsFalse());
}

TEST_F(EvaluateTest, CapturedirRunsCommandInEntryDirAndBindsStdout) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("/x.txt", "x.txt", vfs::FileType::kRegular, md);
  // -capturedir:NAME runs the command in the entry's directory ("/") and binds its
  // stdout (trailing newline stripped) to {capture.NAME}.
  EXPECT_THAT(Match({"-capturedir:cwd", "/bin/sh", "-c", "pwd -P", ";"}, visit), IsTrue());
  EXPECT_THAT(outputs_["cwd"], "/");
}

TEST_F(EvaluateTest, CaptureWithoutOutputSinkIsANoOp) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("/x.txt", "x.txt", vfs::FileType::kRegular, md);
  capture_outputs_ = false;
  // An unwired evaluator neither runs the command nor binds a value. The deliberately failing
  // command distinguishes that no-op from executing it and forwarding its result.
  EXPECT_THAT(Match({"-capture:unused", "/bin/sh", "-c", "exit 1", ";"}, visit), IsTrue());
  EXPECT_THAT(outputs_, IsEmpty());
}

TEST_F(EvaluateTest, ExecFieldsSubstitutesRegexCaptures) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("a/b/c.txt", "c.txt", vfs::FileType::kRegular, md);
  // A preceding -regex match records its groups; -exec {1} then references group 1
  // ("a/b"). The child compares it to a/b -> exit 0 -> true.
  exec_fields_ = true;
  EXPECT_THAT(
      Match({"-regex", "(.*)/([^/]+)\\.(.*)", "-exec", "/bin/sh", "-c", "test \"{1}\" = a/b", ";"}, visit), IsTrue());
  // Without the gate there is no capture and {1} stays literal -> not equal -> false.
  exec_fields_ = false;
  EXPECT_THAT(
      Match({"-regex", "(.*)/([^/]+)\\.(.*)", "-exec", "/bin/sh", "-c", "test \"{1}\" = a/b", ";"}, visit), IsFalse());
}

TEST_F(EvaluateTest, CapturesAreVisibleLeftToRightOnly) {
  // The variable store accumulates left-to-right, so a binding (here a -regex
  // match) is in scope for later actions and only later -- the guarantee that
  // capture/-exec chaining rests on.
  vfs::Metadata md;
  const Visit visit = MakeVisit("a/b/c.txt", "c.txt", vfs::FileType::kRegular, md);
  exec_fields_ = true;
  // -regex to the LEFT of -exec: its groups are bound when -exec runs, so {1} == "a/b".
  EXPECT_THAT(
      Match({"-regex", "(.*)/([^/]+)\\.(.*)", "-exec", "/bin/sh", "-c", "test \"{1}\" = a/b", ";"}, visit), IsTrue());
  // -regex to the RIGHT: not yet evaluated when -exec runs, so {1} is still empty there.
  EXPECT_THAT(
      Match({"-exec", "/bin/sh", "-c", "test -z \"{1}\"", ";", "-regex", "(.*)/([^/]+)\\.(.*)"}, visit), IsTrue());
}

TEST_F(EvaluateTest, CaptureBindsOutputNamespace) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("a/b/c.txt", "c.txt", vfs::FileType::kRegular, md);
  exec_fields_ = true;  // so the -exec reading {capture.tag} renders the vocabulary
  // -capture runs the command and binds {capture.tag}; the later -exec reads it.
  EXPECT_THAT(
      Match(
          {"-capture:tag", "/bin/sh", "-c", "printf hi", ";", "-exec", "/bin/sh", "-c", "test \"{capture.tag}\" = hi",
           ";"},
          visit),
      IsTrue());
}

TEST_F(EvaluateTest, PrintfDocsDocumentEveryDirective) {
  // Drift guard for --help=printf: every % directive in the SOT table (kPrintfDirectives,
  // via PrintfDirectiveLetters) is documented by PrintfDocs, and the xff %{field} escape too.
  std::string codes;
  for (const auto& [code, description] : PrintfDocs()) {
    codes += code;
    codes += ' ';
  }
  for (const char letter : PrintfDirectiveLetters()) {
    EXPECT_THAT(codes, HasSubstr(std::string("%") + letter)) << "undocumented -printf directive %" << letter;
  }
  EXPECT_THAT(codes, HasSubstr("%{NAME}"));
}

TEST_F(EvaluateTest, SizeUnitDocsDocumentEveryUnit) {
  // Drift guard for --help=size: every unit suffix in the SOT (kSizeUnits, via
  // SizeUnitSuffixes) is documented by SizeUnitDocs.
  std::string codes;
  for (const auto& [code, description] : SizeUnitDocs()) {
    codes += code;
    codes += ' ';
  }
  for (const char suffix : SizeUnitSuffixes()) {
    EXPECT_THAT(codes, HasSubstr(std::string(1, suffix))) << "undocumented -size unit: " << suffix;
  }
}

TEST_F(EvaluateTest, PresentationVocabulariesHaveStableStorage) {
  const auto columns = LsColumns();
  const auto printf_docs = PrintfDocs();
  const auto size_docs = SizeUnitDocs();
  EXPECT_THAT(LsColumns().data(), Eq(columns.data()));
  EXPECT_THAT(PrintfDocs().data(), Eq(printf_docs.data()));
  EXPECT_THAT(SizeUnitDocs().data(), Eq(size_docs.data()));
}

}  // namespace
}  // namespace xff::engine
