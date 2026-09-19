#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

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

# Unicode-specific tests: fixtures are independent of control-character scenarios.
test::unicode_filename_round_trips_in_human_formats() {
  local dir name out mode
  dir="$(test_tmpdir unicode_filename)"
  mkdir -p "${dir}"
  name="café.txt" # U+00E9: Latin e with acute, encoded as UTF-8.
  : >"${dir}/${name}"
  for mode in aligned tree md; do
    out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -type f --format="${mode}" --color=never)"
    expect_output_contains "${name}" "${out}"
  done
}

test_runner
