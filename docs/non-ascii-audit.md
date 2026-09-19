# Non-ASCII source audit

## Scope

Audited every tracked file in the complete PR stack at `db0cd780fb8ac10729af48e14d4da954caa36902` (#875), including
C++, tests, scripts, workflows, documentation, and checked-in extension files. Build outputs and
external dependency checkouts are not repository source. Also scanned the current working copy
of every tracked file in the user workspace without changing its existing edits.

- Stack: 25 UTF-8 text files, 114 lines containing non-ASCII characters.
- User workspace: 20 tracked text files, 78 affected lines.
- Stack binary/non-UTF-8 files: 26; listed separately below, not treated as text violations.
- Non-ASCII tracked path names in user workspace: 0.

This report is ASCII-only. Code points and escaped source excerpts identify the findings without
reproducing the literal characters. A finding is an inventory entry, not automatically a violation. Origins are assigned from line blame to the earliest
PR in the current stack containing that commit; older lines are marked `Before #868`.

## Actions

- Replace literal Chinese test text with English/ASCII general examples and named escaped Unicode
  data where a dedicated Unicode regression needs it. This extends F06 beyond scoped-table tests.
- Replace decorative typographic punctuation in prose/comments with plain ASCII equivalents.
- Review README check/triangle markers for replacement with explicit ASCII words.
- Preserve intentional Unicode output. Histogram blocks may appear literally in the documented
  code, test, and prose locations, with code-point explanations. Tree connector glyphs likewise
  remain literal in their renderer and Unicode-specific tests. Isolate Unicode filename and
  column-width regression coverage in specifically named sections and fixtures.
- The em-dash prohibition hook is legitimate detection code, not unwanted prose. Use an escaped
  byte pattern for it; do not weaken its detection.
- Replace HTML separator literals with ASCII markup/entities if preserving the rendered symbol.
- Do not rewrite binary assets or archive fixtures as text.

## Exact text locations

### .pre-commit-config.yaml

- Line 79; origin Before #868; U+2014 EM DASH.

  ```text
  "        entry: \"\u2014\""
  ```

### AGENTS.md

- Line 10; origin Before #868; U+00B7 MIDDLE DOT.

  ```text
  "Build & test: `bazel test //...` \u00b7 sanitizers: `bazel test //... --config=clang --config=asan`"
  ```

- Line 87; origin Before #868; U+2026 HORIZONTAL ELLIPSIS.

  ```text
  "   - `EXPECT_THAT(s, StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr(\"\u2026\")))`"
  ```

### CHANGELOG.md

- Line 151; origin Before #868; U+2019 RIGHT SINGLE QUOTATION MARK.

  ```text
  "  their merge or tagged commit in main\u2019s history."
  ```

- Line 154; origin Before #868; U+2019 RIGHT SINGLE QUOTATION MARK.

  ```text
  "  each table\u2019s note from the next summary,"
  ```

- Line 170; origin Before #868; U+201C LEFT DOUBLE QUOTATION MARK; U+201D RIGHT DOUBLE QUOTATION MARK.

  ```text
  "- Add copyable \u201cSee also\u201d help commands across focused option, expression, and topic pages."
  ```

### README.md

- Line 92; origin Before #868; U+25B3 WHITE UP-POINTING TRIANGLE.

  ```text
  "The matrix compares native, built-in capabilities. A `\u25b3` means the tool covers a narrower form of"
  ```

- Line 99; origin Before #868; U+25B3 WHITE UP-POINTING TRIANGLE; U+2713 CHECK MARK.

  ```text
  "| **Filesystem expression language**      |   \u2713    |  \u25b3   |  -   |   -   |   -    |  -   |   -    |     -      |       -       | **\u2713 GNU/BSD `find` vocabulary**          |"
  ```

- Line 100; origin Before #868; U+2713 CHECK MARK.

  ```text
  "| **Multi-threaded filesystem traversal** |   -    |  \u2713   |  \u2713   |   -   |   -    |  -   |   -    |     -      |       -       | **\u2713 Native worker pool (`-j`)**          |"
  ```

- Line 101; origin Before #868; U+2713 CHECK MARK.

  ```text
  "| **Ignore-file and VCS awareness**       |   -    |  \u2713   |  \u2713   |   -   |   -    |  -   |   -    |     -      |       -       | **\u2713 Layered and configurable**           |"
  ```

- Line 102; origin Before #868; U+25B3 WHITE UP-POINTING TRIANGLE; U+2713 CHECK MARK.

  ```text
  "| **Path glob and regex filtering**       |   \u2713    |  \u2713   |  \u2713   |   \u25b3   |   \u25b3    |  -   |   -    |     -      |       \u25b3       | **\u2713 Multiple selectable grammars**       |"
  ```

- Line 103; origin Before #868; U+2713 CHECK MARK.

  ```text
  "| **Ranked fuzzy path matching**          |   -    |  -   |  -   |   \u2713   |   -    |  -   |   -    |     -      |       -       | **\u2713 `-fuzzy`, `--sort=score`**           |"
  ```

- Line 104; origin Before #868; U+2713 CHECK MARK.

  ```text
  "| **Regex content search with context**   |   -    |  -   |  \u2713   |   -   |   -    |  -   |   -    |     -      |       -       | **\u2713 `-grep`, `-rxc`, `--context`**       |"
  ```

- Line 105; origin Before #868; U+25B3 WHITE UP-POINTING TRIANGLE; U+2713 CHECK MARK.

  ```text
  "| **Language and MIME filtering**         |   -    |  -   |  \u25b3   |   -   |   -    |  -   |   -    |     -      |       -       | **\u2713 `-lang`, `-mime`**                   |"
  ```

- Line 106; origin Before #868; U+2713 CHECK MARK.

  ```text
  "| **Overrideable MIME metadata**          |   -    |  -   |  -   |   -   |   -    |  -   |   -    |     -      |       -       | **\u2713 JSON layers + `mime-db` extra**      |"
  ```

- Line 107; origin Before #868; U+2713 CHECK MARK.

  ```text
  "| **Overrideable language metadata**      |   -    |  -   |  -   |   -   |   -    |  -   |   -    |     -      |       -       | **\u2713 JSON layers + Linguist extra**       |"
  ```

- Line 108; origin Before #868; U+25B3 WHITE UP-POINTING TRIANGLE; U+2713 CHECK MARK.

  ```text
  "| **Text, binary, and EOL tests**         |   -    |  -   |  \u25b3   |   -   |   -    |  -   |   -    |     -      |       -       | **\u2713 `-text`, `-binary`, `-eof*`**        |"
  ```

- Line 109; origin Before #868; U+25B3 WHITE UP-POINTING TRIANGLE; U+2713 CHECK MARK.

  ```text
  "| **Structured JSON/CSV/table output**    |   -    |  -   |  \u2713   |   -   |   \u25b3    |  -   |   -    |     -      |       \u25b3       | **\u2713 Eight output formats**               |"
  ```

- Line 110; origin Before #868; U+25B3 WHITE UP-POINTING TRIANGLE; U+2713 CHECK MARK.

  ```text
  "| **Tree rendering**                      |   -    |  -   |  -   |   -   |   \u2713    |  -   |   -    |     -      |       \u25b3       | **\u2713 `--format=tree`**                    |"
  ```

- Line 111; origin Before #868; U+25B3 WHITE UP-POINTING TRIANGLE; U+2713 CHECK MARK.

  ```text
  "| **Field templates and rewrites**        |  GNU   |  -   |  \u25b3   |   \u25b3   |   -    |  -   |   -    |     -      |       \u25b3       | **\u2713 Shared `{field}` vocabulary**        |"
  ```

- Line 112; origin Before #868; U+25B3 WHITE UP-POINTING TRIANGLE; U+2713 CHECK MARK.

  ```text
  "| **Grouped size/count summaries**        |   -    |  -   |  -   |   -   |   -    |  \u25b3   |   -    |     -      |       \u25b3       | **\u2713 `--summary`**                        |"
  ```

- Line 113; origin Before #868; U+2713 CHECK MARK.

  ```text
  "| **Native histograms and statistics**    |   -    |  -   |  -   |   -   |   -    |  -   |   -    |     -      |       -       | **\u2713 `--histogram`**                      |"
  ```

- Line 114; origin Before #868; U+25B3 WHITE UP-POINTING TRIANGLE; U+2713 CHECK MARK.

  ```text
  "| **Cryptographic hashing**               |   -    |  -   |  -   |   -   |   -    |  -   |   -    |     \u2713      |       \u25b3       | **\u2713 `-hash`, `{hash}`, `-hasheq`**       |"
  ```

- Line 115; origin Before #868; U+25B3 WHITE UP-POINTING TRIANGLE; U+2713 CHECK MARK.

  ```text
  "| **Single-pass hash verification tally** |   -    |  -   |  -   |   -   |   -    |  -   |   -    |     \u25b3      |       -       | **\u2713 `--summary=hash-verification`**      |"
  ```

- Line 116; origin Before #868; U+2713 CHECK MARK.

  ```text
  "| **Duplicate-content grouping**          |   -    |  -   |  -   |   -   |   -    |  -   |   -    |     -      |       -       | **\u2713 `--summary=hash`**                   |"
  ```

- Line 117; origin Before #868; U+2713 CHECK MARK.

  ```text
  "| **Reference-file near-duplicate match** |   -    |  -   |  -   |   -   |   -    |  -   |   -    |     -      |       -       | **\u2713 Exact word-shingle Jaccard**         |"
  ```

- Line 118; origin Before #868; U+25B3 WHITE UP-POINTING TRIANGLE; U+2713 CHECK MARK.

  ```text
  "| **Per-file content comparison/diff**    |   -    |  -   |  -   |   -   |   -    |  -   |   \u2713    |     \u25b3      |       -       | **\u2713 `-cmp`, `-diff`**                    |"
  ```

- Line 119; origin Before #868; U+25B3 WHITE UP-POINTING TRIANGLE; U+2713 CHECK MARK.

  ```text
  "| **Gitignore-aware tree comparison**     |   -    |  -   |  -   |   -   |   -    |  -   |   \u25b3    |     \u25b3      |       -       | **\u2713 status selection or unified patch**  |"
  ```

- Line 120; origin Before #868; U+25B3 WHITE UP-POINTING TRIANGLE; U+2713 CHECK MARK.

  ```text
  "| **Virtual archive traversal**           |   -    |  -   |  \u25b3   |   -   |   -    |  -   |   -    |     -      |       \u25b3       | **\u2713 Members use the full expression**    |"
  ```

- Line 121; origin Before #868; U+25B3 WHITE UP-POINTING TRIANGLE; U+2713 CHECK MARK.

  ```text
  "| **Nested archive content search**       |   -    |  -   |  \u25b3   |   -   |   -    |  -   |   -    |     -      |       \u25b3       | **\u2713 Depth-controlled transparent reads** |"
  ```

- Line 122; origin Before #868; U+25B3 WHITE UP-POINTING TRIANGLE; U+2713 CHECK MARK.

  ```text
  "| **SquashFS/Snap/AppImage search**       |   -    |  -   |  -   |   -   |   -    |  -   |   -    |     -      |       \u25b3       | **\u2713 Indexed virtual filesystem extra**   |"
  ```

- Line 123; origin Before #868; U+2713 CHECK MARK.

  ```text
  "| **Archive creation from matches**       |   -    |  -   |  -   |   -   |   -    |  -   |   -    |     -      |       \u2713       | **\u2713 `--pack` sink**                      |"
  ```

- Line 124; origin Before #868; U+25B3 WHITE UP-POINTING TRIANGLE; U+2713 CHECK MARK.

  ```text
  "| **Standards-framed Brotli archives**    |   -    |  -   |  -   |   -   |   -    |  -   |   -    |     -      |       \u25b3       | **\u2713 RFC 9841 default; raw optional**     |"
  ```

- Line 125; origin Before #868; U+25B3 WHITE UP-POINTING TRIANGLE; U+2713 CHECK MARK.

  ```text
  "| **Safe delete preview**                 |   \u25b3    |  \u25b3   |  -   |   -   |   -    |  -   |   -    |     -      |       -       | **\u2713 `--dry-run`, `--safe`**              |"
  ```

- Line 126; origin Before #868; U+25B3 WHITE UP-POINTING TRIANGLE; U+2713 CHECK MARK.

  ```text
  "| **Parallel/batched per-match exec**     |   \u25b3    |  \u2713   |  -   |   \u25b3   |   -    |  -   |   -    |     -      |       -       | **\u2713 `-exec ... +`, `-j`**                |"
  ```

- Line 127; origin Before #868; U+2713 CHECK MARK.

  ```text
  "| **Capture command output as a field**   |   -    |  -   |  -   |   -   |   -    |  -   |   -    |     -      |       -       | **\u2713 `-capture`, `{capture.NAME}`**       |"
  ```

- Line 128; origin Before #868; U+2713 CHECK MARK.

  ```text
  "| **Sharded-dataset validation**          |   -    |  -   |  -   |   -   |   -    |  -   |   -    |     -      |       -       | **\u2713 collapse + status matching**         |"
  ```

### docs/benchmarks/execution-resources.md

- Line 132; origin #873; U+2013 EN DASH.

  ```text
  "| broad | listing    |    1 |              0.266 | 0.196\u20131.593       |            0.216 |          19.53 |"
  ```

- Line 133; origin #873; U+2013 EN DASH.

  ```text
  "| broad | listing    |    8 |              0.156 | 0.143\u20130.250       |            0.130 |          19.27 |"
  ```

- Line 134; origin #873; U+2013 EN DASH.

  ```text
  "| broad | summary    |    1 |              0.234 | 0.222\u20130.276       |            0.234 |          19.45 |"
  ```

- Line 135; origin #873; U+2013 EN DASH.

  ```text
  "| broad | summary    |    8 |              0.158 | 0.157\u20130.234       |            0.156 |          19.42 |"
  ```

- Line 136; origin #873; U+2013 EN DASH.

  ```text
  "| broad | hash       |    1 |              1.529 | 1.330\u20132.466       |            0.219 |          19.91 |"
  ```

- Line 137; origin #873; U+2013 EN DASH.

  ```text
  "| broad | hash       |    8 |              1.111 | 1.058\u20131.824       |            0.162 |          19.69 |"
  ```

- Line 138; origin #873; U+2013 EN DASH.

  ```text
  "| broad | hash_twice |    1 |              2.735 | 2.166\u20134.129       |            0.193 |          19.88 |"
  ```

- Line 139; origin #873; U+2013 EN DASH.

  ```text
  "| broad | hash_twice |    8 |              1.873 | 1.833\u20132.358       |            0.164 |          20.22 |"
  ```

- Line 140; origin #873; U+2013 EN DASH.

  ```text
  "| broad | hash_lines |    1 |              1.899 | 1.565\u20132.900       |            0.206 |          19.80 |"
  ```

- Line 141; origin #873; U+2013 EN DASH.

  ```text
  "| broad | hash_lines |    8 |              1.366 | 1.362\u20131.520       |            0.167 |          19.83 |"
  ```

- Line 142; origin #873; U+2013 EN DASH.

  ```text
  "| broad | compare    |    1 |              1.308 | 1.046\u20132.700       |            1.305 |          30.48 |"
  ```

- Line 143; origin #873; U+2013 EN DASH.

  ```text
  "| broad | compare    |    8 |              0.889 | 0.858\u20130.897       |            0.888 |          30.95 |"
  ```

- Line 144; origin #873; U+2013 EN DASH.

  ```text
  "| deep  | listing    |    1 |              0.206 | 0.175\u20130.208       |            0.053 |          19.81 |"
  ```

- Line 145; origin #873; U+2013 EN DASH.

  ```text
  "| deep  | listing    |    8 |              0.158 | 0.129\u20130.158       |            0.042 |          19.41 |"
  ```

- Line 146; origin #873; U+2013 EN DASH.

  ```text
  "| deep  | summary    |    1 |              0.192 | 0.163\u20130.226       |            0.190 |          19.67 |"
  ```

- Line 147; origin #873; U+2013 EN DASH.

  ```text
  "| deep  | summary    |    8 |              0.159 | 0.130\u20130.165       |            0.158 |          19.56 |"
  ```

- Line 148; origin #873; U+2013 EN DASH.

  ```text
  "| deep  | hash       |    1 |              1.446 | 1.288\u20131.588       |            0.080 |          20.95 |"
  ```

- Line 149; origin #873; U+2013 EN DASH.

  ```text
  "| deep  | hash       |    8 |              1.092 | 0.917\u20131.280       |            0.067 |          20.09 |"
  ```

- Line 150; origin #873; U+2013 EN DASH.

  ```text
  "| deep  | hash_twice |    1 |              2.555 | 2.554\u20132.640       |            0.075 |          21.41 |"
  ```

- Line 151; origin #873; U+2013 EN DASH.

  ```text
  "| deep  | hash_twice |    8 |              1.907 | 1.513\u20132.087       |            0.069 |          20.42 |"
  ```

- Line 152; origin #873; U+2013 EN DASH.

  ```text
  "| deep  | hash_lines |    1 |              2.142 | 2.134\u20132.204       |            0.091 |          21.19 |"
  ```

- Line 153; origin #873; U+2013 EN DASH.

  ```text
  "| deep  | hash_lines |    8 |              1.476 | 1.261\u20131.720       |            0.070 |          19.91 |"
  ```

- Line 154; origin #873; U+2013 EN DASH.

  ```text
  "| deep  | compare    |    1 |              1.721 | 1.567\u20131.726       |            1.718 |          34.77 |"
  ```

- Line 155; origin #873; U+2013 EN DASH.

  ```text
  "| deep  | compare    |    8 |              0.953 | 0.918\u20131.271       |            0.951 |          33.06 |"
  ```

### docs/design-config.md

- Line 316; origin Before #868; U+2019 RIGHT SINGLE QUOTATION MARK.

  ```text
  "Boolean capabilities use their own flag families or an existing option\u2019s value set."
  ```

### docs/design-parallel.md

- Line 4; origin Before #868; U+00A7 SECTION SIGN.

  ```text
  "> **Refines** `design.md` \u00a7\"Cross-cutting concerns\" (Determinism). Where the two"
  ```

- Line 6; origin Before #868; U+00B7 MIDDLE DOT.

  ```text
  "> Status: **Implemented** \u00b7 2026-06-27 \u00b7 Org: MBO Works"
  ```

### docs/history-roadmap.md

- Line 222; origin Before #868; U+2026 HORIZONTAL ELLIPSIS.

  ```text
  "     `bazel run @\u2026//:refresh_all --config=clang`, but `--config=clang` only configures the build"
  ```

- Line 473; origin Before #868; U+2026 HORIZONTAL ELLIPSIS.

  ```text
  "  checks a sidecar value and `! -hasheq \u2026` selects drift); `-hasheq:ALGO[/ENCODING]` shares the"
  ```

- Line 674; origin Before #868; U+00A7 SECTION SIGN.

  ```text
  "  the untrusted project layer is gone). Reverses the `design.md` \u00a7149 / `design-config.md`"
  ```

- Line 1128; origin Before #868; U+2588 FULL BLOCK; U+2589 LEFT SEVEN EIGHTHS BLOCK; U+258A LEFT THREE QUARTERS BLOCK; U+258B LEFT FIVE EIGHTHS BLOCK; U+258C LEFT HALF BLOCK; U+258D LEFT THREE EIGHTHS BLOCK; U+258E LEFT ONE QUARTER BLOCK; U+258F LEFT ONE EIGHTH BLOCK.

  ```text
  "    its box-drawing: Unicode block bars (`\u2588` plus the partials `\u258f\u258e\u258d\u258c\u258b\u258a\u2589` for sub-cell precision) when"
  ```

### docs/history.md

- Line 711; origin Before #868; U+201C LEFT DOUBLE QUOTATION MARK; U+201D RIGHT DOUBLE QUOTATION MARK.

  ```text
  "  \u201cthe ten best good matches\u201d without coupling selection and ranking in one argument. Multiple fuzzy"
  ```

### docs/implementation-plan.md

- Line 8; origin Before #868; U+00B7 MIDDLE DOT.

  ```text
  "> Status: implemented system map \u00b7 2026-09-10"
  ```

### docs/review-0.6.0.md

- Line 23; origin Before #868; U+2013 EN DASH.

  ```text
  "B01\u2013B09 have merged resolutions (B07 was a corrected diagnosis with regression coverage)."
  ```

- Line 188; origin Before #868; U+201C LEFT DOUBLE QUOTATION MARK; U+201D RIGHT DOUBLE QUOTATION MARK.

  ```text
  "reads \u201ccombines the hermetic Clang toolchain with size\u201d."
  ```

- Line 300; origin Before #868; U+00D7 MULTIPLICATION SIGN.

  ```text
  "**Acceptance:** publish and test a producer \u00d7 format matrix covering listing, status, patch, grep,"
  ```

- Line 392; origin Before #868; U+201C LEFT DOUBLE QUOTATION MARK; U+201D RIGHT DOUBLE QUOTATION MARK.

  ```text
  "then advanced capabilities. Clarify that \u201cdisk use\u201d summary examples report apparent entry sizes,"
  ```

- Line 441; origin Before #868; U+201C LEFT DOUBLE QUOTATION MARK; U+201D RIGHT DOUBLE QUOTATION MARK.

  ```text
  "\u201csomething matched\u201d explanation does not convey it."
  ```

- Line 539; origin Before #868; U+201C LEFT DOUBLE QUOTATION MARK.

  ```text
  "**Recommendation:** suggest close registered names, or explicitly say \u201cplace this short global before"
  ```

- Line 540; origin Before #868; U+201D RIGHT DOUBLE QUOTATION MARK.

  ```text
  "the roots; the double-dash form is position-independent\u201d where such a form exists. Do not invent"
  ```

- Line 550; origin Before #868; U+00D7 MULTIPLICATION SIGN.

  ```text
  "**Acceptance:** cover every field consumer with captures; producer \u00d7 format combinations; repeated"
  ```

- Line 594; origin Before #868; U+2013 EN DASH.

  ```text
  "| Tree comparison and diff                     | Status, patch, scopes, accounting, formats, filtering and exit behavior sampled.                                                                                          | B05, B06, S01, S03, S08\u2013S13 |"
  ```

- Line 595; origin Before #868; U+2013 EN DASH.

  ```text
  "| Summaries, histograms, collections           | Broad reductions sampled; invalid control handling and multi-table schemas need work.                                                                                     | B02\u2013B06, S03, S08\u2013S10       |"
  ```

- Line 609; origin Before #868; U+2013 EN DASH.

  ```text
  "4. **Everyday usability:** S03, S06\u2013S09, S11, S12, S15, S18, with executable examples."
  ```

### docs/test-plan.md

- Line 7; origin Before #868; U+00B7 MIDDLE DOT.

  ```text
  "> Status: implemented verification map \u00b7 2026-09-10"
  ```

### tools/coverage_index.py

- Line 241; origin Before #868; U+00B7 MIDDLE DOT.

  ```text
  "        '    <p><a href=\"lcov/\">Browse detailed LCOV source coverage</a> \u00b7 '"
  ```

- Line 242; origin Before #868; U+00B7 MIDDLE DOT.

  ```text
  "        '<a href=\"coverage-summary.json\">Coverage data (JSON)</a> \u00b7 '"
  ```

- Line 243; origin Before #868; U+00B7 MIDDLE DOT.

  ```text
  "        '<a href=\"coverage-meta.json\">Report metadata (JSON)</a> \u00b7 '"
  ```

### tools/release_site.py

- Line 347; origin Before #868; U+00B7 MIDDLE DOT.

  ```text
  "                    f'<footer>Release snapshot \u00b7 {sha}</footer></body></html>\\n')"
  ```

### tools/release_smoke.py

- Line 45; origin #874; U+96EA CJK UNIFIED IDEOGRAPH-96EA.

  ```text
  "        left_files = {\"same.txt\": \"same\\n\", \"changed.txt\": \"before\\n\", 'left,\\\"\u96ea\\n\\t\\\\.total': \"left\\n\"}"
  ```

### xff/cli/csv_test.sh

- Line 168; origin #874; U+96EA CJK UNIFIED IDEOGRAPH-96EA.

  ```text
  "  name=$'a|b\\n\\r\\t\\033\\177\\\\\u96ea.txt'"
  ```

- Line 172; origin #874; U+96EA CJK UNIFIED IDEOGRAPH-96EA.

  ```text
  "    expect_output_contains 'a|b\\n\\r\\t\\x1B\\x7F\\\\\u96ea.txt' \"${out}\""
  ```

- Line 176; origin #874; U+96EA CJK UNIFIED IDEOGRAPH-96EA.

  ```text
  "  expect_output_contains 'a\\|b\\\\n\\\\r\\\\t\\\\x1B\\\\x7F\\\\\\\\\u96ea.txt' \"${out}\""
  ```

### xff/cli/histogram_test.sh

- Line 63; origin Before #868; U+2588 FULL BLOCK.

  ```text
  "  expect_output_contains \"\u2588\" \"${out}\""
  ```

### xff/cli/summary_test.sh

- Line 749; origin #870; U+00E9 LATIN SMALL LETTER E WITH ACUTE.

  ```text
  "for name, content in [(\"same.txt\", \"same\"), (\"changed.cc\", \"right!!\"), (\"right.\u00e9\", \"R\")]:"
  ```

### xff/engine/run.cc

- Line 667; origin Before #868; U+2589 LEFT SEVEN EIGHTHS BLOCK; U+258A LEFT THREE QUARTERS BLOCK; U+258B LEFT FIVE EIGHTHS BLOCK; U+258C LEFT HALF BLOCK; U+258D LEFT THREE EIGHTHS BLOCK; U+258E LEFT ONE QUARTER BLOCK; U+258F LEFT ONE EIGHTH BLOCK.

  ```text
  "  constexpr std::array<std::string_view, 8> kPartials = {\"\", \"\u258f\", \"\u258e\", \"\u258d\", \"\u258c\", \"\u258b\", \"\u258a\", \"\u2589\"};"
  ```

- Line 671; origin Before #868; U+2588 FULL BLOCK.

  ```text
  "    bar += \"\u2588\";  // full block"
  ```

### xff/matching/regex/regex.cc

- Line 322; origin Before #868; U+2026 HORIZONTAL ELLIPSIS.

  ```text
  "// The kFnmatch grammar: a flat shell wildcard via POSIX fnmatch (`*`/`?`/`[\u2026]`, `*` matching any"
  ```

- Line 325; origin Before #868; U+2026 HORIZONTAL ELLIPSIS.

  ```text
  "// wraps the pattern in `*\u2026*` so it matches anywhere (`**` collapses to `*` in POSIX fnmatch, so the"
  ```

### xff/matching/regex/regex.h

- Line 37; origin Before #868; U+2026 HORIZONTAL ELLIPSIS.

  ```text
  "// test. kFnmatch is a flat shell wildcard (POSIX fnmatch: `*`/`?`/`[\u2026]`, where `*` matches any"
  ```

### xff/matching/regex/regex_test.cc

- Line 256; origin Before #868; U+2026 HORIZONTAL ELLIPSIS.

  ```text
  "  // PartialMatch wraps the pattern in `*\u2026*` so it matches anywhere; FindFirst reports the whole text"
  ```

### xff/presentation/render/render.cc

- Line 431; origin Before #868; U+2500 BOX DRAWINGS LIGHT HORIZONTAL; U+251C BOX DRAWINGS LIGHT VERTICAL AND RIGHT.

  ```text
  "  const std::string_view tee = unicode_ ? \"\u251c\u2500\u2500 \" : \"|-- \";"
  ```

- Line 432; origin Before #868; U+2500 BOX DRAWINGS LIGHT HORIZONTAL; U+2514 BOX DRAWINGS LIGHT UP AND RIGHT.

  ```text
  "  const std::string_view elbow = unicode_ ? \"\u2514\u2500\u2500 \" : \"`-- \";"
  ```

- Line 433; origin Before #868; U+2502 BOX DRAWINGS LIGHT VERTICAL.

  ```text
  "  const std::string_view vertical = unicode_ ? \"\u2502   \" : \"|   \";"
  ```

### xff/presentation/render/render_test.cc

- Line 240; origin #874; U+96EA CJK UNIFIED IDEOGRAPH-96EA.

  ```text
  "    std::string out = stream.Add({\"a\\n\\r\\t\\x1b\\x7f\\\\\u96ea\"});"
  ```

- Line 244; origin #874; U+96EA CJK UNIFIED IDEOGRAPH-96EA.

  ```text
  "        a\\n\\r\\t\\x1B\\x7F\\\\\u96ea"
  ```

- Line 252; origin #874; U+96EA CJK UNIFIED IDEOGRAPH-96EA.

  ```text
  "  tree.Add(\"root\\n/child\\t\\x1b\\\\\u96ea\");"
  ```

- Line 255; origin #874; U+96EA CJK UNIFIED IDEOGRAPH-96EA.

  ```text
  "      `-- child\\t\\x1B\\\\\u96ea"
  ```

- Line 347; origin Before #868; U+2500 BOX DRAWINGS LIGHT HORIZONTAL; U+251C BOX DRAWINGS LIGHT VERTICAL AND RIGHT.

  ```text
  "                         \"\u251c\u2500\u2500 README.md\\n\""
  ```

- Line 348; origin Before #868; U+2500 BOX DRAWINGS LIGHT HORIZONTAL; U+2514 BOX DRAWINGS LIGHT UP AND RIGHT.

  ```text
  "                         \"\u2514\u2500\u2500 src\\n\""
  ```

- Line 349; origin Before #868; U+2500 BOX DRAWINGS LIGHT HORIZONTAL; U+251C BOX DRAWINGS LIGHT VERTICAL AND RIGHT.

  ```text
  "                         \"    \u251c\u2500\u2500 main.cc\\n\""
  ```

- Line 350; origin Before #868; U+2500 BOX DRAWINGS LIGHT HORIZONTAL; U+2514 BOX DRAWINGS LIGHT UP AND RIGHT.

  ```text
  "                         \"    \u2514\u2500\u2500 util.cc\\n\"));"
  ```

- Line 386; origin Before #868; U+2500 BOX DRAWINGS LIGHT HORIZONTAL; U+251C BOX DRAWINGS LIGHT VERTICAL AND RIGHT.

  ```text
  "                         \"\u251c\u2500\u2500 a\\n\""
  ```

- Line 387; origin Before #868; U+2500 BOX DRAWINGS LIGHT HORIZONTAL; U+2502 BOX DRAWINGS LIGHT VERTICAL; U+2514 BOX DRAWINGS LIGHT UP AND RIGHT.

  ```text
  "                         \"\u2502   \u2514\u2500\u2500 x\\n\""
  ```

- Line 388; origin Before #868; U+2500 BOX DRAWINGS LIGHT HORIZONTAL; U+2514 BOX DRAWINGS LIGHT UP AND RIGHT.

  ```text
  "                         \"\u2514\u2500\u2500 b\\n\"));"
  ```

### xff/presentation/render/scoped_table_test.cc

- Line 46; origin #870; U+754C CJK UNIFIED IDEOGRAPH-754C; U+9762 CJK UNIFIED IDEOGRAPH-9762.

  ```text
  "        \u754c\u9762"
  ```

- Line 61; origin #870; U+754C CJK UNIFIED IDEOGRAPH-754C; U+9762 CJK UNIFIED IDEOGRAPH-9762.

  ```text
  "        \u754c\u9762       1   25.00%  1  B   25.00%      -        -     -        -      0    0.00%  0  B    0.00%      3   75.00%  3  B   75.00%"
  ```

## User-workspace locations

These are an independent scan of tracked working files, including local edits; they do not imply
that the current branch contains the open PR stack.

- `.pre-commit-config.yaml`: 79.
- `AGENTS.md`: 10, 87.
- `CHANGELOG.md`: 61, 64, 80.
- `README.md`: 57, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93.
- `docs/design-config.md`: 313.
- `docs/design-parallel.md`: 4, 6.
- `docs/history-roadmap.md`: 222, 473, 674, 1128.
- `docs/history.md`: 711.
- `docs/implementation-plan.md`: 8.
- `docs/review-0.6.0.md`: 168, 202, 284, 333, 424, 425, 435, 458, 459, 473.
- `docs/test-plan.md`: 7.
- `tools/coverage_index.py`: 241, 242, 243.
- `tools/release_site.py`: 347.
- `xff/cli/histogram_test.sh`: 63.
- `xff/engine/run.cc`: 670, 674.
- `xff/matching/regex/regex.cc`: 311, 314.
- `xff/matching/regex/regex.h`: 36.
- `xff/matching/regex/regex_test.cc`: 245.
- `xff/presentation/render/render.cc`: 404, 405, 406.
- `xff/presentation/render/render_test.cc`: 283, 284, 285, 286, 322, 323, 324.

## Binary or non-UTF-8 files

These are image assets, compressed language data, and archive/filesystem fixtures. Their bytes are
not source-text character usage.

- `docs/assets/apple-touch-icon.png`
- `docs/assets/favicon.ico`
- `docs/assets/favicon.png`
- `docs/assets/mboworks-logo.png`
- `extra_modules/archive/test_data/entrybz2.phar`
- `extra_modules/archive/test_data/entrygz.phar`
- `extra_modules/archive/test_data/example.crx`
- `extra_modules/archive/test_data/example.deb`
- `extra_modules/archive/test_data/example.gem`
- `extra_modules/archive/test_data/example.jar`
- `extra_modules/archive/test_data/example.jmod`
- `extra_modules/archive/test_data/example.rpm`
- `extra_modules/archive/test_data/example.whl`
- `extra_modules/archive/test_data/many.tar.gz`
- `extra_modules/archive/test_data/mini.tar.br`
- `extra_modules/archive/test_data/npm-example.tgz`
- `extra_modules/archive/test_data/plain.phar`
- `extra_modules/archive/test_data/sfx-example.zip`
- `extra_modules/archive/test_data/sha256.phar`
- `extra_modules/archive/test_data/tarbased.phar.tar`
- `extra_modules/archive/test_data/targz.phar.tar.gz`
- `extra_modules/archive/test_data/wholebz2.phar.bz2`
- `extra_modules/archive/test_data/wholegz.phar.gz`
- `extra_modules/archive/test_data/zipbased.phar.zip`
- `extra_modules/language_db/data/languages.json.br`
- `extra_modules/squashfs/test_data/mini.sqfs`

## Integration cleanup verification

The #868 integration worktree now passes a complete tracked-text scan with zero non-ASCII
text files. Intentional Unicode behavior uses escapes; README symbols use ASCII words. Binary
fixtures remain unchanged. The pre-commit ASCII text check explicitly excludes binary formats,
including PHP archives that file-type identification can mistake for text.
