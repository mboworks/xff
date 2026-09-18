// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include "xff/presentation/render/scoped_table.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "xff/presentation/format/format.h"
#include "xff/presentation/format/text_width.h"
#include "xff/presentation/render/render.h"

namespace xff::render {
namespace {

constexpr auto kMetrics = std::to_array<std::string_view>({"Count", "% count", "Size", "% size"});
using Widths = std::array<std::size_t, 4>;

Widths HeaderWidths() {
  return {kMetrics.at(0).size(), kMetrics.at(1).size(), kMetrics.at(2).size(), kMetrics.at(3).size()};
}

std::size_t ScopeWidth(const Widths& widths) {
  std::size_t width = 6;  // Three two-column gaps.
  for (const auto metric : widths) {
    width += metric;
  }
  return width;
}

std::string PadLabel(std::string_view label, std::size_t width) {
  const std::size_t columns = format::TextColumns(label);
  return std::string(label) + std::string(width > columns ? width - columns : 0, ' ');
}

std::string MetricHeader(const Widths& widths) {
  std::string line;
  for (std::size_t metric = 0; metric < kMetrics.size(); ++metric) {
    if (metric != 0) {
      line += "  ";
    }
    line += format::PadLeft(kMetrics.at(metric), widths.at(metric));
  }
  return line;
}

std::string MetricCells(const std::array<std::string, 4>& cells, const Widths& widths) {
  std::string line;
  for (std::size_t metric = 0; metric < cells.size(); ++metric) {
    if (metric != 0) {
      line += "  ";
    }
    line += format::PadLeft(cells.at(metric), widths.at(metric));
  }
  return line;
}

std::vector<Widths> ColumnWidths(const std::vector<std::string>& scopes, const std::vector<ScopedTableRow>& rows) {
  std::vector<Widths> widths(scopes.size(), HeaderWidths());
  for (const ScopedTableRow& row : rows) {
    for (std::size_t scope = 0; scope < scopes.size(); ++scope) {
      for (std::size_t metric = 0; metric < kMetrics.size(); ++metric) {
        widths.at(scope).at(metric) = std::max(widths.at(scope).at(metric), row.metrics.at(scope).at(metric).size());
      }
    }
  }
  for (std::size_t scope = 0; scope < scopes.size(); ++scope) {
    const std::size_t name_width = format::TextColumns(scopes.at(scope));
    const std::size_t span = ScopeWidth(widths.at(scope));
    if (name_width > span) {
      widths.at(scope).back() += name_width - span;
    }
  }
  return widths;
}

std::string RenderNarrow(
    const std::vector<std::string>& scopes,
    const std::vector<ScopedTableRow>& rows,
    const std::vector<Widths>& scope_widths,
    std::size_t width,
    bool with_header) {
  Widths widths = HeaderWidths();
  std::size_t label_width = 5;  // "Scope".
  for (std::size_t scope = 0; scope < scopes.size(); ++scope) {
    label_width = std::max(label_width, format::TextColumns(scopes.at(scope)) + 2);
    for (std::size_t metric = 0; metric < kMetrics.size(); ++metric) {
      widths.at(metric) = std::max(widths.at(metric), scope_widths.at(scope).at(metric));
    }
  }
  std::string result;
  if (with_header) {
    result = "Group\n" + PadLabel("Scope", label_width) + "  " + MetricHeader(widths) + "\n";
  }
  for (const ScopedTableRow& row : rows) {
    for (const std::string& part : format::WrapColumns(row.label, width)) {
      result += part + "\n";
    }
    for (std::size_t scope = 0; scope < scopes.size(); ++scope) {
      result +=
          PadLabel("  " + scopes.at(scope), label_width) + "  " + MetricCells(row.metrics.at(scope), widths) + "\n";
    }
  }
  return result;
}

}  // namespace

std::string RenderScopedTable(
    const std::vector<std::string>& scopes,
    std::vector<ScopedTableRow> rows,
    std::size_t width,
    bool with_header) {
  if (scopes.empty()) {
    return {};
  }
  const Renderer escaping(Format::kPlain, PathEncoding::kEscape);
  for (ScopedTableRow& row : rows) {
    if (row.quote_label) {
      row.label = JsonQuote(row.label);
    } else {
      row.label = escaping.Record(row.label);
      row.label.pop_back();  // Remove the record terminator, preserving escaped embedded controls.
    }
  }
  const std::vector<Widths> widths = ColumnWidths(scopes, rows);
  std::size_t label_width = 5;  // "Group".
  for (const ScopedTableRow& row : rows) {
    label_width = std::max(label_width, format::TextColumns(row.label));
  }
  std::size_t total_width = label_width;
  for (std::size_t scope = 0; scope < scopes.size(); ++scope) {
    total_width += 2 + std::max(ScopeWidth(widths.at(scope)), format::TextColumns(scopes.at(scope)));
  }
  if (width != 0 && total_width > width) {
    return RenderNarrow(scopes, rows, widths, width, with_header);
  }
  std::string result;
  if (with_header) {
    result = PadLabel("Group", label_width);
    for (std::size_t scope = 0; scope < scopes.size(); ++scope) {
      result += "  " + PadLabel(scopes.at(scope), ScopeWidth(widths.at(scope)));
    }
    while (!result.empty() && result.back() == ' ') {
      result.pop_back();
    }
    result += "\n" + std::string(label_width, ' ');
    for (const Widths& scope : widths) {
      result += "  " + MetricHeader(scope);
    }
    result += "\n";
  }
  for (const ScopedTableRow& row : rows) {
    result += PadLabel(row.label, label_width);
    for (std::size_t scope = 0; scope < scopes.size(); ++scope) {
      result += "  " + MetricCells(row.metrics.at(scope), widths.at(scope));
    }
    result += "\n";
  }
  return result;
}

}  // namespace xff::render
