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
# End-to-end test of per-directory ignore files (--ignore-files): .ignore and
# .xffignore are respected only when opted in (find-compatible default is off),
# stack per directory (a deeper file overrides a shallower one), prune directories,
# and are all disabled by -u / --no-ignore. Drives the real binary.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

# `test_tmpdir` allocates each tree under bashtest's managed scratch root. Its random suffix keeps
# test names out of printed paths, where a name could accidentally satisfy a negative assertion.

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

# Tree: top.log, src/main.cc, src/debug.log, build/out.o.
#   <root>/.xffignore      => *.log       (ignore logs everywhere)
#   <root>/src/.xffignore  => !debug.log  (re-include debug.log in src/)
_make_tree() {
  local path
  path="$(test_tmpdir tree)"
  mkdir -p "${path}/src" "${path}/build"
  touch "${path}/top.log" "${path}/src/main.cc" "${path}/src/debug.log" "${path}/build/out.o"
  printf '*.log\n' >"${path}/.xffignore"
  printf '!debug.log\n' >"${path}/src/.xffignore"
  echo "${path}"
}

test::ignore_files_off_by_default() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" "${root}" -type f 2>&1)"
  expect_matches "(^|${NL}|/)top\.log(\$|${NL})" "${out}" # find-compatible: .xffignore not consulted
}

test::ignore_files_honored_when_enabled() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" --ignore-files "${root}" -type f 2>&1)"
  expect_not_matches "(^|${NL}|/)top\.log(\$|${NL})" "${out}" # dropped by the root *.log rule
  expect_matches "(^|${NL}|/)main\.cc(\$|${NL})" "${out}"     # unaffected
}

test::deeper_ignore_file_overrides_shallower() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" --ignore-files "${root}" -type f 2>&1)"
  # src/.xffignore's !debug.log re-includes it even though the root *.log would drop it.
  expect_matches "(^|${NL}|/)debug\.log(\$|${NL})" "${out}"
}

test::no_ignore_master_switch_disables() {
  local root out_u out_long
  root="$(_make_tree)"
  out_u="$("$(_xff_bin)" --ignore-files -u "${root}" -type f 2>&1)"
  out_long="$("$(_xff_bin)" --ignore-files --no-ignore "${root}" -type f 2>&1)"
  expect_matches "(^|${NL}|/)top\.log(\$|${NL})" "${out_u}"    # -u forces ignore processing off
  expect_matches "(^|${NL}|/)top\.log(\$|${NL})" "${out_long}" # --no-ignore likewise
}

test::ignore_file_prunes_a_directory() {
  local root out
  root="$(test_tmpdir tree)"
  mkdir -p "${root}"
  mkdir -p "${root}/keep" "${root}/skip"
  touch "${root}/keep/a" "${root}/skip/b"
  printf 'skip/\n' >"${root}/.xffignore" # directory-only rule
  out="$("$(_xff_bin)" --ignore-files "${root}" -type f 2>&1)"
  expect_not_matches "(^|${NL}|/)skip/b(\$|${NL})" "${out}" # skip/ pruned, its contents gone
  expect_matches "(^|${NL}|/)keep/a(\$|${NL})" "${out}"
}

test::dot_ignore_file_is_also_respected() {
  local root out
  root="$(test_tmpdir tree)"
  mkdir -p "${root}"
  touch "${root}/a.tmp" "${root}/a.cc"
  printf '*.tmp\n' >"${root}/.ignore" # the generic .ignore, not just .xffignore
  out="$("$(_xff_bin)" --ignore-files "${root}" -type f 2>&1)"
  expect_not_matches "(^|${NL}|/)a\.tmp(\$|${NL})" "${out}"
  expect_matches "(^|${NL}|/)a\.cc(\$|${NL})" "${out}"
}

test_runner
