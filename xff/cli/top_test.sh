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
  touch "${root}/f_o_o" "${root}/foo" "${root}/foobar" "${root}/food" "${root}/xxfoo"
  touch "${root}/bar" "${root}/bard" "${root}/unrelated"
  echo "${root}"
}

test::top_keeps_the_exact_best_matches() {
  local root out
  root="$(_make_tree)"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -fuzzy foo -top 2 --sort=tree)"
  expect_eq $'./foo\n./foobar' "${out}"
}

test::actions_before_top_run_for_every_candidate_but_actions_after_run_for_survivors() {
  local root out
  root="$(_make_tree)"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -fuzzy foo -printf 'before:%f\n' \
    -top 2 -printf 'after:%f\n' --sort=tree)"
  expect_eq $'before:f_o_o\nbefore:foo\nbefore:foobar\nbefore:food\nbefore:xxfoo\nafter:foo\nafter:foobar' "${out}"
}

test::separate_top_nodes_keep_independent_complete_candidate_sets() {
  # `foobar` first reaches the right-hand -top only after losing the left-hand one. The right-hand
  # selection must wait for that late candidate rather than choosing once from an incomplete set.
  local root out
  root="$(_make_tree)"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -a \
    \( -fuzzy foo -top 1 -printf 'F:%f\n' \) -o \
    \( -type f -fuzzy bar -top 1 -printf 'B:%f\n' \) --sort=tree)"
  expect_eq $'F:foo\nB:bar' "${out}"
}

test::position_relative_to_collect_selects_what_the_summary_reads() {
  local root out
  root="$(_make_tree)"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -fuzzy foo -collect -top 2 \
    -printf 'listed:%f\n' --summary --human=off --sort=tree)"
  expect_matches $'^listed:foo\nlisted:foobar\nSummary\nGroup[^\n]*\ntotal +5 +100.00% +0 +0.00%$' "${out}" # collect ran before the ranked filter

  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -fuzzy foo -top 2 -collect \
    --summary --human=off --sort=tree)"
  expect_matches "total +2 +100.00% +0 +0.00%$" "${out}" # -collect suppresses implicit listing
}

test::top_rejects_incomparable_score_domains() {
  local root out rc
  root="$(_make_tree)"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -fuzzy:fzf:80% foo -fuzzy:fzf:50% bar -top 2 2>&1)" \
    && rc=0 || rc="${?}"
  expect_eq "2" "${rc}"
  expect_output_contains "different quality thresholds" "${out}"

  out="$(cd "${root}" && "$(_xff_bin)" --exact . -fuzzy:fzf foo -fuzzy:levenshtein foo -top 2 2>&1)" \
    && rc=0 || rc="${?}"
  expect_eq "2" "${rc}"
  expect_output_contains "different models" "${out}"
}

test::top_requires_a_score_on_every_path_and_validates_its_count() {
  local root out rc
  root="$(_make_tree)"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -top 2 2>&1)" && rc=0 || rc="${?}"
  expect_eq "2" "${rc}"
  expect_output_contains "must follow a successful" "${out}"

  out="$(cd "${root}" && "$(_xff_bin)" --exact . -fuzzy foo -top nope 2>&1)" && rc=0 || rc="${?}"
  expect_eq "2" "${rc}"
  expect_output_contains "expects a count" "${out}"

  out="$(cd "${root}" && "$(_xff_bin)" --exact . -fuzzy foo -top -1 2>&1)" && rc=0 || rc="${?}"
  expect_eq "2" "${rc}"
  expect_output_contains "cannot be negative" "${out}"
}

test::top_zero_is_valid_and_keeps_none() {
  local root out
  root="$(_make_tree)"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -fuzzy foo -top 0 --sort=tree)"
  expect_eq "" "${out}"
}

test_runner
