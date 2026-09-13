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
# End-to-end test of -hasheq (the manifest-verification companion of -hash): TRUE when the file's
# computed digest equals the EXPECTED field template (a literal or {def.NAME}), -hasheq:ALGO
# [/ENCODING], hex case-insensitivity, `! -hasheq` selecting drift, the bad-spec usage error, and
# the find-style rejection of this xff extension. Drives the real binary (reads files).

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

test::hasheq_matches_and_mismatches() {
  local dir out
  dir="$(test_tmpdir vfy)"
  mkdir -p "${dir}"
  printf 'abc' >"${dir}/f.txt"
  # A matching EXPECTED (the sha256 of "abc") makes -hasheq true, so the implicit print emits it.
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -name f.txt -hasheq "${_SHA256_ABC}" 2>&1)"
  expect_output_contains "${dir}/f.txt" "${out}"
  # A non-matching EXPECTED makes -hasheq false, so nothing prints.
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -name f.txt -hasheq "deadbeef" 2>&1)"
  expect_output_not_contains "${dir}/f.txt" "${out}"
}

test::hasheq_reads_expected_from_a_define() {
  local dir out
  dir="$(test_tmpdir vfydef)"
  mkdir -p "${dir}"
  printf 'abc' >"${dir}/f.txt"
  # EXPECTED is a field template, so a {def.NAME} value drives the comparison (a sidecar manifest).
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --define="SUMS=${_SHA256_ABC}" \
    "${dir}" -name f.txt -hasheq '{def.SUMS}' 2>&1)"
  expect_output_contains "${dir}/f.txt" "${out}"
}

test::hasheq_hex_is_case_insensitive() {
  local dir out upper
  dir="$(test_tmpdir vfycase)"
  mkdir -p "${dir}"
  printf 'abc' >"${dir}/f.txt"
  upper="$(printf '%s' "${_SHA256_ABC}" | tr 'a-f' 'A-F')"
  # An upper-cased hex expected still matches the lower-cased computed digest (hex folds case).
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -name f.txt -hasheq "${upper}" 2>&1)"
  expect_output_contains "${dir}/f.txt" "${out}"
}

test::hasheq_algo_and_encoding_selectors() {
  local dir out
  dir="$(test_tmpdir vfyalgo)"
  mkdir -p "${dir}"
  printf 'abc' >"${dir}/f.txt"
  # -hasheq:md5 checks against the md5 digest ...
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -name f.txt -hasheq:md5 "${_MD5_ABC}" 2>&1)"
  expect_output_contains "${dir}/f.txt" "${out}"
  # ... and -hasheq:sha256/base64 against the base64-encoded digest.
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -name f.txt -hasheq:sha256/base64 "${_B64_ABC}" 2>&1)"
  expect_output_contains "${dir}/f.txt" "${out}"
}

test::not_hasheq_selects_drift() {
  local dir out
  dir="$(test_tmpdir vfydrift)"
  mkdir -p "${dir}"
  printf 'abc' >"${dir}/good.txt"
  printf 'xyz' >"${dir}/bad.txt"
  # `! -hasheq EXPECTED` with the sha256 of "abc" selects the file whose content changed.
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -type f ! -hasheq "${_SHA256_ABC}" 2>&1)"
  expect_output_contains "${dir}/bad.txt" "${out}"
  expect_output_not_contains "${dir}/good.txt" "${out}"
}

test::hasheq_bad_spec_and_find_style_are_usage_errors() {
  local dir out rc
  dir="$(test_tmpdir vfyerr)"
  mkdir -p "${dir}"
  printf 'abc' >"${dir}/f.txt"
  # An unknown algorithm in -hasheq:ALGO is a usage error.
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -name f.txt -hasheq:crc32 x 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains 'unknown algorithm or encoding' "${out}"
  # -hasheq is an xff extension; the find style rejects it.
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --config=find "${dir}" -hasheq x 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains 'find style' "${out}"
}

test_runner
