// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, rmSync, readFileSync } from "node:fs";
import { createServer } from "node:http";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { test } from "node:test";
import { chromium } from "playwright";

const tools = fileURLToPath(new URL("..", import.meta.url));
const bundle = fileURLToPath(new URL("dist/landscape.js", import.meta.url));
const options = {
  headless: true,
  args: ["--enable-webgl", "--ignore-gpu-blocklist"],
};
if (process.env.CHROME_PATH) options.executablePath = process.env.CHROME_PATH;

test("offline chart: picking, highlights, sorting, scale, orbit, resize and collapse", async () => {
  const temporary = mkdtempSync(join(tmpdir(), "xff-landscape-"));
  const browser = await chromium.launch(options);
  try {
    const output = join(temporary, "report.html");
    execFileSync(process.env.PYTHON || "python3", [
      "-c",
      `
import sys
from pathlib import Path
sys.path.insert(0, sys.argv[1])
from benchmark_landscape import render
from benchmark_landscape_test import report
data = report()
for task in data['tasks']:
    task['name'] = 'z-fast' if task['name'] == 'fast' else 'a-slow'
    task['participants']['rg']['samples'][0]['elapsed_seconds'] *= (1 + task['files'] / 10000 + task['cpus'] / 100 + (0.01 if task['dataset'] == 'deep' else 0))
Path(sys.argv[3]).write_text(render(data, Path(sys.argv[2]).read_text()))
`,
      tools,
      bundle,
      output,
    ]);
    const page = await browser.newPage({
      viewport: { width: 1400, height: 1200 },
    });
    const errors = [];
    page.on("pageerror", (error) => errors.push(error.message));
    page.on("console", (message) => {
      if (message.type() === "error") errors.push(message.text());
    });
    const network = [];
    await page.route(/^https?:/, (route) => {
      network.push(route.request().url());
      return route.abort();
    });
    await page.goto(pathToFileURL(output).href);
    await page.waitForFunction(() => typeof chart !== "undefined");
    assert.equal(await page.locator("#landscape canvas").count(), 1);
    assert.equal(await page.locator("#range").inputValue(), "1");
    assert.equal(
      await page.locator("#landscape").getAttribute("data-range"),
      "100",
    );
    await page.selectOption("#metric", "factor");
    assert.equal(
      await page.locator("#landscape").getAttribute("data-range"),
      "1",
    );
    await page.selectOption("#range", "auto");
    await page.selectOption("#normalization", "raw");
    for (const order of ["similarity", "average", "alphabetical"]) {
      for (const metric of ["percent", "factor"]) {
        await page.selectOption("#task-order", order);
        await page.selectOption("#metric", metric);
        if (metric === "factor") {
          assert.deepEqual(
            await page
              .locator(".landscape-legend-ticks span")
              .allTextContents(),
            await page.evaluate(
              (order) => figures[order].factor.layout.scene.yaxis.ticktext,
              order,
            ),
          );
        }
        const point = await page.evaluate(() => {
          const root = document.getElementById("landscape");
          for (let y = 100; y < root.clientHeight - 100; y += 12) {
            for (let x = 100; x < root.clientWidth - 100; x += 12) {
              const point = chart.pick(x, y);
              if (point) return { x, y, ...point, position: undefined };
            }
          }
          return null;
        });
        assert.ok(point, `pick in ${order}/${metric}`);
        const matches = await page.evaluate(
          ({ order, metric, point }) => {
            const surface = figures[order][metric].data.find(
              (surface) => surface.name === point.surface,
            );
            return surface.text[point.row][point.column];
          },
          { order, metric, point },
        );
        assert.equal(point.text, matches);
        await page.locator("#landscape").scrollIntoViewIfNeeded();
        const box = await page.locator("#landscape canvas").boundingBox();
        await page.mouse.move(box.x + point.x, box.y + point.y);
        assert.equal(await page.locator(".hover-axis-value").count(), 1);
        const hoverTable = page.locator(".landscape-hover-table");
        assert.equal(await hoverTable.isVisible(), true);
        const hoverPanel = page.locator(".landscape-hover-panel");
        const hoverBounds = await hoverPanel.boundingBox();
        const scaleBounds = await page
          .locator(".landscape-legend-panel")
          .boundingBox();
        assert.equal(hoverBounds.x, scaleBounds.x);
        assert.ok(hoverBounds.y >= scaleBounds.y + scaleBounds.height + 8);
        assert.equal(
          await hoverPanel.evaluate(
            (element) => getComputedStyle(element).pointerEvents,
          ),
          "none",
        );
        assert.deepEqual(
          (await hoverTable.locator("th").allTextContents()).slice(0, 5),
          ["Task", "Tree", "Allocation", "File count", "Reference tool"],
        );
        assert.equal(
          await hoverTable
            .locator("th")
            .first()
            .evaluate((element) => getComputedStyle(element).textAlign),
          "left",
        );
        assert.equal(
          await hoverTable
            .locator("td")
            .first()
            .evaluate((element) => getComputedStyle(element).textAlign),
          "right",
        );
        const indicator = page.locator(".landscape-legend-marker");
        assert.equal(await indicator.isVisible(), true);
        const expectedPosition = await page.evaluate(
          ({ point, order, metric }) => {
            const figure = figures[order][metric];
            const extent = Math.max(
              ...(
                figure.layout.scene.yaxis.range ||
                figure.data.flatMap((surface) => surface.y.flat())
              ).map(Math.abs),
              0.01,
            );
            return ((point.value + extent) / (2 * extent)) * 100;
          },
          { point, order, metric },
        );
        assert.ok(
          Math.abs(
            (await indicator.evaluate((element) =>
              Number.parseFloat(element.style.left),
            )) - expectedPosition,
          ) < 0.001,
        );
        assert.ok(
          await page
            .locator("#landscape span")
            .evaluateAll(
              (elements) =>
                elements.filter(
                  (element) => element.style.fontWeight === "bold",
                ).length >= 2,
            ),
        );
        await page.mouse.move(0, 0);
        assert.equal(await page.locator(".hover-axis-value").count(), 0);
        assert.equal(await indicator.isVisible(), false);
      }
    }
    const originalFigures = await page.evaluate(() => JSON.stringify(figures));
    for (const metric of ["percent", "factor"]) {
      await page.selectOption("#metric", metric);
      for (const range of ["0.2", "0.5", "1", "2"]) {
        await page.selectOption("#range", range);
        const extent = Number(range) * (metric === "percent" ? 100 : 1);
        assert.equal(
          await page.locator("#landscape").getAttribute("data-range"),
          String(extent),
        );
        assert.deepEqual(
          await page.locator(".landscape-legend-ticks span").allTextContents(),
          [-extent, -extent / 2, 0, extent / 2, extent].map(
            (value) =>
              `${metric === "factor" && value > 0 ? "+" : ""}${value}${metric === "percent" ? "%" : ""}`,
          ),
        );
      }
      await page.selectOption("#range", "0.2");
      const scan = async () =>
        page.evaluate(() => {
          const root = document.getElementById("landscape");
          for (let y = 40; y < root.clientHeight; y += 12)
            for (let x = 40; x < root.clientWidth; x += 12) {
              const point = chart.pick(x, y);
              if (point)
                return {
                  value: point.value,
                  y: point.position.y,
                  text: point.text,
                };
            }
          return null;
        });
      const capped = await scan();
      assert.ok(capped);
      assert.equal(Math.abs(capped.y), 2);
      assert.ok(capped.text.includes("Capped at"));
      await page.selectOption("#overflow", "cut");
      assert.equal(await scan(), null);
      await page.selectOption("#overflow", "cap");
    }
    await page.selectOption("#range", "auto");
    await page.selectOption("#metric", "percent");
    const legendBox = await page
      .locator(".landscape-legend-scale")
      .boundingBox();
    await page.mouse.move(
      legendBox.x + legendBox.width / 2,
      legendBox.y + legendBox.height / 2,
    );
    assert.ok(
      Math.abs(
        Number(await page.locator("#landscape").getAttribute("data-minimum")),
      ) < 0.001,
    );
    const filtered = await page.evaluate(() => {
      const result = [];
      for (let y = 100; y < 800; y += 20)
        for (
          let x = 100;
          x < document.querySelector("#landscape").clientWidth;
          x += 20
        ) {
          const point = chart.pick(x, y);
          if (point) result.push(point.value);
        }
      return result;
    });
    assert.ok(filtered.length > 0);
    assert.ok(filtered.every((value) => value >= 0));
    await page.mouse.move(0, 0);
    assert.equal(
      await page.locator("#landscape").getAttribute("data-minimum"),
      null,
    );
    const captureCanvas = () =>
      page.evaluate(() => {
        chart.resize();
        return document.querySelector("#landscape canvas").toDataURL();
      });
    assert.equal(
      await page.locator(".landscape-preview-opacity").inputValue(),
      "0.5",
    );
    assert.deepEqual(
      await page.locator(".landscape-preview-opacity option").allTextContents(),
      ["50% transparent", "75% transparent", "Hide"],
    );
    assert.equal(
      await page.locator(".landscape-preview-plane").isChecked(),
      true,
    );
    await page.locator(".landscape-preview-plane").uncheck();
    const unfilteredImage = await captureCanvas();
    const previews = [];
    for (const value of ["0", "0.5", "0.25"]) {
      await page.selectOption(".landscape-preview-opacity", value);
      await page.mouse.move(
        legendBox.x + legendBox.width / 2,
        legendBox.y + legendBox.height / 2,
      );
      previews.push(await captureCanvas());
      await page.mouse.move(0, 0);
      assert.equal(await captureCanvas(), unfilteredImage);
    }
    assert.equal(
      new Set(previews).size,
      3,
      "hide and both transparency levels render differently",
    );
    await page.locator(".landscape-preview-plane").check();
    await page.mouse.move(
      legendBox.x + legendBox.width / 2,
      legendBox.y + legendBox.height / 2,
    );
    assert.notEqual(
      await captureCanvas(),
      previews[2],
      "threshold plane is rendered",
    );
    assert.equal(await page.locator(".hover-axis-value").textContent(), "0%");
    for (const metric of ["percent", "factor"]) {
      await page.selectOption("#metric", metric);
      await page.selectOption("#range", "1");
      const scale = await page.locator(".landscape-legend-scale").boundingBox();
      await page.mouse.move(
        scale.x + scale.width * 0.63,
        scale.y + scale.height / 2,
      );
      assert.equal(
        await page.locator(".hover-axis-value").textContent(),
        metric === "percent" ? "26%" : "0.26",
      );
      const beforeOrbit = await page.locator(".hover-axis-value").boundingBox();
      await page.locator("#landscape canvas").focus();
      await page.keyboard.press("ArrowRight");
      assert.equal(await page.locator(".hover-axis-value").isVisible(), true);
      assert.notDeepEqual(
        await page.locator(".hover-axis-value").boundingBox(),
        beforeOrbit,
      );
      await page.keyboard.press("Home");
      await page.mouse.move(0, 0);
      assert.equal(await page.locator(".hover-axis-value").count(), 0);
    }
    await page.selectOption("#metric", "percent");
    await page.selectOption("#range", "auto");
    await page.mouse.move(0, 0);
    assert.equal(await captureCanvas(), unfilteredImage);
    await page.selectOption(".landscape-preview-opacity", "0.5");
    await page.locator(".landscape-preview-plane").uncheck();
    await page.selectOption("#metric", "factor");
    assert.equal(
      await page.locator(".landscape-preview-opacity").inputValue(),
      "0.5",
    );
    assert.equal(
      await page.locator(".landscape-preview-plane").isChecked(),
      false,
    );
    assert.equal(
      await page.evaluate(() => JSON.stringify(figures)),
      originalFigures,
    );
    // Small-extent color scales have fewer stops and must still compile on the GPU.
    await page.evaluate(() => {
      const figure = structuredClone(figures.similarity.percent);
      figure.layout.coloraxis.colorscale = [
        [0, "#ff3030"],
        [0.25, "#24477b"],
        [0.5, "#3269b5"],
        [0.75, "#24477b"],
        [1, "#35ef69"],
      ];
      figures.small = { percent: figure };
      chart.update("small", "percent");
    });
    await page.locator("#landscape canvas").focus();
    for (let index = 0; index < 55; index++)
      await page.keyboard.press("ArrowRight");
    await page.keyboard.press("Home");
    await page.setViewportSize({ width: 700, height: 1000 });
    await page.waitForFunction(
      () =>
        document.querySelector("#landscape canvas").clientWidth ===
        document.getElementById("landscape").clientWidth,
    );
    await page.locator("#landscape-panel > summary").click();
    assert.equal(await page.locator("#landscape canvas").isVisible(), false);
    await page.locator("#landscape-panel > summary").click();
    assert.equal(await page.locator("#landscape canvas").isVisible(), true);
    assert.deepEqual(network, []);
    assert.deepEqual(errors, []);
    await page.evaluate(() => chart.dispose());
    assert.equal(await page.locator("#landscape canvas").count(), 0);
    const fallback = await browser.newPage();
    await fallback.addInitScript(() => {
      const getContext = HTMLCanvasElement.prototype.getContext;
      HTMLCanvasElement.prototype.getContext = function (type, ...args) {
        return type.startsWith("webgl")
          ? null
          : getContext.call(this, type, ...args);
      };
    });
    await fallback.goto(pathToFileURL(output).href);
    assert.match(
      await fallback.locator("#landscape").innerText(),
      /requires WebGL2/,
    );
    assert.ok((await fallback.locator("table").count()) > 0);
    await fallback.selectOption("#metric", "factor");
  } finally {
    await browser.close();
    rmSync(temporary, { recursive: true, force: true });
  }
});

test("history platform/version controls preserve selection and recover from loading errors", async () => {
  const temporary = mkdtempSync(join(tmpdir(), "xff-explorer-"));
  execFileSync(process.env.PYTHON || "python3", [
    "-c",
    `
import sys, json
from pathlib import Path
sys.path.insert(0, sys.argv[1])
from benchmark_landscape import publish
from benchmark_landscape_test import report, preview_report
root = Path(sys.argv[3])
(root / 'index.html').write_text('<h1>History</h1><table><tr><td>Original</td></tr></table>')
(root / 'version-links.json').write_text(json.dumps({'a' * 40: [dict(label='PR #12', href='https://github.com/owner/project/pull/12')], 'b' * 40: [dict(label='Release v1.0.0', href='https://github.com/owner/project/releases/tag/v1.0.0')]}))
for run, platform, commit in [(1, 'linux', 'a'), (2, 'linux', 'b'), (3, 'macos', 'a')]:
    folder = root / 'runs' / str(run) / '1' / platform
    folder.mkdir(parents=True)
    source = dict(id=run, run_attempt=1, head_sha=commit * 40, head_branch='main', created_at=f'2026-09-{run:02d}')
    data = dict(tool_comparisons=report(), platform=platform, source=source)
    if platform == 'macos':
        data.update(kind='backfill', purpose='ci-replacement', head=commit * 40,
                    revision={'date': source['created_at']}, completed_at='2026-10-02T12:00:00Z')
    (folder / 'report.json').write_text(json.dumps(data))
    (folder / 'index.html').write_text('<h1>Report</h1>')
folder = root / 'local/macos-test/batch' / ('c' * 40)
folder.mkdir(parents=True)
(folder / 'report.json').write_text(json.dumps(dict(tool_comparisons=report(cpus=(1, 3, 10)),
    platform='macos', kind='backfill', purpose='local-addition', series='macos-test', head='c' * 40,
    revision=dict(date='2026-09-01T00:00:00Z'), completed_at='2026-10-02T00:00:00Z')))
(folder / 'index.html').write_text('<h1>Local report</h1>')
folder = root / 'previews/952/4/1/linux'
folder.mkdir(parents=True)
(folder / 'report.json').write_text(json.dumps(dict(tool_comparisons=preview_report(), platform='linux',
    kind='pr-preview', pull_number=952, head='d' * 40, branch_head='e' * 40, base='b' * 40,
    source=dict(id=4, run_attempt=1, head_branch='feature', event='pull_request',
                pull_requests=[{'number': 952}], created_at='2026-10-03T00:00:00Z'))))
publish(root, Path(sys.argv[2]).read_text(), [folder / 'report.json'])
`,
    tools,
    bundle,
    temporary,
  ]);
  const server = createServer((request, response) => {
    try {
      const file = new URL(request.url, "http://localhost").pathname;
      response.setHeader(
        "Content-Type",
        file.endsWith(".js")
          ? "text/javascript"
          : file.endsWith(".json")
            ? "application/json"
            : "text/html",
      );
      response.end(
        readFileSync(join(temporary, file === "/" ? "index.html" : file)),
      );
    } catch {
      response.writeHead(404);
      response.end();
    }
  });
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const browser = await chromium.launch(options);
  try {
    const page = await browser.newPage();
    const errors = [];
    page.on("pageerror", (error) => errors.push(error.message));
    let releaseInitial;
    const initialResponse = new Promise((resolve) => {
      releaseInitial = resolve;
    });
    await page.route("**/landscape.json", async (route) => {
      await initialResponse;
      await route.continue();
    });
    await page.goto(`http://127.0.0.1:${server.address().port}/`);
    assert.equal(
      await page
        .locator("[data-chart]")
        .evaluate((element) => element.clientHeight),
      850,
    );
    assert.equal(await page.locator("[data-chart] canvas").count(), 0);
    releaseInitial();
    await page.waitForFunction(() =>
      document.querySelector("[data-chart] canvas"),
    );
    await page.unroute("**/landscape.json");
    const waitCommit = (commit) =>
      page.waitForFunction(
        (commit) =>
          document.querySelector("[data-chart]").dataset.commit === commit &&
          !document.querySelector("[data-chart]").hasAttribute("aria-busy"),
        commit.repeat(40),
      );
    await page.selectOption('[data-control="platform"]', "linux");
    await waitCommit("b");
    assert.equal(
      await page.locator('[data-control="range"]').inputValue(),
      "1",
    );
    assert.equal(
      await page.locator("[data-chart]").getAttribute("data-range"),
      "100",
    );
    assert.equal(
      await page.locator("[data-version-links] a").getAttribute("href"),
      "https://github.com/owner/project/releases/tag/v1.0.0",
    );
    const slider = page.locator('[data-control="version"]');
    const host = page.locator("[data-chart]");
    const versionPanel = page.locator("[data-version-panel]");
    const legendPanel = page.locator(".landscape-legend-panel");
    const panelBox = await versionPanel.boundingBox();
    const legendBox = await legendPanel.boundingBox();
    const chartBox = await host.boundingBox();
    assert.equal(panelBox.x, chartBox.x + 12);
    assert.equal(panelBox.y, chartBox.y + 12);
    assert.equal(legendBox.x, panelBox.x);
    assert.ok(legendBox.y >= panelBox.y + panelBox.height + 8);
    assert.equal(
      await versionPanel.locator('[data-control="version"]').count(),
      1,
    );
    assert.equal(
      await page.locator("[data-version-count]").textContent(),
      "2 of 2",
    );
    assert.equal(await page.locator("[data-platform]").textContent(), "linux");
    assert.equal(await page.locator("[data-measured-row]").isVisible(), true);
    assert.equal(
      await page.locator("[data-measured]").textContent(),
      "Not recorded",
    );
    assert.equal(
      await page
        .locator(".landscape-version-table tr:last-child th")
        .textContent(),
      "Details",
    );
    assert.equal(
      (await page.locator("[data-platform-details]").boundingBox()).height,
      48,
    );
    for (const panel of [versionPanel, legendPanel])
      assert.equal(
        await panel.evaluate(
          (element) => getComputedStyle(element).borderTopWidth,
        ),
        "1px",
      );
    for (const tick of await page
      .locator(".landscape-legend-ticks span")
      .all()) {
      const bounds = await tick.boundingBox();
      assert.ok(bounds.x >= legendBox.x);
      assert.ok(bounds.x + bounds.width <= legendBox.x + legendBox.width);
    }
    const originalBox = await host.boundingBox();
    const originalCanvas = await host.locator("canvas").elementHandle();
    await page.selectOption(".landscape-preview-opacity", "0.5");
    await page.locator(".landscape-preview-plane").uncheck();
    let releaseResponse, requestStarted;
    const heldResponse = new Promise((resolve) => {
      releaseResponse = resolve;
    });
    const loading = new Promise((resolve) => {
      requestStarted = resolve;
    });
    await page.route("**/runs/1/1/linux/landscape.json", async (route) => {
      requestStarted();
      await heldResponse;
      await route.continue();
    });
    await slider.fill("0");
    await loading;
    assert.equal(await host.isVisible(), true);
    assert.equal(await host.getAttribute("data-commit"), "b".repeat(40));
    assert.deepEqual(await host.boundingBox(), originalBox);
    assert.deepEqual(await versionPanel.boundingBox(), panelBox);
    assert.deepEqual(await legendPanel.boundingBox(), legendBox);
    assert.ok(
      (await page.locator("[data-report]").textContent()).includes(
        "b".repeat(10),
      ),
    );
    releaseResponse();
    await waitCommit("a");
    assert.equal(
      await page.locator(".landscape-preview-opacity").inputValue(),
      "0.5",
    );
    assert.equal(
      await page.locator(".landscape-preview-plane").isChecked(),
      false,
    );
    assert.equal(
      await page.locator("[data-version-links] a").getAttribute("href"),
      "https://github.com/owner/project/pull/12",
    );
    await page.unroute("**/runs/1/1/linux/landscape.json");
    assert.deepEqual(await host.boundingBox(), originalBox);
    assert.deepEqual(await versionPanel.boundingBox(), panelBox);
    assert.deepEqual(await legendPanel.boundingBox(), legendBox);
    assert.equal(
      await originalCanvas.evaluate(
        (canvas) => canvas === document.querySelector("[data-chart] canvas"),
      ),
      true,
    );
    await page.selectOption('[data-control="platform"]', "macos");
    await page.waitForFunction(
      () =>
        document.querySelector("[data-version-count]").textContent === "1 of 1",
    );
    assert.equal(await slider.isDisabled(), true);
    assert.equal(
      await page.locator("[data-measured]").textContent(),
      "2026-10-02",
    );
    await page.selectOption('[data-control="platform"]', "linux");
    await page.waitForFunction(
      () =>
        document.querySelector("[data-version-count]").textContent === "1 of 2",
    );
    assert.equal(await slider.inputValue(), "0");
    await page.selectOption('[data-control="metric"]', "factor");
    await page.selectOption('[data-control="order"]', "alphabetical");
    const beforeFailurePanel = await versionPanel.boundingBox();
    const beforeFailureLegend = await legendPanel.boundingBox();
    await page.route("**/runs/2/1/linux/landscape.json", (route) =>
      route.fulfill({ status: 503, body: "unavailable" }),
    );
    await slider.fill("1");
    await page.waitForFunction(() =>
      document
        .querySelector('[role="status"]')
        .textContent.includes("Unable to load"),
    );
    assert.equal(await page.locator("[data-chart]").isVisible(), true);
    assert.equal(await slider.isVisible(), true);
    assert.equal(await host.getAttribute("data-commit"), "a".repeat(40));
    assert.equal(
      await page.locator("[data-version-count]").textContent(),
      "1 of 2",
    );
    assert.equal(
      await page.locator("[data-report]").textContent(),
      "a".repeat(10),
    );
    assert.deepEqual(await versionPanel.boundingBox(), beforeFailurePanel);
    assert.deepEqual(await legendPanel.boundingBox(), beforeFailureLegend);
    assert.equal(
      await host.evaluate((element) => element.clientHeight),
      originalBox.height,
    );
    await slider.fill("0");
    await waitCommit("a");
    assert.equal(
      await page.locator("[data-chart]").getAttribute("data-metric"),
      "factor",
    );
    await page.selectOption(
      '[data-control="platform"]',
      "Local / macos-test / ",
    );
    await waitCommit("c");
    assert.equal(await page.locator("[data-links-row]").isVisible(), true);
    assert.equal(
      await page.locator("[data-version-links]").textContent(),
      "Not available",
    );
    for (const value of ["1/10", "3/10", "1/3"]) {
      await page.selectOption('[data-control="allocations"]', value);
      assert.equal(
        await page.locator('[data-control="allocations"]').inputValue(),
        value,
      );
      assert.equal(
        await page.locator("[data-chart]").getAttribute("data-metric"),
        "factor",
      );
    }
    assert.equal(
      await page.locator("[data-measured]").textContent(),
      "2026-10-02",
    );
    assert.equal(await page.locator("[data-measured-row]").isVisible(), true);
    assert.equal(await page.locator('[role="status"]').isVisible(), false);
    const localPanelBox = await versionPanel.boundingBox();
    assert.equal(localPanelBox.y, chartBox.y + 12);
    assert.ok(
      (await legendPanel.boundingBox()).y >=
        localPanelBox.y + localPanelBox.height + 8,
    );
    assert.equal(
      await page
        .locator("[data-report]")
        .textContent()
        .then((text) => text.includes("run undefined")),
      false,
    );
    await page.selectOption('[data-control="source"]', "pr-952");
    await waitCommit("d");
    assert.equal(
      await page.locator('[data-control="platform"]').inputValue(),
      "linux",
    );
    assert.equal(await slider.isDisabled(), true);
    assert.match(
      await page.locator("[data-version-links]").textContent(),
      /PR #952 preview/,
    );
    assert.match(
      await page.locator("[data-overview]").textContent(),
      /WARNING/,
    );
    assert.match(
      await page.locator("[data-overview]").textContent(),
      /Largest regressions/,
    );
    assert.equal(await host.evaluate((element) => element.clientHeight), 850);
    assert.equal(
      await originalCanvas.evaluate(
        (canvas) => canvas === document.querySelector("[data-chart] canvas"),
      ),
      true,
    );
    await page.selectOption('[data-control="source"]', "merged");
    await page.unroute("**/runs/2/1/linux/landscape.json");
    await page.selectOption('[data-control="platform"]', "macos");
    await waitCommit("a");
    assert.match(
      await page.locator("[data-overview]").textContent(),
      /regression status is unknown/,
    );
    assert.deepEqual(errors, []);
  } finally {
    await browser.close();
    await new Promise((resolve) => server.close(resolve));
    rmSync(temporary, { recursive: true, force: true });
  }
});
