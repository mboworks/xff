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
  local root i
  root="$(test_tmpdir tree)"
  mkdir -p "${root}/d1" "${root}/d2" "${root}/d3" "${root}/d4" "${root}/d5"
  for i in {1..10}; do
    : >"${root}/f${i}.txt"
  done
  echo "${root}"
}

test::max_results_caps_the_aggregate_implicit_listing() {
  local root out
  root="$(_make_tree)"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . \
    \( -type f -first 10 \) -o \( -type d -first 5 \) --max-results=12 --sort=tree)"
  expect_eq $'.\n./d1\n./d2\n./d3\n./d4\n./f1.txt\n./f10.txt\n./f2.txt\n./f3.txt\n./f4.txt\n./f5.txt\n./f6.txt' "${out}"
}

test::max_results_does_not_truncate_reductions() {
  local root out
  root="$(_make_tree)"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f --max-results=2 --summary --sort=tree)"
  expect_eq "total  10" "${out}"
}

test::explicit_actions_keep_their_positional_expression_semantics() {
  local root out
  root="$(_make_tree)"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -print --max-results=2 --sort=tree)"
  expect_eq $'./f1.txt\n./f10.txt\n./f2.txt\n./f3.txt\n./f4.txt\n./f5.txt\n./f6.txt\n./f7.txt\n./f8.txt\n./f9.txt' "${out}"
}

test::last_max_results_wins_and_zero_lists_none() {
  local root out
  root="$(_make_tree)"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f --max-results=2 --max-results=3 --sort=tree)"
  expect_eq $'./f1.txt\n./f10.txt\n./f2.txt' "${out}"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f --max-results=0 --sort=tree)"
  expect_eq "" "${out}"
}

test::max_results_rejects_missing_malformed_and_negative_counts() {
  local root out rc
  root="$(_make_tree)"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . --max-results 2>&1)" && rc=0 || rc="${?}"
  expect_eq "2" "${rc}"
  expect_output_contains "expects '=N'" "${out}"

  out="$(cd "${root}" && "$(_xff_bin)" --exact . --max-results=many 2>&1)" && rc=0 || rc="${?}"
  expect_eq "2" "${rc}"
  expect_output_contains "expects a count" "${out}"

  out="$(cd "${root}" && "$(_xff_bin)" --exact . --max-results=-1 2>&1)" && rc=0 || rc="${?}"
  expect_eq "2" "${rc}"
  expect_output_contains "cannot be negative" "${out}"
}

test_runner
