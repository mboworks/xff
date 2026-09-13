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
# End-to-end test of -ls column alignment and the --buffer modes: the columns line
# up (via a buffered ColumnBuffer), and off/all/N are accepted. Drives the binary.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

# `test_tmpdir` allocates each tree under bashtest's managed scratch root. Its random suffix keeps
# test names out of printed paths, where a name could accidentally satisfy a negative assertion.

_xff_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/testing/xff"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff -path '*xff/cli/testing/xff' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

# Runs xff asserting exit 0, surfacing output on failure (so a sanitizer abort is
# diagnosable rather than a bare empty-output miss). Echoes the captured output.
_run() {
  local out rc
  out="$("$(_xff_bin)" "$@" 2>&1)" && rc=0 || rc=$?
  if [[ "${rc}" != "0" ]]; then
    echo "xff $* exited ${rc}; output:"
    printf '%s\n' "${out}"
  fi
  expect_eq "0" "${rc}"
  printf '%s' "${out}"
}

# Two files with very different size widths (1 vs 6 digits) and name lengths.
_make_tree() {
  local path
  path="$(test_tmpdir tree)"
  head -c 1 /dev/zero >"${path}/aaa"
  head -c 100000 /dev/zero >"${path}/b"
  echo "${path}"
}

test::ls_lists_entries_with_perms() {
  local root out
  root="$(_make_tree)"
  out="$(_run "${root}" -type f -ls)"
  expect_output_contains 'aaa' "${out}"
  expect_matches '\-rw' "${out}" # the permission column is present
}

test::ls_columns_are_aligned() {
  local root out plen_a plen_b
  root="$(_make_tree)"
  out="$(_run "${root}" -type f -ls)"
  # The path is the last field; when the columns are aligned, the fixed prefix before
  # the path is the same width on every row regardless of size or name length.
  plen_a="$(printf '%s\n' "${out}" | awk '$NF ~ /aaa$/ { print length($0) - length($NF) }')"
  plen_b="$(printf '%s\n' "${out}" | awk '$NF ~ /\/b$/  { print length($0) - length($NF) }')"
  expect_eq "${plen_a}" "${plen_b}"
}

test::buffer_modes_are_accepted() {
  local root
  root="$(_make_tree)"
  # off (min widths), all (full buffering), and an explicit window all run and list.
  expect_output_contains 'aaa' "$(_run --buffer=off "${root}" -type f -ls)"
  expect_output_contains 'aaa' "$(_run --buffer=all "${root}" -type f -ls)"
  expect_output_contains 'aaa' "$(_run --buffer=1 "${root}" -type f -ls)"
}

test::help_topic_documents_buffer() {
  expect_output_contains 'buffer' "$(_run --help=--buffer)"
}

test::ls_colours_the_name_column_like_the_plain_listing() {
  local root out
  root="$(test_tmpdir tree)"
  mkdir -p "${root}"
  mkdir -p "${root}/sub"
  # The bug this fixes: `xff . --color` coloured and `xff . -ls --color` did not, in the same run. Only
  # the NAME is wrapped, as `ls -l` does, so the metadata columns stay plain.
  out="$("$(_xff_bin)" --color=always "${root}" -ls -type d 2>&1)"
  expect_matches $'\033\\[1;34m' "${out}"     # xff's directory colour, on the path
  expect_matches $'drwx[^\033]*\033' "${out}" # the permissions column comes BEFORE any escape
  # And the theme is the same source the plain listing reads.
  out="$(LS_COLORS='di=01;35' "$(_xff_bin)" --color=always "${root}" -ls -type d 2>&1)"
  expect_matches $'\033\\[01;35m' "${out}"
  # Colour off means no escapes at all, as before.
  out="$("$(_xff_bin)" --color=never "${root}" -ls -type d 2>&1)"
  expect_not_matches $'\033\\[' "${out}"
}

test::ls_size_units_line_up_across_rows() {
  # A bare byte count has no prefix letter, so without padding its `B` sits one column left of the
  # `B` in `kB` while the numbers line up - or the reverse if the whole cell is right-aligned. The
  # size column pads the unit, so both the numbers and the unit letters align.
  local root out bytes_column kilo_column
  root="$(test_tmpdir tree)"
  mkdir -p "${root}"
  printf '%*s' 991 x >"${root}/small"
  printf '%*s' 1200 y >"${root}/large"
  out="$("$(_xff_bin)" --human=si "${root}" -type f -ls 2>&1)"
  expect_output_contains '991  B' "${out}" # the bare byte count pads where the prefix letter would be
  expect_output_contains '1.2 kB' "${out}"
  # And the `B` itself lands in the same column on both rows: one past the space in " B ", one past
  # the `k` in "kB " (awk selects the row and reports the offset in one pass).
  bytes_column="$(awk '/ B /{print index($0, " B ") + 1}' <<<"${out}")"
  kilo_column="$(awk '/kB /{print index($0, "kB ") + 1}' <<<"${out}")"
  expect_eq "${bytes_column}" "${kilo_column}"
  # IEC prefixes are a letter longer, so the byte row pads by two there.
  out="$("$(_xff_bin)" --human=iec "${root}" -type f -ls 2>&1)"
  expect_matches '991   B' "${out}"
}

test_runner
