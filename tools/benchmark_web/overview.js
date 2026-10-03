// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

export function renderOverview(host, summary) {
  if (!host) return;
  const heading = document.createElement("h2");
  heading.textContent = "Performance overview";
  host.replaceChildren(heading);
  const text = (value) => {
    const paragraph = document.createElement("p");
    paragraph.textContent = value;
    host.append(paragraph);
    return paragraph;
  };
  if (!summary || summary.status === "unavailable") {
    text(
      "No compatible merged baseline comparisons are available; regression status is unknown.",
    );
    return;
  }
  const signed = (value) => `${value >= 0 ? "+" : ""}${value.toFixed(1)}%`;
  const status = text(
    summary.warning_count
      ? `WARNING: ${summary.warning_count} cases exceed the ${summary.threshold_percent}% slowdown threshold. Advisory only.`
      : `No compatible case exceeds the ${summary.threshold_percent}% slowdown threshold. Advisory only.`,
  );
  if (summary.warning_count) status.style.color = "#a12622";
  status.style.fontWeight = "bold";
  const counts = summary.all;
  text(
    `Typical change: ${signed(counts.change_percent)}; ${counts.faster} faster, ${counts.within_threshold} within ${summary.threshold_percent}%, ${counts.slower} slower. ` +
      `${counts.comparisons}/${summary.total_comparisons} compatible comparisons; ${summary.unmatched_comparisons} unavailable.`,
  );
  text(
    "Each XFF/main timing ratio is divided by its reference-tool timing ratio. Typical change is their equal-weight geometric mean. Positive is slower; negative is faster. Correlated cases are not a significance test.",
  );
  if (summary.baseline?.head)
    text(
      `Baseline: ${summary.baseline.head.slice(0, 10)}; run ${summary.baseline.run || "unrecorded"}, attempt ${summary.baseline.attempt || "unrecorded"}; ${summary.baseline.policy || "sampling policy unrecorded"}.`,
    );
  if (!summary.largest_regressions.length) return;
  const table = document.createElement("table");
  const caption = document.createElement("caption");
  caption.textContent = "Largest regressions";
  caption.style.textAlign = "left";
  table.append(caption);
  const row = (values, tag) => {
    const tr = document.createElement("tr");
    values.forEach((value, index) => {
      const cell = document.createElement(tag);
      cell.textContent = value;
      cell.style.textAlign = [2, 3, 5].includes(index) ? "right" : "left";
      tr.append(cell);
    });
    table.append(tr);
  };
  row(["Task", "Tree", "Workers", "Files", "Reference", "Change"], "th");
  for (const result of summary.largest_regressions)
    row(
      [
        result.task,
        result.dataset,
        result.cpus,
        result.files.toLocaleString(),
        result.reference,
        signed(result.normalized_change_percent),
      ],
      "td",
    );
  host.append(table);
}
