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
# Binary-level test of --pager. Drives the real binary because paging reads
# isatty(stdout): the test captures stdout into a variable (a pipe, not a tty),
# so auto never pages. A pass-through pager proves the piped text is the same bytes
# as the unpaged output.

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

test::pager_never_prints_help_to_stdout() {
  expect_output_contains "eXtended File Find" "$("$(_xff_bin)" --pager=never --help 2>&1)"
}

test::invalid_pager_all_is_a_usage_error() {
  local out status=0
  out="$("$(_xff_bin)" --pager=all --help 2>&1)" || status=$?
  expect_eq "2" "${status}"
  expect_output_contains "invalid --pager value: all" "${out}"
}

test::failed_explicit_pager_falls_back_to_stdout() {
  local bin paged plain
  bin="$(_xff_bin)"
  paged="$("${bin}" '--pager=exit 127' --help 2>&1)"
  plain="$("${bin}" --pager=never --help 2>&1)"
  expect_eq "${plain}" "${paged}"
}

test::pager_always_pipes_through_the_pager_verbatim() {
  # A cat pager is a pass-through: --pager=always feeds the help through it, so the
  # bytes are identical to the unpaged --pager=never output.
  local bin paged plain
  bin="$(_xff_bin)"
  paged="$(XFF_PAGER='cat' "${bin}" --pager=always --help 2>&1)"
  plain="$("${bin}" --pager=never --help 2>&1)"
  expect_eq "${plain}" "${paged}"
}

test::pager_help_does_not_page_when_stdout_is_not_a_tty() {
  # Default (help): captured stdout is a pipe, so a would-hang pager is never run and
  # the output equals --pager=never. A blocking pager (sleep) would deadlock the test
  # if auto paged here.
  local bin help plain
  bin="$(_xff_bin)"
  help="$(XFF_PAGER='sleep 30' "${bin}" --help 2>&1)"
  plain="$("${bin}" --pager=never --help 2>&1)"
  expect_eq "${plain}" "${help}"
}

test::no_pager_alias_is_accepted_on_a_real_search() {
  # --no-pager is a recognized global (alias for --pager=never), so it does not trip the
  # unknown-flag error on an ordinary search.
  local root out
  root="$(test_tmpdir tree)"
  mkdir -p "${root}"
  printf 'x\n' >"${root}/a.txt"
  out="$("$(_xff_bin)" "${root}" -type f --no-pager 2>&1)"
  expect_output_contains "a.txt" "${out}"
}

test::man_pager_never_emits_raw_roff() {
  # --pager=never keeps --man as raw roff source (for `mandoc` / redirect / install),
  # so the roff title macro is present verbatim.
  expect_output_contains ".TH " "$("$(_xff_bin)" --pager=never --man 2>&1)"
}

test::man_pager_always_routes_through_the_man_pager_verbatim() {
  # A cat man-pager is a pass-through, so --pager=always --man feeds the roff through it
  # unchanged - identical to the raw --pager=never roff. Proves the man path is taken and
  # uses $XFF_MANPAGER (not the mandoc default, so the test does not depend on mandoc).
  local bin paged raw
  bin="$(_xff_bin)"
  paged="$(XFF_MANPAGER='cat' "${bin}" --pager=always --man 2>&1)"
  raw="$("${bin}" --pager=never --man 2>&1)"
  expect_eq "${raw}" "${paged}"
}

test::man_pager_help_stays_raw_off_a_tty() {
  # Default (help): captured stdout is a pipe, so --man is not paged/formatted and stays
  # raw roff. A blocking man-pager (sleep) would deadlock the test if auto paged here.
  local bin help raw
  bin="$(_xff_bin)"
  help="$(XFF_MANPAGER='sleep 30' "${bin}" --man 2>&1)"
  raw="$("${bin}" --pager=never --man 2>&1)"
  expect_eq "${raw}" "${help}"
}

test::pager_auto_leaves_a_piped_listing_alone() {
  # The automatic value is terminal-only: captured stdout is a pipe, so it must not start a
  # pager (a blocking one would deadlock this test) and the listing must arrive verbatim.
  local root bin auto plain
  root="$(test_tmpdir tree)"
  mkdir -p "${root}"
  printf 'x\n' >"${root}/a.txt"
  bin="$(_xff_bin)"
  auto="$(XFF_PAGER='sleep 30' "${bin}" --pager=auto "${root}" -type f 2>&1)"
  plain="$("${bin}" --pager=never "${root}" -type f 2>&1)"
  expect_eq "${plain}" "${auto}"
}

test::pager_always_pages_ls_even_when_stdout_is_a_pipe() {
  # `always` is the explicit escape from `auto`'s terminal-only safety. A fake installed `less`
  # leaves a marker proving it ran despite captured stdout being a pipe.
  local root pager_bin marker marker_text bin plain paged
  root="$(test_tmpdir tree)"
  pager_bin="$(test_tmpdir pager-bin)"
  marker="${pager_bin}/invoked"
  mkdir -p "${root}"
  mkdir -p "${pager_bin}"
  printf '%s\n' '#!/bin/sh' "printf invoked >'${marker}'" 'exec /bin/cat' >"${pager_bin}/less"
  chmod +x "${pager_bin}/less"
  printf 'x\n' >"${root}/a.txt"
  bin="$(_xff_bin)"
  plain="$("${bin}" "${root}" -type f -ls --pager=never 2>&1)"
  paged="$(PATH="${pager_bin}:${PATH}" "${bin}" "${root}" -type f -ls --pager=always 2>&1)"
  marker_text="$(<"${marker}")"
  expect_eq "${plain}" "${paged}"
  expect_eq "invoked" "${marker_text}"
}

test::explicit_pager_command_pages_ls() {
  local root bin plain paged
  root="$(test_tmpdir tree)"
  mkdir -p "${root}"
  printf 'x\n' >"${root}/a.txt"
  bin="$(_xff_bin)"
  plain="$("${bin}" "${root}" -type f -ls --pager=never 2>&1)"
  paged="$("${bin}" "${root}" -type f -ls "--pager=sed 's/^/COMMAND:/'" 2>&1)"
  expect_eq "COMMAND:${plain}" "${paged}"
}

test::help_documents_pager() {
  # Self-documentation: the complete option inventory lists --pager.
  expect_output_contains "--pager" "$("$(_xff_bin)" --help=all 2>&1)"
  # And the command value, whose help has to explain automatic discovery and the escape hatch.
  local out
  out="$("$(_xff_bin)" --help=--pager 2>&1)"
  expect_output_contains "COMMAND" "${out}"
  expect_output_contains "-ok" "${out}" # the primaries that suppress it
}

test::config_pager_follows_resolution_order_and_is_separate_from_safe_mode() {
  local dir out
  dir="$(test_tmpdir config_pager)"
  printf '%s' content >"${dir}/victim"
  cat >"${dir}/user.ini" <<'INI'
--pager="sed 's/^/CONFIG:/'"
[plain]
--no-pager
INI
  out="$(XFF_TEST_USER_CONFIG="${dir}/user.ini" "$(_xff_bin)" "${dir}/victim" --safe --block-execution -printf '%f\n')"
  expect_eq 'CONFIG:victim' "${out}"
  out="$(XFF_TEST_USER_CONFIG="${dir}/user.ini" "$(_xff_bin)" "${dir}/victim" --config=plain -printf '%f\n')"
  expect_eq victim "${out}"
  out="$(XFF_TEST_USER_CONFIG="${dir}/user.ini" "$(_xff_bin)" "${dir}/victim" --no-pager -printf '%f\n')"
  expect_eq victim "${out}"
}

test_runner
