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
# End-to-end test of the composable-extras UX in a lean (default) build: a flag whose build extra
# is not compiled in (--archive, needing --//xff:xff_archive) stays present and listed, is grouped under
# a distinct "Extras" heading in --help with a note on what to rebuild with, is documented as NOT
# built in by --help=NAME, and is a hard immediate error (exit 2) when actually used - never a silent
# no-op. Drives the real (lean) binary.

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

test::disabled_extra_flag_is_a_hard_error_when_used() {
  local out rc
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --archive . 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "no archive support" "${out}"
  expect_output_contains "--//xff:xff_archive" "${out}" # names what to rebuild with
}

test::disabled_extra_flag_is_listed_with_a_not_built_note() {
  # The flag is NOT hidden or reported as unknown: --help=all lists it with a
  # note that its build extra is absent and what to rebuild with.
  local out
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --help=all 2>&1)"
  expect_matches "\-\-archive" "${out}" # listed, not hidden
  expect_output_contains "NOT built into this binary" "${out}"
  expect_output_contains "--//xff:xff_archive" "${out}" # names what to rebuild with
}

test::disabled_extra_help_topic_marks_it_not_built_in() {
  local out rc
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --help=--archive 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_matches "\-\-archive" "${out}"
  expect_output_contains "NOT built into this binary" "${out}"
  expect_output_contains "descend into archives" "${out}" # the summary still shows
}

test::help_extras_lists_every_build_extra_and_availability() {
  # `--help=extras` enumerates the optional build extras and whether THIS (lean) binary links each.
  # Neither pcre2 (the --regextype value extra) nor archive (a flag extra) is built in here, and
  # each names the flag to rebuild with.
  local out rc
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --help=extras 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_output_contains "build extras" "${out}"
  expect_matches "pcre2.*not built in" "${out}"
  expect_output_contains "--//xff:xff_pcre" "${out}"
  expect_matches "archive.*not built in" "${out}"
  expect_output_contains "--//xff:xff_archive" "${out}"
  expect_matches "squashfs.*not built in" "${out}"
  expect_output_contains "--//xff:xff_squashfs" "${out}"
}

test_runner
