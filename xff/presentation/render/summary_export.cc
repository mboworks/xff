// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/presentation/render/summary_export.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "xff/presentation/render/render.h"

namespace xff::render {
namespace {

constexpr auto kMetrics = std::to_array<std::string_view>({"count", "count_percent", "bytes", "size_percent"});

std::string Percent(std::uint64_t value, std::uint64_t total, unsigned precision) {
  const double percent = total == 0 ? 0.0 : 100.0 * static_cast<double>(value) / static_cast<double>(total);
  return absl::StrFormat("%.*f", precision, percent);
}

std::vector<std::string> MetricCells(const SummaryExportMetrics& metrics, unsigned precision) {
  return {
      absl::StrCat(metrics.count), Percent(metrics.count, metrics.total_count, precision),
      metrics.bytes.has_value() ? absl::StrCat(metrics.bytes->value) : "",
      metrics.bytes.has_value() ? Percent(metrics.bytes->value, metrics.bytes->total, precision) : ""};
}

}  // namespace

SummaryExport::SummaryExport(std::vector<std::string> scopes) : scopes_(std::move(scopes)) {}

std::string SummaryExport::Header(Format format) const {
  std::vector<std::string> cells{"record",   "request",   "summary",       "template", "scope",
                                 "root",     "left_root", "right_root",    "type",     "group",
                                 "is_total", "count",     "count_percent", "bytes",    "size_percent"};
  cells.reserve(cells.size() + (kMetrics.size() * scopes_.size()));
  for (const std::string& scope : scopes_) {
    for (const std::string_view metric : kMetrics) {
      cells.push_back(absl::StrCat(scope, ".", metric));
    }
  }
  return EncodeTabularRow(format, cells);
}

std::string SummaryExport::Row(const SummaryExportRecord& record, Format format, unsigned precision) const {
  std::vector<std::string> cells{
      "summary",
      absl::StrCat(record.request),
      std::string(record.summary),
      std::string(record.key_template),
      std::string(record.scope),
      std::string(record.root),
      std::string(record.left_root),
      std::string(record.right_root),
      std::string(record.type),
      std::string(record.group),
      record.is_total ? "true" : "false"};
  cells.reserve(cells.size() + (kMetrics.size() * (1 + scopes_.size())));
  auto metrics = record.metrics.has_value() ? MetricCells(*record.metrics, precision) : std::vector<std::string>(4);
  cells.insert(cells.end(), metrics.begin(), metrics.end());
  for (const std::string& scope : scopes_) {
    const auto found = record.scoped_metrics.find(scope);
    metrics =
        found == record.scoped_metrics.end() ? std::vector<std::string>(4) : MetricCells(found->second, precision);
    cells.insert(cells.end(), metrics.begin(), metrics.end());
  }
  return EncodeTabularRow(format, cells);
}

}  // namespace xff::render
