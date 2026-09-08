<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Release website

Release notes use [`.github/release-notes.md.template`](../.github/release-notes.md.template),
rendered by [`tools/release_notes.sh TAG`](../tools/release_notes.sh), to link to that tag's
versioned website and related release resources. The existing changelog and installation notes
remain included.

The [website](https://mboworks.github.io/xff/) forwards to the latest published stable release at
`site/tag/<tag>/`, preserving the exact Git tag name. Each release keeps its converted HTML,
images, and configured files. Retrying publication leaves an existing snapshot unchanged; a
different commit cannot replace it. Older versions remain directly accessible.

[`release-site.json`](../release-site.json) defines the layout. Source names are relative to the
repository root; destinations are relative to that release's site directory. For example:

```json
{
  "pages": {
    "README.md": "index.html",
    "docs/guide.md": "guide/index.html"
  },
  "files": {
    "schema/example.json": "schema/v1.json"
  },
  "links": [
    {
      "label": "Release",
      "href": "https://github.com/{owner}/{repo}/releases/tag/{tag}"
    }
  ]
}
```

Use existing source files in the actual configuration. `pages` converts Markdown; optional `files`
copies other files unchanged. `generated_html` maps a tracked Markdown source name to a generated
standalone HTML source and its destination. This lets repository links to `XFF.md` resolve to the
native `XFF.html` emitted by the released binary instead of a second rendering of the Markdown.
`README.md` must map to `index.html`. The generated
`documents.html`, `release.json`, `release-site.json`, and `assets/` paths are reserved.
Destination paths cannot have hidden components (names starting with a dot), because the Pages
artifact uploader excludes them. Hidden source paths remain valid; for example,
`.github/workflows/README.md` maps to `workflows/index.html`.

Navigation links support `{owner}`, `{repo}`, `{tag}`, `{version}`, and `{commit}`. `{version}`
omits a leading `v` for compatibility with coverage report paths. By default, the configuration
and content come from the release tag. Every linked local Markdown page, including directory
README links, must have a `pages` mapping. Publication fails for an omitted mapping, a missing
generated file, or a broken anchor within the snapshot. Links to configured pages follow their
destination mappings; other local source links use the exact release commit. Embedded images are
copied, including remote badges. Markdown conversion uses the
[GitHub Markdown API](https://docs.github.com/en/rest/markdown/markdown) at publication time;
browsing the result requires no Markdown renderer or CDN.

After the Release workflow succeeds, `Publish release site` retains the snapshot on
`coverage-pages` and deploys the complete Pages tree. Coverage and site publication share a
concurrency group to preserve both trees. GitHub's latest stable release selects the root redirect;
backfilling an older release does not make it latest. The workflow can also be dispatched with a
published tag to retry publication. Enable GitHub Pages with **GitHub Actions** as its source, and
set the repository's About website to `https://mboworks.github.io/xff/`.

## Backfill a historical release

No new release or tag change is needed. Manually dispatch `Publish release site` with `tag` set to
the historical release and `config_path` set to a tracked JSON file on `main`. Leave `config_path`
empty to use a configuration already in the tag. For example, after selecting a compatible
configuration and an existing tag:

```sh
gh workflow run pages.yml --repo mboworks/xff --ref main \
  -f tag="${RELEASE_TAG}" -f config_path=release-site.json
```

The override controls only publication layout; all Markdown and copied files come from the selected
tag. Each new snapshot retains the exact configuration as `release-site.json`, with its SHA-256,
origin, and source commit in `release.json`. A configuration can serve several historical tags when
its sources exist in each. For another layout, commit another configuration and select its path.
Missing sources or links fail publication instead of using newer content. Retrying a published tag
preserves its original HTML and configuration.

Local regression tests:

```sh
python3 -m unittest discover -s tools -p release_site_test.py
```

CI also converts the configured documentation and checks the generated links in a disposable
runner directory. It never commits, retains, or deploys that preview.

Publication downloads the immutable, attested `xff_full-linux-x86_64` release executable and runs
`--help=full:html`. The resulting standalone document is copied byte-for-byte into the snapshot and
participates in the same link and anchor validation as Markdown-rendered pages. `release.json`
records its SHA-256 alongside the release commit and configuration hash.

Release coverage links select `https://mboworks.github.io/xff/coverage/tag/<version>/`, matching the
coverage publisher rather than the moving main-branch report.
