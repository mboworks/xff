// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
async function start() {
  const response = await fetch(
    new URL("../database-manifest.json", import.meta.url),
  );
  if (!response.ok)
    throw new Error("Benchmark manifest: HTTP " + response.status);
  const manifest = await response.json();
  const series = new Map(manifest.series.map((s) => [s.id, s]));
  function resolveRecord(id) {
    const seen = new Set();
    while (manifest.aliases?.[id] && !seen.has(id)) {
      seen.add(id);
      id = manifest.aliases[id];
    }
    return id;
  }
  const worker = new Worker(new URL("database-worker.js", import.meta.url));
  const pending = new Map(),
    loaded = new Map(),
    metrics = new Map();
  let sequence = 0,
    current,
    selection = 0;
  let activeSeries;
  const loading = document.getElementById("database-loading");
  function loadingState(stage, label = "") {
    loading.hidden = false;
    loading.querySelector("[data-loading-stage]").textContent = stage;
    loading.querySelector("[data-loading-label]").textContent = label;
    document
      .getElementById("benchmark-explorer")
      .setAttribute("aria-busy", "true");
  }
  function loadedState() {
    loading.hidden = true;
    document
      .getElementById("benchmark-explorer")
      .setAttribute("aria-busy", "false");
  }
  const mb = (v) => (v / 1e6).toFixed(1) + " MB";
  const ms = (v) => Math.round(v) + " ms";
  function storage() {
    const total = manifest.series.reduce(
      (sum, s) => sum + s.compressed_bytes,
      0,
    );
    document.getElementById("storage-summary").textContent =
      `${manifest.series.length} databases \u00b7 ${mb(total)} current data${manifest.storage ? ` \u00b7 ${manifest.storage.files} site files` : ""}`;
    const body = document.getElementById("storage-body");
    body.replaceChildren();
    const explanation = document.createElement("p");
    explanation.textContent =
      "Only the selected dataseries is downloaded and decoded. Revision and view changes query its local SQLite database. Original report bytes and their SHA-256 identities are retained.";
    body.append(explanation);
    const table = document.createElement("table");
    for (const s of manifest.series) {
      const m = metrics.get(s.id) || {},
        tr = document.createElement("tr");
      for (const value of [
        s.label,
        `${s.reports} reports`,
        mb(s.compressed_bytes) + " transferred",
        mb(s.expanded_bytes) + " SQLite",
        m.stage || "Not loaded",
        m.downloadMs !== undefined ? ms(m.downloadMs) + " download" : "",
        m.decodeMs !== undefined ? ms(m.decodeMs) + " decode" : "",
        m.openMs !== undefined ? ms(m.openMs) + " verify/open" : "",
        m.queryMs !== undefined ? ms(m.queryMs) + " query/chart" : "",
      ]) {
        const td = document.createElement("td");
        td.textContent = value;
        tr.append(td);
      }
      table.append(tr);
    }
    body.append(table);
    if (manifest.storage) {
      const retained = document.createElement("p");
      retained.textContent = `Current aggregates: ${mb(total)}. Previous generations retained on the publication branch: ${mb(manifest.storage.previous_database_bytes)}. ${manifest.catalog.length} selectable reports share one renderer. Only current databases are downloaded by the viewer.`;
      body.append(retained);
    }
    const notes = document.createElement("p");
    notes.textContent =
      "Original JSON and complete aggregates use zstd. At most two series databases remain open in the worker. Timings describe this browser\u2019s loading, not the measured workloads.";
    body.append(notes);
  }
  storage();
  worker.onmessage = ({ data }) => {
    if (data.progress) {
      metrics.set(data.series, { ...metrics.get(data.series), ...data });
      storage();
      if (!loading.hidden && data.series === activeSeries)
        loadingState(data.stage, series.get(data.series)?.label);
      return;
    }
    const promise = pending.get(data.id);
    if (!promise) return;
    pending.delete(data.id);
    promise.cleanup?.();
    if (data.error) promise.reject(new Error(data.error));
    else promise.resolve(data.value);
  };
  worker.onerror = (error) => {
    for (const p of pending.values()) {
      p.cleanup?.();
      p.reject(new Error(error.message));
    }
    pending.clear();
  };
  function request(action, entry, document, signal) {
    return new Promise((resolve, reject) => {
      if (signal?.aborted) {
        reject(new DOMException("Selection changed", "AbortError"));
        return;
      }
      const id = ++sequence;
      const abort = () => {
        pending.delete(id);
        worker.postMessage({ action: "cancel", id });
        reject(new DOMException("Selection changed", "AbortError"));
      };
      pending.set(id, {
        resolve,
        reject,
        cleanup: () => signal?.removeEventListener("abort", abort),
      });
      signal?.addEventListener("abort", abort, { once: true });
      worker.postMessage({
        id,
        action,
        series: series.get(entry.database),
        record: entry.record_id,
        document,
      });
    });
  }

  function report(result) {
    current = result;
    const { catalog, model, documents } = result;
    document.getElementById("report-status").textContent =
      `${catalog.platform} \u00b7 ${catalog.label} \u00b7 ${model.rows.length.toLocaleString()} measured comparisons \u00b7 ${catalog.binary?.state || "Not recorded"}`;
    const actions = document.getElementById("report-actions");
    actions.replaceChildren();
    const selector = document.createElement("select");
    selector.setAttribute("aria-label", "Original observation document");
    documents.forEach((doc, index) =>
      selector.add(
        new Option(
          `${doc.paths?.[0] || "Original document " + (index + 1)} \u00b7 ${doc.sha.slice(0, 12)} \u00b7 ${mb(doc.original_bytes)}`,
          doc.sha,
        ),
      ),
    );
    const download = document.createElement("button");
    download.textContent = "Download original JSON";
    download.onclick = async () => {
      download.disabled = true;
      download.textContent = "Reading original observations\u2026";
      try {
        const { data, sha } = await request(
          "document",
          catalog,
          selector.value,
        );
        const url = URL.createObjectURL(
          new Blob([data], { type: "application/json" }),
        );
        const a = document.createElement("a");
        a.href = url;
        a.download = `xff-${catalog.commit.slice(0, 10)}-${sha.slice(0, 12)}.json`;
        a.click();
        setTimeout(() => URL.revokeObjectURL(url), 1000);
      } catch (error) {
        document.getElementById("report-status").textContent = error.message;
      } finally {
        download.disabled = false;
        download.textContent = "Download original JSON";
      }
    };
    const search = document.createElement("input");
    search.type = "search";
    search.placeholder = "Filter measured cases";
    search.setAttribute("aria-label", "Filter measured cases");
    actions.append(selector, download, search);
    const host = document.getElementById("report-table");
    function renderRows() {
      host.replaceChildren();
      const table = document.createElement("table"),
        thead = document.createElement("thead"),
        heading = document.createElement("tr");
      for (const value of [
        "Task",
        "Tree",
        "Workers",
        "Files",
        "Reference",
        "XFF (ms)",
        "Reference (ms)",
        "Reference / XFF",
      ]) {
        const th = document.createElement("th");
        th.textContent = value;
        heading.append(th);
      }
      thead.append(heading);
      table.append(thead);
      const tbody = document.createElement("tbody"),
        filter = search.value.toLowerCase();
      for (const row of model.rows) {
        if (
          filter &&
          !`${row.task} ${row.dataset} ${row.reference} ${row.files} ${row.cpus}`
            .toLowerCase()
            .includes(filter)
        )
          continue;
        const tr = document.createElement("tr");
        for (const value of [
          row.task,
          row.dataset,
          row.cpus,
          row.files.toLocaleString("en-US"),
          row.reference,
          (row.xff_seconds * 1000).toFixed(3),
          (row.reference_seconds * 1000).toFixed(3),
          (1 / row.xff_over_reference).toFixed(3) + "\u00d7",
        ]) {
          const td = document.createElement("td");
          td.textContent = String(value);
          tr.append(td);
        }
        tbody.append(tr);
      }
      table.append(tbody);
      host.append(table);
    }
    search.oninput = renderRows;
    renderRows();
  }

  const provider = {
    async load(entry, signal) {
      const selected = ++selection;
      activeSeries = entry.database;
      loadingState(
        "Preparing measurement view\u2026",
        series.get(entry.database)?.label,
      );
      const id = entry.record_id;
      try {
        let result = loaded.get(id);
        if (!result) {
          try {
            result = await request("report", entry, undefined, signal);
          } catch (error) {
            if (
              signal?.aborted ||
              !error.message.includes("Database download: HTTP 404")
            )
              throw error;
            const response = await fetch(
              new URL("../database-manifest.json", import.meta.url),
              { cache: "reload", signal },
            );
            if (!response.ok) throw error;
            const latest = await response.json();
            Object.assign(manifest, latest);
            series.clear();
            latest.series.forEach((value) => series.set(value.id, value));
            loaded.clear();
            const updated = latest.catalog.find(
              (value) => value.record_id === resolveRecord(id),
            );
            if (!updated)
              throw new Error(
                "This report was superseded. Reload the history to choose a current report.",
              );
            result = await request("report", updated, undefined, signal);
          }
          loaded.set(id, result);
          while (loaded.size > 4) loaded.delete(loaded.keys().next().value);
        }
        if (selected !== selection) return result.figures;
        Object.assign(entry, result.catalog);
        report(result);
        const url = new URL(location.href);
        url.searchParams.set("series", entry.database);
        url.searchParams.set("report", id);
        history.replaceState(null, "", url);
        return result.figures;
      } catch (error) {
        if (selected === selection && error.name !== "AbortError")
          document.getElementById("report-status").textContent =
            "Could not load measurements: " + error.message;
        throw error;
      } finally {
        if (selected === selection)
          requestAnimationFrame(() =>
            requestAnimationFrame(() => {
              if (selected === selection) loadedState();
            }),
          );
      }
    },
  };
  const requested = new URL(location.href).searchParams.get("report");
  const legacy = new URL(location.href).searchParams.get("path");
  const initial = resolveRecord(requested || manifest.legacy_paths?.[legacy]);
  if (legacy && !initial)
    document.querySelector("header p").textContent =
      "This historical URL has no active report. Showing the latest available history; select a dataset and version below.";
  window.XffBenchmarkHistory(
    document.getElementById("benchmark-explorer"),
    manifest.catalog,
    { provider, initialRecord: initial },
  );
}
start().catch((error) => {
  const panel = document.getElementById("database-loading");
  panel.querySelector(".loading-spinner").remove();
  panel.querySelector("[data-loading-stage]").textContent =
    "Unable to load benchmark history";
  panel.querySelector("[data-loading-label]").textContent = error.message;
});
