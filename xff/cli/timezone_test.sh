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
# Civil-time behavior and demand-driven macOS framework initialization.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

_xff() {
  XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/no-user-config" \
    "${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/testing/xff" "$@"
}

_tree() {
  local dir="$1"
  mkdir -p "${dir}"
  echo hit >"${dir}/winter"
  echo hit >"${dir}/summer"
  TZ=UTC touch -t 202001020030 "${dir}/winter"
  TZ=UTC touch -t 202007020030 "${dir}/summer"
}

test::local_zone_preserves_dst_in_templates_and_printf() {
  local dir out
  dir="$(test_tmpdir timezone)"
  _tree "${dir}"
  out="$(TZ=America/New_York _xff "${dir}" -type f '--template={name} {mtime:%Y-%m-%d %H:%M %z}')"
  expect_output_contains 'winter 2020-01-01 19:30 -0500' "${out}"
  expect_output_contains 'summer 2020-07-01 20:30 -0400' "${out}"
  out="$(TZ=America/New_York _xff "${dir}" -type f -printf '%f %TY-%Tm-%Td %TH:%TM %Tz\n')"
  expect_output_contains 'winter 2020-01-01 19:30 -0500' "${out}"
  expect_output_contains 'summer 2020-07-01 20:30 -0400' "${out}"
}

test::local_zone_reaches_columns_summaries_and_configs() {
  local dir out
  dir="$(test_tmpdir timezone-fields)"
  _tree "${dir}"
  out="$(TZ=America/New_York _xff "${dir}" -type f --format=csv '--columns=name,mtime:%z')"
  expect_output_contains '-0500' "${out}"
  expect_output_contains '-0400' "${out}"
  out="$(TZ=America/New_York _xff "${dir}" -type f '--summary={mtime:%z}')"
  expect_output_contains '-0500' "${out}"
  expect_output_contains '-0400' "${out}"
  echo '--template="{name} {mtime:%z}"' >"${dir}/time.rc"
  out="$(TZ=America/New_York _xff --xffrc="${dir}/time.rc" "${dir}" -name winter)"
  expect_eq 'winter -0500' "${out}"
}

test::calendar_comparisons_use_the_local_zone() {
  local dir out
  dir="$(test_tmpdir timezone-match)"
  _tree "${dir}"
  out="$(TZ=America/New_York _xff "${dir}" -name winter -newermt '2020-01-01 20:00:00')"
  expect_eq '' "${out}"
  out="$(TZ=UTC _xff "${dir}" -name winter -newermt '2020-01-01 20:00:00')"
  expect_eq "${dir}/winter" "${out}"
}

test::ordinary_search_defers_corefoundation_on_macos() {
  if [[ "${OSTYPE}" != darwin* ]]; then
    return
  fi
  local dir out
  dir="$(test_tmpdir timezone-loader)"
  _tree "${dir}"
  out="$(DYLD_PRINT_INITIALIZERS=1 _xff --version 2>&1)"
  expect_output_not_contains 'CoreFoundation.framework' "${out}"
  out="$(DYLD_PRINT_INITIALIZERS=1 _xff "${dir}" -type f 2>&1)"
  expect_output_not_contains 'CoreFoundation.framework' "${out}"
  out="$(DYLD_PRINT_INITIALIZERS=1 _xff "${dir}" -rxc hit 2>&1)"
  expect_output_not_contains 'CoreFoundation.framework' "${out}"
  out="$(DYLD_PRINT_INITIALIZERS=1 _xff "${dir}" -type f --timezone=UTC '--template={mtime:%z}' 2>&1)"
  expect_output_not_contains 'CoreFoundation.framework' "${out}"
  out="$(DYLD_PRINT_INITIALIZERS=1 _xff "${dir}" -type f '--template={mtime:%z}' 2>&1)"
  expect_output_contains 'CoreFoundation.framework' "${out}"
}

test_runner
