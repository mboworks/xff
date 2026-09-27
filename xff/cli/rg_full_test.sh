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
# Exercise rg integration with the full extension set, including root archive probing.
set -euo pipefail
# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

_bin() { echo "${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/testing/xff_full"; }
_check() {
  local expected_rc="$1" expected="$2" out rc
  shift 2
  out="$("$(_bin)" "$@" 2>&1)" && rc=0 || rc=$?
  expect_eq "${expected_rc}" "${rc}"
  expect_eq "${expected}" "${out}"
}

test::full_rg_invocation_reads_piped_input_with_pcre2() {
  local root out
  root="$(test_tmpdir invocation)"
  ln -s "$(_bin)" "${root}/rg"
  out="$(printf 'hit\nmiss\n' | "${root}/rg" -Pox 'h|hit')"
  expect_eq hit "${out}"
}

test::stdin_search_survives_automatic_archive_probing() {
  printf 'hit\nmiss\n' | _check 0 hit --rg hit
  printf 'hit\nmiss\n' | _check 0 hit --rg hit -
  printf 'miss\n' | _check 1 '' --rg hit
  printf 'miss\n' | _check 1 '' --rg hit -
  printf '' | _check 1 '' --rg hit -
}

test::whole_line_pcre2_preserves_alternation_and_match_portions() {
  printf 'hit\nhits\n' | _check 0 hit --rg -Pox 'h|hit'
  printf 'hit\nhits\n' | _check 0 1 --rg -Px --count-matches 'h|hit'
}

test::archive_members_have_automatic_prefixes_with_explicit_overrides() {
  local root
  root="$(test_tmpdir archive)"
  printf 'hit\n' >"${root}/one.txt"
  printf 'hit\n' >"${root}/two.txt"
  COPYFILE_DISABLE=1 tar -cf "${root}/box.tar" -C "${root}" one.txt two.txt
  _check 0 "${root}/box.tar!one.txt:hit"$'\n'"${root}/box.tar!two.txt:hit" \
    --rg hit "${root}/box.tar" --archive=roots --sort=dir
  _check 0 $'hit\nhit' --rg -I hit "${root}/box.tar" --archive=roots
  _check 0 "${root}/one.txt:hit" --rg -H hit "${root}/one.txt"
  _check 0 hit --rg hit "${root}/one.txt"
  printf '%s\n' '--no-filename' >"${root}/user.ini"
  XFF_TEST_USER_CONFIG="${root}/user.ini" \
    _check 0 $'hit\nhit' --rg hit "${root}/box.tar" --archive=roots
}

test::named_search_roots_feed_packing_and_summaries() {
  local root out
  root="$(test_tmpdir named-roots)"
  mkdir -p "${root}/left" "${root}/right"
  printf 'hit left\n' >"${root}/left/same.txt"
  printf 'hit right\n' >"${root}/right/same.txt"
  "$(_bin)" --rg hit --root="left=${root}/left" --root="right=${root}/right" --pack="${root}/result.tar"
  expect_eq 'hit left' "$(tar -xOf "${root}/result.tar" left/same.txt)"
  expect_eq 'hit right' "$(tar -xOf "${root}/result.tar" right/same.txt)"
  out="$("$(_bin)" --rg hit --root="left=${root}/left" --root="right=${root}/right" --summary --summary-scope=root)"
  expect_output_contains "${root}/left" "${out}"
  expect_output_contains "${root}/right" "${out}"
  expect_output_contains 'Summary scope:' "${out}"
}

test::parallel_pcre2_and_default_archive_probing_preserve_ordered_output() {
  local root index serial parallel
  root="$(test_tmpdir parallel)"
  for ((index = 0; index < 300; ++index)); do
    printf 'before\nhit hit\nafter\n' >"${root}/${index}.txt"
  done
  # Default archive-root probing must not disable host-file content workers.
  serial="$("$(_bin)" --rg -j1 -P -C1 --sort=dir 'h|hit' "${root}")"
  parallel="$("$(_bin)" --rg -j4 -P -C1 --sort=dir 'h|hit' "${root}")"
  expect_eq "${serial}" "${parallel}"
  serial="$("$(_bin)" --rg -j1 -P --count-matches --sort=dir 'h|hit' "${root}")"
  parallel="$("$(_bin)" --rg -j4 -P --count-matches --sort=dir 'h|hit' "${root}")"
  expect_eq "${serial}" "${parallel}"
  COPYFILE_DISABLE=1 tar -cf "${root}/box.tar" -C "${root}" 0.txt 1.txt
  serial="$("$(_bin)" --rg -j1 -P --sort=dir hit "${root}" "${root}/box.tar")"
  parallel="$("$(_bin)" --rg -j4 -P --sort=dir hit "${root}" "${root}/box.tar")"
  expect_eq "${serial}" "${parallel}"
  expect_output_contains "${root}/box.tar!0.txt:hit hit" "${parallel}"
}

test::pcre2_multiline_uses_the_shared_context_and_count_renderers() {
  local root out
  root="$(test_tmpdir multiline)"
  printf 'pre\nalpha one\nbeta two\npost\n' >"${root}/input"
  out="$("$(_bin)" --rg -PUn -C1 'alpha[^\n]*\nbeta' "${root}/input")"
  expect_eq $'1-pre\n2:alpha one\n3:beta two\n4-post' "${out}"
  out="$("$(_bin)" --rg -PUc 'alpha[^\n]*\nbeta' "${root}/input")"
  expect_eq '1' "${out}"
}

test::bundled_language_candidates_work_in_rg_and_native_filters() {
  local root
  root="$(test_tmpdir bundled-types)"
  printf 'hit header\n' >"${root}/header.h"
  printf 'hit script\n' >"${root}/script.pl"
  _check 0 'hit header' --rg -I -tcpp hit "${root}" --xff -lang Objective-C
  _check 0 'hit script' --rg -I -tProlog -tRaku hit "${root}" --xff -lang Perl
  _check 0 'header.h|C' "${root}" -lang C++ -printf '%f|%{lang}\n'
}

test::archive_members_remain_discovered_files_under_explicit_archive_roots() {
  local root out
  root="$(test_tmpdir archive-types)"
  printf 'hit source\n' >"${root}/one.cc"
  printf 'hit text\n' >"${root}/one.txt"
  COPYFILE_DISABLE=1 tar -cf "${root}/box.tar" -C "${root}" one.cc one.txt
  out="$("$(_bin)" --rg -I -tcpp hit "${root}/box.tar")"
  expect_eq 'hit source' "${out}"
  out="$("$(_bin)" --file-type=cpp "${root}/box.tar" -type f -printf '%f\n')"
  # The explicitly named archive itself remains selected; only discovered members are filtered.
  expect_eq $'box.tar\none.cc' "${out}"
}

test_runner
