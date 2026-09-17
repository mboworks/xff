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
# End-to-end test of --histogram: ASCII vs Unicode bars (via --unicode), the count per bucket
# sorted by height, suppression of the per-match listing, combining with --summary, and the
# unknown-bucket usage error. Drives the real binary.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

# A real newline for line-anchored expect_matches patterns (it matches the whole text).
NL=$'\n'

_xff_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/testing/xff"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff -path '*xff/cli/testing/xff' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

# A tree with three .cc and one .h, so the ext histogram is cc=3 (tallest) and h=1.
_make_tree() {
  local dir="$1"
  mkdir -p "${dir}"
  local f
  for f in a.cc b.cc c.cc d.h; do echo x >"${dir}/${f}"; done
}

test::histogram_ascii_bars_scaled_to_tallest() {
  local dir out
  dir="$(test_tmpdir histascii)"
  _make_tree "${dir}"
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --histogram=ext --unicode=never "${dir}" -type f 2>&1)"
  # cc (3) and h (1) each get a bar of '#'; cc is the tallest so it is the widest.
  expect_matches "cc[[:space:]]+3[[:space:]]+#+" "${out}"
  expect_matches "h[[:space:]]+1[[:space:]]+#+" "${out}"
  # The per-match listing is suppressed: the file paths never appear.
  expect_output_not_contains "a.cc" "${out}"
}

test::histogram_unicode_bars() {
  local dir out
  dir="$(test_tmpdir histuni)"
  _make_tree "${dir}"
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --histogram=ext --unicode=always "${dir}" -type f 2>&1)"
  # Unicode block bars use the full-block character.
  expect_output_contains "█" "${out}"
}

test::histogram_combines_with_summary() {
  local dir out
  dir="$(test_tmpdir histsum)"
  _make_tree "${dir}"
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --summary=type --histogram=ext --unicode=never "${dir}" -type f 2>&1)"
  # The --summary table (a total row) and the histogram bars both appear.
  expect_output_contains "total" "${out}"
  expect_matches "cc[[:space:]]+3[[:space:]]+#+" "${out}"
}

test::histogram_unknown_bucket_is_a_usage_error() {
  local dir out rc
  dir="$(test_tmpdir histerr)"
  mkdir -p "${dir}"
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --histogram=bogus "${dir}" 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "unknown --histogram bucket" "${out}"
}

test::histogram_numeric_measure_aggregates_a_field() {
  local dir out
  dir="$(test_tmpdir histnum)"
  mkdir -p "${dir}"
  printf 'a\nb\nc\n' >"${dir}/x.cc" # 3 lines
  printf 'd\ne\n' >"${dir}/y.cc"    # 2 lines
  printf 'z\n' >"${dir}/w.h"        # 1 line
  # ext:sum(lines): cc = 3 + 2 = 5 (tallest), h = 1.
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --histogram='ext:sum(lines)' --unicode=never "${dir}" -type f 2>&1)"
  expect_matches "cc[[:space:]]+5[[:space:]]+#+" "${out}"
  expect_matches "h[[:space:]]+1[[:space:]]+#+" "${out}"
}

test::histogram_metric_without_aggregator_is_a_usage_error() {
  local dir out rc
  dir="$(test_tmpdir histnoagg)"
  mkdir -p "${dir}"
  # A numeric metric with no aggregator (ext:lines) is a usage error, per the design.
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --histogram='ext:lines' "${dir}" 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "needs an aggregator" "${out}"
}

test::histogram_numeric_range_buckets() {
  local dir out
  dir="$(test_tmpdir histrange)"
  mkdir -p "${dir}"
  head -c 5 /dev/zero >"${dir}/small" # 5 bytes -> the "1-9" magnitude range
  head -c 150 /dev/zero >"${dir}/big" # 150 bytes -> the "100-999" range
  # A numeric size bucket groups by order of magnitude into ascending ranges (a distribution).
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --histogram=size --unicode=never "${dir}" -type f 2>&1)"
  expect_output_contains "1-9" "${out}"
  expect_output_contains "100-999" "${out}"
}

test::histogram_by_mime_groups_by_media_type() {
  local dir out
  dir="$(test_tmpdir histmime)"
  mkdir -p "${dir}"
  echo x >"${dir}/a.txt"
  echo x >"${dir}/b.txt"
  echo x >"${dir}/c.md"
  # --histogram=mime reuses the {mime} field: text/plain (2, tallest), text/markdown (1).
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --histogram=mime --unicode=never "${dir}" -type f 2>&1)"
  expect_matches "text/plain[[:space:]]+2[[:space:]]+#+" "${out}"
  expect_matches "text/markdown[[:space:]]+1[[:space:]]+#+" "${out}"
}

test::histogram_by_user_groups_under_the_owner() {
  local dir out
  dir="$(test_tmpdir histuser)"
  mkdir -p "${dir}"
  echo x >"${dir}/a.txt"
  echo x >"${dir}/b.txt"
  echo x >"${dir}/c.txt"
  # --histogram=user reuses the {user} field; all files share one owner (the test user), so one bar
  # of 3. The owner name is runtime-dependent, so assert the count and that the listing is suppressed.
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --histogram=user --unicode=never "${dir}" -type f 2>&1)"
  expect_matches "3[[:space:]]+#+" "${out}"
  expect_output_not_contains "a.txt" "${out}"
}

test::histogram_width_sets_the_bar_length() {
  local dir out
  dir="$(test_tmpdir histwidth)"
  _make_tree "${dir}" # cc=3 (tallest), h=1
  # --histogram-width=10: the tallest bar (cc) fills exactly 10 cells (default is 40).
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --histogram=ext --histogram-width=10 --unicode=never "${dir}" -type f 2>&1)"
  expect_matches "cc[[:space:]]+3[[:space:]]+##########(${NL}|\$)" "${out}"
}

test::histogram_markdown_composes_with_summary() {
  local dir out
  dir="$(test_tmpdir histmarkdown)"
  _make_tree "${dir}"
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --summary=type --histogram=ext --format=md "${dir}" -type f 2>&1)"
  expect_output_contains "## Summary by file type" "${out}"
  expect_output_contains "## Histogram ext" "${out}"
  expect_output_contains "| bucket | value |" "${out}"
  expect_output_contains "| ------ | ----: |" "${out}"
  expect_output_contains "| cc     |     3 |" "${out}"
  expect_output_contains "| h      |     1 |" "${out}"
}

test::histogram_rejects_unsupported_formats_before_print() {
  local dir out rc format
  dir="$(test_tmpdir histunsupported)"
  _make_tree "${dir}"
  for format in csv tsv nul tree; do
    out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --histogram=ext "--format=${format}" "${dir}" -print 2>&1)" && rc=0 || rc=$?
    expect_eq "2" "${rc}"
    expect_output_contains "histograms require --format=plain, aligned, jsonl, or markdown" "${out}"
    expect_output_not_contains "a.cc" "${out}"
  done
}

test_runner
