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
# End-to-end test of -hash (via xff/hash -> mbo::digest): the `<digest>  <path>` sha256sum
# layout, -hash:ALGO[/ENCODING], the --hash-algorithm / --hash-encoding defaults for both the
# action and the {hash} field, the usage errors, and the find-style rejection of this xff
# extension. Drives the real binary (reads files).

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

# The sha256 / md5 / base64 of "abc" are stable, spec-pinned vectors (mbo::digest owns
# conformance); here they verify the CLI plumbing end to end.
_SHA256_ABC="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
_MD5_ABC="900150983cd24fb0d6963f7d28e17f72"
_B64_ABC="ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0="

test::hash_action_prints_digest_and_path() {
  local dir out
  dir="$(test_tmpdir hashact)"
  mkdir -p "${dir}"
  printf 'abc' >"${dir}/f.txt"
  # -hash prints `<digest>  <path>` (default sha256), the sha256sum layout.
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -name f.txt -hash 2>&1)"
  expect_output_contains "${_SHA256_ABC}  ${dir}/f.txt" "${out}"
  # -hash:ALGO / -hash:ALGO/ENCODING pick the algorithm and encoding.
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -name f.txt -hash:md5 2>&1)"
  expect_output_contains "${_MD5_ABC}  ${dir}/f.txt" "${out}"
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -name f.txt -hash:sha256/base64 2>&1)"
  expect_output_contains "${_B64_ABC}  ${dir}/f.txt" "${out}"
}

test::hash_global_default_applies_to_action_and_field() {
  local dir out
  dir="$(test_tmpdir hashglob)"
  mkdir -p "${dir}"
  printf 'abc' >"${dir}/f.txt"
  # --hash-algorithm=md5 is the default for a bare -hash ...
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --hash-algorithm=md5 "${dir}" -name f.txt -hash 2>&1)"
  expect_output_contains "${_MD5_ABC}  ${dir}/f.txt" "${out}"
  # ... and for a bare {hash} field via the %{hash} printf escape.
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --hash-algorithm=md5 "${dir}" -name f.txt -printf '%{hash}\n' 2>&1)"
  expect_output_contains "${_MD5_ABC}" "${out}"
  # --hash-encoding=base64 renders the digest in base64.
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --hash-encoding=base64 "${dir}" -name f.txt -hash 2>&1)"
  expect_output_contains "${_B64_ABC}  ${dir}/f.txt" "${out}"
}

test::hash_bad_spec_and_find_style_are_usage_errors() {
  local dir out rc
  dir="$(test_tmpdir hasherr)"
  mkdir -p "${dir}"
  printf 'abc' >"${dir}/f.txt"
  # An unknown algorithm in -hash:ALGO is a usage error.
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -name f.txt -hash:crc32 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains 'unknown algorithm or encoding' "${out}"
  # A bad --hash-encoding is a usage error.
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --hash-encoding=b64 "${dir}" -name f.txt -hash 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  # The shared value check reports it first, naming the flag and its accepted values.
  expect_output_contains 'unknown value' "${out}"
  expect_output_contains '--hash-encoding' "${out}"
  # -hash is an xff extension; the find style rejects it.
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --config=find "${dir}" -hash 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains 'find style' "${out}"
}

test_runner
