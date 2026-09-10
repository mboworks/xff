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
  EXPECT_TRUE(Match({"-true"}, visit));
  EXPECT_FALSE(Match({"-false"}, visit));
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
  EXPECT_FALSE(Match({"-true", "-xor", "-true"}, visit));
  EXPECT_TRUE(Match({"-true", "-xor", "-false"}, visit));
  EXPECT_TRUE(Match({"-false", "-xor", "-true"}, visit));
  EXPECT_FALSE(Match({"-false", "-xor", "-false"}, visit));
}

TEST_F(EvaluateTest, XnorMatchesWhenBothSidesAgree) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo", "foo", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-true", "-xnor", "-true"}, visit));
  EXPECT_FALSE(Match({"-true", "-xnor", "-false"}, visit));
  EXPECT_FALSE(Match({"-false", "-xnor", "-true"}, visit));
  EXPECT_TRUE(Match({"-false", "-xnor", "-false"}, visit));
}

TEST_F(EvaluateTest, NandIsTheNegationOfAnd) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo", "foo", vfs::FileType::kRegular, md);
  EXPECT_FALSE(Match({"-true", "-nand", "-true"}, visit));
  EXPECT_TRUE(Match({"-true", "-nand", "-false"}, visit));
  EXPECT_TRUE(Match({"-false", "-nand", "-true"}, visit));
  EXPECT_TRUE(Match({"-false", "-nand", "-false"}, visit));
}

TEST_F(EvaluateTest, NorIsTheNegationOfOr) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo", "foo", vfs::FileType::kRegular, md);
  EXPECT_FALSE(Match({"-true", "-nor", "-true"}, visit));
  EXPECT_FALSE(Match({"-true", "-nor", "-false"}, visit));
  EXPECT_FALSE(Match({"-false", "-nor", "-true"}, visit));
  EXPECT_TRUE(Match({"-false", "-nor", "-false"}, visit));
}

TEST_F(EvaluateTest, DaystartIsAPositionalNoOp) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo", "foo", vfs::FileType::kRegular, md);
  // -daystart is consumed by the driver (it shifts the age-test reference to local
  // midnight); as a predicate it is a no-op that matches, so it never filters.
  EXPECT_TRUE(Match({"-daystart"}, visit));
  EXPECT_TRUE(Match({"-daystart", "-name", "foo"}, visit));
}

TEST_F(EvaluateTest, NameGlobsBasename) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo.txt", "foo.txt", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-name", "*.txt"}, visit));
  EXPECT_TRUE(Match({"-name", "foo.*"}, visit));
  EXPECT_FALSE(Match({"-name", "*.md"}, visit));
  EXPECT_FALSE(Match({"-name", "dir/*"}, visit)) << "-name matches the basename, not the path";
}

TEST_F(EvaluateTest, INameFoldsCase) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/Foo.TXT", "Foo.TXT", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-iname", "foo.txt"}, visit));
  EXPECT_FALSE(Match({"-name", "foo.txt"}, visit));
}

TEST_F(EvaluateTest, FsNativeFoldsNameOnCaseFoldingVolume) {
  // fold_name_case (set by the driver for an entry on a case-folding volume, xff
  // style, no --exact) makes the case-sensitive -name fold like -iname, so it
  // matches the way the filesystem itself resolves the name.
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/Foo.TXT", "Foo.TXT", vfs::FileType::kRegular, md);
  EXPECT_FALSE(Match({"-name", "foo.txt"}, visit)) << "byte-exact by default (case-sensitive volume / --exact)";
  fold_name_case_ = true;
  EXPECT_TRUE(Match({"-name", "foo.txt"}, visit)) << "FS-native fold on a case-folding volume";
  EXPECT_TRUE(Match({"-name", "Foo.TXT"}, visit)) << "the exact-case name still matches when folding";
}

TEST_F(EvaluateTest, FsNativeFoldsPathOnCaseFoldingVolume) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("Dir/Foo.TXT", "Foo.TXT", vfs::FileType::kRegular, md);
  EXPECT_FALSE(Match({"-path", "dir/foo.txt"}, visit));
  fold_name_case_ = true;
  EXPECT_TRUE(Match({"-path", "dir/foo.txt"}, visit)) << "-path folds too under FS-native matching";
}

TEST_F(EvaluateTest, PathGlobsWholePath) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("a/b/c.txt", "c.txt", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-path", "a/*/c.txt"}, visit));
  EXPECT_TRUE(Match({"-path", "*/c.txt"}, visit)) << "* spans slashes for -path";
  EXPECT_FALSE(Match({"-path", "c.txt"}, visit));
  EXPECT_TRUE(Match({"-ipath", "A/B/*"}, visit));
}

TEST_F(EvaluateTest, FuzzyPercentThresholdGatesTheMatch) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/far_out_of", "far_out_of", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-fuzzy:0%", "foo"}, visit));
  EXPECT_FALSE(Match({"-fuzzy:100%", "foo"}, visit));
  EXPECT_TRUE(Match({"-fuzzy:100%", "far_out_of"}, visit));
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

  EXPECT_TRUE(Match({"-fuzzy:sequence:67%", "fo"}, visit));
  EXPECT_THAT(fuzzy_score_, Optional(Eq(67)));
  EXPECT_FALSE(Match({"-fuzzy:sequence:68%", "fo"}, visit));

  EXPECT_TRUE(Match({"-fuzzy:levenshtein:67%", "fof"}, visit));
  EXPECT_THAT(fuzzy_score_, Optional(Eq(67)));
  EXPECT_FALSE(Match({"-fuzzy:levenshtein:68%", "fof"}, visit));

  EXPECT_TRUE(Match({"-fuzzy:shingles:33%", "fof"}, visit));
  EXPECT_THAT(fuzzy_score_, Optional(Eq(33)));
  EXPECT_FALSE(Match({"-fuzzy:shingles:34%", "fof"}, visit));
}

TEST_F(EvaluateTest, FuzzyAndUsesTheWeakestNormalizedScore) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/far_out_of", "far_out_of", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-fuzzy", "foo", "-fuzzy", "far_out_of"}, visit));
  const std::optional<int> weak = fuzzy::Percent("foo", "far_out_of", false);
  EXPECT_THAT(fuzzy_score_, Eq(weak));
}

TEST_F(EvaluateTest, PureFuzzyOrEvaluatesBothAlternativesAndUsesTheBestScore) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/far_out_of", "far_out_of", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-fuzzy", "foo", "-o", "-fuzzy", "far_out_of"}, visit));
  EXPECT_THAT(fuzzy_score_, Optional(Eq(100)));
}

TEST_F(EvaluateTest, WholenameIsSynonymForPath) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("a/b/c.txt", "c.txt", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-wholename", "a/*/c.txt"}, visit));  // -wholename == -path
  EXPECT_FALSE(Match({"-wholename", "c.txt"}, visit));     // matches the whole path, not the basename
  EXPECT_TRUE(Match({"-iwholename", "A/B/*"}, visit));     // -iwholename folds case, like -ipath
  EXPECT_FALSE(Match({"-wholename", "A/B/*"}, visit));     // -wholename is case-sensitive
}

TEST_F(EvaluateTest, TypeMatchesFileType) {
  vfs::Metadata file_md;
  const Visit file = MakeVisit("x", "x", vfs::FileType::kRegular, file_md);
  vfs::Metadata dir_md;
  const Visit dir = MakeVisit("d", "d", vfs::FileType::kDirectory, dir_md);
  EXPECT_TRUE(Match({"-type", "f"}, file));
  EXPECT_FALSE(Match({"-type", "d"}, file));
  EXPECT_TRUE(Match({"-type", "d"}, dir));
  EXPECT_FALSE(Match({"-type", "f"}, dir));
}

TEST_F(EvaluateTest, MimeGlobsTheExtensionDerivedMediaType) {
  vfs::Metadata md;
  const Visit png = MakeVisit("dir/photo.png", "photo.png", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-mime", "image/*"}, png));    // image/png matches the glob
  EXPECT_TRUE(Match({"-mime", "image/png"}, png));  // and the exact type
  EXPECT_FALSE(Match({"-mime", "text/*"}, png));
  // MIME type/subtype names are case-insensitive (RFC 2045/6838): an upper- or mixed-case pattern
  // matches the (canonical lowercase) type just the same.
  EXPECT_TRUE(Match({"-mime", "IMAGE/*"}, png));
  EXPECT_TRUE(Match({"-mime", "Image/PNG"}, png));
  // An unknown / absent extension is application/octet-stream (the file(1) fallback).
  vfs::Metadata bin_md;
  const Visit bin = MakeVisit("dir/README", "README", vfs::FileType::kRegular, bin_md);
  EXPECT_TRUE(Match({"-mime", "application/octet-stream"}, bin));
  EXPECT_FALSE(Match({"-mime", "text/*"}, bin));
}

TEST_F(EvaluateTest, LangGlobsTheExtensionOrFilenameLanguage) {
  vfs::Metadata md;
  const Visit cpp = MakeVisit("src/main.cc", "main.cc", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-lang", "C*"}, cpp));   // "C++" matches the glob
  EXPECT_TRUE(Match({"-lang", "c++"}, cpp));  // and case-insensitively (canonical is "C++")
  EXPECT_FALSE(Match({"-lang", "Python"}, cpp));
  // Filename-classified languages match too.
  const Visit mk = MakeVisit("proj/Makefile", "Makefile", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-lang", "Makefile"}, mk));
  // An unrecognized name has no language (""), matched only by a `*` / empty pattern.
  const Visit bin = MakeVisit("dir/photo.jpg", "photo.jpg", vfs::FileType::kRegular, md);
  EXPECT_FALSE(Match({"-lang", "C*"}, bin));
  EXPECT_TRUE(Match({"-lang", "*"}, bin));
}

TEST_F(EvaluateTest, TypeListMatchesAnyListedType) {
  vfs::Metadata file_md;
  const Visit file = MakeVisit("x", "x", vfs::FileType::kRegular, file_md);
  vfs::Metadata dir_md;
  const Visit dir = MakeVisit("d", "d", vfs::FileType::kDirectory, dir_md);
  vfs::Metadata link_md;
  const Visit link = MakeVisit("l", "l", vfs::FileType::kSymlink, link_md);
  // GNU's "-type f,d": matches if the entry is any of the listed types.
  EXPECT_TRUE(Match({"-type", "f,d"}, file));
  EXPECT_TRUE(Match({"-type", "f,d"}, dir));
  EXPECT_FALSE(Match({"-type", "f,d"}, link));
  EXPECT_TRUE(Match({"-type", "l,p,f"}, file));  // order does not matter
  // A malformed list (empty element, trailing comma, unknown letter) never matches.
  EXPECT_FALSE(Match({"-type", "f,"}, file));
  EXPECT_FALSE(Match({"-type", ",f"}, file));
  EXPECT_FALSE(Match({"-type", "f,z"}, file));
}

TEST_F(EvaluateTest, AndOrNotShortCircuit) {
  vfs::Metadata md;
  const Visit txt = MakeVisit("dir/foo.txt", "foo.txt", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-type", "f", "-name", "*.txt"}, txt));        // implicit -a
  EXPECT_FALSE(Match({"-type", "d", "-name", "*.txt"}, txt));       // -a, first fails
  EXPECT_TRUE(Match({"-type", "d", "-o", "-name", "*.txt"}, txt));  // -o
  EXPECT_TRUE(Match({"!", "-type", "d"}, txt));                     // -not
  EXPECT_FALSE(Match({"!", "-name", "*.txt"}, txt));
}

TEST_F(EvaluateTest, PrintActionsEmit) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo.txt", "foo.txt", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-print"}, visit));
  EXPECT_THAT(emitted_, "dir/foo.txt\n");
  EXPECT_TRUE(Match({"-print0"}, visit));
  EXPECT_THAT(emitted_, std::string("dir/foo.txt\0", 12));
}

TEST_F(EvaluateTest, FileActionsWriteRecordsToNamedFiles) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo.txt", "foo.txt", vfs::FileType::kRegular, md);
  // Each f-action mirrors its stdout counterpart's bytes, routed to the FILE arg
  // (Match clears file_emitted_ each call, so each map holds just that action's output).
  EXPECT_TRUE(Match({"-fprint", "out"}, visit));
  EXPECT_THAT(file_emitted_, ElementsAre(Pair("out", "dir/foo.txt\n")));
  EXPECT_TRUE(Match({"-fprint0", "out"}, visit));
  EXPECT_THAT(file_emitted_, ElementsAre(Pair("out", std::string("dir/foo.txt\0", 12))));
  EXPECT_TRUE(Match({"-fprintf", "out", "%p:%f\n"}, visit));
  EXPECT_THAT(file_emitted_, ElementsAre(Pair("out", "dir/foo.txt:foo.txt\n")));
  EXPECT_TRUE(Match({"-fls", "log"}, visit));
  EXPECT_THAT(file_emitted_, ElementsAre(Pair("log", HasSubstr("dir/foo.txt"))));
}

TEST_F(EvaluateTest, ShortCircuitSkipsAction) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("dir/foo.txt", "foo.txt", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-type", "f", "-print"}, visit));
  EXPECT_THAT(emitted_, "dir/foo.txt\n");
  EXPECT_FALSE(Match({"-type", "d", "-print"}, visit)) << "-type d fails; -a short-circuits before -print";
  EXPECT_THAT(emitted_, IsEmpty());
}

TEST_F(EvaluateTest, SizeMatchesBytesAndUnits) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.size = 5;  // bytes
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-size", "5c"}, visit));
  EXPECT_FALSE(Match({"-size", "4c"}, visit));
  EXPECT_TRUE(Match({"-size", "+4c"}, visit));
  EXPECT_TRUE(Match({"-size", "-6c"}, visit));
  EXPECT_FALSE(Match({"-size", "+5c"}, visit));
  EXPECT_TRUE(Match({"-size", "1"}, visit)) << "5 bytes rounds up to one 512-byte block";
  EXPECT_TRUE(Match({"-size", "1k"}, visit)) << "5 bytes rounds up to one 1k unit";
}

TEST_F(EvaluateTest, SizeMatchesLargeUnits) {
  // T/P/E continue the k/M/G binary scale (2^40/2^50/2^60). Exact multiples make the
  // round-up-to-unit arithmetic land on a clean count.
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.size = 3ULL * 1'024 * 1'024 * 1'024 * 1'024;  // 3 TiB
  const Visit tib{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-size", "3T"}, tib));
  EXPECT_TRUE(Match({"-size", "+2T"}, tib));
  EXPECT_FALSE(Match({"-size", "2T"}, tib));
  md.size = 2ULL * 1'024 * 1'024 * 1'024 * 1'024 * 1'024;  // 2 PiB
  const Visit pib{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-size", "2P"}, pib));
  md.size = 1ULL * 1'024 * 1'024 * 1'024 * 1'024 * 1'024 * 1'024;  // 1 EiB (2^60)
  const Visit eib{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-size", "1E"}, eib));
}

TEST_F(EvaluateTest, SizeExplicitUnitsKeepDecimalAndBinaryMeaningsDistinct) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.size = 1'000'001;
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-size", "2MB"}, visit)) << "SI MB rounds 1,000,001 bytes up to two units";
  EXPECT_TRUE(Match({"-size", "1MiB"}, visit)) << "the same length fits in one IEC MiB";
  EXPECT_TRUE(Match({"-size", "1M"}, visit)) << "legacy M remains the binary MiB scale";
  EXPECT_FALSE(Match({"-size", "1MB"}, visit));
  EXPECT_TRUE(Match({"-size", "1000001B"}, visit));
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
  EXPECT_TRUE(Match({"-blocks", "16"}, visit));  // 8192 bytes / 512-byte block = 16
  EXPECT_TRUE(Match({"-blocks", "+8"}, visit));  // more than 8 blocks
  EXPECT_TRUE(Match({"-blocks", "8k"}, visit));  // exactly 8 KiB allocated
  EXPECT_FALSE(Match({"-blocks", "+8k"}, visit));
  EXPECT_FALSE(Match({"-blocks", "1c"}, visit));  // not 1 byte allocated
  EXPECT_TRUE(Match({"-size", "1c"}, visit));     // but -size sees the 1 apparent byte
}

TEST_F(EvaluateTest, BlocksZeroForUnallocatedEntry) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;  // blocks defaults to 0 (nothing allocated)
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_FALSE(Match({"-blocks", "+0"}, visit));  // occupies no disk
  EXPECT_TRUE(Match({"-blocks", "0"}, visit));
}

TEST_F(EvaluateTest, PermMatchesOctalModes) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.mode = 0644;  // rw-r--r--
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-perm", "644"}, visit));  // exact
  EXPECT_FALSE(Match({"-perm", "640"}, visit));
  EXPECT_TRUE(Match({"-perm", "-200"}, visit));   // -MODE: owner-write bit set
  EXPECT_TRUE(Match({"-perm", "-044"}, visit));   // group + other read set
  EXPECT_FALSE(Match({"-perm", "-022"}, visit));  // group/other write NOT set
  EXPECT_TRUE(Match({"-perm", "/040"}, visit));   // /MODE: any bit (group read) set
  EXPECT_FALSE(Match({"-perm", "/022"}, visit));  // none of group/other write set
  EXPECT_TRUE(Match({"-perm", "+040"}, visit));   // BSD +MODE: any-of, like /MODE
  EXPECT_FALSE(Match({"-perm", "+022"}, visit));  // none of group/other write set
}

TEST_F(EvaluateTest, PermMatchesSymbolicModes) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.mode = 0644;  // rw-r--r--
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  // Exact: the symbolic mode resolves (from zero) to exactly the file's bits.
  EXPECT_TRUE(Match({"-perm", "u=rw,go=r"}, visit));  // == 0644
  EXPECT_FALSE(Match({"-perm", "u=rw,g=r"}, visit));  // == 0640, file is 0644
  EXPECT_FALSE(Match({"-perm", "u+w"}, visit));       // == 0200 exactly; file has more
  // -MODE: all requested bits present.
  EXPECT_TRUE(Match({"-perm", "-u+w"}, visit));       // owner write set
  EXPECT_TRUE(Match({"-perm", "-u+r,go+r"}, visit));  // all of owner/group/other read set
  EXPECT_FALSE(Match({"-perm", "-g+w"}, visit));      // group write NOT set
  EXPECT_FALSE(Match({"-perm", "-a+x"}, visit));      // no execute bits set
  // /MODE: any requested bit present.
  EXPECT_TRUE(Match({"-perm", "/u+x,g+r"}, visit));  // group read is set
  EXPECT_FALSE(Match({"-perm", "/a+x"}, visit));     // no execute bits at all
  // A malformed symbolic mode never matches.
  EXPECT_FALSE(Match({"-perm", "u?w"}, visit));
  EXPECT_THAT(Match({"-perm", "u"}, visit), IsFalse());
  EXPECT_THAT(Match({"-perm", "u+q"}, visit), IsFalse());
  EXPECT_THAT(Match({"-perm", "u+w,u-w,go+r"}, visit), IsFalse());

  // An omitted "who" behaves as 'a' (all classes): "+r" resolves to 0444, not 0400.
  vfs::Metadata r_md;
  r_md.type = vfs::FileType::kRegular;
  r_md.mode = 0444;  // r--r--r--
  const Visit r_all{.path = "r", .name = "r", .depth = 1, .metadata = r_md};
  EXPECT_TRUE(Match({"-perm", "+r"}, r_all));    // exact: omitted who == a, so 0444
  EXPECT_FALSE(Match({"-perm", "u+r"}, r_all));  // exact: explicit u+r == 0400, file is 0444
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
  EXPECT_TRUE(Match({"-perm", "-u+s"}, visit));      // setuid bit set
  EXPECT_FALSE(Match({"-perm", "-g+s"}, visit));     // setgid NOT set
  EXPECT_TRUE(Match({"-perm", "-u+s,a+x"}, visit));  // setuid + all execute set
}

TEST_F(EvaluateTest, EmptyMatchesZeroByteFilesNotOthers) {
  vfs::Metadata empty_file;
  empty_file.type = vfs::FileType::kRegular;
  empty_file.size = 0;
  EXPECT_TRUE(Match({"-empty"}, Visit{.path = "f", .name = "f", .depth = 1, .metadata = empty_file}));

  vfs::Metadata nonempty_file;
  nonempty_file.type = vfs::FileType::kRegular;
  nonempty_file.size = 5;
  EXPECT_FALSE(Match({"-empty"}, Visit{.path = "f", .name = "f", .depth = 1, .metadata = nonempty_file}));

  vfs::Metadata link;
  link.type = vfs::FileType::kSymlink;
  EXPECT_FALSE(Match({"-empty"}, Visit{.path = "l", .name = "l", .depth = 1, .metadata = link}))
      << "-empty matches only regular files and directories";
}

TEST_F(EvaluateTest, SparseMatchesFilesWithHoles) {
  vfs::Metadata sparse;
  sparse.type = vfs::FileType::kRegular;
  sparse.size = 1'000'000;
  sparse.blocks = 8;  // 8 * 512 = 4096, far less than the 1 MB apparent size -> sparse
  EXPECT_TRUE(Match({"-sparse"}, Visit{.path = "s", .name = "s", .depth = 1, .metadata = sparse}));

  vfs::Metadata dense;
  dense.type = vfs::FileType::kRegular;
  dense.size = 4'096;
  dense.blocks = 8;  // 4096 fully allocated -> not sparse
  EXPECT_FALSE(Match({"-sparse"}, Visit{.path = "d", .name = "d", .depth = 1, .metadata = dense}));

  vfs::Metadata empty;
  empty.type = vfs::FileType::kRegular;  // zero-size is not sparse
  EXPECT_FALSE(Match({"-sparse"}, Visit{.path = "e", .name = "e", .depth = 1, .metadata = empty}));
}

TEST_F(EvaluateTest, LinksMatchesHardLinkCount) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.nlink = 1;
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-links", "1"}, visit));
  EXPECT_FALSE(Match({"-links", "2"}, visit));
  EXPECT_TRUE(Match({"-links", "-2"}, visit));
  EXPECT_TRUE(Match({"-links", "+0"}, visit));
  EXPECT_FALSE(Match({"-links", "+1"}, visit));
}

TEST_F(EvaluateTest, NewerFalseWhenReferenceMissing) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  // The reference cannot be stat'd, so -newer is false (real comparisons are
  // covered by the conformance test against actual files).
  EXPECT_FALSE(Match({"-newer", "/no/such/reference/file"}, visit));
}

TEST_F(EvaluateTest, InumMatchesInodeNumber) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.ino = 4'242;
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-inum", "4242"}, visit));
  EXPECT_FALSE(Match({"-inum", "4243"}, visit));
  EXPECT_TRUE(Match({"-inum", "-5000"}, visit));   // inode < 5000
  EXPECT_FALSE(Match({"-inum", "+4242"}, visit));  // not strictly greater than itself
}

TEST_F(EvaluateTest, UsedMatchesWholeDaysBetweenAtimeAndCtime) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.ctime = absl::FromUnixSeconds(1'600'000'000);
  md.atime = md.ctime + absl::Hours(24 * 5) + absl::Hours(3);  // 5 days 3h later -> floor 5
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-used", "5"}, visit));  // exactly 5 whole days
  EXPECT_FALSE(Match({"-used", "6"}, visit));
  EXPECT_TRUE(Match({"-used", "+4"}, visit));  // more than 4
  EXPECT_FALSE(Match({"-used", "+5"}, visit));
  EXPECT_TRUE(Match({"-used", "-6"}, visit));  // fewer than 6
  EXPECT_FALSE(Match({"-used", "-5"}, visit));

  // atime before ctime yields a negative day delta (accessed before status change).
  vfs::Metadata before;
  before.type = vfs::FileType::kRegular;
  before.ctime = absl::FromUnixSeconds(1'600'000'000);
  before.atime = before.ctime - absl::Hours(24 * 2);  // 2 days earlier -> -2
  const Visit earlier{.path = "g", .name = "g", .depth = 1, .metadata = before};
  EXPECT_FALSE(Match({"-used", "0"}, earlier));  // -2 is not 0
  EXPECT_TRUE(Match({"-used", "-1"}, earlier));  // -2 < 1
}

TEST_F(EvaluateTest, SamefileFalseWhenReferenceMissing) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  // No reference to stat, so -samefile is false (the real same-inode match is
  // covered by the conformance test against an actual hard link).
  EXPECT_FALSE(Match({"-samefile", "/no/such/reference/file"}, visit));
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
  EXPECT_TRUE(Match({"-readable"}, visit));
  EXPECT_TRUE(Match({"-writable"}, visit));
  EXPECT_FALSE(Match({"-executable"}, visit));  // no execute bit (X_OK is not bypassed, even for root)
  stdfs::remove(tmp);
  EXPECT_FALSE(Match({"-readable"}, visit));  // gone -> not accessible
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
  EXPECT_TRUE(Match({"-fstype", *actual}, visit));
  EXPECT_FALSE(Match({"-fstype", "xff_no_such_fstype"}, visit));
  stdfs::remove(tmp);
  EXPECT_FALSE(Match({"-fstype", *actual}, visit));  // gone -> statfs fails -> never matches
}

TEST_F(EvaluateTest, ContentMatchesLiteralSubstring) {
  const std::string path = WriteContentFile("literal.txt", "the quick brown fox\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "literal.txt", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-content", "quick brown"}, visit));
  EXPECT_TRUE(Match({"-content", "fox"}, visit));
  EXPECT_FALSE(Match({"-content", "QUICK"}, visit));     // -content is case-sensitive
  EXPECT_FALSE(Match({"-content", "lazy dog"}, visit));  // absent
  // '.' is a literal character, not a regex wildcard: "q.ick" matches "quick" as a
  // regex but not as the literal substring -content searches for.
  EXPECT_FALSE(Match({"-content", "q.ick"}, visit));
}

TEST_F(EvaluateTest, DiffTreatsUnreadableSourceAsDifferent) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("missing-source", "missing-source", vfs::FileType::kRegular, md);
  EXPECT_FALSE(Match({"-diff:none", "missing-target"}, visit));
}

TEST_F(EvaluateTest, IcontentFoldsCase) {
  const std::string path = WriteContentFile("fold.txt", "Hello World");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "fold.txt", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-icontent", "hello"}, visit));
  EXPECT_TRUE(Match({"-icontent", "WORLD"}, visit));
  EXPECT_FALSE(Match({"-icontent", "goodbye"}, visit));
}

TEST_F(EvaluateTest, RxcMatchesRegexAnywhere) {
  const std::string path = WriteContentFile("rx.txt", "id=12345 name=foo\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "rx.txt", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-rxc", "id=[0-9]+"}, visit));  // unanchored: matches a substring
  EXPECT_TRUE(Match({"-rxc", "name=[a-z]+"}, visit));
  EXPECT_FALSE(Match({"-rxc", "id=[a-z]+"}, visit));  // digits are not letters
  EXPECT_FALSE(Match({"-rxc", "^name="}, visit));     // 'name=' is not at the start of the content
}

TEST_F(EvaluateTest, RxcUnderExactMatchesLiterally) {
  // --regextype=EXACT reaches -rxc too (not just -grep): the content predicate matches the argument
  // as a literal substring, so metacharacters are plain text.
  const std::string path = WriteContentFile("rx_exact.txt", "value = a[0-9]b here\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "rx_exact.txt", vfs::FileType::kRegular, md);
  regextype_ = "EXACT";
  EXPECT_TRUE(Match({"-rxc", "a[0-9]b"}, visit));  // the literal bracket text is present
  EXPECT_FALSE(Match({"-rxc", "a5b"}, visit));     // the regex interpretation is gone under EXACT
}

TEST_F(EvaluateTest, RxcUnderFnmatchUsesShellWildcards) {
  // --regextype=FNMATCH treats the -rxc argument as a shell glob: `*` matches any run, `?` one char.
  const std::string path = WriteContentFile("rx_glob.txt", "the quick brown fox\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "rx_glob.txt", vfs::FileType::kRegular, md);
  regextype_ = "FNMATCH";
  EXPECT_TRUE(Match({"-rxc", "quick*fox"}, visit));   // glob spans the middle
  EXPECT_TRUE(Match({"-rxc", "q?ick"}, visit));       // '?' matches one char
  EXPECT_FALSE(Match({"-rxc", "quick.fox"}, visit));  // '.' is literal in a glob, no dot here
}

TEST_F(EvaluateTest, RxcUnderGlobUsesPathAwareGlob) {
  // --regextype=GLOB reaches -rxc: a path-aware shell glob (`*` stops at '/', `**` crosses).
  const std::string path = WriteContentFile("rx_pathglob.txt", "path is src/app/main.cc\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "rx_pathglob.txt", vfs::FileType::kRegular, md);
  regextype_ = "GLOB";
  EXPECT_TRUE(Match({"-rxc", "src/**/main.cc"}, visit));  // `**` crosses the 'app' directory
  EXPECT_TRUE(Match({"-rxc", "src/*/main.cc"}, visit));   // one segment
  EXPECT_FALSE(Match({"-rxc", "src/main.cc"}, visit));    // `*`/segment does not cross '/', 'app' is in the way
}

TEST_F(EvaluateTest, IrxcFoldsCase) {
  const std::string path = WriteContentFile("irx.txt", "STATUS: OK");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "irx.txt", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-irxc", "status: ok"}, visit));
  EXPECT_FALSE(Match({"-rxc", "status: ok"}, visit));  // the case-sensitive form does not match
}

TEST_F(EvaluateTest, ContentSkipsBinaryFiles) {
  // A NUL byte in the sniffed prefix marks the file binary; content search skips it,
  // so even a literal substring that is present does not match (grep/rg behaviour).
  const std::string path = WriteContentFile("bin", std::string("ELF\0needle here", 15));
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "bin", vfs::FileType::kRegular, md);
  EXPECT_FALSE(Match({"-content", "needle"}, visit));
  EXPECT_FALSE(Match({"-rxc", "needle"}, visit));
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
  EXPECT_TRUE(Match({"-grep", "TODO"}, visit));  // returns true because a line matched
  EXPECT_THAT(emitted_, EqualsText(absl::StrCat(path, ":1:first TODO line\n", path, ":3:another TODO here\n")));
}

TEST_F(EvaluateTest, GrepUsesRegexNotLiteral) {
  const std::string path = WriteContentFile("grep_rx.txt", "id=12345\nname=foo\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "grep_rx.txt", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-grep", "id=[0-9]+"}, visit));
  EXPECT_THAT(emitted_, EqualsText(absl::StrCat(path, ":1:id=12345\n")));
}

TEST_F(EvaluateTest, GrepWithNoMatchingLineIsFalseAndSilent) {
  const std::string path = WriteContentFile("grep_none.txt", "nothing to see\nhere at all\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "grep_none.txt", vfs::FileType::kRegular, md);
  EXPECT_FALSE(Match({"-grep", "TODO"}, visit));
  EXPECT_THAT(emitted_, IsEmpty());
}

TEST_F(EvaluateTest, GrepSkipsBinaryFiles) {
  const std::string path = WriteContentFile("grep_bin", std::string("ELF\0TODO here\n", 14));
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "grep_bin", vfs::FileType::kRegular, md);
  EXPECT_FALSE(Match({"-grep", "TODO"}, visit));
  EXPECT_THAT(emitted_, IsEmpty());
}

TEST_F(EvaluateTest, GrepExactModeMatchesLiterally) {
  // grep_literal (--regextype=EXACT) treats the pattern as a literal substring, so
  // '.' is a literal dot, not a regex wildcard.
  const std::string path = WriteContentFile("grep_exact.txt", "price 3.50\nprice 3X50\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "grep_exact.txt", vfs::FileType::kRegular, md);
  regextype_ = "EXACT";
  EXPECT_TRUE(Match({"-grep", "3.50"}, visit));
  EXPECT_THAT(emitted_, EqualsText(absl::StrCat(path, ":1:price 3.50\n")));  // only the literal 3.50, not 3X50
}

TEST_F(EvaluateTest, GrepExactModeAcceptsRegexMetacharactersAsLiterals) {
  // A pattern that is not a valid regex is still a fine literal under EXACT.
  const std::string path = WriteContentFile("grep_lit.txt", "call foo(bar) now\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "grep_lit.txt", vfs::FileType::kRegular, md);
  regextype_ = "EXACT";
  EXPECT_TRUE(Match({"-grep", "foo(bar"}, visit));
  EXPECT_THAT(emitted_, EqualsText(absl::StrCat(path, ":1:call foo(bar) now\n")));
}

TEST_F(EvaluateTest, GrepFormatRendersTemplatePerMatchLine) {
  // -grep:FORMAT overrides the default path:line:text; {line}/{text} bind per line.
  const std::string path = WriteContentFile("grep_fmt.txt", "alpha\nhit one\nbeta\nhit two\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "grep_fmt.txt", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-grep:{line}: {text}", "hit"}, visit));
  EXPECT_THAT(emitted_, EqualsText("2: hit one\n4: hit two\n"));
}

TEST_F(EvaluateTest, GrepFormatCanReferenceEntryFields) {
  // The template sees the entry's field vocabulary too, not just {line}/{text}.
  const std::string path = WriteContentFile("grep_fmt2.txt", "x hit y\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "grep_fmt2.txt", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-grep:{path}#{line}", "hit"}, visit));
  EXPECT_THAT(emitted_, EqualsText(absl::StrCat(path, "#1\n")));
}

TEST_F(EvaluateTest, GrepFormatMatchAndColumnExtractTheMatch) {
  // {match} is the matched substring (grep -o), {column} its 1-based start.
  const std::string path = WriteContentFile("grep_o.txt", "code E42 here\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "grep_o.txt", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-grep:{column}:{match}", "E[0-9]+"}, visit));
  EXPECT_THAT(emitted_, EqualsText("6:E42\n"));  // E42 starts at column 6 (after "code ")
}

TEST_F(EvaluateTest, GrepFormatMatchInExactModeUsesTheLiteralSpan) {
  const std::string path = WriteContentFile("grep_o2.txt", "aXbXc\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "grep_o2.txt", vfs::FileType::kRegular, md);
  regextype_ = "EXACT";
  EXPECT_TRUE(Match({"-grep:{column} {match}", "X"}, visit));
  EXPECT_THAT(emitted_, EqualsText("2 X\n"));  // first literal X at column 2
}

TEST_F(EvaluateTest, GrepCountEmitsPerFileMatchLineCount) {
  // --count: one path:count (matching lines) instead of the lines; supersedes FORMAT.
  const std::string path = WriteContentFile("grep_c.txt", "TODO a\nx\nTODO b\nTODO c\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "grep_c.txt", vfs::FileType::kRegular, md);
  grep_count_ = true;
  EXPECT_TRUE(Match({"-grep:{line}", "TODO"}, visit));  // FORMAT is superseded by --count
  EXPECT_THAT(emitted_, EqualsText(absl::StrCat(path, ":3\n")));
}

TEST_F(EvaluateTest, GrepCountEmitsNothingWhenNoLineMatches) {
  const std::string path = WriteContentFile("grep_c0.txt", "nothing here\n");
  vfs::Metadata md;
  const Visit visit = MakeVisit(path, "grep_c0.txt", vfs::FileType::kRegular, md);
  grep_count_ = true;
  EXPECT_FALSE(Match({"-grep", "TODO"}, visit));  // no match -> false, and no count line
  EXPECT_THAT(emitted_, IsEmpty());
}

TEST_F(EvaluateTest, ContentNonRegularOrMissingDoesNotMatch) {
  // A directory has no searchable content and a missing path is unreadable: both are
  // a clean no-match, not an error.
  vfs::Metadata dir_md;
  const Visit dir = MakeVisit(".", ".", vfs::FileType::kDirectory, dir_md);
  EXPECT_FALSE(Match({"-content", "anything"}, dir));
  vfs::Metadata md;
  const Visit missing = MakeVisit("/no/such/xff/file", "file", vfs::FileType::kRegular, md);
  EXPECT_FALSE(Match({"-content", "anything"}, missing));
  EXPECT_FALSE(Match({"-rxc", "anything"}, missing));
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
  EXPECT_TRUE(Match({"-lname", "*/target.txt"}, visit));   // glob against the (unresolved) target text
  EXPECT_FALSE(Match({"-lname", "*/TARGET.txt"}, visit));  // -lname is case-sensitive
  EXPECT_TRUE(Match({"-ilname", "*/TARGET.txt"}, visit));  // -ilname folds case
  // A non-symlink never matches, even when the target glob otherwise would.
  vfs::Metadata reg;
  reg.type = vfs::FileType::kRegular;
  const Visit regular{.path = path, .name = "xff_lname_probe.link", .depth = 1, .metadata = reg};
  EXPECT_FALSE(Match({"-lname", "*"}, regular));
  stdfs::remove(link);
}

TEST_F(EvaluateTest, XtypeFollowsSymlinkTarget) {
  namespace stdfs = ::std::filesystem;
  const stdfs::path dir = stdfs::temp_directory_path() / "xff_xtype_probe.d";
  stdfs::remove_all(dir);
  ASSERT_TRUE(stdfs::create_directories(dir));
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
  EXPECT_TRUE(Match({"-xtype", "f"}, v_file));  // link -> regular file
  EXPECT_FALSE(Match({"-xtype", "d"}, v_file));
  EXPECT_TRUE(Match({"-xtype", "d"}, v_dir));     // link -> directory
  EXPECT_TRUE(Match({"-xtype", "l"}, v_broken));  // broken link reports type l
  EXPECT_FALSE(Match({"-xtype", "f"}, v_broken));
  // A non-symlink behaves exactly like -type.
  vfs::Metadata reg_md;
  reg_md.type = vfs::FileType::kRegular;
  const Visit v_reg{.path = file_s, .name = "target.txt", .depth = 1, .metadata = reg_md};
  EXPECT_TRUE(Match({"-xtype", "f"}, v_reg));
  EXPECT_FALSE(Match({"-xtype", "l"}, v_reg));
  stdfs::remove_all(dir);
}

TEST_F(EvaluateTest, MTimeMatchesWholeDaysAgo) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.mtime = now_ - absl::Hours(60);  // 2.5 days ago -> floor to 2 whole days
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-mtime", "2"}, visit));  // floor(2.5) == 2
  EXPECT_FALSE(Match({"-mtime", "3"}, visit));
  EXPECT_TRUE(Match({"-mtime", "+1"}, visit));   // strictly older than 1 day
  EXPECT_FALSE(Match({"-mtime", "+2"}, visit));  // not strictly older than 2
  EXPECT_TRUE(Match({"-mtime", "-3"}, visit));   // strictly younger than 3 days
}

TEST_F(EvaluateTest, MMinMatchesWholeMinutesAgo) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.mtime = now_ - absl::Minutes(150);  // 150 minutes ago
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-mmin", "150"}, visit));
  EXPECT_TRUE(Match({"-mmin", "+100"}, visit));
  EXPECT_FALSE(Match({"-mmin", "+150"}, visit));
  EXPECT_TRUE(Match({"-mmin", "-200"}, visit));
}

TEST_F(EvaluateTest, BTimeMatchesWholeDaysSinceBirth) {
  // BSD -Btime: the -mtime of the birth time. Same whole-day floor / +N older /
  // -N younger semantics, measured against `btime`.
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.btime = now_ - absl::Hours(60);  // born 2.5 days ago -> floor to 2 whole days
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-Btime", "2"}, visit));  // floor(2.5) == 2
  EXPECT_FALSE(Match({"-Btime", "3"}, visit));
  EXPECT_TRUE(Match({"-Btime", "+1"}, visit));   // strictly older than 1 day
  EXPECT_FALSE(Match({"-Btime", "+2"}, visit));  // not strictly older than 2
  EXPECT_TRUE(Match({"-Btime", "-3"}, visit));   // strictly younger than 3 days
}

TEST_F(EvaluateTest, BMinMatchesWholeMinutesSinceBirth) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.btime = now_ - absl::Minutes(150);  // born 150 minutes ago
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-Bmin", "150"}, visit));
  EXPECT_TRUE(Match({"-Bmin", "+100"}, visit));
  EXPECT_FALSE(Match({"-Bmin", "+150"}, visit));
  EXPECT_TRUE(Match({"-Bmin", "-200"}, visit));
}

TEST_F(EvaluateTest, BirthtimePredicatesNeverMatchWhenBtimeUnrecorded) {
  // btime is optional: when the kernel/FS did not record it, -Btime/-Bmin match
  // nothing -- there is no value to compare -- and must not borrow another stamp.
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;  // md.btime deliberately left empty
  md.mtime = now_ - absl::Hours(60);  // a present mtime must not be used as a fallback
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_FALSE(Match({"-Btime", "2"}, visit));
  EXPECT_FALSE(Match({"-Btime", "-9999"}, visit));  // even a wide window: no btime -> no match
  EXPECT_FALSE(Match({"-Bmin", "-9999"}, visit));
}

TEST_F(EvaluateTest, BirthtimePredicateFlagsUnsupportedWhenBtimeUnrecorded) {
  // An unrecorded birth time is an impossible task: besides not matching, the
  // predicate raises the control side-channel so the driver can fail (or, under
  // --skip-unsupported, warn and skip). Covers -Btime/-Bmin and the X=B -newerXY.
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;  // no btime
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_FALSE(Match({"-Btime", "2"}, visit));
  EXPECT_THAT(control_.unsupported, Not(IsEmpty()));
  EXPECT_FALSE(Match({"-Bmin", "2"}, visit));
  EXPECT_THAT(control_.unsupported, Not(IsEmpty()));
  EXPECT_FALSE(Match({"-newerBt", "@1"}, visit));
  EXPECT_THAT(control_.unsupported, Not(IsEmpty()));
}

TEST_F(EvaluateTest, BirthtimePredicateDoesNotFlagUnsupportedWhenBtimePresent) {
  // With a recorded birth time the predicate evaluates normally and leaves the
  // control side-channel clear (no impossible-task signal).
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.btime = now_ - absl::Hours(60);
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-Btime", "2"}, visit));
  EXPECT_THAT(control_.unsupported, IsEmpty());
}

TEST_F(EvaluateTest, MTimeAcceptsBsdUnitSuffix) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.mtime = now_ - absl::Hours(3);  // 3 hours ago
  md.atime = now_ - absl::Hours(3);
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  // A BSD s/m/h/d/w suffix overrides the day default (here, hours).
  EXPECT_TRUE(Match({"-mtime", "3h"}, visit));     // floor(3h / 1h) == 3
  EXPECT_TRUE(Match({"-mtime", "+2h"}, visit));    // older than 2 hours
  EXPECT_FALSE(Match({"-mtime", "+3h"}, visit));   // not older than 3
  EXPECT_TRUE(Match({"-mtime", "-4h"}, visit));    // younger than 4 hours
  EXPECT_FALSE(Match({"-mtime", "-3h"}, visit));   // not younger than 3
  EXPECT_TRUE(Match({"-mtime", "+179m"}, visit));  // 180 min old, older than 179 minutes
  EXPECT_TRUE(Match({"-mtime", "-1d"}, visit));    // younger than 1 day
  EXPECT_TRUE(Match({"-mtime", "+1s"}, visit));    // older than 1 second
  EXPECT_TRUE(Match({"-atime", "+2h"}, visit));    // suffix works on -atime too
  EXPECT_FALSE(Match({"-mtime", "3x"}, visit));    // unrecognised suffix -> no match
}

TEST_F(EvaluateTest, MMinRejectsUnitSuffix) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.mtime = now_ - absl::Minutes(150);
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  // -mmin is GNU integer-minutes only; a BSD-style suffix is not its vocabulary.
  EXPECT_FALSE(Match({"-mmin", "150m"}, visit));
  EXPECT_FALSE(Match({"-mmin", "-3h"}, visit));
}

TEST_F(EvaluateTest, MTimeAcceptsXffWordDuration) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.mtime = now_ - absl::Hours(24 * 7 * 3) - absl::Hours(3);  // 3 weeks 3 hours ago
  md.atime = md.mtime;
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  // "+span" = older than the span; "-span" = younger than it (the span reaches
  // back through ParseTimeString, so compound terms work).
  EXPECT_TRUE(Match({"-mtime", "+3 weeks"}, visit));           // 3w3h old is older than 3 weeks
  EXPECT_FALSE(Match({"-mtime", "+3 weeks 4 hours"}, visit));  // but not older than 3w4h
  EXPECT_TRUE(Match({"-mtime", "-3 weeks 4 hours"}, visit));   // it is younger than 3w4h
  EXPECT_FALSE(Match({"-mtime", "-3 weeks"}, visit));          // not younger than 3 weeks
  EXPECT_TRUE(Match({"-atime", "+1 day"}, visit));             // works on -atime too
  EXPECT_FALSE(Match({"-mtime", "3 weeks"}, visit));           // the word form requires an explicit sign
}

TEST_F(EvaluateTest, UidAndGidMatchNumericOwner) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.uid = 501;
  md.gid = 20;
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-uid", "501"}, visit));
  EXPECT_FALSE(Match({"-uid", "500"}, visit));
  EXPECT_TRUE(Match({"-uid", "+500"}, visit));  // uid strictly greater than 500
  EXPECT_TRUE(Match({"-gid", "20"}, visit));
  EXPECT_FALSE(Match({"-gid", "21"}, visit));
  EXPECT_TRUE(Match({"-gid", "-21"}, visit));  // gid strictly less than 21
}

TEST_F(EvaluateTest, AccessAndChangeTimeFamily) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.atime = now_ - absl::Hours(60);     // 2.5 days / 3600 minutes ago
  md.ctime = now_ - absl::Minutes(150);  // 2.5 hours / 150 minutes ago
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-atime", "2"}, visit));  // -atime/-amin read atime: floor(2.5 days) == 2
  EXPECT_TRUE(Match({"-atime", "+1"}, visit));
  EXPECT_TRUE(Match({"-amin", "3600"}, visit));
  EXPECT_TRUE(Match({"-amin", "+3000"}, visit));
  EXPECT_TRUE(Match({"-ctime", "0"}, visit));  // -ctime/-cmin read ctime: 2.5 hours floors to 0 days
  EXPECT_TRUE(Match({"-cmin", "150"}, visit));
  EXPECT_FALSE(Match({"-cmin", "+150"}, visit));
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
  EXPECT_TRUE(Match({"-user", "501"}, visit));
  EXPECT_FALSE(Match({"-user", "500"}, visit));
  EXPECT_TRUE(Match({"-group", "20"}, visit));
  EXPECT_FALSE(Match({"-group", "21"}, visit));
}

TEST_F(EvaluateTest, NouserNogroupMatchOrphanedIds) {
  vfs::Metadata owned_md;
  owned_md.type = vfs::FileType::kRegular;
  owned_md.uid = 0;  // root: present in passwd
  owned_md.gid = 0;  // present in group
  const Visit owned{.path = "a", .name = "a", .depth = 1, .metadata = owned_md};
  EXPECT_FALSE(Match({"-nouser"}, owned));
  EXPECT_FALSE(Match({"-nogroup"}, owned));
  vfs::Metadata orphan_md;
  orphan_md.type = vfs::FileType::kRegular;
  orphan_md.uid = 2'000'000'001U;  // unassigned on Linux/macOS runners (avoids macOS nobody == -2)
  orphan_md.gid = 2'000'000'001U;
  const Visit orphan{.path = "b", .name = "b", .depth = 1, .metadata = orphan_md};
  EXPECT_TRUE(Match({"-nouser"}, orphan));
  EXPECT_TRUE(Match({"-nogroup"}, orphan));
}

TEST_F(EvaluateTest, CommaEvaluatesBothValueIsRight) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  // The comma list evaluates both operands; its value is the right operand's.
  EXPECT_FALSE(Match({"-true", ",", "-false"}, visit));
  EXPECT_TRUE(Match({"-false", ",", "-true"}, visit));
  // Side effects on both sides still occur: two -print actions emit two records.
  EXPECT_TRUE(Match({"-print", ",", "-print"}, visit));
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
    EXPECT_FALSE(Match(action, Visit{.path = "a.tar!x", .name = "x", .depth = 1, .metadata = member})) << label;
    EXPECT_THAT(control_.unsupported, HasSubstr("archive member")) << label;
  }
}

TEST_F(EvaluateTest, WriteActionsStillActOnARealFile) {
  // The guard keys on the entry's SOURCE, so an ordinary file is untouched by it: -exec still runs and
  // reports true, and nothing is recorded as unsupported.
  vfs::Metadata real;
  real.type = vfs::FileType::kRegular;
  EXPECT_TRUE(Match({"-exec", "true", ";"}, Visit{.path = "f", .name = "f", .depth = 1, .metadata = real}));
  EXPECT_THAT(control_.unsupported, IsEmpty());
}

TEST_F(EvaluateTest, PruneAndQuitSetControl) {
  vfs::Metadata md;
  md.type = vfs::FileType::kDirectory;
  const Visit visit{.path = "d", .name = "d", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-prune"}, visit));
  EXPECT_TRUE(control_.prune);
  EXPECT_FALSE(control_.quit);
  EXPECT_TRUE(Match({"-quit"}, visit));
  EXPECT_TRUE(control_.quit);
  EXPECT_FALSE(control_.prune);  // control is reset per Match
  // A short-circuited -prune does not fire: `-type f -prune` on a directory.
  EXPECT_FALSE(Match({"-type", "f", "-prune"}, visit));
  EXPECT_FALSE(control_.prune);
}

TEST_F(EvaluateTest, RegexMatchesWholePath) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("a/b/c.txt", "c.txt", vfs::FileType::kRegular, md);
  // -regex matches the whole path (not just the basename); -iregex folds case.
  EXPECT_TRUE(Match({"-regex", ".*\\.txt"}, visit));
  EXPECT_FALSE(Match({"-regex", ".*\\.md"}, visit));
  EXPECT_FALSE(Match({"-regex", "c\\.txt"}, visit));  // must match the whole path, not the basename
  EXPECT_TRUE(Match({"-iregex", ".*\\.TXT"}, visit));
}

TEST_F(EvaluateTest, NewerXYFalseWhenReferenceMissing) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("f", "f", vfs::FileType::kRegular, md);
  // -newerXY with an unreadable reference is false; real field comparisons are
  // covered by the conformance test against actual files.
  EXPECT_FALSE(Match({"-newermm", "/no/such/reference"}, visit));
  EXPECT_FALSE(Match({"-newerac", "/no/such/reference"}, visit));
}

TEST_F(EvaluateTest, AnewerCnewerCompareEntryTimeToReferenceMtime) {
  // -anewer/-cnewer are the classic spellings of -neweram/-newercm: the entry's
  // atime/ctime vs the reference file's mtime. False when the reference is gone.
  vfs::Metadata missing_md;
  const Visit missing = MakeVisit("f", "f", vfs::FileType::kRegular, missing_md);
  EXPECT_FALSE(Match({"-anewer", "/no/such/reference"}, missing));
  EXPECT_FALSE(Match({"-cnewer", "/no/such/reference"}, missing));

  namespace stdfs = ::std::filesystem;
  const stdfs::path ref = stdfs::temp_directory_path() / "xff_anewer_ref.tmp";
  { std::ofstream(ref) << "r"; }  // reference mtime is ~now (between the two fixed times below)
  const std::string ref_path = ref.string();
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.atime = absl::FromUnixSeconds(2'000'000'000);  // 2033, after the reference's mtime
  md.ctime = absl::FromUnixSeconds(1'000'000'000);  // 2001, before the reference's mtime
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-anewer", ref_path}, visit));   // atime newer than the reference mtime
  EXPECT_FALSE(Match({"-cnewer", ref_path}, visit));  // ctime older than the reference mtime
  stdfs::remove(ref);
}

TEST_F(EvaluateTest, NewerBtComparesBirthTimeToTimeString) {
  // -newerBt (X=B, Y=t): the entry's birth time vs a time string. now_ == 1.7e9.
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.btime = now_;
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_TRUE(Match({"-newerBt", "@1500000000"}, visit));   // born after an earlier instant
  EXPECT_FALSE(Match({"-newerBt", "@1700000001"}, visit));  // not after a later instant
}

TEST_F(EvaluateTest, NewerBCombosAreFalseWhenBirthTimeUnrecorded) {
  // Every -newerXY touching B is false when that birth time is unrecorded -- here
  // the entry's (X=B). Both the time-string and file-reference forms short-circuit.
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;  // no btime
  md.mtime = now_;
  const Visit visit{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_FALSE(Match({"-newerBt", "@1"}, visit));                  // X=B, no btime
  EXPECT_FALSE(Match({"-newerBm", "/no/such/reference"}, visit));  // X=B (also a missing ref)
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
  EXPECT_TRUE(Match({"-newerBm", ref_path}, younger));
  md.btime = absl::FromUnixSeconds(1'000'000'000);  // 2001, before the reference's mtime
  const Visit older{.path = "f", .name = "f", .depth = 1, .metadata = md};
  EXPECT_FALSE(Match({"-newerBm", ref_path}, older));
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
    EXPECT_TRUE(Match({"-newermB", ref_path}, younger));
    md.mtime = absl::FromUnixSeconds(1'000'000'000);  // 2001, before the reference's birth time
    const Visit older{.path = "f", .name = "f", .depth = 1, .metadata = md};
    EXPECT_FALSE(Match({"-newermB", ref_path}, older));
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
  EXPECT_TRUE(Match({"-printf", "%p|%f|%h|%s|%m|%d|%y\\n"}, visit));
  EXPECT_THAT(emitted_, "a/b/c.txt|c.txt|a/b|42|644|2|f\n");
  EXPECT_TRUE(Match({"-printf", "%%\\t%n"}, visit));  // literal %, tab escape, link count
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
  EXPECT_TRUE(Match({"-printf", "%u|%U|%g|%G"}, visit));
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
  EXPECT_TRUE(Match({"-printf", "%t|%TY-%Tm-%Td %TH:%TM:%TS|%AY"}, visit));
  EXPECT_THAT(emitted_, "Sun Sep 13 12:26:40 2020|2020-09-13 12:26:40|1970");
  // The rendering zone honors EvalContext::tz (--timezone): 12:26 UTC is 13:26 in UTC+1.
  tz_ = absl::FixedTimeZone(3'600);
  EXPECT_TRUE(Match({"-printf", "%TH"}, visit));
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
  EXPECT_TRUE(Match({"-println"}, visit));
  EXPECT_THAT(emitted_, "a/b/c.txt\n");
  // -printfln: -printf plus a trailing OS line ending the format need not carry.
  EXPECT_TRUE(Match({"-printfln", "%f|%s"}, visit));
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
  EXPECT_TRUE(Match({"-ls"}, visit));
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
  EXPECT_FALSE(Match({"-ok", "/bin/echo", "{}", ";"}, visit));
  EXPECT_THAT(last_prompt_, "/bin/echo dir/foo.txt? ");
  // Affirmative: the command runs and -ok mirrors its exit status.
  confirm_reply_ = true;
  EXPECT_TRUE(Match({"-ok", "/bin/sh", "-c", "exit 0", ";"}, visit));
  EXPECT_FALSE(Match({"-ok", "/bin/sh", "-c", "exit 1", ";"}, visit));
}

TEST_F(EvaluateTest, NewerMtComparesEntryTimeToTimeString) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.mtime = absl::FromUnixSeconds(1'600'000'000);  // 2020-09-13, a fixed mtime
  const Visit visit = MakeVisit("f", "f", vfs::FileType::kRegular, md);
  // @epoch reference: mtime is newer than an earlier instant, not a later one.
  EXPECT_TRUE(Match({"-newermt", "@1500000000"}, visit));
  EXPECT_FALSE(Match({"-newermt", "@1700000000"}, visit));
  // ISO date form (local zone); the coarse year gap is timezone-independent.
  EXPECT_TRUE(Match({"-newermt", "2001-01-01"}, visit));
  EXPECT_FALSE(Match({"-newermt", "2099-01-01"}, visit));
  // An unparseable time string never matches.
  EXPECT_FALSE(Match({"-newermt", "yesterday"}, visit));
}

TEST_F(EvaluateTest, NewerMtAcceptsRelativeTimeStrings) {
  vfs::Metadata md;
  md.type = vfs::FileType::kRegular;
  md.mtime = now_ - absl::Hours(48);  // modified two days before the reference clock
  const Visit visit = MakeVisit("f", "f", vfs::FileType::kRegular, md);
  EXPECT_TRUE(Match({"-newermt", "3 days ago"}, visit));  // within the last three days
  EXPECT_TRUE(Match({"-newermt", "-3 days"}, visit));     // same, sign form
  EXPECT_FALSE(Match({"-newermt", "1 day ago"}, visit));  // not within the last day
  EXPECT_FALSE(Match({"-newermt", "now"}, visit));        // older than now
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
  EXPECT_FALSE(Match({"-newermt", "2020-01-02"}, visit));
  tz_ = absl::FixedTimeZone(3'600);  // UTC+1: ref = 2020-01-01 23:00 UTC; mtime is 30 min later -> newer
  EXPECT_TRUE(Match({"-newermt", "2020-01-02"}, visit));
}

TEST_F(EvaluateTest, ExecFieldsGatesNamedPlaceholderSubstitution) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("a/b/f.txt", "f.txt", vfs::FileType::kRegular, md);
  // Default (find-exact): {name} is not a placeholder, so the child compares the
  // literal "{name}" against "f.txt" -> not equal -> child exits 1 -> false.
  exec_fields_ = false;
  EXPECT_FALSE(Match({"-exec", "/bin/sh", "-c", "test \"{name}\" = f.txt", ";"}, visit));
  // With --exec-fields, {name} renders the basename -> equal -> child exits 0 -> true.
  exec_fields_ = true;
  EXPECT_TRUE(Match({"-exec", "/bin/sh", "-c", "test \"{name}\" = f.txt", ";"}, visit));
}

TEST_F(EvaluateTest, ExecdirRunsChildInEntryDirectoryWithDotSlashBasename) {
  vfs::Metadata md;
  // Entry "/x.txt" -> -execdir runs the child in "/" with {} expanded to "./x.txt"
  // ("/" always exists, so the chdir succeeds under the test sandbox).
  const Visit visit = MakeVisit("/x.txt", "x.txt", vfs::FileType::kRegular, md);
  exec_fields_ = false;
  EXPECT_TRUE(Match({"-execdir", "/bin/sh", "-c", "test \"$(pwd -P)\" = /", ";"}, visit));  // ran in "/"
  EXPECT_TRUE(Match({"-execdir", "/bin/sh", "-c", "test \"{}\" = ./x.txt", ";"}, visit));   // {} -> ./basename
  EXPECT_FALSE(Match({"-execdir", "/bin/sh", "-c", "test \"{}\" = ./nope", ";"}, visit));
}

TEST_F(EvaluateTest, ExecdirHonorsExecFields) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("/f.txt", "f.txt", vfs::FileType::kRegular, md);
  // With --exec-fields, {name} renders the basename (the vocabulary still sees the
  // full path); without it, {name} stays literal and the comparison fails.
  exec_fields_ = true;
  EXPECT_TRUE(Match({"-execdir", "/bin/sh", "-c", "test \"{name}\" = f.txt", ";"}, visit));
  exec_fields_ = false;
  EXPECT_FALSE(Match({"-execdir", "/bin/sh", "-c", "test \"{name}\" = f.txt", ";"}, visit));
}

TEST_F(EvaluateTest, OkdirPromptsWithDotSlashBasenameThenRunsInEntryDirOnYes) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("/x.txt", "x.txt", vfs::FileType::kRegular, md);
  confirm_reply_ = true;  // affirmative: -okdir runs the command, like -execdir
  EXPECT_TRUE(Match({"-okdir", "/bin/sh", "-c", "test \"$(pwd -P)\" = /", ";"}, visit));  // ran in "/"
  EXPECT_TRUE(Match({"-okdir", "/bin/sh", "-c", "test \"{}\" = ./x.txt", ";"}, visit));   // {} -> ./basename
  // The prompt substitutes {} -> ./basename and ends with "? ".
  EXPECT_TRUE(Match({"-okdir", "/bin/echo", "{}", ";"}, visit));
  EXPECT_THAT(last_prompt_, "/bin/echo ./x.txt? ");  // tokens joined, then "? " (no space before, like -ok)
}

TEST_F(EvaluateTest, OkdirDeclinedDoesNotRunAndIsFalse) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("/x.txt", "x.txt", vfs::FileType::kRegular, md);
  confirm_reply_ = false;  // declined: not run, -okdir is false (the command would exit 0)
  EXPECT_FALSE(Match({"-okdir", "/bin/sh", "-c", "exit 0", ";"}, visit));
}

TEST_F(EvaluateTest, CapturedirRunsCommandInEntryDirAndBindsStdout) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("/x.txt", "x.txt", vfs::FileType::kRegular, md);
  // -capturedir:NAME runs the command in the entry's directory ("/") and binds its
  // stdout (trailing newline stripped) to {capture.NAME}.
  EXPECT_TRUE(Match({"-capturedir:cwd", "/bin/sh", "-c", "pwd -P", ";"}, visit));
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
  EXPECT_TRUE(Match({"-regex", "(.*)/([^/]+)\\.(.*)", "-exec", "/bin/sh", "-c", "test \"{1}\" = a/b", ";"}, visit));
  // Without the gate there is no capture and {1} stays literal -> not equal -> false.
  exec_fields_ = false;
  EXPECT_FALSE(Match({"-regex", "(.*)/([^/]+)\\.(.*)", "-exec", "/bin/sh", "-c", "test \"{1}\" = a/b", ";"}, visit));
}

TEST_F(EvaluateTest, CapturesAreVisibleLeftToRightOnly) {
  // The variable store accumulates left-to-right, so a binding (here a -regex
  // match) is in scope for later actions and only later -- the guarantee that
  // capture/-exec chaining rests on.
  vfs::Metadata md;
  const Visit visit = MakeVisit("a/b/c.txt", "c.txt", vfs::FileType::kRegular, md);
  exec_fields_ = true;
  // -regex to the LEFT of -exec: its groups are bound when -exec runs, so {1} == "a/b".
  EXPECT_TRUE(Match({"-regex", "(.*)/([^/]+)\\.(.*)", "-exec", "/bin/sh", "-c", "test \"{1}\" = a/b", ";"}, visit));
  // -regex to the RIGHT: not yet evaluated when -exec runs, so {1} is still empty there.
  EXPECT_TRUE(Match({"-exec", "/bin/sh", "-c", "test -z \"{1}\"", ";", "-regex", "(.*)/([^/]+)\\.(.*)"}, visit));
}

TEST_F(EvaluateTest, CaptureBindsOutputNamespace) {
  vfs::Metadata md;
  const Visit visit = MakeVisit("a/b/c.txt", "c.txt", vfs::FileType::kRegular, md);
  exec_fields_ = true;  // so the -exec reading {capture.tag} renders the vocabulary
  // -capture runs the command and binds {capture.tag}; the later -exec reads it.
  EXPECT_TRUE(Match(
      {"-capture:tag", "/bin/sh", "-c", "printf hi", ";", "-exec", "/bin/sh", "-c", "test \"{capture.tag}\" = hi", ";"},
      visit));
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
