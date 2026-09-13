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
# Binary-level test of -cmp (#88): the byte-exact content-comparison matcher.
# TRUE = identical bytes, so `! -cmp` lists files that differ; the TARGET is a field
# template ({def.B}/{relpath}), enabling tree-vs-tree change detection; and -cmp is an
# xff extension the find style rejects. Drives the real binary (reads files on disk).

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

# `test_tmpdir` allocates each tree under bashtest's managed scratch root. Its random suffix keeps
# test names out of printed paths, where a name could accidentally satisfy a negative assertion.

# A real newline for line-anchored expect_matches patterns (whole-text [[ =~ ]]).
NL=$'\n'

_xff_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/testing/xff"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff -path '*xff/cli/testing/xff' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

# Trees A and B: same.txt is byte-identical in both; diff.txt differs; only.txt is
# A-only (no counterpart under B). Echoes "A B".
_make_trees() {
  local a b
  a="$(test_tmpdir tree)"
  b="$(test_tmpdir tree)"
  printf 'hello\n' >"${a}/same.txt"
  printf 'hello\n' >"${b}/same.txt"
  printf 'left\n' >"${a}/diff.txt"
  printf 'right\n' >"${b}/diff.txt"
  printf 'orphan\n' >"${a}/only.txt"
  echo "${a} ${b}"
}

test::cmp_true_for_identical_false_for_differing() {
  local a b out
  read -r a b <<<"$(_make_trees)"
  # -cmp TARGET is TRUE (same) -> the default -print lists it. same.txt matches its twin.
  out="$("$(_xff_bin)" "${a}" -name same.txt -cmp "${b}/same.txt" 2>&1)"
  expect_matches "(^|${NL}|/)same\.txt(\$|${NL})" "${out}"
  # diff.txt differs -> -cmp false -> not printed.
  out="$("$(_xff_bin)" "${a}" -name diff.txt -cmp "${b}/diff.txt" 2>&1)"
  expect_not_matches "(^|${NL}|/)diff\.txt(\$|${NL})" "${out}"
}

test::bang_cmp_lists_changed_files_across_a_parallel_tree() {
  local a b out
  read -r a b <<<"$(_make_trees)"
  # ! -cmp '{def.B}/{relpath}': files under A that differ from their B counterpart.
  # diff.txt differs; only.txt has no B counterpart (missing -> differs); same.txt matches.
  out="$("$(_xff_bin)" --define=B="${b}" "${a}" -type f '!' -cmp '{def.B}/{relpath}' 2>&1)"
  expect_matches "(^|${NL}|/)diff\.txt(\$|${NL})" "${out}"
  expect_matches "(^|${NL}|/)only\.txt(\$|${NL})" "${out}"
  expect_not_matches "(^|${NL}|/)same\.txt(\$|${NL})" "${out}"
}

test::cmp_is_rejected_in_strict_find_style() {
  local a b rc
  read -r a b <<<"$(_make_trees)"
  "$(_xff_bin)" --config=find "${a}" -cmp "${b}/same.txt" >/dev/null 2>&1 && rc=0 || rc=$?
  expect_eq "2" "${rc}" # -cmp is an xff extension -> usage error under --config=find
}

test::help_topic_documents_cmp() {
  expect_output_contains "byte-identical" "$("$(_xff_bin)" --help=-cmp 2>&1)"
}

test_runner
