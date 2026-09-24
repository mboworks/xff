// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";

window.XffLandscape = function createLandscape(root, figures) {
  root.style.cssText =
    "position:relative;width:100%;height:850px;min-width:320px;overflow:hidden";
  let renderer;
  try {
    renderer = new THREE.WebGLRenderer({ antialias: true });
  } catch {
    root.textContent =
      "The 3D chart requires WebGL2. All measurements remain available in the tables below.";
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
  tooltip.hidden = true;
  tooltip.style.cssText =
    "position:absolute;z-index:3;background:#fff;color:#172333;border:1px solid #667;border-radius:4px;box-shadow:0 2px 10px #0003;padding:8px;pointer-events:none;font:12px system-ui;max-width:90%";
  root.append(tooltip);
  const legend = document.createElement("div");
  legend.style.cssText =
    "position:absolute;left:12px;top:12px;background:#fffffff0;padding:8px;font:12px system-ui;color:#172333;max-width:90%";
  root.append(legend);
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
    root
      .querySelectorAll(".hover-axis-value")
      .forEach((element) => element.remove());
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
    // A measurement need not land on a vertical-axis tick; label its exact height.
    const tag = document.createElement("span");
    const metric = root.dataset.metric;
    tag.textContent = `${Number(point.value.toFixed(2))}${metric === "percent" ? "%" : ""}`;
    tag.style.cssText =
      "position:absolute;background:#ffe49b;font:12px monospace;padding:2px;pointer-events:none";
    const projected = feet[2].clone().project(camera);
    tag.style.left = `${((projected.x + 1) * width) / 2}px`;
    tag.style.top = `${((1 - projected.y) * height) / 2}px`;
    tag.style.transform = "translate(-100%,-50%)";
    tag.className = "hover-axis-value";
    root.append(tag);
    draw();
  }
  function clear() {
    clearHover();
    scene.remove(contents);
    contents.traverse((object) => {
      object.geometry?.dispose();
      object.material?.map?.dispose();
      object.material?.dispose();
    });
    contents = new THREE.Group();
    scene.add(contents);
    labels.forEach(({ element }) => element.remove());
    labels = [];
    surfaces = [];
    tooltip.hidden = true;
  }
  function update(order = "similarity", metric = "percent") {
    clear();
    root.dataset.metric = metric;
    const figure = figures[order][metric],
      axes = figure.layout.scene,
      colors = figure.layout.coloraxis;
    const flat = figure.data
      .flatMap((surface) => surface.y.flat())
      .filter((value) => value !== null);
    const ymax = Math.max(...(axes.yaxis.range || flat).map(Math.abs), 0.01);
    const xmax = Math.max(...axes.xaxis.tickvals.map(Math.abs), 0.01);
    const zmax = Math.max(...axes.zaxis.tickvals.map(Math.abs), 0.01);
    const position = (x, y, z) =>
      new THREE.Vector3((x / xmax) * 4, (y / ymax) * 2, (z / zmax) * 4);
    for (const surface of figure.data) {
      const positions = [],
        values = [],
        indices = [],
        points = [],
        valid = [];
      const columns = surface.y[0].length;
      for (let row = 0; row < surface.y.length; row++) {
        for (let column = 0; column < columns; column++) {
          const value = surface.y[row][column];
          const vertex = position(
            surface.x[row][column],
            value ?? 0,
            surface.z[row][column],
          );
          positions.push(...vertex.toArray());
          values.push(surface.surfacecolor[row][column] ?? 0);
          valid.push(value !== null);
          points.push({
            text: surface.text[row][column],
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
      const stops = colors.colorscale;
      const material = new THREE.ShaderMaterial({
        side: THREE.DoubleSide,
        uniforms: {
          thresholds: {
            value: stops.map(
              (stop) => stop[0] * (colors.cmax - colors.cmin) + colors.cmin,
            ),
          },
          palette: { value: stops.map((stop) => new THREE.Color(stop[1])) },
        },
        vertexShader:
          "attribute float performanceValue; varying float value; void main(){value=performanceValue;gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0);}",
        // Interpolate the measurement first, then map through the approved color scale.
        fragmentShader: `uniform float thresholds[${stops.length}]; uniform vec3 palette[${stops.length}]; varying float value;
          void main(){vec3 c=palette[0];for(int i=1;i<${stops.length};i++){
          float t=clamp((value-thresholds[i-1])/(thresholds[i]-thresholds[i-1]),0.0,1.0);
          if(value>=thresholds[i-1])c=mix(palette[i-1],palette[i],t);}gl_FragColor=vec4(c,1.0);
          #include <colorspace_fragment>
          }`,
      });
      const mesh = new THREE.Mesh(geometry, material);
      mesh.userData.points = points;
      contents.add(mesh);
      surfaces.push(mesh);
      // Keep isolated observations visible when a row, column or neighboring cell is missing.
      const dots = points.filter((_, index) => valid[index]);
      const dotGeometry = new THREE.BufferGeometry().setFromPoints(
        dots.map((point) => point.position),
      );
      const dotMaterial = new THREE.PointsMaterial({
        color: "#172333",
        size: 2,
        sizeAttenuation: false,
      });
      const markers = new THREE.Points(dotGeometry, dotMaterial);
      markers.userData.points = dots;
      contents.add(markers);
      surfaces.push(markers);
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
    const ticks = axes.yaxis.tickvals || [-ymax, -ymax / 2, 0, ymax / 2, ymax];
    ticks.forEach((value, index) => {
      const y = (value / ymax) * 2;
      line([new THREE.Vector3(-4, y, -4), new THREE.Vector3(4, y, -4)]);
      label(
        axes.yaxis.ticktext?.[index] ?? `${Number(value.toFixed(1))}%`,
        new THREE.Vector3(-4.2, y, -4),
        "right",
      );
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
    legend.replaceChildren();
    const title = document.createElement("div");
    title.textContent = axes.yaxis.title.text;
    const bar = document.createElement("div");
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
    bar.style.cssText = `width:320px;max-width:100%;height:12px;margin:4px 0;background:linear-gradient(to right,${gradient.join(",")})`;
    const scale = document.createElement("div");
    scale.className = "landscape-legend-ticks";
    scale.style.cssText =
      "position:relative;height:18px;width:320px;max-width:100%";
    ticks.forEach((value, index) => {
      const tick = document.createElement("span");
      tick.textContent =
        axes.yaxis.ticktext?.[index] ?? `${Number(value.toFixed(1))}%`;
      tick.style.cssText = `position:absolute;left:${((value + ymax) / (2 * ymax)) * 100}%;transform:translateX(-50%)`;
      scale.append(tick);
    });
    legend.append(title, bar, scale);
    resize();
  }
  const ray = new THREE.Raycaster();
  ray.params.Points.threshold = 0.06;
  function pick(x, y) {
    ray.setFromCamera(
      new THREE.Vector2((x / width) * 2 - 1, 1 - (y / height) * 2),
      camera,
    );
    const hit = ray.intersectObjects(surfaces)[0];
    if (!hit) return null;
    if (!hit.face) return hit.object.userData.points[hit.index];
    // Compare projected corners; never transpose row and column after task reordering.
    const distance = (point) => {
      const projected = point.position.clone().project(camera);
      return (
        (((projected.x + 1) * width) / 2 - x) ** 2 +
        (((1 - projected.y) * height) / 2 - y) ** 2
      );
    };
    return [hit.face.a, hit.face.b, hit.face.c]
      .map((index) => hit.object.userData.points[index])
      .reduce((best, point) =>
        distance(point) < distance(best) ? point : best,
      );
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
    tooltip.style.left = `${Math.max(0, Math.min(width - tooltip.offsetWidth, event.clientX - rect.left + 14))}px`;
    tooltip.style.top = `${Math.max(0, Math.min(height - tooltip.offsetHeight, event.clientY - rect.top + 14))}px`;
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
