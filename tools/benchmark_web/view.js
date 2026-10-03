// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

export function reserveChart(root) {
  root.style.cssText =
    "position:relative;width:100%;height:850px;min-width:320px;overflow:hidden";
}

export function viewOptions(metric, range, overflow, normalization) {
  for (const option of range.options) {
    option.textContent =
      option.value === "auto"
        ? "Auto"
        : `+/- ${Number(option.value) * (metric === "percent" ? 100 : 1)}${metric === "percent" ? "%" : ""}`;
  }
  return {
    limit:
      range.value === "auto"
        ? null
        : Number(range.value) * (metric === "percent" ? 100 : 1),
    overflow: overflow.value,
    normalized: normalization?.value === "reference",
  };
}
