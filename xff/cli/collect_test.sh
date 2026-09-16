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
# End-to-end test of `-collect[=NAME]` (see TODO.md's result-set shaping design). The load-bearing
# behaviour is that ORDER selects the reading, which is the entire argument for a primary over a
# global: `-collect -first 3 --summary` summarises everything while listing three, and
# `-first 3 -collect --summary` summarises only the three. Both are asserted on the summary's COUNT,
# because that is the number the two readings disagree about. `--sort=dir` throughout: which three
# you get follows the order.

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

# Six files of known, distinct sizes (10, 20, ... 60 bytes), so the summary's total says exactly
# which entries reached it: 210 for all six, 60 for the first three. `test_tmpdir` allocates each
# fixture under bashtest's managed scratch root without leaking a test name into printed paths.
_make_tree() {
  local path i
  path="$(test_tmpdir tree)"
  for i in 1 2 3 4 5 6; do
    head -c "$((i * 10))" /dev/zero | tr '\0' 'x' >"${path}/f${i}.txt"
  done
  echo "${path}"
}

test::collect_before_first_summarises_everything_but_lists_the_cap() {
  # "collect all, show a few, summarise all" - the reading no truncating test can express alone,
  # because a false test removes the entry from every sink.
  local root out
  root="$(_make_tree)"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -collect -first 3 -println --summary --sort=dir)"
  # Exactly the first three are listed; naming them beats counting lines, because it also pins WHICH
  # three the cap kept (which follows --sort).
  expect_output_contains "f1.txt" "${out}"
  expect_output_contains "f2.txt" "${out}"
  expect_output_contains "f3.txt" "${out}"
  expect_output_not_contains "f4.txt" "${out}"
  # ... while the summary still sees all six (10+20+...+60 == 210 bytes).
  expect_matches "total[[:space:]]+6[[:space:]]+100.00%[[:space:]]+210" "${out}"
}

test::first_before_collect_summarises_only_the_cap() {
  # The other order: only the three survive into the collection, so the summary reports three.
  local root out
  root="$(_make_tree)"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -first 3 -collect --summary --sort=dir)"
  expect_matches "total[[:space:]]+3[[:space:]]+100.00%[[:space:]]+60" "${out}"
}

test::collect_is_an_action_so_it_suppresses_the_implicit_print() {
  # Summary-only falls out of find's existing implicit-print rule rather than needing a --quiet.
  local root out
  root="$(_make_tree)"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -collect --summary --sort=dir)"
  expect_not_matches "f[0-9]\.txt" "${out}"
  expect_matches "total[[:space:]]+6" "${out}"
}

test::a_named_collection_reads_like_the_default_one() {
  local root out
  root="$(_make_tree)"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -collect:big --summary --sort=dir)"
  expect_matches "total[[:space:]]+6[[:space:]]+100.00%[[:space:]]+210" "${out}"
}

test::sharing_a_name_needs_the_bang_modifier() {
  # Sharing is supported but must be explicit: `!` on the later node. Per node rather than a whole-run
  # flag, so it cannot quietly loosen the other -collect in a long command.
  local root out status
  root="$(_make_tree)"
  status=0
  out="$(cd "${root}" && "$(_xff_bin)" --exact . \( -type f -collect:all \) -o \( -type d -collect:!all \) --summary --sort=dir)" || status=$?
  expect_eq "0" "${status}"
  # The six files plus the root directory, gathered into one collection by two different nodes.
  expect_matches "total[[:space:]]+7" "${out}"
}

test::an_unmarked_repeat_is_an_error_naming_the_modifier() {
  # A silently shared collection shows up only as a doubled total, so it fails closed and the message
  # spells the fix.
  local root out status
  root="$(_make_tree)"
  status=0
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -collect:a -collect:a --summary 2>&1)" || status=$?
  expect_eq "2" "${status}"
  expect_output_contains "duplicate -collect name 'a'" "${out}"
  expect_output_contains "-collect:!a" "${out}"
}

test::a_marked_repeat_collects_each_entry_twice() {
  # What `!` opts into: both nodes append, so an entry reached by both is collected twice.
  local root out
  root="$(_make_tree)"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -collect:a -collect:!a --summary --sort=dir)"
  expect_matches "total[[:space:]]+12[[:space:]]+100.00%[[:space:]]+420" "${out}"
}

test::a_name_must_be_an_identifier() {
  # Punctuation in a NAME is reserved for modifiers (that is what makes `!` unambiguous), and a name is
  # referenced as an identifier anyway.
  local root out status
  root="$(_make_tree)"
  status=0
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -collect:bad-name --summary 2>&1)" || status=$?
  expect_eq "2" "${status}"
  expect_output_contains "is not a NAME" "${out}"
}

test::an_empty_name_is_a_usage_error() {
  local root out status
  root="$(_make_tree)"
  status=0
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -collect: --summary 2>&1)" || status=$?
  expect_eq "2" "${status}"
  expect_output_contains "needs a NAME" "${out}"
}

test::collect_in_an_unreachable_branch_still_switches_the_source() {
  # Presence is SYNTACTIC, exactly like find's implicit -print: the summary reads the collection
  # even when no -collect ever executed, so it is empty. Surprising once, hence a test and a
  # documented example rather than a discovery.
  local root out
  root="$(_make_tree)"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -a -false -collect --summary --sort=dir)"
  expect_not_matches "total[[:space:]]+6" "${out}"
}

test::the_find_style_rejects_collect() {
  local root out status
  root="$(_make_tree)"
  status=0
  out="$(cd "${root}" && "$(_xff_bin)" --exact --config=find . -collect 2>&1)" || status=$?
  expect_eq "2" "${status}"
  expect_output_contains "xff extension" "${out}"
}

test::buffer_bounds_the_collection_and_overflow_is_an_error() {
  # A collection holds every match until the walk ends, so --buffer bounds it. Overflow FAILS rather
  # than truncating: a summary computed over part of the walk looks exactly like a correct one.
  local root out status
  root="$(_make_tree)"
  status=0
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -collect --buffer=2 --summary --sort=dir 2>&1)" || status=$?
  expect_eq "2" "${status}"
  expect_output_contains "exceeded --buffer" "${out}"
  expect_output_contains "2 rows" "${out}"
}

test::a_byte_budget_bounds_the_collection_too() {
  local root out status
  root="$(_make_tree)"
  status=0
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -collect --buffer=10B --summary --sort=dir 2>&1)" || status=$?
  expect_eq "2" "${status}"
  expect_output_contains "10 bytes" "${out}"
}

test::a_sufficient_buffer_collects_everything() {
  local root out
  root="$(_make_tree)"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -collect --buffer=100 --summary --sort=dir)"
  expect_matches "total[[:space:]]+6[[:space:]]+100.00%[[:space:]]+210" "${out}"
}

test::without_a_buffer_flag_the_collection_is_unbounded() {
  # The default is deliberately NO cap: a number chosen here would be a guess. --buffer is how a run
  # that would exhaust memory is made to say so instead.
  local root out
  root="$(_make_tree)"
  out="$(cd "${root}" && "$(_xff_bin)" --exact . -type f -collect --summary --sort=dir)"
  expect_matches "total[[:space:]]+6[[:space:]]+100.00%[[:space:]]+210" "${out}"
}

test_runner
