// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/presentation/render/summary_export.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mbo/testing/matchers.h"
#include "xff/presentation/render/render.h"

namespace xff::render {
namespace {

using ::mbo::testing::EqualsText;
using ::mbo::testing::WithDropIndent;
using ::testing::HasSubstr;

struct SummaryExportTest : ::testing::Test {};

TEST_F(SummaryExportTest, ScalarRowsRetainIdentityRawNumbersAndFullDenominators) {
  const SummaryExport renderer;
  const SummaryExportRecord row{
      .request = 3,
      .summary = "ext",
      .scope = "all",
      .group = "total",
      .metrics =
          SummaryExportMetrics{
              .count = 2'500,
              .total_count = 10'000,
              .bytes = SummaryExportBytes{.value = 1'024, .total = 4'096},
          },
  };
  EXPECT_THAT(renderer.Header(Format::kCsv), WithDropIndent(EqualsText(R"out(
      record,request,summary,template,scope,root,left_root,right_root,type,group,is_total,count,count_percent,bytes,size_percent
  )out")));
  EXPECT_THAT(renderer.Row(row, Format::kCsv, 3), WithDropIndent(EqualsText(R"out(
      summary,3,ext,,all,,,,,total,false,2500,25.000,1024,25.000
  )out")));
}

TEST_F(SummaryExportTest, ScopeColumnsKeepSelectorOrderAndDistinguishMissingFromZero) {
  const SummaryExport renderer({"right-total", "left-total"});
  const SummaryExportRecord row{
      .summary = "ext",
      .scope = "right-total,left-total",
      .left_root = "a",
      .right_root = "b",
      .group = "cc",
      .scoped_metrics =
          {{"left-total", {.count = 0, .total_count = 0, .bytes = SummaryExportBytes{.value = 0, .total = 0}}}},
  };
  EXPECT_THAT(
      renderer.Header(Format::kCsv),
      HasSubstr(
          "right-total.count,right-total.count_percent,right-total.bytes,right-total.size_percent,left-total.count"));
  EXPECT_THAT(renderer.Row(row, Format::kCsv, 2), WithDropIndent(EqualsText(R"out(
      summary,0,ext,,"right-total,left-total",,a,b,,cc,false,,,,,,,,,0,0.00,0,0.00
  )out")));
}

TEST_F(SummaryExportTest, ExtractionHasNoByteDimensionAndTotalsHaveExplicitIdentity) {
  const SummaryExport renderer;
  const SummaryExportRecord row{
      .request = 1,
      .summary = "template",
      .key_template = "{capture.words:m/,/x/}",
      .scope = "all",
      .group = "total",
      .is_total = true,
      .metrics = SummaryExportMetrics{.count = 2, .total_count = 2},
  };
  EXPECT_THAT(renderer.Row(row, Format::kCsv, 0), WithDropIndent(EqualsText(R"out(
      summary,1,template,"{capture.words:m/,/x/}",all,,,,,total,true,2,100,,
  )out")));
}

TEST_F(SummaryExportTest, EscapesUsingTheSameCsvAndTsvContractAsListings) {
  const SummaryExport renderer;
  const SummaryExportRecord row{.summary = "ext", .scope = "all", .group = "a,\"b\t\\c\nd"};
  EXPECT_THAT(renderer.Row(row, Format::kCsv, 2), HasSubstr("\"a,\"\"b\t\\c\nd\""));
  EXPECT_THAT(renderer.Row(row, Format::kTsv, 2), HasSubstr("a,\"b\\t\\\\c\\nd\tfalse"));
}

}  // namespace
}  // namespace xff::render
