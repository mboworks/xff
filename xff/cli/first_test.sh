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
# End-to-end test of `-first N` (see TODO.md's result-set shaping design). The behaviours that
# matter are the count, that each USE keeps its own budget, and that a false test removes the entry
# from the sinks too - that last one is a documented consequence, not an accident, so it is pinned
# here to keep anyone from "fixing" it. `--sort=dir` throughout: which N you get follows the order,
# so an unsorted walk would make these assertions platform-dependent.

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

# Six files and three directories, so a cap is visibly smaller than the set.
# `test_tmpdir` allocates each tree under bashtest's managed scratch root. Its random suffix keeps
# test names out of printed paths, where a name could accidentally satisfy a negative assertion.
_make_tree() {
  local path
  path="$(test_tmpdir tree)"
  mkdir -p "${path}/d1" "${path}/d2" "${path}/d3"
  local i
  for i in 1 2 3 4 5 6; do
    : >"${path}/f${i}.txt"
  done
  echo "${path}"
}

test::first_caps_the_result_set() {
  local root out
  root="$(_make_tree)"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -first 3 --sort=dir)"
  expect_eq "3" "$(wc -l <<<"${out}" | tr -d ' ')"
}

test::first_keeps_a_budget_per_use_not_per_run() {
  # The reason -first is a primary and not a global: two instances, two budgets. A whole-run flag
  # could only ever hold one, so this command would be inexpressible.
  local root out
  root="$(_make_tree)"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . \( -type f -first 2 \) -o \( -type d -first 2 \) --sort=dir)"
  expect_eq "4" "$(wc -l <<<"${out}" | tr -d ' ')" # 2 files AND 2 directories
  expect_matches "f1\.txt" "${out}"
  expect_matches "f2\.txt" "${out}"
  expect_not_matches "f3\.txt" "${out}"
}

test::a_capped_entry_is_gone_from_the_summary_too() {
  # DOCUMENTED consequence: a test returning false removes the entry from every sink, so -first
  # narrows what --summary counts. This is why -collect exists (collect first, then cap). If this
  # ever reports 6, the filter has stopped being a filter.
  local root out
  root="$(_make_tree)"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -first 2 --summary --sort=dir 2>&1)"
  expect_output_contains "2" "${out}"
  expect_output_not_contains "total  6" "${out}"
}

test::first_rejects_a_count_it_cannot_read() {
  # A typo is an ERROR, not an empty result set. Matching nothing would be indistinguishable from a
  # tree with no matches, so `-first nope` would look like a working command that found nothing.
  local root out rc
  root="$(_make_tree)"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -first nope 2>&1)" && rc=0 || rc="${?}"
  expect_eq "2" "${rc}"
  expect_output_contains "expects a count" "${out}"
  expect_output_contains "nope" "${out}" # the message quotes what was actually typed
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -first -3 2>&1)" && rc=0 || rc="${?}"
  expect_eq "2" "${rc}"
  expect_output_contains "cannot be negative" "${out}"
}

test::first_zero_is_valid_and_means_none() {
  # Unlike a typo, 0 is unambiguous: the caller asked for no results and gets them.
  local root out rc
  root="$(_make_tree)"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -first 0 --sort=dir)" && rc=0 || rc="${?}"
  expect_eq "0" "${rc}"
  expect_eq "" "${out}"
}

test::first_is_an_xff_extension_the_find_style_rejects() {
  local root out rc
  root="$(_make_tree)"
  out="$(cd "${root}" && "$(_xff_bin)" --config=find . -type f -first 3 2>&1)" && rc=0 || rc="${?}"
  expect_eq "2" "${rc}"
  expect_output_contains "xff extension" "${out}"
}

test_runner
