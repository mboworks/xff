// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_PRESENTATION_RENDER_SCOPED_TABLE_H_
#define XFF_PRESENTATION_RENDER_SCOPED_TABLE_H_

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace xff::render {

// Numeric values are preformatted; labels are escaped here. Accounting never changes.
struct ScopedTableRow {
  std::string label;
  std::vector<std::array<std::string, 4>> metrics;
  bool quote_label = false;  // Quote ambiguous data labels, escaping their bytes exactly once.
};

// One plain-text table. Width zero keeps scope columns side by side; otherwise a
// table too wide for the budget uses one labelled scope row per group. Numeric
// cells are never truncated, even if their indivisible row exceeds the budget.
std::string RenderScopedTable(
    const std::vector<std::string>& scopes,
    std::vector<ScopedTableRow> rows,
    std::size_t width,
    bool with_header);

}  // namespace xff::render

#endif  // XFF_PRESENTATION_RENDER_SCOPED_TABLE_H_
