// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
// Build every chart view from the same measured comparison rows.
const escape = (s) =>
  String(s)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;");
const table = (rows) =>
  '<table class="landscape-hover-table"><tbody>' +
  rows
    .map(
      ([k, v]) =>
        `<tr><th scope="row">${escape(k)}</th><td>${escape(v)}</td></tr>`,
    )
    .join("") +
  "</tbody></table>";
const key = (parts) => JSON.stringify(parts);
const signed = (n, digits = 2) => (n >= 0 ? "+" : "") + n.toFixed(digits);

function pairs(model, rows, mode) {
  const all = new Map(
    rows.map((r) => [key([r.task, r.reference]), [r.task, r.reference]]),
  );
  for (const pair of model.order.pairs || []) all.set(key(pair), pair);
  if (mode === "similarity" && model.order.pairs?.length) {
    const known = new Set(model.order.pairs.map(key));
    return [
      ...model.order.pairs,
      ...[...all]
        .filter(([k]) => !known.has(k))
        .map(([, v]) => v)
        .sort(),
    ];
  }
  const result = [...all.values()].sort(
    (a, b) => a[0].localeCompare(b[0]) || a[1].localeCompare(b[1]),
  );
  if (mode === "average") {
    const means = new Map();
    for (const row of rows) {
      const k = key([row.task, row.reference]),
        values = means.get(k) || [0, 0];
      values[0] += 100 * (1 - row.xff_over_reference);
      values[1]++;
      means.set(k, values);
    }
    result.sort((a, b) => {
      const x = means.get(key(a)) || [0, 1],
        y = means.get(key(b)) || [0, 1];
      return y[0] / y[1] - x[0] / x[1] || key(a).localeCompare(key(b));
    });
  }
  return result;
}

function figure(model, groups, mode, metric) {
  const rows = model.rows.filter((r) => groups.includes(r.cpus));
  const order = pairs(model, rows, mode),
    counts = [...new Set(model.counts)].sort((a, b) => a - b);
  const lookup = new Map(
    rows.map((r) => [
      key([r.dataset, r.cpus, r.files, r.task, r.reference]),
      r,
    ]),
  );
  const norm = new Map(
    Object.entries(model.normalization).map(([k, v]) => [
      key(JSON.parse(k)),
      v,
    ]),
  );
  const extent = Math.max(
    20,
    ...rows.map((r) => Math.abs(100 * (1 - r.xff_over_reference))),
  );
  const stops = new Map([
    [-extent, "#ff3030"],
    [-20, "#84202a"],
    [-10, "#24477b"],
    [0, "#3269b5"],
    [10, "#24477b"],
    [20, "#12572d"],
    [extent, "#35ef69"],
  ]);
  const colorscale = [...stops]
    .sort((a, b) => a[0] - b[0])
    .map(([v, c]) => [(v + extent) / (2 * extent), c]);
  const traces = [],
    ticks = [],
    labels = [];
  groups.forEach((cpu, sideIndex) => {
    const side = sideIndex ? 1 : -1,
      ordered = sideIndex ? counts : [...counts].reverse();
    const xs = ordered.map((n) => side * (0.3 + Math.log10(n / counts[0])));
    ticks.push(...xs);
    labels.push(...ordered.map((n) => n.toLocaleString("en-US")));
    for (const [dataset, direction] of [
      ["broad", -1],
      ["deep", 1],
    ]) {
      const x = [],
        y = [],
        z = [],
        text = [],
        normalized_text = [],
        surfacecolor = [];
      order.forEach(([task, reference], index) => {
        const heights = [],
          colors = [],
          hover = [],
          normalized = [];
        for (const files of ordered) {
          const row = lookup.get(key([dataset, cpu, files, task, reference]));
          if (!row) {
            heights.push(null);
            colors.push(null);
            hover.push("Missing measurement");
            normalized.push("Missing measurement");
            continue;
          }
          const percent = 100 * (1 - row.xff_over_reference);
          heights.push(
            metric === "factor" ? -Math.log10(1 - percent / 100) : percent,
          );
          colors.push(percent);
          const details = [
            ["Task", task],
            ["Tree", dataset[0].toUpperCase() + dataset.slice(1)],
            ["Allocation", row.workload?.allocation || model.allocations[cpu]],
            ["File count", files.toLocaleString("en-US")],
            ["Reference tool", reference],
            ["Relative performance", signed(percent) + "%"],
            [
              "Performance factor",
              (1 / row.xff_over_reference).toFixed(2) + "x (reference/xff)",
            ],
            ["xff/reference", row.xff_over_reference.toFixed(2)],
            ["XFF time", (row.xff_seconds * 1000).toFixed(3) + " ms"],
            [
              "Reference time",
              (row.reference_seconds * 1000).toFixed(3) + " ms",
            ],
          ];
          for (const [label, k] of [
            ["Workload recipe", "recipe"],
            ["Production target", "target"],
            ["XFF SHA-256", "sha256"],
            ["Sampling", "sampling"],
            ["Measured", "measured"],
            ["Platform", "platform"],
          ])
            if (row.workload?.[k]) details.push([label, row.workload[k]]);
          hover.push(table(details));
          const value = norm.get(key([dataset, task, cpu, files, reference]));
          const extra =
            value?.status === "available"
              ? [
                  [
                    "Reference-normalized XFF",
                    (value.xff_seconds * 1000).toFixed(3) + " ms",
                  ],
                  [
                    "Window reference mean",
                    (value.reference_mean_seconds * 1000).toFixed(3) + " ms",
                  ],
                  ["Correction factor", value.factor.toFixed(4)],
                  ["Reference window", value.window.length + " measurements"],
                  ["Window start", value.window[0].date.slice(0, 10)],
                  ["Window end", value.window.at(-1).date.slice(0, 10)],
                ]
              : [
                  [
                    "Reference window",
                    "Unavailable: " +
                      (value?.reason ||
                        "No compatible reference history is available."),
                  ],
                ];
          normalized.push(table([...details, ...extra]));
        }
        x.push(xs);
        y.push(heights);
        z.push(xs.map(() => direction * (index + 1)));
        text.push(hover);
        normalized_text.push(normalized);
        surfacecolor.push(colors);
      });
      traces.push({
        type: "surface",
        x,
        y,
        z,
        text,
        normalized_text,
        surfacecolor,
        name:
          dataset[0].toUpperCase() +
          dataset.slice(1) +
          " / " +
          model.allocations[cpu],
      });
    }
  });
  const taskTicks = order
    .map((_, i) => -order.length + i)
    .concat(order.map((_, i) => i + 1));
  const taskLabels = [...[...order].reverse(), ...order].map(
    ([a, b]) => a + " / " + b + "   ",
  );
  const yaxis = {
    title: { text: "Relative performance (%)" },
    zeroline: true,
    ticksuffix: "%",
  };
  const colorbar = {
    title: { text: "Relative performance %" },
    ticksuffix: "%",
  };
  if (metric === "factor") {
    const maximum = Math.max(
      0.01,
      ...rows.map((r) => Math.abs(Math.log10(r.xff_over_reference))),
    );
    const target = maximum / 4,
      decade = Math.floor(Math.log10(target));
    const step = [1, 2, 5, 10]
        .map((n) => n * 10 ** decade)
        .find((n) => n >= target),
      n = Math.ceil(maximum / step);
    const powers = Array.from({ length: 2 * n + 1 }, (_, i) => (i - n) * step);
    Object.assign(yaxis, {
      title: { text: "Relative performance (log10 ratio)" },
      tickvals: powers,
      ticktext: powers.map((v) =>
        v ? signed(v, Math.max(0, -decade)).replace(/\.0+$/, "") : "0",
      ),
      range: [-n * step, n * step],
    });
    const ct = powers
      .map((p, i) => [100 * (1 - 10 ** -p), yaxis.ticktext[i]])
      .filter(([p]) => p >= -extent && p <= extent);
    Object.assign(colorbar, {
      title: { text: "log10 ratio" },
      tickvals: ct.map((v) => v[0]),
      ticktext: ct.map((v) => v[1]),
    });
  }
  return {
    data: traces,
    layout: {
      title: { text: model.title },
      coloraxis: { cmin: -extent, cmax: extent, colorscale, colorbar },
      scene: {
        xaxis: {
          title: {
            text:
              model.allocations[groups[0]] +
              " \u2190 File count \u2192 " +
              model.allocations[groups[1]],
          },
          tickvals: ticks,
          ticktext: labels,
        },
        yaxis,
        zaxis: {
          title: { text: "Broad FS \u2190 Task \u2192 Deep FS" },
          tickvals: taskTicks,
          ticktext: taskLabels,
        },
      },
    },
  };
}

export function figures(model) {
  const cpus = [...new Set(model.cpus)].sort((a, b) => a - b);
  const modes = (groups) =>
    Object.fromEntries(
      ["similarity", "average", "alphabetical"].map((mode) => [
        mode,
        Object.fromEntries(
          ["percent", "factor"].map((metric) => [
            metric,
            figure(model, groups, mode, metric),
          ]),
        ),
      ]),
    );
  const result = {
    ...modes(cpus.slice(0, 2)),
    overview: model.overview,
    task_order: model.order,
  };
  if (cpus.length > 2)
    result.allocation_pairs = cpus.flatMap((a, i) =>
      cpus.slice(i + 1).map((b) => ({
        value: a + "/" + b,
        label: model.allocations[a] + " / " + model.allocations[b],
        figures: modes([a, b]),
      })),
    );
  return result;
}
