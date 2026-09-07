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
# End-to-end test of the extended `xff_full` binary (#85 / composable extras, #115). It asserts the
# dual-binary scaffolding - `xff_full` is a real, runnable binary whose `_full` invocation name maps
# back to the xff style (so it behaves exactly as `xff`) - and the PCRE2 extra. It is config-aware,
# so it passes in both builds: a plain build links no PCRE2 backend (--regextype=PCRE2 is a usage
# error, never a silent RE2 fallback), while a `--config=xff_full` build links it (PCRE2 patterns
# work). The full CI cell runs it under `--config=xff_full` to exercise the linked path.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

# `test_tmpdir` allocates each tree under bashtest's managed scratch root. Its random suffix keeps
# test names out of printed paths, where a name could accidentally satisfy a negative assertion.

_xff_full_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/xff_full"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff_full -path '*xff/cli/xff_full' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

test::full_binary_resolves_to_the_xff_style_via_argv0() {
  # `-grep` is an xff extension the find style rejects; that `xff_full` accepts it proves its
  # `_full` invocation name resolved to the xff style (DefaultStyleForProgram strips `_full`).
  local root out rc
  root="$(test_tmpdir tree)"
  mkdir -p "${root}"
  printf 'has TODO here\n' >"${root}/f.txt"
  out="$("$(_xff_full_bin)" "${root}" -type f -grep 'TODO' 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_matches '/f\.txt:1:has TODO here' "${out}"
}

test::full_binary_man_page_uses_its_invocation_name() {
  local out
  out="$("$(_xff_full_bin)" --pager=never --man)"
  expect_matches '^\.TH xff_full 1' "${out}"
  expect_output_contains $'.SH NAME\nxff_full \\- eXtended File Find' "${out}"
  expect_output_contains $'.SH SYNOPSIS\n.B xff_full' "${out}"
}

test::man_page_uses_a_symlink_invocation_name() {
  local dir out
  dir="$(test_tmpdir symlink)"
  ln -s "$(_xff_full_bin)" "${dir}/xff"
  out="$("${dir}/xff" --pager=never --man)"
  expect_matches '^\.TH xff 1' "${out}"
  expect_output_contains $'.SH NAME\nxff \\- eXtended File Find' "${out}"
  expect_output_contains $'.SH SYNOPSIS\n.B xff' "${out}"
}

test::full_binary_pcre2_works_when_the_extra_is_linked_else_errors() {
  # This test drives xff_full in whichever build config it was compiled with, so it passes both
  # ways: a plain build links no PCRE2 backend (--regextype=PCRE2 is a usage error), while a
  # `--config=xff_full` build links it (a PCRE2-only pattern - here a backreference RE2 rejects -
  # actually matches). Detect which by probing --regextype=PCRE2 on a bare root.
  local root out rc bin
  bin="$(_xff_full_bin)"
  root="$(test_tmpdir tree)"
  mkdir -p "${root}"
  printf 'the the fox\nunique here\n' >"${root}/f.txt"
  if "${bin}" --regextype=PCRE2 "${root}" >/dev/null 2>&1; then
    # PCRE2 is linked: a backreference (unsupported by RE2) matches the doubled word.
    out="$("${bin}" --regextype=PCRE2 "${root}" -type f -grep '(\w+) \1' 2>&1)" && rc=0 || rc=$?
    expect_eq "0" "${rc}"
    expect_matches '/f\.txt:1:the the fox' "${out}"
  else
    # PCRE2 is not linked (the extra is off): --regextype=PCRE2 is a usage error, never a fallback.
    out="$("${bin}" --regextype=PCRE2 "${root}" -grep 'x' 2>&1)" && rc=0 || rc=$?
    expect_eq "2" "${rc}"
    expect_matches 'not built into this binary' "${out}"
  fi
}

test_runner
