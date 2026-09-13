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
# End-to-end test of -diff (via mbo::diff): a unified diff by default, the -diff:STYLE output
# formats, TRUE = same polarity (silent when equal), a binary side noted on stderr, and the
# --diff-algorithm usage errors. Drives the real binary (the diff is emitted by the action).

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

_xff_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/xff"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff -path '*xff/cli/xff' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

# The per-mode output (u/c/n/y/none) is golden-tested by the `diff_golden` targets: one
# committed expected-output file per mode, checked with mbo's diff_test. This bashtest covers
# what is not stdout-golden-able -- the TRUE = same polarity, --diff-algorithm acceptance, the
# binary stderr note, and the usage errors.

test::diff_polarity_true_when_equal_false_when_different() {
  local dir out
  dir="$(test_tmpdir diffpol)"
  mkdir -p "${dir}"
  printf 'one\ntwo\n' >"${dir}/a.txt"
  printf 'one\nTWO\n' >"${dir}/b.txt"
  printf 'one\ntwo\n' >"${dir}/same.txt"
  # Equal -> -diff is silent (no diff) but TRUE, so a trailing -print fires.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -name a.txt -diff "${dir}/same.txt" -print 2>&1)"
  expect_output_not_contains '@@' "${out}"
  expect_output_contains 'a.txt' "${out}"
  # Different -> -diff:none is the silent matcher and FALSE, so -print does not fire.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -name a.txt -diff:none "${dir}/b.txt" -print 2>&1)"
  expect_output_not_contains 'a.txt' "${out}"
  # A valid --diff-algorithm is accepted and still produces the diff.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --diff-algorithm=naive "${dir}" -name a.txt -diff "${dir}/b.txt" 2>&1)"
  expect_output_contains '+TWO' "${out}"
}

test::diff_ignore_normalizes_and_rejects_bad_values() {
  local dir out rc
  dir="$(test_tmpdir diffign)"
  mkdir -p "${dir}"
  printf 'one\ntwo   \nthree\n' >"${dir}/left.txt" # trailing whitespace on line 2
  printf 'one\ntwo\nthree\n' >"${dir}/right.txt"
  printf 'keep\nDEBUG x\nkeep2\n' >"${dir}/ml.txt"
  printf 'keep\nDEBUG y\nkeep2\n' >"${dir}/mr.txt"
  printf 'a\nb' >"${dir}/nonl.txt"     # no final newline
  printf 'a\nb\n' >"${dir}/withnl.txt" # same content, with a final newline
  # Without normalization the trailing whitespace differs -> -diff:none is FALSE, no -print.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -name left.txt -diff:none "${dir}/right.txt" -print 2>&1)"
  expect_output_not_contains 'left.txt' "${out}"
  # --diff-ignore=trail folds the trailing whitespace -> equal -> -print fires.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --diff-ignore=trail "${dir}" -name left.txt -diff:none "${dir}/right.txt" -print 2>&1)"
  expect_output_contains 'left.txt' "${out}"
  # --diff-ignore-matching drops the differing DEBUG line before comparing -> equal.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --diff-ignore-matching='^DEBUG' "${dir}" -name ml.txt -diff:none "${dir}/mr.txt" -print 2>&1)"
  expect_output_contains 'ml.txt' "${out}"
  # A missing final newline differs by default (-> no -print), but --diff-ignore=eofnl equates them.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -name nonl.txt -diff:none "${dir}/withnl.txt" -print 2>&1)"
  expect_output_not_contains 'nonl.txt' "${out}"
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --diff-ignore=eofnl "${dir}" -name nonl.txt -diff:none "${dir}/withnl.txt" -print 2>&1)"
  expect_output_contains 'nonl.txt' "${out}"
  # An unknown token is a usage error (eol/lead are not tokens: trail and change/ws cover them).
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --diff-ignore=eol "${dir}" -name left.txt -diff "${dir}/right.txt" 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains 'unknown --diff-ignore token' "${out}"
  # A malformed --diff-ignore-matching regex is a usage error (one clean line, no RE2 log noise).
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --diff-ignore-matching='[' "${dir}" -name left.txt -diff "${dir}/right.txt" 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains 'invalid --diff-ignore-matching regex' "${out}"
}

test::diff_ignore_is_configurable_and_cli_wins() {
  local cfg dir out rc
  dir="$(test_tmpdir diffcfg)"
  mkdir -p "${dir}"
  printf 'one\ntwo   \nthree\n' >"${dir}/left.txt"
  printf 'one\ntwo\nthree\n' >"${dir}/right.txt"
  printf 'keep\nDEBUG x\nkeep2\n' >"${dir}/ml.txt"
  printf 'keep\nDEBUG y\nkeep2\n' >"${dir}/mr.txt"

  cfg="${dir}/xffrc"
  printf -- '--diff-ignore=trail --diff-ignore-matching=^DEBUG\n' >"${cfg}"
  out="$(XFF_CONFIG="${cfg}" "$(_xff_bin)" "${dir}" -name left.txt -diff:none "${dir}/right.txt" -print 2>&1)"
  expect_output_contains 'left.txt' "${out}"
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --xffrc="${cfg}" "${dir}" -name ml.txt -diff:none "${dir}/mr.txt" -print 2>&1)"
  expect_output_contains 'ml.txt' "${out}"

  # Resolved config globals are prepended, so the command line remains the highest-precedence
  # tier: an empty CLI value disables the configured normalization.
  out="$(XFF_CONFIG="${cfg}" "$(_xff_bin)" --diff-ignore= "${dir}" -name left.txt -diff:none "${dir}/right.txt" -print 2>&1)"
  expect_output_not_contains 'left.txt' "${out}"

  # Config values use the same pre-walk validation as CLI values.
  printf -- '--diff-ignore=bogus\n' >"${cfg}"
  out="$(XFF_CONFIG="${cfg}" "$(_xff_bin)" "${dir}" -name left.txt -diff:none "${dir}/right.txt" 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains 'unknown --diff-ignore token' "${out}"
}

test::diff_binary_notes_on_stderr_and_bad_inputs_are_usage_errors() {
  local dir out rc
  dir="$(test_tmpdir diffbin)"
  mkdir -p "${dir}"
  printf 'x\000one' >"${dir}/p.bin" # embedded NUL -> binary
  printf 'x\000two' >"${dir}/q.bin"
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -name p.bin -diff "${dir}/q.bin" 2>&1)"
  expect_output_contains 'Binary files' "${out}" # byte-compared, not text-diffed
  # A bad -diff:STYLE is a usage error.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -name p.bin -diff:zz "${dir}/q.bin" 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains 'unknown style' "${out}"
  # A bad --diff-algorithm is a usage error.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --diff-algorithm=bogus "${dir}" -name p.bin -diff "${dir}/q.bin" 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  # The shared value check fires before the diff resolver's own message, and lists the accepted
  # values from the flag's table - so this asserts the generic wording plus the offending value.
  expect_output_contains 'unknown value' "${out}"
  expect_output_contains '--diff-algorithm' "${out}"
}

test::diff_format_and_context_globals() {
  local dir out rc
  dir="$(test_tmpdir difffmt)"
  mkdir -p "${dir}"
  printf 'a\nb\nc\nd\ne\nf\ng\n' >"${dir}/one.txt"
  printf 'a\nb\nc\nX\ne\nf\ng\n' >"${dir}/two.txt" # one changed line (line 4)
  # --diff-format=normal emits the `NcN` normal format (no unified `@@` hunk header).
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --diff-format=normal "${dir}" -name one.txt -diff "${dir}/two.txt" 2>&1)"
  expect_output_contains '4c4' "${out}"
  expect_output_not_contains '@@' "${out}"
  # --diff-context=1 narrows the unified hunk to one line of context on each side.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --diff-context=1 "${dir}" -name one.txt -diff "${dir}/two.txt" 2>&1)"
  expect_output_contains '@@ -3,3 +3,3 @@' "${out}"
  # A bad --diff-format is a usage error.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --diff-format=bogus "${dir}" -name one.txt -diff "${dir}/two.txt" 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains 'unknown value' "${out}"
  expect_output_contains '--diff-format' "${out}"
  # A bad --diff-context is a usage error.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --diff-context=x "${dir}" -name one.txt -diff "${dir}/two.txt" 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains 'bad --diff-context value' "${out}"
}

test_runner
