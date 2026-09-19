// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/status/status_macros.h"
#include "mbo/testing/status.h"
#include "xff/engine/run.h"
#include "xff/parser/parser.h"

namespace xff::engine {
namespace {

using ::mbo::testing::StatusIs;
using ::testing::HasSubstr;
using ::testing::Not;

struct ResourceInspectionTest : ::testing::Test {
  static absl::StatusOr<std::string> Inspect(const std::vector<std::string>& arguments) {
    MBO_ASSIGN_OR_RETURN(const auto command, parser::Parse(arguments));
    return ExplainResources(command);
  }
};

TEST_F(ResourceInspectionTest, InspectsAbsentRootsWithoutTraversal) {
  ASSERT_OK_AND_ASSIGN(const auto output, Inspect({"--jobs=3", "/absent-resource-fixture", "-type", "f"}));
  EXPECT_THAT(output, HasSubstr("directory-workers-per-walk\t3"));
  EXPECT_THAT(output, HasSubstr("eligible-command-workers-per-walk\t3 (independent"));
  EXPECT_THAT(output, HasSubstr("content-field-occurrences\t0"));
  EXPECT_THAT(output, Not(HasSubstr("column-buffer\t")));
  EXPECT_THAT(output, HasSubstr("static inspection, not measured usage"));
}

TEST_F(ResourceInspectionTest, ComparisonInventoriesAreIndependentOfColumnBufferLimits) {
  ASSERT_OK_AND_ASSIGN(
      const auto output,
      Inspect({"/left", "/right", "--compare=summary", "--summary=ext", "--buffer=off", "--jobs=2"}));
  EXPECT_THAT(output, HasSubstr("walks\t2"));
  EXPECT_THAT(output, HasSubstr("directory-workers-per-walk\t2"));
  EXPECT_THAT(output, HasSubstr("comparison-state\tboth matched inventories"));
  EXPECT_THAT(output, HasSubstr("comparison-summary-state\tper-entry contributions"));
  EXPECT_THAT(output, HasSubstr("--buffer is not a process-memory cap or a comparison-inventory bound"));
  ASSERT_OK_AND_ASSIGN(
      const auto all, Inspect({"/left", "/right", "--compare=summary", "--summary=ext", "--summary-scope=all"}));
  EXPECT_THAT(all, Not(HasSubstr("comparison-summary-state\t")));
}

TEST_F(ResourceInspectionTest, ActiveListingFieldsRespectSuppressionAndColumnPrecedence) {
  ASSERT_OK_AND_ASSIGN(const auto active, Inspect({"/root", "--template={hash} {lines}"}));
  EXPECT_THAT(active, HasSubstr("content-field-occurrences\t2"));
  ASSERT_OK_AND_ASSIGN(const auto reduced, Inspect({"/root", "--template={hash}", "--summary=ext"}));
  EXPECT_THAT(reduced, HasSubstr("content-field-occurrences\t0"));
  ASSERT_OK_AND_ASSIGN(const auto cleared, Inspect({"/root", "--template={hash}", "--summary=ext", "--summary=none"}));
  EXPECT_THAT(cleared, HasSubstr("content-field-occurrences\t1"));
  ASSERT_OK_AND_ASSIGN(
      const auto columns, Inspect({"/root", "--template={hash}", "--columns=name", "--format=aligned"}));
  EXPECT_THAT(columns, HasSubstr("content-field-occurrences\t0"));
  ASSERT_OK_AND_ASSIGN(const auto disabled, Inspect({"/root", "--template={hash}", "--implicit-print=no"}));
  EXPECT_THAT(disabled, HasSubstr("content-field-occurrences\t0"));
}

TEST_F(ResourceInspectionTest, PrintfAndExecutionUseTheirActualFieldGrammars) {
  ASSERT_OK_AND_ASSIGN(const auto printf_output, Inspect({"/root", "-printf", "%%{hash} %{lines}"}));
  EXPECT_THAT(printf_output, HasSubstr("content-field-occurrences\t1"));
  ASSERT_OK_AND_ASSIGN(const auto literal, Inspect({"/root", "-exec", "echo", "{hash}", ";"}));
  EXPECT_THAT(literal, HasSubstr("content-field-occurrences\t0"));
  ASSERT_OK_AND_ASSIGN(const auto expanded, Inspect({"--exec-fields", "/root", "-exec", "echo", "{hash}", ";"}));
  EXPECT_THAT(expanded, HasSubstr("content-field-occurrences\t1"));
  ASSERT_OK_AND_ASSIGN(const auto batch, Inspect({"/root", "-exec", "echo", "{}", "+"}));
  EXPECT_THAT(batch, HasSubstr("command-batch-state\tmatched paths retained"));
}

TEST_F(ResourceInspectionTest, GrepCountDisablesAttachedTemplateReads) {
  ASSERT_OK_AND_ASSIGN(const auto lines, Inspect({"/root", "-grep:{hash}", "pattern"}));
  EXPECT_THAT(lines, HasSubstr("content-field-occurrences\t1"));
  ASSERT_OK_AND_ASSIGN(const auto counts, Inspect({"/root", "--count", "-grep:{hash}", "pattern"}));
  EXPECT_THAT(counts, HasSubstr("content-field-occurrences\t0"));
}

TEST_F(ResourceInspectionTest, ColumnBuffersUseRuntimeDefaultsAndCaps) {
  ASSERT_OK_AND_ASSIGN(const auto listing, Inspect({"/root", "--format=aligned"}));
  EXPECT_THAT(listing, HasSubstr("listing-column-buffer\tall rows"));
  ASSERT_OK_AND_ASSIGN(const auto authored, Inspect({"/root", "--format=aligned", "--template={name}"}));
  EXPECT_THAT(authored, Not(HasSubstr("listing-column-buffer\t")));
  ASSERT_OK_AND_ASSIGN(const auto ls, Inspect({"/root", "-ls"}));
  EXPECT_THAT(ls, HasSubstr("action-column-buffer\t100 rows"));
  ASSERT_OK_AND_ASSIGN(const auto bounded, Inspect({"/root", "-ls", "--buffer=1KiB"}));
  EXPECT_THAT(bounded, HasSubstr("action-column-buffer\t1024 cell bytes"));
  ASSERT_OK_AND_ASSIGN(const auto file_ls, Inspect({"/root", "-fls", "output"}));
  EXPECT_THAT(file_ls, Not(HasSubstr("action-column-buffer\t")));
}

TEST_F(ResourceInspectionTest, CollectionLimitsDistinguishUnlimitedFromDisabledAlignment) {
  ASSERT_OK_AND_ASSIGN(const auto unlimited, Inspect({"/root", "-collect", "--buffer=off"}));
  EXPECT_THAT(unlimited, HasSubstr("row cap unlimited; path/name/root byte cap unlimited"));
  ASSERT_OK_AND_ASSIGN(const auto rows, Inspect({"/root", "-collect", "--buffer=10"}));
  EXPECT_THAT(rows, HasSubstr("row cap 10; path/name/root byte cap unlimited"));
  ASSERT_OK_AND_ASSIGN(const auto bytes, Inspect({"/root", "-collect", "--buffer=1KiB"}));
  EXPECT_THAT(bytes, HasSubstr("row cap unlimited; path/name/root byte cap 1024"));
}

TEST_F(ResourceInspectionTest, ReductionReadsRespectActiveRequests) {
  ASSERT_OK_AND_ASSIGN(const auto hash, Inspect({"/root", "--summary=hash"}));
  EXPECT_THAT(hash, HasSubstr("content-field-occurrences\t1"));
  ASSERT_OK_AND_ASSIGN(const auto histogram, Inspect({"/root", "--histogram=lines:sum(lines)"}));
  EXPECT_THAT(histogram, HasSubstr("line-histogram-consumers\t1"));
  ASSERT_OK_AND_ASSIGN(const auto cleared, Inspect({"/root", "--summary=hash", "--summary=none"}));
  EXPECT_THAT(cleared, HasSubstr("line-histogram-consumers\t0"));
  EXPECT_THAT(cleared, Not(HasSubstr("reduction-state\t")));
}

TEST_F(ResourceInspectionTest, RetentionModesFollowEffectiveControls) {
  ASSERT_OK_AND_ASSIGN(const auto tree, Inspect({"/root", "--format=tree", "--template={hash}"}));
  EXPECT_THAT(tree, HasSubstr("tree-output-state\tmatching paths"));
  EXPECT_THAT(tree, HasSubstr("content-field-occurrences\t0"));
  ASSERT_OK_AND_ASSIGN(const auto rank, Inspect({"/root", "--sort=score", "-fuzzy", "a"}));
  EXPECT_THAT(rank, HasSubstr("ranking-state\tentry output retained"));
  ASSERT_OK_AND_ASSIGN(const auto unranked, Inspect({"/root", "--sort=score", "--sort=dir", "-fuzzy", "a"}));
  EXPECT_THAT(unranked, Not(HasSubstr("ranking-state\t")));
  ASSERT_OK_AND_ASSIGN(const auto deferred, Inspect({"/root", "-fuzzy", "a", "-top", "1"}));
  EXPECT_THAT(deferred, HasSubstr("deferred-expression-state\tcandidates retained"));
  ASSERT_OK_AND_ASSIGN(const auto shards, Inspect({"/root", "--shards"}));
  EXPECT_THAT(shards, HasSubstr("shard-state\tphysical entries retained"));
  ASSERT_OK_AND_ASSIGN(const auto pack, Inspect({"/root", "--pack=output.tar"}));
  EXPECT_THAT(pack, HasSubstr("archive-pack-state\tentry plan retained"));
}

TEST_F(ResourceInspectionTest, ExpressionCostsDescribeBothConditionalBranches) {
  ASSERT_OK_AND_ASSIGN(const auto output, Inspect({"/root", "!", "-content", "a", "-o", "-content", "b"}));
  EXPECT_THAT(output, HasSubstr("expensive-primaries\t-content,-content"));
  EXPECT_THAT(output, HasSubstr("registry cost tier, not a content-read classification"));
}

TEST_F(ResourceInspectionTest, SummaryTemplatesAndAlignedColumnsCountTheirFields) {
  ASSERT_OK_AND_ASSIGN(const auto summary, Inspect({"/root", "--summary={hash} {lines}"}));
  EXPECT_THAT(summary, HasSubstr("content-field-occurrences\t2"));
  ASSERT_OK_AND_ASSIGN(const auto columns, Inspect({"/root", "--format=aligned", "--columns=hash,lines"}));
  EXPECT_THAT(columns, HasSubstr("content-field-occurrences\t2"));
  ASSERT_OK_AND_ASSIGN(const auto off, Inspect({"/root", "-ls", "--buffer=off"}));
  EXPECT_THAT(off, HasSubstr("action-column-buffer\toff"));
  ASSERT_OK_AND_ASSIGN(const auto all, Inspect({"/root", "-ls", "--buffer=all"}));
  EXPECT_THAT(all, HasSubstr("action-column-buffer\tall rows"));
}

TEST_F(ResourceInspectionTest, InvalidLimitsRemainErrorsDuringInspection) {
  EXPECT_THAT(Inspect({"/root", "--histogram=none"}), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(Inspect({"/root", "--summary-scope=root"}), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(Inspect({"/root", "--jobs=0"}), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(Inspect({"/root", "--buffer=garbage"}), StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace xff::engine
