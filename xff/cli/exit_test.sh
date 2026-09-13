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
# Binary-level test of the exit-code model (design.md review #9): the default is
# find semantics (0 ran / 2 error; match status never affects exit), while --quiet
# (suppress output, exit by match) and --exit-match (keep output, exit by match)
# make "1 = no match" reachable. An error always outranks match status (exit 2).
# Exit codes are inherently a binary-level concern, so this drives the real binary.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

# Path to the built xff binary in the test runfiles.
_xff_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/testing/xff"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff -path '*xff/cli/testing/xff' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

# A directory holding a single file, isolated from any ambient config.
_tree() {
  local dir
  dir="$(test_tmpdir "$1")"
  : >"${dir}/a.txt"
  echo "${dir}"
}

test::default_no_match_still_exits_zero() {
  local dir
  dir="$(_tree default)"
  # No --quiet/--exit-match: match status must not affect the exit (find semantics).
  # Nothing matches "nope.zzz", yet the run exits 0 because it ran without error.
  local rc
  XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -name 'nope.zzz' >/dev/null 2>&1 && rc=0 || rc=$?
  expect_eq "0" "${rc}"
}

test::quiet_match_exits_zero_with_no_output() {
  local dir out rc
  dir="$(_tree qmatch)"
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --quiet "${dir}" -name a.txt 2>/dev/null)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_eq "" "${out}" # --quiet suppresses output
}

test::quiet_no_match_exits_one() {
  local dir out rc
  dir="$(_tree qnomatch)"
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --quiet "${dir}" -name 'nope.zzz' 2>/dev/null)" && rc=0 || rc=$?
  expect_eq "1" "${rc}"
  expect_eq "" "${out}"
}

test::exit_match_keeps_output_on_match() {
  local dir out rc
  dir="$(_tree emmatch)"
  # --exit-match exits by match but, unlike --quiet, keeps the normal output.
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --exit-match "${dir}" -name a.txt 2>/dev/null)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  local lines=()
  local line
  while IFS= read -r line; do lines+=("${line}"); done <<<"${out}"
  expect_contains "${dir}/a.txt" "${lines[@]}"
}

test::exit_match_no_match_exits_one() {
  local dir rc
  dir="$(_tree emnomatch)"
  XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --exit-match "${dir}" -name 'nope.zzz' >/dev/null 2>&1 && rc=0 || rc=$?
  expect_eq "1" "${rc}"
}

test::error_outranks_match_status() {
  local rc
  # A missing root is a per-path error; even under --quiet (where no match would be
  # exit 1) the error wins and the exit is 2.
  XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --quiet "${TEST_TMPDIR}/does-not-exist" >/dev/null 2>&1 && rc=0 || rc=$?
  expect_eq "2" "${rc}"
}

test::dash_q_is_a_grep_alias_of_quiet() {
  local dir out rc
  dir="$(_tree qalias)"
  # -q behaves exactly like --quiet: suppress output, exit by match.
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" -q "${dir}" -name a.txt 2>/dev/null)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_eq "" "${out}"
  XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" -q "${dir}" -name 'nope.zzz' >/dev/null 2>&1 && rc=0 || rc=$?
  expect_eq "1" "${rc}"
}

test::unknown_global_flag_is_a_usage_error() {
  local dir out rc
  dir="$(_tree unknownflag)"
  # An unrecognized leading option is a usage error (exit 2), not silently ignored.
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --bogus-flag "${dir}" 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "unknown option" "${out}" # prominent, actionable message
  expect_output_contains "--bogus-flag" "${out}"   # names the offending flag
}

test::unknown_flag_value_is_a_usage_error() {
  # A typo in a VALUE used to select the default silently: `--case=insensitve` matched
  # case-sensitively and the run looked like it worked. Now it is a usage error naming the flag,
  # the bad value, and what the flag accepts.
  local dir out rc
  dir="$(_tree unknownvalue)"
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --case=insensitve "${dir}" 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "unknown value" "${out}"
  expect_output_contains "insensitve" "${out}" # names the offending value
  expect_output_contains "--case" "${out}"     # and the flag
  expect_output_contains "sensitive" "${out}"  # and what it accepts, from the flag's own table
}

test::a_free_text_value_is_never_rejected() {
  # Most valued flags take a path / format / regex, so the check must stay out of their way.
  local dir rc
  dir="$(_tree freetext)"
  XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --define=A=B --template='{path}' "${dir}" >/dev/null 2>&1 \
    && rc=0 || rc=$?
  expect_eq "0" "${rc}"
}

test::the_shared_vocabulary_spellings_are_accepted() {
  # A tri-state flag takes more than its table documents (yes / 1 beside on), so the check must
  # use the shared parser rather than the table.
  local dir rc spelling
  dir="$(_tree vocabulary)"
  for spelling in on off yes no true false 1 0 auto; do
    XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "--gitignore=${spelling}" "${dir}" >/dev/null 2>&1 \
      && rc=0 || rc=$?
    expect_eq "0" "${rc}"
  done
}

test::known_global_flag_is_accepted() {
  local dir rc
  dir="$(_tree knownflag)"
  # A valid leading global (--sort) is accepted; the run exits 0.
  XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --sort "${dir}" -name a.txt >/dev/null 2>&1 && rc=0 || rc=$?
  expect_eq "0" "${rc}"
}

test_runner
