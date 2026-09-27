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
# End-to-end rg frontend contract, independent of installed rg or ambient configuration.
set -euo pipefail
# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

_bin() { echo "${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/testing/xff"; }
_check() {
  local expected_rc="$1" expected="$2" out rc
  shift 2
  out="$("$(_bin)" "$@" 2>&1)" && rc=0 || rc=$?
  expect_eq "${expected_rc}" "${rc}"
  expect_eq "${expected}" "${out}"
}
_tree() {
  local root
  root="$(test_tmpdir tree)"
  printf 'TODO one\nother\nTODO two TODO\n' >"${root}/a.cc"
  printf 'other\n' >"${root}/b.txt"
  : >"${root}/empty"
  echo "${root}"
}

test::lines_case_prefixes_and_portions() {
  local root
  root="$(_tree)"
  _check 0 $'TODO one\nTODO two TODO' --rg TODO "${root}/a.cc"
  _check 0 $'1:TODO one\n3:TODO two TODO' --rg -n TODO "${root}/a.cc"
  _check 0 $'TODO\nTODO\nTODO' --rg -o TODO "${root}/a.cc"
  _check 0 $'TODO\nTODO\nTODO' --rg -io todo "${root}/a.cc"
  _check 1 '' --rg todo "${root}/a.cc"
  _check 0 $'TODO one\nTODO two TODO' --rg -S todo "${root}/a.cc"
  _check 0 "${root}/a.cc:TODO one"$'\n'"${root}/a.cc:TODO two TODO" --rg -H TODO "${root}/a.cc"
  _check 0 $'TODO one\nTODO two TODO' --rg -HI TODO "${root}/a.cc"
  _check 0 $'TODO one\nTODO two TODO' --rg -nN TODO "${root}/a.cc"
}

test::inversion_counts_filenames_and_exit_status() {
  local root
  root="$(_tree)"
  _check 0 other --rg -v TODO "${root}/a.cc"
  _check 0 other --rg -v TODO "${root}/b.txt"
  _check 0 other --rg -vo TODO "${root}/b.txt"
  _check 0 2 --rg -c TODO "${root}/a.cc"
  _check 0 3 --rg --count-matches TODO "${root}/a.cc"
  _check 0 3 --rg -co TODO "${root}/a.cc"
  _check 1 '' --rg -c TODO "${root}/b.txt"
  _check 0 "${root}/a.cc" --rg -l TODO "${root}/a.cc"
  _check 0 "${root}/b.txt" --rg --files-without-match TODO "${root}/b.txt"
  _check 0 "${root}/empty" --rg --files-without-match TODO "${root}/empty"
  _check 1 '' --rg -l TODO "${root}/empty"
  _check 0 '' --rg -q TODO "${root}/a.cc"
  _check 1 '' --rg -q missing "${root}/a.cc"
}

test::pattern_union_empty_pattern_files_and_fixed_strings() {
  local root
  root="$(_tree)"
  _check 0 $'TODO one\nother\nTODO two TODO' --rg -e TODO -e other "${root}/a.cc"
  _check 0 $'TODO one\nTODO two TODO' --rg -e TODO -e TODO "${root}/a.cc"
  _check 1 '' --rg -v -e TODO -e other "${root}/a.cc"
  printf 'TODO\r\nother' >"${root}/patterns"
  _check 0 $'TODO one\nother\nTODO two TODO' --rg -f "${root}/patterns" "${root}/a.cc"
  _check 1 '' --rg -f "${root}/empty" "${root}/a.cc"
  _check 0 "${root}/a.cc" --rg -f "${root}/empty" --files-without-match "${root}/a.cc"
  _check 0 $'TODO one\nother\nTODO two TODO' --rg -e '' "${root}/a.cc"
  _check 0 other --rg -x other "${root}/a.cc"
  _check 1 '' --rg -Fx TODO "${root}/a.cc"
  _check 0 $'TODO one\nTODO two TODO' --rg -w TODO "${root}/a.cc"
  _check 1 '' --rg -w TOD "${root}/a.cc"
  _check 1 '' --rg -F T.DO "${root}/a.cc"
}

test::filters_do_not_change_output_patterns() {
  local root
  root="$(_tree)"
  _check 0 $'TODO one\nTODO two TODO' --rg -I TODO "${root}" --xff -name '*.cc' -rxc other
  _check 1 '' --rg -I TODO "${root}" --xff -name '*.txt'
  _check 0 other --rg -Iv TODO "${root}" --xff -name '*.txt'
  _check 0 $'TODO one\nTODO two TODO' --rg -I TODO "${root}" --xff -name '*.cc' + -name '*.txt'
  _check 0 "${root}/a.cc" --rg TODO "${root}" --no-match-output
  _check 0 $'TODO one\nTODO two TODO' --rg -I -g '*.cc' TODO "${root}"
  _check 1 '' --rg -I -g '!*.cc' TODO "${root}"
}

test::stdin_explicit_implicit_and_pattern_input() {
  local root
  root="$(_tree)"
  printf 'hit\nmiss\n' | _check 0 hit --rg hit
  printf 'hit\nmiss\n' | _check 0 hit --rg hit -
  printf 'TODO\n' | _check 0 $'TODO one\nTODO two TODO' --rg -f - "${root}/a.cc"
  printf 'none\n' | _check 1 '' --rg hit -
}

test::binary_empty_matches_context_and_max_columns() {
  local root out rc
  root="$(_tree)"
  printf 'before\0TODO\n' >"${root}/binary"
  _check 1 '' --rg TODO "${root}/binary"
  # Only matching keeps binary bytes out of the shell capture.
  _check 0 TODO --rg -ao TODO "${root}/binary"
  _check 0 '' --rg -o '^' "${root}/a.cc"
  _check 0 $'TODO one\nother\nTODO two TODO' --rg -C1 TODO "${root}/a.cc"
  _check 0 $'TODO one\nother\nTODO two TODO' --rg -A1 -B1 TODO "${root}/a.cc"
  _check 0 $'[Omitted long matching line]\n[Omitted long matching line]' --rg -M3 TODO "${root}/a.cc"
  out="$("$(_bin)" --rg -e '[' "${root}" 2>&1)" && rc=0 || rc=$?
  expect_eq 2 "${rc}"
  expect_matches 'xff:' "${out}"
}

test::errors_are_not_silent_or_action_fallbacks() {
  local root out rc
  root="$(_tree)"
  out="$("$(_bin)" --rg TODO "${root}" --xff -delete 2>&1)" && rc=0 || rc=$?
  expect_eq 2 "${rc}"
  expect_matches 'not actions' "${out}"
  expect_eq yes "$(if [[ -f "${root}/a.cc" ]]; then echo yes; fi)"
  out="$("$(_bin)" --rg -f "${root}/missing" "${root}" 2>&1)" && rc=0 || rc=$?
  expect_eq 2 "${rc}"
  out="$("$(_bin)" --rg TODO "${root}/missing" 2>&1)" && rc=0 || rc=$?
  expect_eq 2 "${rc}"
}

test::complete_ini_files_keep_native_syntax_and_mandatory_globals() {
  local root out rc
  root="$(_tree)"
  printf '%s\n' '--require-system-globals' '--block-file-deletion' '[search]' '--line-number' '-name *.cc' >"${root}/system.ini"
  printf '%s\n' '[search]' '--only-matching' >"${root}/user.ini"
  out="$(XFF_TEST_SYSTEM_CONFIG="${root}/system.ini" XFF_TEST_USER_CONFIG="${root}/user.ini" "$(_bin)" --rg --config=search -I TODO "${root}")"
  expect_eq $'1:TODO\n3:TODO\n3:TODO' "${out}"
  printf '%s\n' '--rg' >"${root}/system.ini"
  out="$(XFF_TEST_SYSTEM_CONFIG="${root}/system.ini" "$(_bin)" --rg TODO "${root}" 2>&1)" && rc=0 || rc=$?
  expect_eq 2 "${rc}"
  expect_matches 'command.line' "${out}"
}

test_runner
