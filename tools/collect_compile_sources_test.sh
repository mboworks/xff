#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

collector="${TEST_SRCDIR}/${TEST_WORKSPACE}/tools/collect_compile_sources.sh"
fake="${TEST_SRCDIR}/${TEST_WORKSPACE}/tools/collect_compile_sources_fake.sh"

_fixture() {
  local fixture
  fixture="$(test_tmpdir source-query)"
  cp "${fake}" "${fixture}/bazel"
  chmod +x "${fixture}/bazel"
  printf '%s\n' "${fixture}"
}

test::complete_queries_filter_third_party_and_allow_empty_packages() {
  local fixture output rc
  fixture="$(_fixture)"
  output="$(PATH="${fixture}:${PATH}" "${collector}" //first/... //empty/... //second/...)" && rc=0 || rc=$?
  expect_eq 0 "${rc}"
  expect_eq $'@@//xff/engine/run.cc\n@@xff_archive+//:backend.cc' "${output}"
}

test::a_failed_later_query_reports_the_pattern_and_emits_no_partial_list() {
  local fixture output rc
  fixture="$(_fixture)"
  output="$(PATH="${fixture}:${PATH}" "${collector}" //first/... //broken/... //second/... 2>"${fixture}/error")" && rc=0 || rc=$?
  expect_eq 1 "${rc}"
  expect_eq '' "${output}"
  expect_output_contains 'controlled query failure' "$(<"${fixture}/error")"
  expect_output_contains 'source query failed for //broken/...' "$(<"${fixture}/error")"
}

test::an_empty_successful_query_does_not_look_like_a_failure() {
  local fixture output rc
  fixture="$(_fixture)"
  output="$(PATH="${fixture}:${PATH}" "${collector}" //empty/...)" && rc=0 || rc=$?
  expect_eq 0 "${rc}"
  expect_eq '' "${output}"
}

test::missing_patterns_are_a_usage_error() {
  local output rc
  output="$("${collector}" 2>&1)" && rc=0 || rc=$?
  expect_eq 2 "${rc}"
  expect_output_contains 'usage:' "${output}"
}
