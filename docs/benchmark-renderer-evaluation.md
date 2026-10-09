<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Benchmark landscape renderer evaluation

A Three.js prototype shows substantial startup and update gains on this host. The follow-up integrates the renderer, including plane-mounted titles, a legend, keyboard controls,
and automated hover verification. The figures below describe the initial prototype, not a new
measurement of the integrated renderer.

## Initial measurements

Both renderers consumed the same six figure variants generated from published benchmark run
35794381777, attempt 1 (Linux data, rendered on a Mac). The chart contains 468 points. Browser:
Chrome using ANGLE Metal on an Apple M5 Pro; 1200 by 900 chart pixels, pixel ratio 1. Four rounds
alternate renderer order, with a fresh browser context for each page and HTTP caching disabled.
The browser process remains shared, so later rounds can benefit from GPU/driver caches.

| Measurement                                        |    Plotly | Three.js prototype |
| :------------------------------------------------- | --------: | -----------------: |
| JavaScript bundle, bytes                           | 4,816,043 |            560,138 |
| Bundle gzip, bytes                                 | 1,469,003 |            139,943 |
| First-use startup, ms (one observation)            |   1,125.9 |              173.3 |
| Subsequent startup, median ms (three observations) |   1,062.5 |               65.1 |
| Selector update, median ms (12 observations)       |    116.95 |               32.5 |

[Raw observations and method](measurements/renderer-prototype-macos-arm64.json) retain all samples.
These are exploratory measurements, not a release regression threshold. The harness includes
animation-frame scheduling. Three.js waits for GPU completion; Plotly waits for its update promise
and another animation frame. Cold browser-process trials and matched frame-completion measurement
are needed before stronger claims. Rotation frame time and hover latency have not been measured.
The prototype omits some Plotly features, including its complete colorbar and export toolbar.

## Rendering approach and findings

- Four independent indexed meshes preserve the CPU/tree gaps. Quads require all four observations;
  missing data must never become invented measurements.
- A fragment shader interpolates the percentage value before looking up the established color
  scale. Interpolating vertex RGB colors directly gave visibly wrong transitions across neutrality.
- DOM labels remain horizontal and can use ordinary text alignment. This removes the need to patch
  Plotly's internal text vectorizer, but the prototype still has title/tick collisions at some views.
- Raycasting identifies the surface triangle and a nearby measured corner for hover content.
  Complete tests must prove its task, tree, CPU, and file count agree after every reorder and metric
  change; no interpolated point should be presented as an actual measurement.
- The prototype retains the existing mirrored log file-count layout, task ordering, log-ratio
  height semantics, and percentage color mapping. It updates on demand rather than running an
  idle animation loop.
- The existing generator serializes six complete figures (about 772 KB for this report). A future
  renderer can carry observations once and derive order/metric views, independently of switching
  plotting libraries.

Three.js uses the [MIT license](https://github.com/mrdoob/three.js/blob/dev/LICENSE). This experiment
pins 0.186.1; bundled redistribution must retain its notice. See the
[official documentation](https://threejs.org/docs/) for renderer and control APIs. No plotting
library is linked into the xff executable; this change affects browser presentation only.

## Integration and remaining checks

The integrated renderer preserves all six order/metric views and the approved color mapping.
Axis titles are textured onto the base plane; orbit, zoom and pan remain unrestricted.
Horizontal ticks extend outward, and hover guides link measurements to highlighted axis labels.
The legend, keyboard controls, reset button, responsive sizing, collapse/reopen and WebGL fallback
are included. Chromium tests exercise all selector combinations and verify point identity against
the generated figures. Published pages share one pinned bundle; standalone reports embed it.

The Performance card always shows the threshold slider. Its value controls the minimum-performance
filter independently of the Threshold plane checkbox, which only shows or hides the plane.
Hovering the heatbar temporarily overrides the minimum without changing the slider or its step
buttons; leaving the heatbar restores the slider threshold.

The View card groups Dataset, Source, Order, Workers, and Timings, followed by Reset. A dataset
groups measurements from the same machine and measurement configuration. When collapsed, the
card's summary retains the selected dataset name. Workers appears when the measurement provides
allocation pairs to select.

The Version card separates Platform (the machine/runner name and architecture) from Dataset type
(the workload recipes and their revisions, such as `broad/v1 + deep/v1`). Details retains the
recorded OS information. Older catalog entries are enriched from their retained reports when
the history page is rendered, including compressed reports.

Help and the measurement detail card appear on the right. Help contains dataset/version guidance
and chart interaction instructions, collapsed by default. View, Version, and Performance appear
on the left in that order.

The full data tables remain the accessible alternative. The renderer does not provide Plotly's
image-export toolbar. Firefox/Safari testing and matched production startup, rotation and hover
measurements remain follow-ups. The initial timings do not establish a cross-browser guarantee.
