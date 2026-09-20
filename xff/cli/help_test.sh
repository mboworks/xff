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
# Binary-level test that `--help` / `-h` print a real usage page (not a stub) and
# exit 0, and that `--version` prints a version and exits 0. Anchors on a few
# stable substrings rather than exact wording, so prose edits do not break it.

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

test::help_prints_usage_and_options() {
  local out rc
  out="$("$(_xff_bin)" --help 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_output_contains 'Usage:' "${out}"
  expect_matches '\-\-config' "${out}" # a shipped global, not the old stub
  expect_output_contains '--help=safety' "${out}"
  expect_output_contains 'COMMAND STRUCTURE' "${out}"
  expect_output_contains 'COMMON TASKS' "${out}"
  expect_output_contains '--help=all' "${out}"
  expect_output_contains '--help=long' "${out}"
  expect_matches 'expressions' "${out}" # the --help=TOPIC index lists the expressions topic
  local n
  n="$(printf '%s\n' "${out}" | wc -l)"
  # The old stub was two lines; a real page is many more.
  expect_eq "yes" "$([[ ${n} -ge 10 ]] && echo yes || echo no)"
}

test::dash_h_matches_long_help() {
  local short long
  short="$("$(_xff_bin)" -h 2>&1)"
  long="$("$(_xff_bin)" --help 2>&1)"
  expect_eq "${long}" "${short}"
}

test::version_prints_and_exits_zero() {
  local out rc
  out="$("$(_xff_bin)" --version 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_output_contains 'xff' "${out}"
}

test::gnu_single_dash_help_matches_long_help() {
  # GNU find compatibility: -help is the single-dash long option for --help.
  local single long
  single="$("$(_xff_bin)" -help 2>&1)"
  long="$("$(_xff_bin)" --help 2>&1)"
  expect_eq "${long}" "${single}"
}

test::gnu_single_dash_version_prints_and_exits_zero() {
  # GNU find compatibility: -version is the single-dash long option for --version.
  local out rc
  out="$("$(_xff_bin)" -version 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_output_contains 'xff' "${out}"
}

test::man_prints_roff_and_exits_zero() {
  local out rc
  out="$("$(_xff_bin)" --man 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_matches "(^|${NL})\.TH xff 1" "${out}" # a roff man page header
  expect_matches "(^|${NL})\.SH OPTIONS" "${out}"
}

test::formatted_full_help_prints_each_structured_reference() {
  local out rc
  expect_eq "$("$(_xff_bin)" --help=full 2>&1)" "$("$(_xff_bin)" --help=LONG:PLAIN 2>&1)"

  out="$("$(_xff_bin)" --help=full:markdown 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_matches "(^|${NL})# xff" "${out}"
  expect_matches "(^|${NL})## Options" "${out}"
  expect_eq "${out}" "$("$(_xff_bin)" --help=long:md 2>&1)"

  out="$("$(_xff_bin)" --help=long:html 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_matches "(^|${NL})<!doctype html>" "${out}"
  expect_matches '<section id="options">' "${out}"
  expect_matches "</html>($|${NL})" "${out}"

  expect_eq "$("$(_xff_bin)" --man 2>&1)" "$("$(_xff_bin)" --help=full:roff 2>&1)"
}

test::notice_markdown_publishes_the_component_manifest() {
  local out
  out="$("$(_xff_bin)" --help=notice --help-format=markdown)"
  expect_output_contains 'RE2' "${out}"
  expect_output_contains 'BSD-3-Clause' "${out}"
  expect_output_contains 'Copyright The Abseil Authors' "${out}"
  expect_output_contains '# Notices' "${out}"
  expect_output_not_contains '**Usage:**' "${out}"
  expect_output_not_contains '## Options' "${out}"
  expect_eq "${out}" "$("$(_xff_bin)" --help=NOTICES:MD)"
}

test::formatted_help_rejects_unknown_formats() {
  local out rc
  out="$("$(_xff_bin)" --help=full:jsonl 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "unknown help format" "${out}"

  out="$("$(_xff_bin)" --help=fields:garbage 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "unknown help format" "${out}"
}

test::removed_standalone_document_flags_are_unknown() {
  local flag out rc
  for flag in --markdown --html; do
    out="$("$(_xff_bin)" "${flag}" 2>&1)" && rc=0 || rc=$?
    expect_eq "2" "${rc}"
    expect_output_contains "unknown option '${flag}'" "${out}"
  done
}

test::overview_examples_run_as_shown() {
  local dir out
  dir="$(test_tmpdir overview_examples)"
  mkdir -p "${dir}/src" "${dir}/left-tree" "${dir}/right-tree"
  : >"${dir}/src/a.cc"
  : >"${dir}/src/a.h"
  : >"${dir}/src/b.txt"
  out="$(cd "${dir}" && "$(_xff_bin)" src -type f -name '*.cc')"
  expect_eq "src/a.cc" "${out}" || return
  out="$(cd "${dir}" && "$(_xff_bin)" . \( -name '*.cc' -o -name '*.h' \) -print)"
  expect_output_contains 'a.cc' "${out}" || return
  expect_output_contains 'a.h' "${out}" || return
  expect_eq "no" "$([[ ${out} == *b.txt* ]] && echo yes || echo no)" || return
  out="$(cd "${dir}" && "$(_xff_bin)" --compare left-tree right-tree)"
  expect_eq "" "${out}"
}

test_runner

test::help_format_applies_to_topics_flags_indexes_and_shortcuts() {
  local target format out
  for target in notice compare --width all list styles extras license=Apache-2.0; do
    for format in markdown html roff; do
      out="$("$(_xff_bin)" "--help=${target}" "--help-format=${format}")"
      expect_output_not_contains '**Usage:** ` `' "${out}"
      expect_output_not_contains 'no help topic' "${out}"
    done
  done
  expect_eq "$("$(_xff_bin)" --help=compare:html)" "$("$(_xff_bin)" --help-format=html --help=compare)"
  expect_eq "$("$(_xff_bin)" --help=full:markdown)" "$("$(_xff_bin)" --help=full --help-format=md)"
  expect_eq "$("$(_xff_bin)" --man)" "$("$(_xff_bin)" --help=long --help-format=roff)"
  expect_output_contains '<!doctype html>' "$("$(_xff_bin)" --help --help-format=html)"
}

test::help_format_rejects_missing_help_and_conflicts() {
  local out rc
  out="$("$(_xff_bin)" --help-format=html 2>&1)" && rc=0 || rc=$?
  expect_eq '2' "${rc}"
  expect_output_contains 'requires help' "${out}"
  out="$("$(_xff_bin)" --version --help-format=html 2>&1)" && rc=0 || rc=$?
  expect_eq '2' "${rc}"
  out="$("$(_xff_bin)" --help --help-format 2>&1)" && rc=0 || rc=$?
  expect_eq '2' "${rc}"
  expect_output_contains 'requires a value' "${out}"
  out="$("$(_xff_bin)" --help=notice:html --help-format=markdown 2>&1)" && rc=0 || rc=$?
  expect_eq '2' "${rc}"
  expect_output_contains 'conflicting help formats' "${out}"
  out="$("$(_xff_bin)" --man --help-format=plain 2>&1)" && rc=0 || rc=$?
  expect_eq '2' "${rc}"
  out="$("$(_xff_bin)" --help=wildth --help-format=html 2>&1)" && rc=0 || rc=$?
  expect_eq '2' "${rc}"
  expect_output_contains 'width' "${out}"
}

test::help_markup_ignores_terminal_width_and_color() {
  local format
  for format in markdown html roff; do
    expect_eq "$("$(_xff_bin)" --help=compare "--help-format=${format}" --width=40 --color=always)" \
      "$("$(_xff_bin)" --help=compare "--help-format=${format}" --width=120 --color=never)"
  done
}
