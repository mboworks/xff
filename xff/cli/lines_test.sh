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
# End-to-end test of the {lines} field (per-file text line count) via the %{lines} -printf
# escape: a multi-line file, a file with no trailing newline, an empty file (0), the empty
# render for a binary file (a NUL byte), and the empty render for a non-regular entry. Drives
# the real binary (reads files).

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

test::lines_field_counts_text_files() {
  local dir out
  dir="$(test_tmpdir lines)"
  mkdir -p "${dir}"
  printf 'a\nb\nc\n' >"${dir}/three.txt" # 3 lines
  printf 'x' >"${dir}/one.txt"           # 1 line, no trailing newline
  printf '' >"${dir}/empty.txt"          # 0 lines
  printf 'a\0b\n' >"${dir}/bin.dat"      # binary (NUL byte) -> empty
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -type f -printf '%{lines}|%{name}\n' 2>&1)"
  expect_output_contains "3|three.txt" "${out}"
  expect_output_contains "1|one.txt" "${out}"
  expect_output_contains "0|empty.txt" "${out}"
  expect_output_contains "|bin.dat" "${out}" # a binary file has no line count
}

test::lines_field_empty_for_non_regular() {
  local dir out
  dir="$(test_tmpdir linesdir)"
  mkdir -p "${dir}"
  # The directory itself is non-regular, so {lines} renders empty (bracketed to show it).
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -maxdepth 0 -type d -printf '[%{lines}]\n' 2>&1)"
  expect_output_contains "[]" "${out}"
}

test_runner
