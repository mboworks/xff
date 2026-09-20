#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

_xff_bin() {
  echo "${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/testing/xff_full"
}
_fixture_dir() {
  dirname "${XFF_APPLE_FIXTURE}"
}

test::packages_expose_payload_files_on_every_host() {
  local file out tree
  tree="$(test_tmpdir fixtures)"
  for file in plain.pkg pbzx.pkg sample.xip nested.pkg native-component.pkg native-product.pkg payload.pbzx raw.pbzx application.zip application.ipa application.ipsw; do
    cp "$(_fixture_dir)/${file}" "${tree}/${file}"
    out="$("$(_xff_bin)" --archive=all --archive-depth=4 "${tree}/${file}" -type f -name info.txt -grep portable -print)"
    expect_output_contains "Applications/Example.app/Contents/info.txt" "${out}"
  done
}

test::depth_keeps_package_payloads_opaque_until_requested() {
  local out tree
  tree="$(test_tmpdir depth)"
  cp "$(_fixture_dir)/sample.xip" "${tree}/sample.xip"
  out="$("$(_xff_bin)" --archive=all --archive-depth=1 "${tree}/sample.xip" -print)"
  expect_output_contains "!Content" "${out}"
  expect_output_not_contains "!Applications" "${out}"
}

test::directory_packages_get_contextual_payload_discovery() {
  local tree out
  tree="$(test_tmpdir package)"
  mkdir -p "${tree}/Example.pkg"
  cp "$(_fixture_dir)/payload.cpio" "${tree}/Example.pkg/Payload"
  cp "$(_fixture_dir)/payload.cpio" "${tree}/Payload"
  out="$("$(_xff_bin)" --archive=all --archive-depth=4 "${tree}" -name info.txt -print)"
  expect_output_contains "Example.pkg/Payload!Applications" "${out}"
  expect_output_not_contains "${tree}/Payload!" "${out}"
}

test::malformed_payload_is_an_error() {
  local tree out status
  tree="$(test_tmpdir malformed)"
  printf pbzx >"${tree}/broken.pbzx"
  out="$("$(_xff_bin)" --archive=all "${tree}/broken.pbzx" 2>&1)" && status=0 || status=$?
  expect_ne 0 "${status}"
  expect_output_contains "truncated PBZX" "${out}"
}

test::xar_rewriting_is_refused_even_under_a_different_extension() {
  local tree out status
  tree="$(test_tmpdir readonly)"
  cp "$(_fixture_dir)/sample.xar" "${tree}/renamed.tar"
  out="$("$(_xff_bin)" --archive=all --archive-delete "${tree}/renamed.tar" -name hello.txt -delete 2>&1)" && status=0 || status=$?
  expect_ne 0 "${status}"
  expect_output_contains "read-only" "${out}"
  cmp "$(_fixture_dir)/sample.xar" "${tree}/renamed.tar"
}

test::package_content_uses_existing_extraction_and_safety_controls() {
  local tree out status
  tree="$(test_tmpdir extraction)"
  cp "$(_fixture_dir)/pbzx.pkg" "${tree}/app.pkg"
  out="$("$(_xff_bin)" --archive=all --archive-depth=3 --archive-extract "${tree}/app.pkg" -name info.txt -exec cat {} \;)"
  expect_output_contains "portable package" "${out}"
  out="$("$(_xff_bin)" --safe --archive=all --archive-depth=3 --archive-extract "${tree}/app.pkg" -name info.txt -exec cat {} \; 2>&1)" && status=0 || status=$?
  expect_ne 0 "${status}"
  expect_output_not_contains "portable package" "${out}"
}

test::custom_separator_preserves_package_namespaces() {
  local tree out
  tree="$(test_tmpdir separator)"
  cp "$(_fixture_dir)/pbzx.pkg" "${tree}/app.pkg"
  out="$("$(_xff_bin)" --archive=all --archive-depth=3 --archive-separator='::' "${tree}/app.pkg" -name info.txt -print)"
  expect_output_contains "app.pkg::Payload::Applications/Example.app/Contents/info.txt" "${out}"
}

test::unsupported_package_payload_is_not_silently_skipped() {
  local tree out status
  tree="$(test_tmpdir unsupported)"
  mkdir -p "${tree}/Example.pkg"
  printf 'unsupported payload' >"${tree}/Example.pkg/Payload"
  out="$("$(_xff_bin)" --archive=all --archive-depth=3 "${tree}" 2>&1)" && status=0 || status=$?
  expect_ne 0 "${status}"
  expect_output_contains "unsupported package payload" "${out}"
}

test_runner "$@"
