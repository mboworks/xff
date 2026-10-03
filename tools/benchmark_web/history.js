// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
import { chartSidebar, panelStyle, reserveChart, viewOptions } from "./view.js";

window.XffBenchmarkHistory = function historyExplorer(root, catalog) {
  const platform = root.querySelector('[data-control="platform"]');
  const version = root.querySelector('[data-control="version"]');
  const status = root.querySelector('[role="status"]');
  const reportLink = root.querySelector("[data-report]");
  const versionLinks = root.querySelector("[data-version-links]");
  const host = root.querySelector("[data-chart]");
  reserveChart(host);
  const versionPanel = root.querySelector("[data-version-panel]");
  versionPanel.style.cssText = panelStyle;
  chartSidebar(host).append(versionPanel);
  const versionStyle = document.createElement("style");
  versionStyle.textContent = `.landscape-version-table{width:100%;border-collapse:collapse;margin:0;font:inherit}
    .landscape-version-table th,.landscape-version-table td{padding:2px 4px;border:0;vertical-align:top;background:transparent}
    .landscape-version-table th{text-align:left;font-weight:500;white-space:nowrap}
    .landscape-version-table td{text-align:right;overflow-wrap:anywhere}
    [data-version-panel] [role=status]{margin-top:4px}`;
  host.append(versionStyle);
  const metric = root.querySelector('[data-control="metric"]');
  const range = root.querySelector('[data-control="range"]');
  const overflow = root.querySelector('[data-control="overflow"]');
  const normalization = root.querySelector('[data-control="normalization"]');
  const allocations = root.querySelector('[data-control="allocations"]');
  let loadedFigures;
  const order = root.querySelector('[data-control="order"]');
  let records = [],
    current,
    chart,
    serial = 0,
    pending;
  const platforms = [...new Set(catalog.map((row) => row.platform))].sort(
    (left, right) =>
      Number(left.startsWith("Local /")) -
        Number(right.startsWith("Local /")) || left.localeCompare(right),
  );
  for (const name of platforms) {
    const option = document.createElement("option");
    option.value = name;
    option.textContent = name;
    platform.append(option);
  }
  async function selectVersion() {
    const index = Number(version.value);
    const record = records[index];
    current = record;
    const request = ++serial;
    pending?.abort();
    pending = new AbortController();
    host.setAttribute("aria-busy", "true");
    const reportText = `${record.label} | ${record.date} | ${record.local ? `measured ${record.measured}` : `run ${record.run}, attempt ${record.attempt}`}`;
    function showReport() {
      reportLink.href = record.report;
      reportLink.textContent = record.commit.slice(0, 10);
      reportLink.title = `Open the full benchmark report: ${reportText}`;
      root.querySelector("[data-version-count]").textContent =
        `${index + 1} of ${records.length}`;
      root.querySelector("[data-platform]").textContent = record.platform;
      root.querySelector("[data-platform-details]").textContent =
        record.platform_details || record.identity;
      const date = root.querySelector("[data-revision-date]");
      date.textContent = record.date.slice(0, 10);
      date.title = record.date;
      root.querySelector("[data-measured-row]").hidden = !record.local;
      root.querySelector("[data-measured]").textContent =
        record.measured?.slice(0, 10) || "";
      root.querySelector("[data-measured]").title = record.measured || "";
      versionLinks.replaceChildren();
      for (const link of record.links || []) {
        const target = new URL(link.href, location.href);
        if (target.protocol !== "https:") continue;
        const anchor = document.createElement("a");
        anchor.href = target.href;
        anchor.textContent = link.label;
        if (versionLinks.childElementCount) versionLinks.append(" / ");
        versionLinks.append(anchor);
      }
      root.querySelector("[data-links-row]").hidden =
        !versionLinks.childElementCount;
    }
    if (!chart) showReport();
    version.setAttribute("aria-valuetext", reportText);
    status.textContent = `Loading ${record.label}...`;
    status.hidden = false;
    try {
      const response = await fetch(record.figures, { signal: pending.signal });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const figures = await response.json();
      if (request !== serial) return;
      loadedFigures = figures;
      const previous = allocations.value;
      allocations.replaceChildren();
      for (const pair of figures.allocation_pairs || [])
        allocations.add(new Option(pair.label, pair.value));
      if ([...allocations.options].some((option) => option.value === previous))
        allocations.value = previous;
      root.querySelector("[data-allocation-label]").hidden =
        !figures.allocation_pairs;
      const selected = selectedFigures();
      if (!chart) chart = window.XffLandscape(host, selected);
      chart.update(
        order.value,
        metric.value,
        selected,
        viewOptions(metric.value, range, overflow, normalization),
      );
      showReport();
      status.textContent = "";
      status.hidden = true;
      host.dataset.commit = record.commit;
    } catch (error) {
      if (request !== serial) return;
      status.textContent = `Unable to load ${record.commit.slice(0, 10)} (${error.message}). ${chart ? "The previous result remains visible. " : ""}Open the linked report or select another version.`;
    } finally {
      if (request === serial) host.removeAttribute("aria-busy");
    }
  }
  function selectedFigures() {
    return (
      loadedFigures?.allocation_pairs?.find(
        (pair) => pair.value === allocations.value,
      )?.figures || loadedFigures
    );
  }
  function selectPlatform() {
    const commit = current?.commit;
    records = catalog.filter((row) => row.platform === platform.value);
    version.max = String(records.length - 1);
    version.disabled = records.length < 2;
    const matching = records.findIndex((row) => row.commit === commit);
    version.value = String(matching < 0 ? records.length - 1 : matching);
    selectVersion();
  }
  platform.addEventListener("change", selectPlatform);
  version.addEventListener("input", selectVersion);
  for (const selector of [
    metric,
    order,
    allocations,
    range,
    overflow,
    normalization,
  ])
    selector.addEventListener("change", () => {
      const view = viewOptions(metric.value, range, overflow, normalization);
      if (chart)
        chart.update(order.value, metric.value, selectedFigures(), view);
    });
  root
    .querySelector("[data-reset]")
    .addEventListener("click", () => chart?.reset?.());
  root.querySelector("details").addEventListener("toggle", (event) => {
    if (event.target.open) requestAnimationFrame(() => chart?.resize());
  });
  if (catalog.length) selectPlatform();
  else {
    status.hidden = false;
    status.textContent = "No retained reports have comparison landscape data.";
  }
};
