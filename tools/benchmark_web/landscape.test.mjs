// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, rmSync } from "node:fs";
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
      }
    }
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
