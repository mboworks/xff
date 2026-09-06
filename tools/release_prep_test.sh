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
# Unit test for tools/release_prep.sh. Builds a throwaway fixture repository in a
# temp directory (release_prep.sh operates relative to its own location, so a
# self-contained copy exercises it without touching the real tree) and checks the
# CHANGELOG guard, the version stamping, the consistency verification, the
# release-notes output, and the release workflow's compiler configurations.
# Run directly (`tools/release_prep_test.sh`) or via pre-commit; no bazel needed.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FAILED=0

fail() {
  echo "FAIL: ${*}" >&2
  FAILED=1
}

# File content as a single string (built-ins only; this is a standalone test, not
# a mboworks/bashtest, so it has no expect_* matchers - and reading files avoids
# the piped-grep SIGPIPE trap the bashtest lint guards against).
slurp() { printf '%s' "$(<"$1")"; }

# Count the lines of a file that contain a literal substring.
count_lines_with() {
  substr="$1"
  n=0
  while IFS= read -r line; do
    case "${line}" in *"${substr}"*) n=$((n + 1)) ;; esac
  done <"$2"
  echo "${n}"
}

# Build a minimal fixture repo under ${root}: the two real tools, a MODULE.bazel
# (our module + an intra-repo bazel_dep on it + a third-party dep that must stay
# untouched), the --version source, and a CHANGELOG whose top version is 1.2.3.
make_fixture() {
  root="$1"
  mkdir -p "${root}/tools" "${root}/xff/cli"
  cp "${HERE}/release_prep.sh" "${root}/tools/release_prep.sh"
  cp "${HERE}/check_module_versions.py" "${root}/tools/check_module_versions.py"
  chmod +x "${root}/tools/release_prep.sh"
  cat >"${root}/MODULE.bazel" <<'EOF'
module(
    name = "test_mod",
    version = "0.0.0",
)
bazel_dep(name = "abseil-cpp", version = "20250814.2")
bazel_dep(name = "test_mod", version = "0.0.0")
EOF
  cat >"${root}/xff/cli/main.cc" <<'EOF'
int main() { std::cout << "xff 0.0.0\n"; }
EOF
  cat >"${root}/CHANGELOG.md" <<'EOF'
# 1.2.3

Notes for one two three.

## Added

- A thing.

# 0.9.0

Older release, must not leak into 1.2.3 notes.
EOF
}

# 1. A tag that does not match the top CHANGELOG heading must fail, and must not
#    stamp anything.
test_guard_rejects_mismatched_tag() {
  root="$(mktemp -d)"
  trap 'rm -rf "${root}"' RETURN
  make_fixture "${root}"
  if out="$("${root}/tools/release_prep.sh" 9.9.9 2>&1)"; then
    fail "guard: expected non-zero exit for tag 9.9.9"
  fi
  case "${out}" in
    *"does not match the top CHANGELOG"*) ;;
    *) fail "guard: message did not mention the CHANGELOG mismatch: ${out}" ;;
  esac
  case "$(slurp "${root}/MODULE.bazel")" in
    *9.9.9*) fail "guard: MODULE.bazel was stamped despite the mismatch" ;;
  esac
}

# 2. A tag that is not (v)X.Y.Z must be rejected outright (a `v` prefix IS
#    accepted and stripped; see the happy path).
test_guard_rejects_nonversion_tag() {
  root="$(mktemp -d)"
  trap 'rm -rf "${root}"' RETURN
  make_fixture "${root}"
  if "${root}/tools/release_prep.sh" 1.2 >/dev/null 2>&1; then
    fail "guard: expected non-zero exit for non-X.Y.Z tag '1.2'"
  fi
}

# 3. The matching (v-prefixed) tag stamps every sentinel location with the BARE
#    version, leaves third-party deps alone, passes the consistency check, and
#    prints the version's CHANGELOG section.
test_happy_path_stamps_and_emits_notes() {
  root="$(mktemp -d)"
  trap 'rm -rf "${root}"' RETURN
  make_fixture "${root}"
  # Pass the real tag form (v1.2.3); release_prep strips the v and stamps 1.2.3.
  if ! notes="$("${root}/tools/release_prep.sh" v1.2.3)"; then
    fail "happy: expected zero exit for tag v1.2.3"
    return
  fi
  case "$(slurp "${root}/MODULE.bazel")" in
    *'version = "1.2.3"'*) ;;
    *) fail "happy: module version was not stamped to 1.2.3" ;;
  esac
  # The intra-repo bazel_dep on our module is stamped too (module + dep = 2 lines).
  if [ "$(count_lines_with '1.2.3' "${root}/MODULE.bazel")" -ne 2 ]; then
    fail "happy: expected module version + intra-repo bazel_dep both stamped"
  fi
  case "$(slurp "${root}/MODULE.bazel")" in
    *'version = "20250814.2"'*) ;;
    *) fail "happy: the third-party bazel_dep version was altered" ;;
  esac
  case "$(slurp "${root}/xff/cli/main.cc")" in
    *'"xff 1.2.3'*) ;;
    *) fail "happy: the --version literal was not stamped" ;;
  esac
  case "${notes}" in
    *"Notes for one two three."*) ;;
    *) fail "happy: notes missing the 1.2.3 body: ${notes}" ;;
  esac
  case "${notes}" in
    *"Older release"*) fail "happy: notes leaked the older 0.9.0 section" ;;
  esac
  case "${notes}" in
    *"https://mboworks.github.io/xff/releases/1.2.3/"*) ;;
    *) fail "happy: notes missing the versioned HTML reference: ${notes}" ;;
  esac
  case "${notes}" in
    *"https://mboworks.github.io/xff/releases/1.2.3/XFF.md"*) ;;
    *) fail "happy: notes missing the versioned XFF.md link: ${notes}" ;;
  esac
  case "${notes}" in
    *"https://mboworks.github.io/xff/coverage/tag/1.2.3/"*) ;;
    *) fail "happy: notes missing the versioned coverage link: ${notes}" ;;
  esac
  case "${notes}" in
    *"releases/download/v1.2.3/xff-linux-x86_64"*) ;;
    *) fail "happy: notes missing the versioned binary download: ${notes}" ;;
  esac
  case "${notes}" in
    *"SHA256SUMS"*"sha256sum -c -"*) ;;
    *) fail "happy: notes missing checksum verification: ${notes}" ;;
  esac
  case "${notes}" in
    *"--pager=never --man > "*"/.local/share/man/man1/xff.1"*) ;;
    *) fail "happy: notes missing generated man-page installation: ${notes}" ;;
  esac
}

# 4. Documentation generation compiles xff code, so it must use the same Clang
#    toolchain as the release binaries and coverage job. GCC remains confined to
#    the explicitly named compatibility job in main.yml.
test_release_reference_uses_clang() {
  workflow="${HERE}/../.github/workflows/release.yml"
  if [ "$(count_lines_with 'bazel build --config=clang --config=xff_docs //xff/cli:xff_reference_gen' "${workflow}")" -ne 1 ]; then
    fail "release workflow: reference generation must build exactly once with Clang"
  fi
}

# 5. Published binaries use the named release configuration and the shared staging script. This
#    prevents the ordinary test jobs and tag workflow from quietly growing separate flag sets.
test_release_binaries_use_shared_configuration_and_staging() {
  release_workflow="${HERE}/../.github/workflows/release.yml"
  main_workflow="${HERE}/../.github/workflows/main.yml"
  if [ "$(count_lines_with 'bazel build --config=release //xff/cli:xff' "${release_workflow}")" -ne 1 ]; then
    fail "release workflow: lean binary must build exactly once with --config=release"
  fi
  if [ "$(count_lines_with 'bazel build --config=release --config=xff_full //xff/cli:xff_full' "${release_workflow}")" -ne 1 ]; then
    fail "release workflow: full binary must build exactly once with --config=release"
  fi
  if [ "$(count_lines_with 'tools/stage_release_artifacts.sh' "${release_workflow}")" -ne 1 ]; then
    fail "release workflow: artifacts must use the shared staging script"
  fi
  if [ "$(count_lines_with '          path: dist/*' "${release_workflow}")" -ne 1 ]; then
    fail "release workflow: must upload raw binaries and the Zstandard platform archive"
  fi
  if [ "$(count_lines_with '            > SHA256SUMS' "${release_workflow}")" -ne 1 ]; then
    fail "release workflow: must generate exactly one checksum manifest"
  fi
  if [ "$(count_lines_with '          subject-path: dist/*' "${release_workflow}")" -ne 1 ]; then
    fail "release workflow: provenance must attest every asset, including SHA256SUMS"
  fi
  if [ "$(count_lines_with 'release_test_config=(--config=release)' "${main_workflow}")" -ne 1 ]; then
    fail "main workflow: release-test options must start with --config=release"
  fi
  if [ "$(count_lines_with 'bazel test //xff/cli:all --config=xff_docs' "${main_workflow}")" -ne 1 ]; then
    fail "main workflow: binary-level tests must use --config=release"
  fi
  if [ "$(count_lines_with '--test_tag_filters=release-binary' "${main_workflow}")" -ne 1 ]; then
    fail "main workflow: release tests must select tagged binary-level bashtests"
  fi
  if [ "$(count_lines_with 'tools/stage_release_artifacts.sh' "${main_workflow}")" -ne 1 ]; then
    fail "main workflow: release cells must exercise the shared staging script"
  fi
  if [ "$(count_lines_with '          path: dist/*' "${main_workflow}")" -ne 1 ]; then
    fail "main workflow: must upload raw binaries and the Zstandard platform archive"
  fi
}

test_guard_rejects_mismatched_tag
test_guard_rejects_nonversion_tag
test_happy_path_stamps_and_emits_notes
test_release_reference_uses_clang
test_release_binaries_use_shared_configuration_and_staging

if [ "${FAILED}" -ne 0 ]; then
  echo "release_prep_test: FAILED" >&2
  exit 1
fi
echo "release_prep_test: all tests passed"
