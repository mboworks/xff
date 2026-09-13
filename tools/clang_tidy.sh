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

# Run clang-tidy as an enforcing, report-only pass over the affected compilation
# scope. Header changes select the first-party translation units that include them,
# including through other project headers, rather than promoting every header to a
# repository-wide sweep.
#
# clang-tidy resolution prefers the hermetic toolchains_llvm binary (so it matches
# the compile DB's clang-22 flags and understands C++23), then a versioned system
# clang-tidy on PATH. A resolved clang-tidy older than the minimum below is treated
# as "not installed" and skipped - clang-tidy 16/17 mis-parse this codebase's C++23
# and emit false positives whose fixes break the build. Mirrors tools/clang_format.sh.

set -euo pipefail

# The minimum clang-tidy major version. Below this, C++23 parsing is unreliable.
readonly MIN_MAJOR=22

function skip() {
  echo "clang-tidy: skipped (${*}). The dedicated CI clang-tidy job supplies these prerequisites." 1>&2
  exit 0
}

# A compile DB is mandatory; without it clang-tidy cannot resolve include paths.
[ -f "compile_commands.json" ] \
  || skip "no compile_commands.json; generate it with './compile_commands-update.sh'"

# The `bazel-<repo>` convenience link points at the execroot; fall back to cwd.
BAZEL_OUTPUT="bazel-$(basename "${PWD}")"
[ -d "${BAZEL_OUTPUT}" ] || BAZEL_OUTPUT="."

# The output base holds every fetched repo, including the `--config=clang`
# dev-dependency LLVM toolchain that the execroot symlink forest may omit.
OUTPUT_BASE="$(bazel info output_base 2>/dev/null || true)"

declare -a CLANG_TIDY_LOCS=(
  # 1) Hermetic toolchain via the `bazel-<repo>` execroot link.
  "${BAZEL_OUTPUT}/external/llvm_toolchain_llvm/bin/clang-tidy"
  "${BAZEL_OUTPUT}/external/llvm_toolchain/bin/clang-tidy"
  "${BAZEL_OUTPUT}/external/toolchains_llvm~~llvm~llvm_toolchain_llvm/bin/clang-tidy"
  "${BAZEL_OUTPUT}/external/toolchains_llvm~~llvm~llvm_toolchain_llvm_llvm/bin/clang-tidy"
  "${BAZEL_OUTPUT}/external/toolchains_llvm++llvm+llvm_toolchain_llvm/bin/clang-tidy"
  "${BAZEL_OUTPUT}/external/toolchains_llvm++llvm+llvm_toolchain_llvm_llvm/bin/clang-tidy"

  # 2) Same hermetic toolchain via the output base (covers the dev-dependency
  #    case where it is absent from the execroot symlink forest). Still hermetic,
  #    so this is tried BEFORE any system clang-tidy below.
  "${OUTPUT_BASE:+${OUTPUT_BASE}/external/toolchains_llvm++llvm+llvm_toolchain_llvm/bin/clang-tidy}"
  "${OUTPUT_BASE:+${OUTPUT_BASE}/external/toolchains_llvm++llvm+llvm_toolchain_llvm_llvm/bin/clang-tidy}"

  # 3) System clang-tidy by versioned name (version may differ; last resort).
  "$(which "clang-tidy-23" 2>/dev/null || true)"
  "$(which "clang-tidy-22" 2>/dev/null || true)"
  "$(which "clang-tidy-21" 2>/dev/null || true)"
  "$(which "clang-tidy-20" 2>/dev/null || true)"
  "$(which "clang-tidy-19" 2>/dev/null || true)"
  "$(which "clang-tidy-18" 2>/dev/null || true)"

  # 4) LLVM_PATH or a plain clang-tidy on PATH.
  "${LLVM_PATH:-/usr}/bin/clang-tidy"
  "$(which clang-tidy 2>/dev/null || true)"
)

CLANG_TIDY=""
for LOC in "${CLANG_TIDY_LOCS[@]}"; do
  if [ -n "${LOC}" ] && [ -x "${LOC}" ]; then
    # Gate on the major version: "LLVM version 22.1.7" -> 22. Skip anything older
    # than MIN_MAJOR (an unparseable version is treated as too old / unusable).
    major="$("${LOC}" --version 2>/dev/null | grep -oE 'version [0-9]+' | grep -oE '[0-9]+' | head -1 || true)"
    if [ -n "${major}" ] && [ "${major}" -ge "${MIN_MAJOR}" ]; then
      CLANG_TIDY="${LOC}"
      break
    fi
  fi
done

[ -n "${CLANG_TIDY}" ] \
  || skip "no clang-tidy >= ${MIN_MAJOR} found; build once with --config=clang to fetch the hermetic toolchain, or install a recent clang-tidy"

# Nothing to check (pre-commit may invoke with no matching files).
[ "${#}" -gt 0 ] || exit 0

# mbo is exposed on the `-isystem` search path (so `#include "mbo/..."` resolves), and it
# ships a plain-text file literally named `version` at its repo root. An explicit -isystem
# is searched before the compiler's builtin libc++, so `#include <version>` (from libc++ /
# abseil) picks up mbo's `version` and the parse collapses. Put the HERMETIC libc++ dir on
# -isystem *before* the recorded command so the real `<version>` wins. It must be the
# hermetic libc++ (matching the DB's clang), not the SDK's, or its mbstate_t/_CTYPE differ.
declare -a EXTRA_ARGS=()
for CXX_V1 in \
  "${OUTPUT_BASE}/external/toolchains_llvm++llvm+llvm_toolchain_llvm_llvm/include/c++/v1" \
  "${OUTPUT_BASE}/external/toolchains_llvm~~llvm~llvm_toolchain_llvm_llvm/include/c++/v1" \
  "${OUTPUT_BASE}/external/llvm_toolchain_llvm/include/c++/v1"; do
  if [ -d "${CXX_V1}" ]; then
    EXTRA_ARGS+=("--extra-arg-before=-isystem${CXX_V1}")
    break
  fi
done

# Checks with no meaning in test code, disabled for `*_test.cc` only (mirrors
# mboworks/mbo). Stating the rule once here - rather than a NOLINT repeated on every
# test - keeps it in one place and covers new tests without anyone remembering to
# annotate. `--checks` with only `-name` entries APPENDS to .clang-tidy's `Checks`
# (removing those), so every other check still applies to tests.
#   * readability-function-cognitive-complexity: a gtest TestBody's score comes from
#     EXPECT_*/ASSERT_* macros expanding to branches, not from refactorable logic.
#   * misc-override-with-different-visibility: our fixtures are `struct`s (AGENTS.md),
#     so their SetUp()/TearDown() overrides are public while ::testing::Test declares
#     them protected - a visibility change forced by convention, on every fixture.
#   * readability-identifier-naming: gtest `struct` fixtures hold their state in
#     `_`-suffixed public members (the fixture's own state, mirroring the production
#     private-member convention), and the TEST_F macro expands to functions whose
#     names the check rejects - both are mandated test scaffolding, not defects.
# concurrency-mt-unsafe is deliberately NOT disabled here: a test that touches process
# environment (getenv/setenv) still carries a real MT hazard, so those few sites keep a
# targeted, commented NOLINT rather than a blanket exemption for the whole test tree.
readonly TEST_DISABLED_CHECKS='-readability-function-cognitive-complexity,-misc-override-with-different-visibility,-readability-identifier-naming'

# Keep local checks bounded even on machines with many logical CPUs. CI explicitly
# supplies its available CPU count; the runner also caps workers to selected tasks.
PARALLELISM="${CLANG_TIDY_JOBS:-2}"
readonly PARALLELISM

# Preserve scope-selection failures; process substitution would hide a stale database error.
SCOPE="$(python3 tools/clang_tidy_scope.py compile_commands.json "${@}")"
declare -a SOURCES=()
declare -a TESTS=()
while IFS= read -r FILE; do
  [ -n "${FILE}" ] || continue
  case "${FILE}" in
    *_test.cc | *_test.cpp | *_test.cxx) TESTS+=("${FILE}") ;;
    *) SOURCES+=("${FILE}") ;;
  esac
done <<<"${SCOPE}"

# Report only: --header-filter restricts diagnostics to this repo's own headers
# (not the toolchain's force-included / system headers), -p points at the compile
# DB. WarningsAsErrors in .clang-tidy makes any finding a non-zero exit. The Python
# coordinator owns output serialization, progress reporting, and interruption cleanup.
OUTPUT="$(mktemp "${TMPDIR:-/tmp}/clang_tidy_out.XXXXXX")"
CDB_DIR="$(mktemp -d "${TMPDIR:-/tmp}/clang_tidy_cdb.XXXXXX")"
trap 'rm -f "${OUTPUT}"; rm -rf "${CDB_DIR}"' EXIT
python3 tools/clang_tidy_compdb.py compile_commands.json "${CDB_DIR}/compile_commands.json"
RUNNER_ARGS=(
  --clang-tidy "${CLANG_TIDY}"
  --compile-database "${CDB_DIR}"
  --jobs "${PARALLELISM}"
  --output "${OUTPUT}"
  "--test-disabled-checks=${TEST_DISABLED_CHECKS}"
)
RUNNER_ARGS+=("${EXTRA_ARGS[@]}")
for FILE in ${SOURCES[@]+"${SOURCES[@]}"}; do RUNNER_ARGS+=(--source "${FILE}"); done
for FILE in ${TESTS[@]+"${TESTS[@]}"}; do RUNNER_ARGS+=(--test "${FILE}"); done
STATUS=0
python3 tools/clang_tidy_runner.py "${RUNNER_ARGS[@]}" || STATUS="${?}"
exit "${STATUS}"
