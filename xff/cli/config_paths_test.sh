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

test::invalid_globals_cannot_discard_a_deletion_block_in_any_tier() {
  local dir tier out rc
  dir="$(test_tmpdir invalid_globals)"
  mkdir -p "${dir}/root"
  printf '%s\n' '--allow-rc-globals' >"${dir}/grant.ini"
  printf '%s\n' '--block-file-deletion --color=garbage' >"${dir}/bad.ini"
  for tier in system user explicit automatic; do
    printf '%s' 'preserved' >"${dir}/root/victim"
    case "${tier}" in
      system)
        out="$(XFF_TEST_SYSTEM_CONFIG="${dir}/bad.ini" "${isolated}" "${dir}/root" -delete 2>&1)" && rc=0 || rc=$?
        ;;
      user)
        out="$(XFF_TEST_USER_CONFIG="${dir}/bad.ini" "${isolated}" "${dir}/root" -delete 2>&1)" && rc=0 || rc=$?
        ;;
      explicit)
        out="$("${isolated}" "${dir}/root" --xffrc="${dir}/bad.ini" -delete 2>&1)" && rc=0 || rc=$?
        ;;
      automatic)
        cp "${dir}/bad.ini" "${dir}/root/.xffrc"
        out="$(XFF_TEST_USER_CONFIG="${dir}/grant.ini" "${isolated}" "${dir}/root" --rc -delete 2>&1)" && rc=0 || rc=$?
        ;;
    esac
    expect_eq 2 "${rc}"
    expect_output_contains 'invalid global config line' "${out}"
    expect_eq preserved "$(cat "${dir}/root/victim")"
  done
}

test::malformed_policy_cannot_be_skipped() {
  local dir out rc
  dir="$(test_tmpdir invalid_skip)"
  printf '%s\n' '--no-require-user-config --color=garbage' >"${dir}/bad.ini"
  out="$(XFF_TEST_USER_CONFIG="${dir}/bad.ini" "${isolated}" --no-user-config --explain 2>&1)" && rc=0 || rc=$?
  expect_eq 2 "${rc}"
  expect_output_contains 'invalid global config line' "${out}"
}

test::unknown_direct_and_composed_selections_fail_before_execution() {
  local dir selection out rc
  dir="$(test_tmpdir unknown_config)"
  printf '%s\n' '[safe]' '--safe' '[outer]' '--config=missing' >"${dir}/user.ini"
  printf '%s' preserved >"${dir}/victim"
  for selection in saef outer; do
    out="$(XFF_TEST_USER_CONFIG="${dir}/user.ini" "${isolated}" "${dir}/victim" "--config=${selection}" -delete 2>&1)" && rc=0 || rc=$?
    expect_eq 2 "${rc}"
    expect_output_contains 'is not defined in an active file' "${out}"
    expect_eq preserved "$(cat "${dir}/victim")"
  done
}

test::forward_composition_into_an_explicit_file_remains_valid() {
  local dir out
  dir="$(test_tmpdir forward_config)"
  printf '%s\n' '[outer]' '--config=later' >"${dir}/user.ini"
  printf '%s\n' '[later]' '-name victim' >"${dir}/task.rc"
  printf '%s' preserved >"${dir}/victim"
  out="$(XFF_TEST_USER_CONFIG="${dir}/user.ini" "${isolated}" "${dir}/victim" --config=outer --xffrc="${dir}/task.rc" -printf '%f')"
  expect_eq victim "${out}"
}

test::config_only_directives_have_individual_help_but_fail_on_cli() {
  local flag out rc
  for flag in require-system-config no-require-system-config require-user-config no-require-user-config allow-xffrc no-allow-xffrc; do
    out="$("${isolated}" "--help=${flag}")"
    expect_output_contains "--${flag}" "${out}"
    out="$("${isolated}" "--${flag}" --explain 2>&1)" && rc=0 || rc=$?
    expect_eq 2 "${rc}"
    expect_output_contains 'config-only' "${out}"
  done
}

test::config_reading_rejects_special_files_and_preserves_regular_symlinks() {
  local dir path out rc
  dir="$(test_tmpdir config_types)"
  mkdir -p "${dir}/directory"
  mkfifo "${dir}/fifo"
  ln -s "${dir}/fifo" "${dir}/fifo-link"
  for path in directory fifo fifo-link; do
    out="$("${isolated}" "--xffrc=${dir}/${path}" --explain 2>&1)" && rc=0 || rc=$?
    expect_eq 2 "${rc}"
    expect_output_contains 'configuration must be a regular file' "${out}"
  done
  printf '%s\n' '--color=never' >"${dir}/regular.ini"
  ln -s "${dir}/regular.ini" "${dir}/regular-link"
  out="$("${isolated}" "--xffrc=${dir}/regular-link" --explain)"
  expect_output_contains '--color=never' "${out}"
}

test_runner
