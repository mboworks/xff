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
# End-to-end test of the non-plain --format renderers. The tabular family: csv / tsv stream a
# header row by default (suppressed by --no-header) with RFC-4180 quoting; aligned / markdown
# buffer the whole table and render it padded (a dashed rule under the header) after the walk;
# --columns and its usage errors are shared by all four. And tree, which renders the matched
# paths as a directory tree (--unicode picks the connectors). Drives the real binary (the
# header / table / tree is emitted by the walk driver, so shell level is the right fit).

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

_xff_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/testing/xff"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff -path '*xff/cli/testing/xff' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

test::csv_emits_a_header_then_quotes_a_comma_field() {
  local dir out
  dir="$(test_tmpdir csv)"
  mkdir -p "${dir}"
  : >"${dir}/plain.txt"
  : >"${dir}/with,comma.txt" # a comma in the name must be RFC-4180 quoted
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --format=csv "${dir}" -type f 2>&1)"
  # The header row is emitted first (before any record).
  expect_eq "path" "$(head -1 <<<"${out}")"
  expect_output_contains "plain.txt" "${out}"
  expect_matches '"[^"]*with,comma[^"]*"' "${out}" # the comma field is wrapped in quotes
}

test::no_header_suppresses_the_header_row() {
  local dir out
  dir="$(test_tmpdir nohdr)"
  mkdir -p "${dir}"
  : >"${dir}/a.txt"
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --format=csv --no-header "${dir}" -type f 2>&1)"
  expect_ne "path" "$(head -1 <<<"${out}")" # first line is a record, not the header
  expect_output_contains "a.txt" "${out}"
}

test::tsv_has_a_header_and_tab_separated_records() {
  local dir out
  dir="$(test_tmpdir tsv)"
  mkdir -p "${dir}"
  : >"${dir}/a.txt"
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --format=tsv "${dir}" -type f 2>&1)"
  expect_eq "path" "$(head -1 <<<"${out}")"
  expect_output_contains "a.txt" "${out}"
}

test::columns_produce_a_multi_column_table_with_a_header() {
  local dir out
  dir="$(test_tmpdir cols)"
  mkdir -p "${dir}"
  echo hi >"${dir}/a.txt" # 3 bytes (hi + newline)
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --format=csv --columns=name,size,type "${dir}" -type f 2>&1)"
  expect_eq "name,size,type" "$(head -1 <<<"${out}")" # the header is the column names
  expect_output_contains "a.txt,3,f" "${out}"
}

test::columns_validation_is_a_usage_error() {
  local dir out rc
  dir="$(test_tmpdir colerr)"
  mkdir -p "${dir}"
  : >"${dir}/a.txt"
  # An unknown column name.
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --format=csv --columns=name,bogus "${dir}" 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "field template: unknown field 'bogus'" "${out}"
  # --columns without a tabular format.
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --columns=name "${dir}" 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "needs a tabular --format" "${out}"
  # --format=csv with an output action (-ls) that suppresses the default listing.
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --format=csv "${dir}" -ls 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "format the default listing" "${out}"
}

test::aligned_renders_a_padded_table_under_a_dashed_rule() {
  local dir out
  dir="$(test_tmpdir aligned)"
  mkdir -p "${dir}"
  echo hi >"${dir}/a.txt"
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --format=aligned --columns=name,size "${dir}" -type f 2>&1)"
  expect_output_contains "name" "${out}" # the header column names
  expect_output_contains "size" "${out}"
  expect_matches "----" "${out}" # a dashed underline separates the header from the rows
  expect_output_contains "a.txt" "${out}"
}

test::markdown_renders_a_github_table_and_md_is_its_alias() {
  local dir out
  dir="$(test_tmpdir md)"
  mkdir -p "${dir}"
  echo hi >"${dir}/a.txt"
  # --format=md is the short alias of --format=markdown.
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --format=md --columns=name,size "${dir}" -type f 2>&1)"
  expect_matches "[|] -+ [|] -+ [|]" "${out}" # the padded | --- | --- | rule
  expect_output_contains "| a.txt" "${out}"
}

test::buffer_bounds_the_aligned_table_without_dropping_rows() {
  local dir out
  dir="$(test_tmpdir abuf)"
  mkdir -p "${dir}"
  : >"${dir}/a.txt"
  : >"${dir}/bb.txt"
  : >"${dir}/ccc.txt"
  # --buffer=1 locks the column widths on the first row then streams the rest; every match
  # must still appear (bounded memory, no data loss) and the header rule is still emitted.
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --format=aligned --buffer=1 --columns=name "${dir}" -type f 2>&1)"
  expect_output_contains "a.txt" "${out}"
  expect_output_contains "bb.txt" "${out}"
  expect_output_contains "ccc.txt" "${out}"
  expect_matches "----" "${out}"
}

test::no_header_drops_the_buffered_table_header() {
  local dir out
  dir="$(test_tmpdir aligned_nohdr)"
  mkdir -p "${dir}"
  echo hi >"${dir}/a.txt"
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --format=aligned --no-header --columns=name,size "${dir}" -type f 2>&1)"
  expect_output_not_contains "name" "${out}" # the header row (and its rule) is gone
  expect_output_contains "a.txt" "${out}"
}

test::tree_renders_a_directory_tree_with_ascii_connectors_under_no_unicode() {
  local dir out rc
  dir="$(test_tmpdir tree)"
  mkdir -p "${dir}/sub"
  : >"${dir}/a.txt"
  : >"${dir}/sub/b.txt"
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --format=tree --unicode=never "${dir}" -type f 2>&1)"
  expect_output_contains "a.txt" "${out}"
  expect_output_contains "b.txt" "${out}" # a deep match pulls in its ancestor dir
  expect_matches "[|]-- " "${out}"        # an ASCII tree connector
  # tree formats the default listing, so an output action is a usage error.
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --format=tree "${dir}" -ls 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "format the default listing" "${out}"
}

test::human_formats_escape_filename_controls() {
  local dir name out mode
  dir="$(test_tmpdir display_controls)"
  mkdir -p "${dir}"
  name=$'a|b\n\r\t\033\177\\.txt'
  : >"${dir}/${name}"
  for mode in aligned tree; do
    out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -type f --format="${mode}" --color=never)"
    expect_output_contains "a|b\\n\\r\\t\\x1B\\x7F\\\\.txt" "${out}"
    expect_output_not_contains "${name}" "${out}"
  done
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -type f --format=md --columns=name)"
  expect_output_contains "a\\|b\\\\n\\\\r\\\\t\\\\x1B\\\\x7F\\\\\\\\.txt" "${out}"
  expect_output_not_contains "${name}" "${out}"
}

test_runner
