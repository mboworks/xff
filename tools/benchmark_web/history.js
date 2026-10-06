// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
import { chartSidebar, panelStyle, reserveChart, viewOptions } from "./view.js";
import { renderOverview } from "./overview.js";

window.XffBenchmarkHistory = function historyExplorer(root, catalog) {
  const platform = root.querySelector('[data-control="platform"]');
  const source = root.querySelector('[data-control="source"]');
  const version = root.querySelector('[data-control="version"]');
  const previousVersion = root.querySelector("[data-version-previous]");
  const nextVersion = root.querySelector("[data-version-next]");
  const status = root.querySelector('[role="status"]');
  const reportLink = root.querySelector("[data-report]");
  const versionLinks = root.querySelector("[data-version-links]");
  const host = root.querySelector("[data-chart]");
  reserveChart(host);
  const versionPanel = root.querySelector("[data-version-panel]");
  versionPanel.style.cssText = panelStyle;
  chartSidebar(host).append(versionPanel);
  const versionStyle = document.createElement("style");
  versionStyle.textContent = `.landscape-card>summary{cursor:pointer;user-select:none;margin:-2px 0 6px}
    .landscape-card:not([open])>summary{margin-bottom:-2px}
    .landscape-version-table{width:100%;border-collapse:collapse;margin:0;font:inherit}
    .landscape-version-table th,.landscape-version-table td{padding:2px 4px;border:0;vertical-align:top;background:transparent}
    .landscape-version-table th{text-align:left;font-weight:500;white-space:nowrap}
    .landscape-version-table td{text-align:right;overflow-wrap:anywhere}
    [data-platform-details]{height:48px;line-height:16px;display:-webkit-box;-webkit-line-clamp:3;-webkit-box-orient:vertical;overflow:hidden}
    [data-version-panel] [role=status]{margin-top:4px;height:16px;line-height:16px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
    [data-version-panel] [role=status][hidden]{display:block;visibility:hidden}
    .landscape-version-control{display:flex;align-items:center;gap:6px;margin-top:6px;width:100%}
    .landscape-version-control input{flex:1;min-width:0;width:100%}
    .landscape-version-control button{width:28px;height:24px;padding:0;font-weight:bold}`;
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
  function updateVersionControls() {
    const index = Number(version.value);
    previousVersion.disabled = version.disabled || index <= 0;
    nextVersion.disabled = version.disabled || index >= records.length - 1;
  }
  const platforms = [...new Set(catalog.map((row) => row.platform))].sort(
    (left, right) =>
      Number(left.startsWith("Local /")) -
        Number(right.startsWith("Local /")) || left.localeCompare(right),
  );
  for (const [value, label] of new Map(
    catalog
      .filter((row) => row.source && row.source !== "merged")
      .map((row) => [row.source, row.source_label]),
  ))
    source?.add(new Option(label, value));
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
      updateVersionControls();
      root.querySelector("[data-platform]").textContent = record.platform;
      root.querySelector("[data-platform-details]").textContent =
        record.platform_details || record.identity;
      root.querySelector("[data-platform-details]").title =
        record.platform_details || record.identity;
      const date = root.querySelector("[data-revision-date]");
      date.textContent = record.date.slice(0, 10);
      date.title = record.date;
      const measured = record.measurement_date || record.measured;
      root.querySelector("[data-measured]").textContent =
        measured?.slice(0, 10) || "Not recorded";
      root.querySelector("[data-measured]").title = measured || "";
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
      if (!versionLinks.childElementCount)
        versionLinks.textContent = "Not available";
    }
    if (!chart) showReport();
    version.setAttribute("aria-valuetext", reportText);
    status.textContent = `Loading ${record.label}...`;
    status.title = status.textContent;
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
      renderOverview(root.querySelector("[data-overview]"), figures.overview);
      status.textContent = "";
      status.title = "";
      status.hidden = true;
      host.dataset.commit = record.commit;
    } catch (error) {
      if (request !== serial) return;
      status.textContent = `Unable to load ${record.commit.slice(0, 10)} (${error.message}). ${chart ? "The previous result remains visible. " : ""}Open the linked report or select another version.`;
      status.title = status.textContent;
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
    records = catalog.filter(
      (row) =>
        row.platform === platform.value &&
        (row.source || "merged") === (source?.value || "merged"),
    );
    if (!records.length) return;
    version.max = String(records.length - 1);
    version.disabled = records.length < 2;
    const matching = records.findIndex((row) => row.commit === commit);
    version.value = String(matching < 0 ? records.length - 1 : matching);
    updateVersionControls();
    selectVersion();
  }
  function selectSource() {
    const previous = platform.value;
    const available = new Set(
      catalog
        .filter(
          (row) => (row.source || "merged") === (source?.value || "merged"),
        )
        .map((row) => row.platform),
    );
    platform.replaceChildren();
    for (const name of platforms.filter((name) => available.has(name)))
      platform.add(new Option(name, name));
    if (available.has(previous)) platform.value = previous;
    if (available.size) selectPlatform();
  }
  source?.addEventListener("change", selectSource);
  platform.addEventListener("change", selectPlatform);
  version.addEventListener("input", selectVersion);
  for (const [button, delta] of [
    [previousVersion, -1],
    [nextVersion, 1],
  ])
    button.addEventListener("click", () => {
      version.value = String(Number(version.value) + delta);
      updateVersionControls();
      selectVersion();
    });
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
  for (const disclosure of root.querySelectorAll("details"))
    disclosure.addEventListener("toggle", (event) => {
      if (event.target.open) requestAnimationFrame(() => chart?.resize());
    });
  if (catalog.length) selectSource();
  else {
    status.hidden = false;
    status.textContent = "No retained reports have comparison landscape data.";
  }
};
