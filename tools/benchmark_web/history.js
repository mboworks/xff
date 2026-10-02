// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
window.XffBenchmarkHistory = function historyExplorer(root, catalog) {
  const platform = root.querySelector('[data-control="platform"]');
  const version = root.querySelector('[data-control="version"]');
  const status = root.querySelector('[role="status"]');
  const reportLink = root.querySelector("[data-report]");
  const host = root.querySelector("[data-chart]");
  const metric = root.querySelector('[data-control="metric"]');
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
    const record = records[Number(version.value)];
    current = record;
    const request = ++serial;
    pending?.abort();
    pending = new AbortController();
    host.hidden = true;
    reportLink.href = record.report;
    reportLink.textContent = `${record.label} | ${record.date} | ${record.local ? `measured ${record.measured}` : `run ${record.run}, attempt ${record.attempt}`}`;
    version.setAttribute("aria-valuetext", reportLink.textContent);
    status.textContent = `Loading ${record.label}...`;
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
      host.hidden = false;
      if (!chart) chart = window.XffLandscape(host, selected);
      chart.update(order.value, metric.value, selected);
      status.textContent = `${Number(version.value) + 1} of ${records.length} measured versions. ${record.identity}`;
      host.dataset.commit = record.commit;
    } catch (error) {
      if (request !== serial) return;
      status.textContent = `Unable to load this chart (${error.message}). Open the linked report or select another version.`;
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
  for (const selector of [metric, order, allocations])
    selector.addEventListener("change", () => {
      if (chart && !host.hidden)
        chart.update(order.value, metric.value, selectedFigures());
    });
  root
    .querySelector("[data-reset]")
    .addEventListener("click", () => chart?.reset?.());
  root.querySelector("details").addEventListener("toggle", (event) => {
    if (event.target.open) requestAnimationFrame(() => chart?.resize());
  });
  if (catalog.length) selectPlatform();
  else
    status.textContent = "No retained reports have comparison landscape data.";
};
