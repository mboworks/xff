# Rules

Some rules for the code layout and its development.

> Language-specific coding style lives in [`STYLE_CPP.md`](STYLE_CPP.md) and
> [`STYLE_SH.md`](STYLE_SH.md). This file keeps the project-level rules that are not language style.

- Everything is under Apache 2 license, see file `LICENSE`.
- Project-owned text files use ASCII prose/comments and straight quotation marks. Put intentional
  Unicode test data in separate Unicode-specific files explicitly excluded from the ASCII check;
  visible samples may be literal, while invisible characters keep explicit escapes. Histogram block glyphs
  U+2588 through U+258F may appear literally beside code-point explanations in
  `xff/engine/run.cc`, `xff/cli/histogram_test.sh`, and `docs/history-roadmap.md` so their
  appearance is visible. Tree connectors U+2500, U+2502, U+2514, and U+251C may appear
  literally in `xff/presentation/render/render.cc` and its tree-output tests.
  The README feature table may use U+2713 checkmarks and U+00B9 superscript one for partial support.
  Binary assets and fixtures are not text; preserve their bytes.
- All sources must be unix-text files: https://en.wikipedia.org/wiki/Text_file
  - Lines end in {LF}.
  - The files are either empty or end in {LF}.
- API changes that are not backwards compatible should not occur in minor version changes.
- Undocumented and private/internal APIs may be changed in any way at any time.
- All exported library code is in the directory `xff`.
  - Directory `xff` has no actual library rules, but may have test rules.
  - Capability directories group related implementation packages:
    - `xff/filesystem`: filesystem policy and discovery (`ignore`, `repo`);
    - `xff/matching`: entry classification and pattern engines (`language`, `mime`, `regex`);
    - `xff/presentation`: field production and output rendering (`color`, `fields`, `format`, `render`).
  - A distinct subsystem may remain directly under `xff` when nesting would hide a public boundary.
    `xff/vfs`, for example, is the stable seam shared with the separately built extension modules.
  - Do not create responsibility-free grouping names such as `core`, `common`, `misc`, `shared`, or
    `util`. Leave a package explicit until a specific capability owns it or it moves to a reusable
    library.
- All public / exported code must:
  - be tested (see [`STYLE_CPP.md`](STYLE_CPP.md) for the GoogleTest conventions),
  - have a documentation.
- All shell files follow [`STYLE_SH.md`](STYLE_SH.md).
