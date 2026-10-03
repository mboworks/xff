// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
(() => {
  if (window.XffSiteStorage) return;
  const originalFetch = window.fetch.bind(window);
  function unpack(value) {
    if (value?.xff_storage !== 1) return value;
    const nodes = [];
    for (const [kind, ...parts] of value.nodes) {
      if (kind === "v") {
        nodes.push(parts[0]);
        continue;
      }
      if (
        parts.some(
          (index) =>
            !Number.isInteger(index) || index < 0 || index >= nodes.length,
        )
      )
        throw new Error("Invalid packed data reference");
      const children = parts.map((index) => nodes[index]);
      if (kind === "l") nodes.push(children);
      else if (kind === "d" && children.length % 2 === 0) {
        const pairs = [];
        for (let index = 0; index < children.length; index += 2)
          pairs.push([children[index], children[index + 1]]);
        nodes.push(Object.fromEntries(pairs));
      } else throw new Error("Invalid packed data node");
    }
    return nodes[value.root];
  }
  async function read(url, options) {
    const response = await originalFetch(url, options);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const text = await new Response(
      response.body.pipeThrough(new DecompressionStream("gzip")),
    ).text();
    return unpack(JSON.parse(text));
  }
  // Published benchmark JSON is compressed explicitly, independent of host Content-Encoding.
  // Keep the existing chart API and cancellation behavior while loading its compact form.
  window.fetch = async (input, options) => {
    const url = new URL(
      input instanceof Request ? input.url : input,
      location.href,
    );
    if (
      url.origin !== location.origin ||
      !url.pathname.includes("/benchmarks/") ||
      !/\/(landscape|normalization)\.json$/.test(url.pathname)
    )
      return originalFetch(input, options);
    url.pathname += ".gz";
    const value = await read(url, options);
    return new Response(JSON.stringify(value), {
      headers: { "Content-Type": "application/json" },
    });
  };
  window.XffSiteStorage = {
    read: (url, options) =>
      window.fetch(url, options).then((response) => response.json()),
    gzip: read,
    unpack,
  };
})();
