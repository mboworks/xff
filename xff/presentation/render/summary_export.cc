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

std::array<std::string, 4> MetricCells(const SummaryExportMetrics& metrics, unsigned precision) {
  return {
      absl::StrCat(metrics.count),
      Percent(metrics.count, metrics.total_count, precision),
      metrics.bytes.has_value() ? absl::StrCat(metrics.bytes->value) : "",
      metrics.bytes.has_value() ? Percent(metrics.bytes->value, metrics.bytes->total, precision) : "",
  };
}

}  // namespace

SummaryExport::SummaryExport(std::vector<std::string> scopes) : scopes_(std::move(scopes)) {}

std::string SummaryExport::Header(Format format) const {
  std::vector<std::string> cells{
      "record", "request", "summary",  "template", "scope",         "root",  "left_root",    "right_root",
      "type",   "group",   "is_total", "count",    "count_percent", "bytes", "size_percent",
  };
  cells.reserve(cells.size() + (kMetrics.size() * scopes_.size()));
  for (const std::string& scope : scopes_) {
    for (const std::string_view metric : kMetrics) {
      cells.push_back(absl::StrCat(scope, ".", metric));
    }
  }
  return EncodeTabularRow(format, cells);
}

std::string SummaryExport::Row(const SummaryExportRecord& record, Format format, unsigned precision) const {
  const std::string request = absl::StrCat(record.request);
  const auto identity = std::to_array<std::string_view>({
      "summary",
      request,
      record.summary,
      record.key_template,
      record.scope,
      record.root,
      record.left_root,
      record.right_root,
      record.type,
      record.group,
      record.is_total ? "true" : "false",
  });
  DelimitedRow encoded(format);
  for (const std::string_view cell : identity) {
    encoded.Append(cell);
  }
  const auto append_metrics = [&](const std::array<std::string, 4>& cells) {
    for (const std::string& cell : cells) {
      encoded.Append(cell);
    }
  };
  append_metrics(record.metrics.has_value() ? MetricCells(*record.metrics, precision) : std::array<std::string, 4>{});
  for (const std::string& scope : scopes_) {
    const auto found = record.scoped_metrics.find(scope);
    append_metrics(
        found == record.scoped_metrics.end() ? std::array<std::string, 4>{} : MetricCells(found->second, precision));
  }
  return std::move(encoded).Finish();
}

}  // namespace xff::render
