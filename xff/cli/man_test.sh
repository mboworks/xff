#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# End-to-end test of `xff --man`: it emits roff source, and that source renders
# through a real roff formatter (mandoc / groff / nroff) into a formatted man page
# with the expected sections and no leftover roff control lines. The formatter is a
# required dependency (mandoc on macOS, installed in CI on Linux); a missing one is a
# hard failure, not a skip.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

# A real newline for line-anchored expect_matches patterns: expect_matches matches
# the whole text ([[ =~ ]]), so `^`/`$` anchor the whole output, not a line.
# `(^|${NL})X` and `X($|${NL})` restore grep's per-line anchoring.
NL=$'\n'

_xff_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/testing/xff"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff -path '*xff/cli/testing/xff' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

# Flattens a formatter's terminal styling to plain text so the assertions match:
#   - the sed strips ANSI SGR escapes (`ESC[...m`), which newer groff/grotty emit BY DEFAULT; they
#     split tokens (e.g. `ESC[4mxff ESC[24m(1)`), so `xff(1)` would never match otherwise;
#   - `col -b` then removes backspace overstriking (mandoc / nroff bold+underline).
# ORDER MATTERS: the sed must run BEFORE `col -b`. `col -b` mangles an SGR escape (it drops the
# `ESC[` control prefix but keeps the parameters as literal text, e.g. `ESC[4mxff` -> `4mxff`), so
# stripping after it is too late -- the ESC anchor is gone. `[[]` matches a literal `[` portably
# (GNU + BSD sed); `${esc}` is a real ESC byte.
_flatten() {
  local esc=$'\033'
  sed "s/${esc}[[][0-9;]*m//g" | col -b
}

test::man_renders_through_a_roff_formatter() {
  local roff scratch tmp rendered renderer=""
  roff="$("$(_xff_bin)" --man)"
  scratch="$(test_tmpdir man)"
  tmp="${scratch}/page.1"
  printf '%s\n' "${roff}" >"${tmp}"

  # Use whatever formatter the host has; _flatten reduces its styling (overstriking AND the
  # SGR escapes newer groff emits) to plain text so the assertions match.
  if command -v mandoc >/dev/null 2>&1; then
    renderer="mandoc"
    rendered="$(mandoc -Tascii "${tmp}" 2>/dev/null | _flatten)"
  elif command -v groff >/dev/null 2>&1; then
    renderer="groff"
    rendered="$(groff -man -Tascii "${tmp}" 2>/dev/null | _flatten)"
  elif command -v nroff >/dev/null 2>&1; then
    renderer="nroff"
    rendered="$(nroff -man "${tmp}" 2>/dev/null | _flatten)"
  fi
  rm -f "${tmp}"

  # A roff formatter is a required test dependency: mandoc ships on macOS and is
  # installed in CI on Linux. Its absence is a hard failure, not a skip.
  if [[ -z "${renderer}" ]]; then
    echo "ERROR: no roff formatter found; install mandoc (or groff/nroff)"
    expect_eq "a roff formatter (mandoc/groff/nroff) on PATH" "none found"
    return 0
  fi

  echo "--- rendered with ${renderer} (first 20 lines) ---"
  printf '%s\n' "${rendered}" | head -20

  # The formatter produced a real man page: title (mandoc keeps `xff(1)`, groff/man-db
  # upper-case to `XFF(1)`, so match either) + section headings and a documented option.
  expect_matches '[Xx][Ff][Ff]\(1\)' "${rendered}"
  expect_output_contains 'NAME' "${rendered}"
  expect_output_contains 'OPTIONS' "${rendered}"
  expect_matches '\-\-sort' "${rendered}"
  # All roff was consumed: no control request survives into the output, neither at
  # line start nor mid-line (a `.B`/`.SH`/... left mid-sentence is printed literally).
  expect_not_matches "(^|${NL})\.[A-Za-z]" "${rendered}"
  expect_not_matches '\.(B|BR|SH|SS|TP|PP|TH) ' "${rendered}"
}

test_runner
