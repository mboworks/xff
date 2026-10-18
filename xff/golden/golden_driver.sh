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

# --- begin runfiles.bash initialization v3 ---
# Locate the Bash runfiles library in directory or manifest-only mode.
f=bazel_tools/tools/bash/runfiles/runfiles.bash
# shellcheck disable=SC1090
[[ -f "${RUNFILES_DIR:-/dev/null}/$f" ]] && source "${RUNFILES_DIR}/$f" 2>/dev/null \
  || source "$(grep -sm1 "^$f " "${RUNFILES_MANIFEST_FILE:-/dev/null}" | cut -f2- -d' ')" 2>/dev/null \
  || source "$0.runfiles/$f" 2>/dev/null \
  || source "$(grep -sm1 "^$f " "$0.runfiles_manifest" | cut -f2- -d' ')" 2>/dev/null \
  || source "$(grep -sm1 "^$f " "$0.exe.runfiles_manifest" | cut -f2- -d' ')" 2>/dev/null \
  || {
    echo >&2 "ERROR: cannot find the bash runfiles library"
    exit 1
  }
# --- end runfiles.bash initialization v3 ---

# Protocol: bashtest, setup, find golden, xff golden, executable (runfiles keys),
# ordered(0/1), "--", then expression arguments. Resolve before entering the fixture.
# shellcheck disable=SC1090,SC1091
source "$(rlocation "$1")"
SETUP="$(rlocation "$2")"
FIND_GOLDEN="$(rlocation "$3")"
XFF_GOLDEN="$(rlocation "$4")"
XFF="$(rlocation "$5")"
ORDERED="$6"
shift 7
EXPR_ARGS=("$@")

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
