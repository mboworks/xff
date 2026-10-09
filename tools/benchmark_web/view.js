// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

export function reserveChart(root) {
  root.style.cssText =
    "position:relative;width:100%;height:850px;min-width:320px;overflow:hidden";
}

export const panelStyle =
  "background:#fffffff0;color:#172333;border:1px solid #667;border-radius:4px;box-shadow:0 2px 10px #0002;margin:0;padding:8px;font:12px system-ui;min-width:0;pointer-events:auto";

export function chartSidebar(root, side = "left") {
  const className =
    side === "right" ? "landscape-sidebar-right" : "landscape-sidebar";
  let sidebar = root.querySelector(`.${className}`);
  if (!sidebar) {
    sidebar = document.createElement("div");
    sidebar.className = className;
    sidebar.style.cssText = `position:absolute;${side}:12px;top:12px;z-index:2;display:grid;font:12px system-ui;gap:1lh;width:340px;max-width:calc(100% - 24px);pointer-events:none`;
    root.append(sidebar);
  }
  return sidebar;
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
