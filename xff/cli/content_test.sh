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
# Binary-level test of the xff content-search predicates: -content / -icontent
# (literal substring) and -rxc / -irxc (regex), the binary-file skip, and the
# find-style gating (these are xff extensions). Drives the real binary because the
# predicates read files on disk; anchors on the matched paths, not exact wording.

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

# A fresh tree per test: text files with known content plus one binary file whose
# NUL byte marks it binary (so content search skips it even though it has a match).
_make_tree() {
  local path
  path="$(test_tmpdir tree)"
  printf 'alpha BETA gamma\n' >"${path}/a.txt"
  printf 'nothing to see here\n' >"${path}/b.txt"
  printf 'id=42 token=abc\n' >"${path}/c.log"
  {
    printf 'ELF'
    printf '\000'
    printf 'needle inside\n'
  } >"${path}/bin.dat"
  echo "${path}"
}

test::content_matches_literal_substring() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" "${root}" -content 'BETA' 2>&1)"
  expect_matches 'a\.txt' "${out}"
  expect_not_matches 'b\.txt' "${out}"
}

test::content_treats_the_pattern_as_literal_not_regex() {
  # '.' is a literal here; "alph." matches "alpha" as a regex but not as a substring.
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" "${root}" -content 'alph.' 2>&1)"
  expect_not_matches 'a\.txt' "${out}"
}

test::content_is_case_sensitive_icontent_folds() {
  local root out_cs out_ci
  root="$(_make_tree)"
  out_cs="$("$(_xff_bin)" "${root}" -content 'beta' 2>&1)"  # lower-case: no match
  out_ci="$("$(_xff_bin)" "${root}" -icontent 'beta' 2>&1)" # folds case: matches
  expect_not_matches 'a\.txt' "${out_cs}"
  expect_matches 'a\.txt' "${out_ci}"
}

test::rxc_matches_a_regular_expression() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" "${root}" -rxc 'id=[0-9]+' 2>&1)"
  expect_matches 'c\.log' "${out}"
  expect_not_matches 'a\.txt' "${out}"
}

test::binary_files_are_skipped() {
  # 'needle' is present in bin.dat, but its NUL byte marks it binary -> not matched.
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" "${root}" -content 'needle' 2>&1)"
  expect_not_matches 'bin\.dat' "${out}"
}

test::content_is_rejected_in_strict_find_style() {
  # -content is an xff extension; --config=find rejects it as a usage error (exit 2).
  local root out rc
  root="$(_make_tree)"
  out="$("$(_xff_bin)" --config=find "${root}" -content 'BETA' 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
}

test::text_and_binary_classify_regular_files() {
  # -text matches a text regular file, -binary a binary one; a directory is neither.
  local root out
  root="$(test_tmpdir tree)"
  mkdir -p "${root}"
  printf 'hello\n' >"${root}/plain.txt" # text
  printf 'a\0b' >"${root}/blob.dat"     # a NUL -> binary
  mkdir "${root}/adir"                  # neither text nor binary
  out="$("$(_xff_bin)" "${root}" -text 2>&1)"
  expect_matches 'plain\.txt' "${out}"
  expect_not_matches 'blob\.dat' "${out}"
  expect_not_matches 'adir' "${out}"
  out="$("$(_xff_bin)" "${root}" -binary 2>&1)"
  expect_matches 'blob\.dat' "${out}"
  expect_not_matches 'plain\.txt' "${out}"
}

test::eofnl_selects_newline_terminated_or_empty_files() {
  # -text -eofnl = well-formed text (ends in newline, or empty); -text ! -eofnl = the
  # missing-final-newline lint.
  local root out
  root="$(test_tmpdir tree)"
  mkdir -p "${root}"
  printf 'done\n' >"${root}/good.txt" # ends in newline
  printf 'oops' >"${root}/nonl.txt"   # no final newline
  : >"${root}/empty.txt"              # empty is complete (zero lines)
  out="$("$(_xff_bin)" "${root}" -text -eofnl 2>&1)"
  expect_matches 'good\.txt' "${out}"
  expect_matches 'empty\.txt' "${out}"
  expect_not_matches 'nonl\.txt' "${out}"
  out="$("$(_xff_bin)" "${root}" -text ! -eofnl 2>&1)"
  expect_matches 'nonl\.txt' "${out}"
  expect_not_matches 'good\.txt' "${out}"
}

test::eof_terminators_pin_the_final_line_ending() {
  # -eofnl / -eofcr / -eofcrlf each test one final terminator (LF / CR / CRLF). A CRLF file ends
  # in LF too, so -eofnl matches it while -eofcrlf is the strict form; a bare-CR file matches only
  # -eofcr. Distinct names (unix/dos/mac) avoid substring collisions in the matchers.
  local root out
  root="$(test_tmpdir tree)"
  mkdir -p "${root}"
  printf 'a\nb\n' >"${root}/unix.txt"    # ends LF
  printf 'a\r\nb\r\n' >"${root}/dos.txt" # ends CRLF
  printf 'a\rb\r' >"${root}/mac.txt"     # ends CR
  printf 'oops' >"${root}/nonl.txt"      # no terminator
  out="$("$(_xff_bin)" "${root}" -eofnl 2>&1)"
  expect_matches 'unix\.txt' "${out}"
  expect_matches 'dos\.txt' "${out}" # CRLF ends in LF, so -eofnl matches
  expect_not_matches 'mac\.txt' "${out}"
  expect_not_matches 'nonl\.txt' "${out}"
  out="$("$(_xff_bin)" "${root}" -eofcr 2>&1)"
  expect_matches 'mac\.txt' "${out}"
  expect_not_matches 'unix\.txt' "${out}"
  expect_not_matches 'dos\.txt' "${out}" # CRLF ends in LF, not a bare CR
  expect_not_matches 'nonl\.txt' "${out}"
  out="$("$(_xff_bin)" "${root}" -eofcrlf 2>&1)"
  expect_matches 'dos\.txt' "${out}"
  expect_not_matches 'unix\.txt' "${out}"
  expect_not_matches 'mac\.txt' "${out}"
  expect_not_matches 'nonl\.txt' "${out}"
}

test::text_flavors_pin_the_line_ending() {
  # Bare -text (=git) is line-ending-agnostic; =posix/=windows/=apple pin LF / CRLF / CR. Distinct
  # names (unix/dos/mac) avoid substring collisions in the matchers.
  local root out
  root="$(test_tmpdir tree)"
  mkdir -p "${root}"
  printf 'a\nb\n' >"${root}/unix.txt"    # LF
  printf 'a\r\nb\r\n' >"${root}/dos.txt" # CRLF
  printf 'a\rb\r' >"${root}/mac.txt"     # CR
  out="$("$(_xff_bin)" "${root}" -text:posix 2>&1)"
  expect_matches 'unix\.txt' "${out}"
  expect_not_matches 'dos\.txt' "${out}"
  expect_not_matches 'mac\.txt' "${out}"
  out="$("$(_xff_bin)" "${root}" -text:windows 2>&1)"
  expect_matches 'dos\.txt' "${out}"
  expect_not_matches 'unix\.txt' "${out}"
  out="$("$(_xff_bin)" "${root}" -text:apple 2>&1)"
  expect_matches 'mac\.txt' "${out}"
  expect_not_matches 'unix\.txt' "${out}"
  out="$("$(_xff_bin)" "${root}" -text:git 2>&1)" # git matches every line-ending style
  expect_matches 'unix\.txt' "${out}"
  expect_matches 'dos\.txt' "${out}"
  expect_matches 'mac\.txt' "${out}"
}

test::text_unknown_flavor_is_a_usage_error() {
  local root out rc
  root="$(test_tmpdir tree)"
  mkdir -p "${root}"
  : >"${root}/f"
  out="$("$(_xff_bin)" "${root}" -text:dos 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_matches 'unknown text flavor' "${out}"
}

test::text_binary_eofnl_are_rejected_in_strict_find_style() {
  # All three are xff extensions; --config=find rejects each (exit 2).
  local root rc
  root="$(test_tmpdir tree)"
  mkdir -p "${root}"
  : >"${root}/f"
  for pred in -text -binary -eofnl -eofcr -eofcrlf; do
    "$(_xff_bin)" --config=find "${root}" "${pred}" >/dev/null 2>&1 && rc=0 || rc=$?
    expect_eq "2" "${rc}"
  done
}

test::help_topic_documents_content() {
  # Self-documentation flows from the registry: --help=-content renders its summary.
  expect_output_contains "content" "$("$(_xff_bin)" --help=-content 2>&1)"
}

test_runner
