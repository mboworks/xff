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
# End-to-end test of the --exclude / --include run-level filter (the first
# user-facing slice of the ignore family): a gitignore-style glob drops matching
# files, a matched directory is pruned, and --include re-includes with the
# gitignore last-match-wins rule. Drives the real binary; asserts on the paths.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

# A real newline for line-anchored expect_matches patterns: expect_matches matches
# the whole text ([[ =~ ]]), so `^`/`$` anchor the whole output, not a line.
# `(^|${NL})X` and `X($|${NL})` restore grep's per-line anchoring.
NL=$'\n'

_xff_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/testing/xff"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff -path '*xff/cli/testing/xff' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

# A fresh tree per test: src/main.cc, src/util.log, build/out.o,
# node_modules/pkg/index.js, keep.log.
# `test_tmpdir` allocates each tree under bashtest's managed scratch root. Its random suffix keeps
# test names out of printed paths, where a name could accidentally satisfy a negative assertion.
_make_tree() {
  local path
  path="$(test_tmpdir tree)"
  mkdir -p "${path}/src" "${path}/build" "${path}/node_modules/pkg"
  touch "${path}/src/main.cc" "${path}/src/util.log" "${path}/build/out.o" \
    "${path}/node_modules/pkg/index.js" "${path}/keep.log"
  echo "${path}"
}

test::exclude_glob_drops_matching_files() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" --exclude='*.log' "${root}" -type f 2>&1)"
  expect_not_matches "(^|${NL}|/)util\.log(\$|${NL})" "${out}"
  expect_not_matches "(^|${NL}|/)keep\.log(\$|${NL})" "${out}"
  expect_matches "(^|${NL}|/)main\.cc(\$|${NL})" "${out}" # a non-matching file stays
}

test::exclude_prunes_a_matching_directory() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" --exclude=build --exclude=node_modules "${root}" -type f 2>&1)"
  expect_not_matches "(^|${NL}|/)out\.o(\$|${NL})" "${out}"    # build/ pruned
  expect_not_matches "(^|${NL}|/)index\.js(\$|${NL})" "${out}" # node_modules/ pruned
  expect_matches "(^|${NL}|/)main\.cc(\$|${NL})" "${out}"
}

test::include_reincludes_with_last_match_wins() {
  local root out
  root="$(_make_tree)"
  # keep.log matches both the *.log exclude and the later keep.log include; the
  # include is last, so it wins and keep.log survives while util.log stays dropped.
  out="$("$(_xff_bin)" --exclude='*.log' --include='keep.log' "${root}" -type f 2>&1)"
  expect_matches "(^|${NL}|/)keep\.log(\$|${NL})" "${out}"
  expect_not_matches "(^|${NL}|/)util\.log(\$|${NL})" "${out}"
}

test::no_filter_lists_everything() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" "${root}" -type f 2>&1)"
  expect_matches "(^|${NL}|/)out\.o(\$|${NL})" "${out}"
  expect_matches "(^|${NL}|/)keep\.log(\$|${NL})" "${out}"
}

test::help_topic_documents_exclude() {
  local out
  out="$("$(_xff_bin)" --help=--exclude 2>&1)"
  expect_output_contains 'gitignore' "${out}"
}

test_runner
