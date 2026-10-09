// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
import "./history.js";
import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { chartSidebar, panelStyle, reserveChart, viewOptions } from "./view.js";

window.XffLandscapeView = viewOptions;

window.XffLandscape = function createLandscape(root, figures) {
  reserveChart(root);
  let renderer;
  try {
    renderer = new THREE.WebGLRenderer({ antialias: true });
  } catch {
    const message = document.createElement("p");
    message.textContent =
      "The 3D chart requires WebGL2. All measurements remain available in the tables below.";
    message.style.cssText =
      "position:absolute;left:12px;bottom:12px;max-width:90%";
    root.append(message);
    return { update() {}, resize() {} };
  }
  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
  root.append(renderer.domElement);
  const scene = new THREE.Scene();
  scene.background = new THREE.Color("#ffffff");
  const camera = new THREE.PerspectiveCamera(40, 1, 0.1, 200);
  camera.position.set(11, 10, 13);
  const controls = new OrbitControls(camera, renderer.domElement);
  // Retain unrestricted orbit, pan and zoom; titles belong to the base plane.
  const canvas = renderer.domElement;
  canvas.tabIndex = 0;
  canvas.setAttribute(
    "aria-label",
    "Benchmark landscape. Arrow keys rotate; plus and minus zoom; Home resets. Full measurements follow in tables.",
  );
  const tooltip = document.createElement("div");
  tooltip.className = "landscape-hover-panel";
  tooltip.hidden = true;
  tooltip.style.cssText = panelStyle + ";background:#fff;pointer-events:none";
  const hoverStyle = document.createElement("style");
  hoverStyle.textContent = `.landscape-hover-table{border-collapse:collapse;width:100%;margin:0;border:0;font:inherit}
    .landscape-hover-table th,.landscape-hover-table td{border:0;padding:2px 4px;vertical-align:top;background:transparent}
    .landscape-hover-table th{text-align:left;font-weight:500;white-space:nowrap}
    .landscape-hover-table td{text-align:right;overflow-wrap:anywhere}`;
  root.append(hoverStyle);
  const legend = document.createElement("details");
  legend.className = "landscape-legend-panel";
  legend.open = true;
  legend.style.cssText = panelStyle;
  const legendSummary = document.createElement("summary");
  const legendTitle = document.createElement("strong");
  legendTitle.textContent = "Performance: %";
  legendSummary.append(legendTitle);
  const legendBody = document.createElement("div");
  legend.append(legendSummary, legendBody);
  const performanceControls = root
    .closest("#benchmark-explorer")
    ?.querySelector("[data-performance-controls]");
  chartSidebar(root).append(legend);
  chartSidebar(root, "right").append(tooltip);
  const legendMarker = document.createElement("div");
  legendMarker.className = "landscape-legend-marker";
  legendMarker.hidden = true;
  legendMarker.style.cssText =
    "position:absolute;top:-4px;width:3px;height:20px;background:#111;box-shadow:0 0 0 1px #ffe600;transform:translateX(-50%);pointer-events:none";
  let legendExtent = 1;
  const minimumPerformance = { value: -1e30 };
  const contextOpacity = { value: 0.5 };
  let showThresholdPlane = true;
  let thresholdFraction = 0;
  const previewShader = `uniform float minimumPerformance; uniform float contextOpacity;
    uniform bool contextPass; varying float measured;
    float previewAlpha(){bool below=measured<minimumPerformance;
    if(below!=contextPass || (contextPass && contextOpacity==0.0))discard;
    return contextPass ? contextOpacity : 1.0;}`;
  let contents = new THREE.Group();
  scene.add(contents);
  let labels = [],
    surfaces = [],
    width = 1,
    height = 1;
  let guides = new THREE.Group();
  scene.add(guides);
  function label(text, position, align, outward) {
    const element = document.createElement("span");
    element.textContent = text.trim();
    element.style.cssText =
      "position:absolute;white-space:nowrap;pointer-events:none;font:11px monospace;color:#172333";
    root.append(element);
    labels.push({ element, position, align, outward });
  }
  function planeTitle(text, center, right, size) {
    const image = document.createElement("canvas");
    const context = image.getContext("2d");
    context.font = "600 64px system-ui";
    image.width = Math.ceil(context.measureText(text).width) + 48;
    image.height = 112;
    context.font = "600 64px system-ui";
    context.fillStyle = "#172333";
    context.textAlign = "center";
    context.textBaseline = "middle";
    context.fillText(text, image.width / 2, image.height / 2);
    const texture = new THREE.CanvasTexture(image);
    texture.colorSpace = THREE.SRGBColorSpace;
    texture.anisotropy = renderer.capabilities.getMaxAnisotropy();
    const material = new THREE.MeshBasicMaterial({
      map: texture,
      transparent: true,
      side: THREE.DoubleSide,
      depthWrite: false,
    });
    const mesh = new THREE.Mesh(
      new THREE.PlaneGeometry(size, (size * image.height) / image.width),
      material,
    );
    const normal = new THREE.Vector3(0, 1, 0);
    const up = new THREE.Vector3().crossVectors(normal, right);
    mesh.quaternion.setFromRotationMatrix(
      new THREE.Matrix4().makeBasis(right, up, normal),
    );
    mesh.position.copy(center);
    mesh.name = "axis-title";
    contents.add(mesh);
  }
  function draw() {
    renderer.render(scene, camera);
    for (const { element, position, align, outward } of labels) {
      const screen = position.clone().project(camera);
      let alignment = align;
      if (outward) {
        const outside = position.clone().add(outward).project(camera);
        alignment = outside.x < screen.x ? "right" : "left";
      }
      element.style.left = `${((screen.x + 1) * width) / 2}px`;
      element.style.top = `${((1 - screen.y) * height) / 2}px`;
      element.style.transform = `translate(${alignment === "right" ? "-100%" : "0%"},-50%)`;
      element.style.display = Math.abs(screen.z) > 1 ? "none" : "block";
    }
  }
  function resize() {
    if (!root.clientWidth || !root.clientHeight) return;
    width = root.clientWidth;
    height = root.clientHeight;
    renderer.setSize(width, height);
    camera.aspect = width / height;
    camera.updateProjectionMatrix();
    draw();
  }
  const observer = new ResizeObserver(resize);
  observer.observe(root);
  controls.addEventListener("change", () => {
    clearHover();
    if (showThresholdPlane && root.dataset.minimum !== undefined)
      showPerformanceValue(
        minimumPerformance.value,
        (minimumPerformance.value / legendExtent) * 2,
      );
    draw();
  });
  function line(points) {
    contents.add(
      new THREE.Line(
        new THREE.BufferGeometry().setFromPoints(points),
        new THREE.LineBasicMaterial({ color: "#c7ccd4" }),
      ),
    );
  }
  function clearHover() {
    tooltip.hidden = true;
    legendMarker.hidden = true;
    root
      .querySelectorAll(".hover-axis-value")
      .forEach((element) => element.remove());
    labels = labels.filter(({ temporary }) => !temporary);
    guides.children.forEach((line) => {
      line.geometry.dispose();
      line.material.dispose();
    });
    guides.clear();
    labels.forEach(({ element }) => {
      element.style.background = "";
      element.style.fontWeight = "";
    });
  }
  function showHover(point) {
    clearHover();
    legendMarker.hidden = false;
    legendMarker.style.left = `${Math.max(0, Math.min(1, (point.value + legendExtent) / (2 * legendExtent))) * 100}%`;
    legendMarker.setAttribute(
      "aria-label",
      `Selected value: ${point.value}${root.dataset.metric === "percent" ? "%" : " log10"}`,
    );
    const { x, y, z } = point.position;
    const feet = [
      new THREE.Vector3(x, -2, 4),
      new THREE.Vector3(4, -2, z),
      new THREE.Vector3(-4, y, -4),
    ];
    // Project onto the base and then along the grid to each labeled axis.
    const base = new THREE.Vector3(x, -2, z);
    for (const path of [
      [point.position, base, feet[0]],
      [base, feet[1]],
      [point.position, new THREE.Vector3(-4, y, z), feet[2]],
    ]) {
      const geometry = new THREE.BufferGeometry().setFromPoints(path);
      const line = new THREE.Line(
        geometry,
        new THREE.LineDashedMaterial({
          color: "#172333",
          dashSize: 0.12,
          gapSize: 0.07,
          depthTest: false,
        }),
      );
      line.computeLineDistances();
      line.renderOrder = 10;
      guides.add(line);
    }
    const match = (a, b) => Math.abs(a - b) < 0.00001;
    for (const { element, position, outward } of labels) {
      const selected = outward?.z
        ? match(position.x, x)
        : outward?.x
          ? match(position.z, z)
          : match(position.y, y);
      if (selected) {
        element.style.background = "#ffe49b";
        element.style.fontWeight = "bold";
      }
    }
    showPerformanceValue(point.value, y);
    draw();
  }
  function showPerformanceValue(value, y) {
    // Measurements and preview planes need not land on an existing axis tick.
    for (const { element, position, outward } of labels) {
      if (!outward && Math.abs(position.y - y) < 0.00001) {
        element.style.background = "#ffe49b";
        element.style.fontWeight = "bold";
      }
    }
    const tag = document.createElement("span");
    const metric = root.dataset.metric;
    tag.textContent = `${Number(value.toFixed(2))}${metric === "percent" ? "%" : ""}`;
    tag.style.cssText =
      "position:absolute;background:#ffe49b;font:12px monospace;padding:2px;pointer-events:none";
    tag.className = "hover-axis-value";
    root.append(tag);
    labels.push({
      element: tag,
      position: new THREE.Vector3(-4, y, -4),
      align: "right",
      temporary: true,
    });
  }
  function clear() {
    minimumPerformance.value = -1e30;
    delete root.dataset.minimum;
    clearHover();
    scene.remove(contents);
    const geometries = new Set();
    contents.traverse((object) => {
      if (object.geometry) geometries.add(object.geometry);
      object.material?.map?.dispose();
      object.material?.dispose();
    });
    geometries.forEach((geometry) => geometry.dispose());
    contents = new THREE.Group();
    scene.add(contents);
    labels.forEach(({ element }) => element.remove());
    labels = [];
    surfaces = [];
    tooltip.hidden = true;
  }
  function update(
    order = "similarity",
    metric = "percent",
    nextFigures = figures,
    view = {},
  ) {
    figures = nextFigures;
    clear();
    root.dataset.metric = metric;
    root.dataset.range = view.limit ?? "auto";
    root.dataset.overflow = view.overflow || "cap";
    const figure = figures[order][metric],
      axes = figure.layout.scene,
      colors = figure.layout.coloraxis;
    const flat = figure.data
      .flatMap((surface) => surface.y.flat())
      .filter((value) => value !== null);
    const ymax =
      view.limit ?? Math.max(...(axes.yaxis.range || flat).map(Math.abs), 0.01);
    legendExtent = ymax;
    const cut = view.overflow === "cut";
    const xmax = Math.max(...axes.xaxis.tickvals.map(Math.abs), 0.01);
    const zmax = Math.max(...axes.zaxis.tickvals.map(Math.abs), 0.01);
    const position = (x, y, z) =>
      new THREE.Vector3((x / xmax) * 4, (y / ymax) * 2, (z / zmax) * 4);
    const preview = new THREE.Group();
    preview.visible = false;
    contents.add(preview);
    const context = new THREE.Group();
    preview.add(context);
    function addContext(geometry, material, ObjectType) {
      const translucent = material.clone();
      translucent.uniforms.minimumPerformance = minimumPerformance;
      translucent.uniforms.contextOpacity = contextOpacity;
      translucent.uniforms.contextPass.value = true;
      translucent.transparent = true;
      translucent.depthWrite = false;
      translucent.forceSinglePass = true;
      context.add(new ObjectType(geometry, translucent));
    }
    const plane = new THREE.Group();
    const planeGeometry = new THREE.PlaneGeometry(8, 8);
    plane.rotation.x = -Math.PI / 2;
    plane.add(
      new THREE.Mesh(
        planeGeometry,
        new THREE.MeshBasicMaterial({
          color: "#e2b946",
          opacity: 0.08,
          transparent: true,
          side: THREE.DoubleSide,
          depthWrite: false,
        }),
      ),
    );
    plane.add(
      new THREE.LineSegments(
        new THREE.EdgesGeometry(planeGeometry),
        new THREE.LineBasicMaterial({
          color: "#7b6525",
          opacity: 0.7,
          transparent: true,
          depthWrite: false,
        }),
      ),
    );
    preview.add(plane);
    for (const surface of figure.data) {
      const positions = [],
        values = [],
        rangeValues = [],
        indices = [],
        points = [],
        valid = [];
      const columns = surface.y[0].length;
      for (let row = 0; row < surface.y.length; row++) {
        for (let column = 0; column < columns; column++) {
          const value = surface.y[row][column];
          const vertex = position(
            surface.x[row][column],
            Math.max(-ymax, Math.min(ymax, value ?? 0)),
            surface.z[row][column],
          );
          positions.push(...vertex.toArray());
          values.push(surface.surfacecolor[row][column] ?? 0);
          rangeValues.push(value ?? 0);
          valid.push(value !== null && (!cut || Math.abs(value) <= ymax));
          let text = view.normalized
            ? surface.normalized_text?.[row][column] ||
              surface.text[row][column]
            : surface.text[row][column];
          if (value !== null && Math.abs(value) > ymax) {
            text = text.replace(
              "</tbody></table>",
              `<tr><th scope="row">Capped at</th><td>${value > 0 ? "+" : "-"}${ymax}${metric === "percent" ? "%" : " (log10)"}</td></tr></tbody></table>`,
            );
          }
          points.push({
            text,
            position: vertex,
            value,
            row,
            column,
            surface: surface.name,
          });
        }
      }
      for (let row = 0; row < surface.y.length - 1; row++) {
        for (let column = 0; column < columns - 1; column++) {
          const a = row * columns + column,
            b = a + 1,
            c = a + columns,
            d = c + 1;
          if ([a, b, c, d].every((index) => valid[index]))
            indices.push(a, c, b, b, c, d);
        }
      }
      const geometry = new THREE.BufferGeometry();
      geometry.setAttribute(
        "position",
        new THREE.Float32BufferAttribute(positions, 3),
      );
      geometry.setAttribute(
        "performanceValue",
        new THREE.Float32BufferAttribute(values, 1),
      );
      geometry.setIndex(indices);
      geometry.setAttribute(
        "rangeValue",
        new THREE.Float32BufferAttribute(rangeValues, 1),
      );
      const stops = colors.colorscale;
      const material = new THREE.ShaderMaterial({
        side: THREE.DoubleSide,
        uniforms: {
          minimumPerformance,
          contextOpacity,
          contextPass: { value: false },
          rangeLimit: { value: ymax },
          thresholds: {
            value: stops.map(
              (stop) => stop[0] * (colors.cmax - colors.cmin) + colors.cmin,
            ),
          },
          palette: { value: stops.map((stop) => new THREE.Color(stop[1])) },
        },
        vertexShader:
          "attribute float performanceValue; attribute float rangeValue; varying float value; varying float measured; void main(){value=performanceValue;measured=rangeValue;gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0);}",
        // Interpolate the measurement first, then map through the approved color scale.
        fragmentShader: `${previewShader}
          uniform float thresholds[${stops.length}]; uniform vec3 palette[${stops.length}]; uniform float rangeLimit; varying float value;
          void main(){float alpha=previewAlpha();vec3 c=palette[0];for(int i=1;i<${stops.length};i++){
          float t=clamp((value-thresholds[i-1])/(thresholds[i]-thresholds[i-1]),0.0,1.0);
          if(value>=thresholds[i-1])c=mix(palette[i-1],palette[i],t);}
          if(measured>rangeLimit)c=vec3(0.08,1.0,0.3);
          if(measured< -rangeLimit)c=vec3(1.0,0.05,0.12);
          gl_FragColor=vec4(c,alpha);
          #include <colorspace_fragment>
          }`,
      });
      const mesh = new THREE.Mesh(geometry, material);
      mesh.userData.points = points;
      contents.add(mesh);
      surfaces.push(mesh);
      addContext(geometry, material, THREE.Mesh);
      // Keep isolated observations visible when a row, column or neighboring cell is missing.
      const dots = points.filter((_, index) => valid[index]);
      const dotGeometry = new THREE.BufferGeometry().setFromPoints(
        dots.map((point) => point.position),
      );
      const dotMaterial = new THREE.ShaderMaterial({
        uniforms: {
          minimumPerformance,
          contextOpacity,
          contextPass: { value: false },
          pointSize: { value: 2 * renderer.getPixelRatio() },
        },
        vertexShader:
          "attribute vec3 color; attribute float rangeValue; uniform float pointSize; varying vec3 tint; varying float measured; void main(){tint=color;measured=rangeValue;gl_PointSize=pointSize;gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0);}",
        fragmentShader: `${previewShader}\nvarying vec3 tint; void main(){gl_FragColor=vec4(tint,previewAlpha());\n#include <colorspace_fragment>\n}`,
      });
      dotGeometry.setAttribute(
        "rangeValue",
        new THREE.Float32BufferAttribute(
          dots.map((point) => point.value),
          1,
        ),
      );
      dotGeometry.setAttribute(
        "color",
        new THREE.Float32BufferAttribute(
          dots.flatMap((point) =>
            new THREE.Color(
              point.value > ymax
                ? "#4fff95"
                : point.value < -ymax
                  ? "#ff3f61"
                  : "#172333",
            ).toArray(),
          ),
          3,
        ),
      );
      const markers = new THREE.Points(dotGeometry, dotMaterial);
      markers.userData.points = dots;
      contents.add(markers);
      surfaces.push(markers);
      addContext(dotGeometry, dotMaterial, THREE.Points);
    }
    axes.xaxis.tickvals.forEach((value, index) => {
      const x = (value / xmax) * 4;
      line([new THREE.Vector3(x, -2, -4), new THREE.Vector3(x, -2, 4)]);
      label(
        axes.xaxis.ticktext[index],
        new THREE.Vector3(x, -2, 4.3),
        "right",
        new THREE.Vector3(0, 0, 1),
      );
    });
    axes.zaxis.tickvals.forEach((value, index) => {
      const z = (value / zmax) * 4;
      line([new THREE.Vector3(-4, -2, z), new THREE.Vector3(4, -2, z)]);
      label(
        axes.zaxis.ticktext[index],
        new THREE.Vector3(4.3, -2, z),
        "left",
        new THREE.Vector3(1, 0, 0),
      );
    });
    const ticks = (!view.limit && axes.yaxis.tickvals) || [
      -ymax,
      -ymax / 2,
      0,
      ymax / 2,
      ymax,
    ];
    const tickLabels =
      (!view.limit && axes.yaxis.ticktext) ||
      ticks.map(
        (value) =>
          `${metric === "factor" && value > 0 ? "+" : ""}${Number(value.toPrecision(6))}${metric === "percent" ? "%" : ""}`,
      );
    ticks.forEach((value, index) => {
      const y = (value / ymax) * 2;
      line([new THREE.Vector3(-4, y, -4), new THREE.Vector3(4, y, -4)]);
      label(tickLabels[index], new THREE.Vector3(-4.2, y, -4), "right");
    });
    planeTitle(
      axes.xaxis.title.text,
      new THREE.Vector3(0, -1.99, 5.2),
      new THREE.Vector3(1, 0, 0),
      6,
    );
    planeTitle(
      "Deep FS \u2190 Task \u2192 Broad FS",
      new THREE.Vector3(7, -1.99, 0),
      new THREE.Vector3(0, 0, -1),
      6,
    );
    legendTitle.textContent = `Performance: ${metric === "percent" ? "%" : "log10"}`;
    legendBody.replaceChildren();
    if (performanceControls) legendBody.append(performanceControls);
    const bar = document.createElement("div");
    bar.className = "landscape-legend-scale";
    // Sample the same percentage palette along the displayed vertical coordinate.
    // Logarithmic heights are uniformly spaced here, just as on the vertical axis.
    const gradient = Array.from({ length: 101 }, (_, index) => {
      const height = -ymax + (index / 100) * 2 * ymax;
      const percent = metric === "factor" ? 100 * (1 - 10 ** -height) : height;
      const u = Math.max(
        0,
        Math.min(1, (percent - colors.cmin) / (colors.cmax - colors.cmin)),
      );
      const stops = colors.colorscale;
      const upper = stops.findIndex((stop) => stop[0] >= u);
      let color = new THREE.Color(stops[0][1]);
      if (upper > 0) {
        const [start, from] = stops[upper - 1],
          [end, to] = stops[upper];
        color = new THREE.Color(from).lerp(
          new THREE.Color(to),
          (u - start) / (end - start),
        );
      }
      return `#${color.getHexString()} ${index}%`;
    });
    bar.style.cssText = `position:relative;width:100%;height:12px;margin:4px 0;background:linear-gradient(to right,${gradient.join(",")})`;
    bar.append(legendMarker);
    const filterNote = document.createElement("div");
    bar.title =
      "Hover scale to preview a minimum; leave to restore the slider threshold.";
    const previewOptions = document.createElement("div");
    previewOptions.style.cssText =
      "display:flex;flex-wrap:wrap;gap:.5rem;align-items:center;max-width:320px;margin-top:4px";
    const belowLabel = document.createElement("label");
    belowLabel.textContent = "Below threshold: ";
    const below = document.createElement("select");
    below.className = "landscape-preview-opacity";
    for (const [value, text] of [
      ["0.5", "50% transparent"],
      ["0.25", "75% transparent"],
      ["0", "Hide"],
    ])
      below.add(new Option(text, value));
    below.value = String(contextOpacity.value);
    below.addEventListener("change", () => {
      contextOpacity.value = Number(below.value);
      draw();
    });
    belowLabel.append(below);
    const planeLabel = document.createElement("label");
    const planeToggle = document.createElement("input");
    planeToggle.type = "checkbox";
    planeToggle.className = "landscape-preview-plane";
    planeToggle.checked = showThresholdPlane;
    const thresholdControls = document.createElement("div");
    thresholdControls.className = "landscape-threshold-controls";
    thresholdControls.style.cssText =
      "display:flex;align-items:center;gap:6px;width:100%;margin-top:4px";
    const lowerThreshold = document.createElement("button");
    lowerThreshold.type = "button";
    lowerThreshold.textContent = "-";
    lowerThreshold.setAttribute("aria-label", "Lower performance threshold");
    const threshold = document.createElement("input");
    threshold.type = "range";
    threshold.min = "0";
    threshold.max = "100";
    threshold.step = "1";
    threshold.value = String(Math.round(thresholdFraction * 100));
    threshold.className = "landscape-threshold-slider";
    threshold.setAttribute("aria-label", "Performance threshold");
    threshold.style.cssText = "flex:1;min-width:0;width:100%";
    const raiseThreshold = document.createElement("button");
    raiseThreshold.type = "button";
    raiseThreshold.textContent = "+";
    raiseThreshold.setAttribute("aria-label", "Raise performance threshold");
    thresholdControls.append(lowerThreshold, threshold, raiseThreshold);
    let hoveredThreshold = null;
    function applyThreshold() {
      const fraction = hoveredThreshold ?? thresholdFraction;
      minimumPerformance.value = (2 * fraction - 1) * ymax;
      preview.visible = true;
      plane.visible = showThresholdPlane;
      plane.position.y = (2 * fraction - 1) * 2;
      root.dataset.minimum = minimumPerformance.value;
      clearHover();
      if (showThresholdPlane)
        showPerformanceValue(minimumPerformance.value, plane.position.y);
      legendMarker.hidden = false;
      legendMarker.style.left = `${fraction * 100}%`;
      filterNote.textContent = `Minimum: ${minimumPerformance.value.toFixed(2)}${metric === "percent" ? "%" : " log10"}`;
      lowerThreshold.disabled = thresholdFraction <= 0;
      raiseThreshold.disabled = thresholdFraction >= 1;
      draw();
    }
    function setThreshold(fraction) {
      thresholdFraction = Math.max(0, Math.min(1, fraction));
      threshold.value = String(Math.round(thresholdFraction * 100));
      applyThreshold();
    }
    planeToggle.addEventListener("change", () => {
      showThresholdPlane = planeToggle.checked;
      applyThreshold();
    });
    planeLabel.append(planeToggle, " Threshold plane");
    previewOptions.append(belowLabel, planeLabel);
    threshold.addEventListener("input", () =>
      setThreshold(Number(threshold.value) / 100),
    );
    for (const [button, delta] of [
      [lowerThreshold, -1],
      [raiseThreshold, 1],
    ])
      button.addEventListener("click", () =>
        setThreshold(thresholdFraction + delta / 100),
      );
    bar.addEventListener("pointermove", (event) => {
      const bounds = bar.getBoundingClientRect();
      hoveredThreshold = Math.max(
        0,
        Math.min(1, (event.clientX - bounds.left) / bounds.width),
      );
      applyThreshold();
    });
    bar.addEventListener("pointerleave", () => {
      hoveredThreshold = null;
      applyThreshold();
    });
    const scale = document.createElement("div");
    scale.className = "landscape-legend-ticks";
    scale.style.cssText = "position:relative;height:18px;width:100%";
    ticks.forEach((value, index) => {
      const tick = document.createElement("span");
      tick.textContent = tickLabels[index];
      tick.style.cssText = `position:absolute;left:${((value + ymax) / (2 * ymax)) * 100}%;transform:translateX(-50%)`;
      scale.append(tick);
    });
    const scaleAxis = document.createElement("div");
    const inset = Math.max(...tickLabels.map((text) => text.length)) / 2 + 1;
    scaleAxis.style.margin = `0 ${inset}ch`;
    scaleAxis.append(bar, scale);
    legendBody.append(scaleAxis, filterNote, previewOptions, thresholdControls);
    const note = document.createElement("div");
    note.textContent = view.limit
      ? cut
        ? "Out of range: cut off"
        : "Out of range: capped in bright green/red"
      : "Range: Auto";
    legendBody.append(note);
    applyThreshold();
    resize();
  }
  const ray = new THREE.Raycaster();
  ray.params.Points.threshold = 0.06;
  function pick(x, y) {
    ray.setFromCamera(
      new THREE.Vector2((x / width) * 2 - 1, 1 - (y / height) * 2),
      camera,
    );
    // Compare projected corners; never transpose row and column after task reordering.
    const distance = (point) => {
      const projected = point.position.clone().project(camera);
      return (
        (((projected.x + 1) * width) / 2 - x) ** 2 +
        (((1 - projected.y) * height) / 2 - y) ** 2
      );
    };
    for (const hit of ray.intersectObjects(surfaces)) {
      const candidates = (
        hit.face ? [hit.face.a, hit.face.b, hit.face.c] : [hit.index]
      )
        .map((index) => hit.object.userData.points[index])
        .filter((point) => point.value >= minimumPerformance.value);
      if (candidates.length)
        return candidates.reduce((best, point) =>
          distance(point) < distance(best) ? point : best,
        );
    }
    return null;
  }
  canvas.addEventListener("pointermove", (event) => {
    if (event.buttons) return;
    const rect = canvas.getBoundingClientRect();
    const point = pick(event.clientX - rect.left, event.clientY - rect.top);
    if (!point) {
      clearHover();
      draw();
      return;
    }
    showHover(point);
    tooltip.hidden = false;
    // All record text has already been escaped by the Python figure generator.
    tooltip.innerHTML = point.text;
  });
  canvas.addEventListener("pointerleave", () => {
    clearHover();
    draw();
  });
  function reset() {
    controls.reset();
  }
  controls.saveState();
  canvas.addEventListener("keydown", (event) => {
    const delta = camera.position.clone().sub(controls.target);
    const spherical = new THREE.Spherical().setFromVector3(delta);
    switch (event.key) {
      case "ArrowLeft":
        spherical.theta -= 0.12;
        break;
      case "ArrowRight":
        spherical.theta += 0.12;
        break;
      case "ArrowUp":
        spherical.phi -= 0.12;
        break;
      case "ArrowDown":
        spherical.phi += 0.12;
        break;
      case "+":
      case "=":
        spherical.radius *= 0.9;
        break;
      case "-":
        spherical.radius *= 1.1;
        break;
      case "Home":
        reset();
        event.preventDefault();
        return;
      default:
        return;
    }
    event.preventDefault();
    spherical.makeSafe();
    camera.position
      .copy(controls.target)
      .add(new THREE.Vector3().setFromSpherical(spherical));
    controls.update();
  });
  update();
  return {
    update,
    resize,
    reset,
    pick,
    dispose() {
      observer.disconnect();
      controls.dispose();
      clear();
      renderer.dispose();
      root.replaceChildren();
    },
  };
};
