#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

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

_make_tree() {
  local root
  root="$(test_tmpdir tree)"
  printf '%s\n' \
    'one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen seventeen eighteen nineteen twenty' \
    >"${root}/reference.txt"
  printf '%s\n' \
    'one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen seventeen eighteen nineteen twentyone' \
    >"${root}/close.txt"
  printf '%s\n' \
    'one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen seventeen red blue green' \
    >"${root}/far.txt"
  printf 'one two\0three four five\n' >"${root}/binary.dat"
  echo "${root}"
}

test::default_keeps_only_the_eighty_percent_near_duplicate() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" "${root}" -name '*.txt' -similar "${root}/reference.txt" 2>&1)"
  expect_output_contains 'close.txt' "${out}"
  expect_output_contains 'reference.txt' "${out}"
  expect_output_not_contains 'far.txt' "${out}"
}

test::explicit_threshold_and_width_change_the_boundary() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" "${root}" -name '*.txt' -similar:5:65% "${root}/reference.txt" 2>&1)"
  expect_output_contains 'close.txt' "${out}"
  expect_output_contains 'far.txt' "${out}"
}

test::binary_content_never_matches_even_at_zero_percent() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" "${root}" -name binary.dat -similar:0% "${root}/reference.txt" 2>&1)"
  expect_output_not_contains 'binary.dat' "${out}"
}

test::find_style_rejects_similarity_and_help_documents_the_full_boundary() {
  local root rc help
  root="$(_make_tree)"
  "$(_xff_bin)" --config=find "${root}" -similar "${root}/reference.txt" >/dev/null 2>&1 && rc=0 || rc=$?
  expect_eq '2' "${rc}"
  help="$("$(_xff_bin)" --help=-similar 2>&1)"
  expect_output_contains 'five-word' "${help}"
  expect_output_contains '80%' "${help}"
  expect_output_contains 'whole-tree clustering is deferred' "${help}"
}

test_runner
