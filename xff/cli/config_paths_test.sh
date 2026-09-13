#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail
# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

production="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/xff"
isolated="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/testing/xff"

test::production_environment_cannot_redirect_config() {
  local dir baseline redirected
  dir="$(test_tmpdir redirect)"
  mkdir -p "${dir}/xff" "${dir}/.config/xff"
  printf '%s\n' '--require-user-config' '--block-execution' >"${dir}/policy.ini"
  cp "${dir}/policy.ini" "${dir}/xff/config"
  cp "${dir}/policy.ini" "${dir}/.config/xff/config"
  baseline="$("${production}" --explain)"
  redirected="$(XFF_CONFIG="${dir}/policy.ini" XDG_CONFIG_HOME="${dir}" HOME="${dir}" XFF_TEST_SYSTEM_CONFIG="${dir}/policy.ini" XFF_TEST_USER_CONFIG="${dir}/policy.ini" "${production}" --explain)"
  expect_eq "${baseline}" "${redirected}"
}

test::missing_explicit_file_is_an_error() {
  local out rc
  out="$("${isolated}" --no-config --xffrc="${TEST_TMPDIR}/missing.rc" --explain 2>&1)" && rc=0 || rc=$?
  expect_eq 2 "${rc}"
  expect_output_contains 'cannot read configuration' "${out}"
}

test::existing_broken_config_cannot_be_skipped() {
  local dir out rc variable
  dir="$(test_tmpdir dangling)"
  ln -s "${dir}/missing" "${dir}/policy.ini"
  for variable in XFF_TEST_SYSTEM_CONFIG XFF_TEST_USER_CONFIG; do
    out="$(env "${variable}=${dir}/policy.ini" "${isolated}" --no-config --explain 2>&1)" && rc=0 || rc=$?
    expect_eq 2 "${rc}"
    expect_output_contains 'configuration entry exists' "${out}"
  done
}

test::unreadable_existing_config_is_an_error() {
  local dir out rc
  dir="$(test_tmpdir unreadable)"
  printf '%s\n' '--require-user-config' >"${dir}/policy.ini"
  chmod 000 "${dir}/policy.ini"
  if [[ -r "${dir}/policy.ini" ]]; then
    # Root can read chmod-000 files; status-level tests cover this independently.
    chmod 600 "${dir}/policy.ini"
    return
  fi
  out="$(XFF_TEST_USER_CONFIG="${dir}/policy.ini" "${isolated}" --no-user-config --explain 2>&1)" && rc=0 || rc=$?
  chmod 600 "${dir}/policy.ini"
  expect_eq 2 "${rc}"
  expect_output_contains 'cannot read configuration' "${out}"
}

test::injected_system_policy_remains_authoritative() {
  local dir out rc
  dir="$(test_tmpdir system)"
  printf '%s\n' '--require-user-config' >"${dir}/system.ini"
  printf '%s\n' '--no-require-user-config' >"${dir}/user.ini"
  out="$(XFF_TEST_SYSTEM_CONFIG="${dir}/system.ini" XFF_TEST_USER_CONFIG="${dir}/user.ini" "${isolated}" --no-user-config --explain 2>&1)" && rc=0 || rc=$?
  expect_eq 2 "${rc}"
  expect_output_contains 'require-user-config' "${out}"
}

test_runner
