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
# Shared driver for the xff_golden macro (xff/golden/golden.bzl). Builds a fixture,
# runs xff in find and xff style over it, normalizes the output, and diffs each mode
# against its committed golden. See golden.bzl for the protocol.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

# Absolute path of a data file from its workspace-relative rootpath.
_rf() { echo "${TEST_SRCDIR}/${TEST_WORKSPACE}/$1"; }

_xff_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/testing/xff"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff -path '*xff/cli/testing/xff' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

# Protocol from xff_golden: $1 setup  $2 find_golden  $3 xff_golden  $4 ordered(0/1)
# $5 "--"  $6.. expr args.
SETUP="$(_rf "$1")"
FIND_GOLDEN="$(_rf "$2")"
XFF_GOLDEN="$(_rf "$3")"
ORDERED="$4"
shift 5 # drop the four positionals and the "--"
EXPR_ARGS=("$@")
XFF="$(_xff_bin)"

# Fixture in a fresh temp tree; the setup script populates $PWD. Cleaned on exit.
TREE="$(mktemp -d)"
trap 'rm -rf "${TREE}"' EXIT
(cd "${TREE}" && bash "${SETUP}")

# Runs xff in `$1` style and normalizes stdout+stderr: the temp root becomes <ROOT> (a
# per-run path), and lines are sorted unless ORDERED (find's readdir order is not
# deterministic). The argv is the case's `args` with every `<ROOT>` token replaced by the
# temp root, so a case places the search root itself (globals such as `--summary` precede
# it, the expression follows). Exit status is not asserted (content-only goldens).
_run() {
  local style="$1" out arg
  local -a argv=()
  for arg in ${EXPR_ARGS[@]+"${EXPR_ARGS[@]}"}; do
    argv+=("${arg//<ROOT>/${TREE}}")
  done
  out="$("${XFF}" "--config=${style}" ${argv[@]+"${argv[@]}"} 2>&1)" || true
  out="${out//${TREE}/<ROOT>}"
  if [[ "${ORDERED}" != "1" ]]; then
    out="$(printf '%s\n' "${out}" | LC_ALL=C sort)"
  fi
  printf '%s' "${out}"
}

test::find_mode() {
  expect_eq "$(cat "${FIND_GOLDEN}")" "$(_run find)"
}

test::xff_mode() {
  expect_eq "$(cat "${XFF_GOLDEN}")" "$(_run xff)"
}

test_runner
