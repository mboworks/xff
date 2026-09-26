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

test_runner
