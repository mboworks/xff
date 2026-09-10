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
# Binary-level test: `xff --explain` writes the effective configuration resolved
# from XFF_CONFIG, tagging each flag with provenance and omitting inactive-style
# lines. Exercises the real cli/main.cc wiring end to end (mboworks/bashtest).

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

# Path to the built xff binary in the test runfiles.
_xff_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/xff"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff -path '*xff/cli/xff' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

test::explain_reflects_effective_config() {
  local cfg="${TEST_TMPDIR}/xff_config"
  # common: always applies; a named config (myx:) applies only when --config=myx is active; an
  # inactive named config (other:) stays inert. (Bare preset selectors like `xff:` are rejected -
  # see explain_rejects_preset_overloading_config.)
  printf 'common: --sort\nmyx: --feature=long\nother: --warn\n' >"${cfg}"
  local out
  out="$(XFF_CONFIG="${cfg}" "$(_xff_bin)" --config=myx --explain)"
  local lines=()
  local line
  while IFS= read -r line; do lines+=("${line}"); done <<<"${out}"

  # common: applies always; myx: applies because --config=myx is active.
  expect_contains "$(printf 'user\t--sort')" "${lines[@]}"
  expect_contains "$(printf 'user\t--feature=long')" "${lines[@]}"
  # The other: line is inert (its named config is not active): --warn must not surface.
  expect_not_contains "$(printf 'user\t--warn')" "${lines[@]}"
  # The CLI selector is echoed with cli provenance.
  expect_contains "$(printf 'cli\t--config=myx')" "${lines[@]}"
  # The source trace reports the consulted user config (found).
  expect_contains "$(printf 'source\tuser\tfound\t%s' "${cfg}")" "${lines[@]}"
}

test::explain_rejects_preset_overloading_config() {
  # A config file may not attach behavior to a built-in preset: a bare `xff:` / `find:` selector
  # (no named config) is dropped in every layer, so a plain preset run stays reproducible.
  local cfg="${TEST_TMPDIR}/xff_overload_config"
  printf 'xff: --feature=long\ncommon: --sort\n' >"${cfg}"
  local dir
  dir="$(test_tmpdir ov)"
  mkdir -p "${dir}"
  : >"${dir}/a.txt"
  local out
  # A normal run warns on stderr that the bare-preset line was dropped.
  out="$(XFF_CONFIG="${cfg}" "$(_xff_bin)" --config=xff "${dir}" -name a.txt 2>&1)"
  expect_matches 'cannot change a preset' "${out}"
  # --explain records the drop in its trace, does not resolve the bare-preset flag, keeps common:.
  out="$(XFF_CONFIG="${cfg}" "$(_xff_bin)" --config=xff --explain 2>&1)"
  expect_matches "dropped[[:space:]]'xff:' in the" "${out}"
  expect_not_matches 'user[[:space:]]--feature=long' "${out}"
  expect_matches 'user[[:space:]]--sort' "${out}"
}

test::config_applies_to_the_run() {
  local dir
  dir="$(test_tmpdir tree)"
  mkdir -p "${dir}"
  : >"${dir}/a.txt"
  local cfg="${TEST_TMPDIR}/xff_apply_config"
  printf 'common: --format=jsonl\n' >"${cfg}"
  # With the config, the default print emits a JSONL object (line starts with '{').
  local with_cfg
  with_cfg="$(XFF_CONFIG="${cfg}" "$(_xff_bin)" "${dir}" -name a.txt)"
  expect_eq "{" "${with_cfg:0:1}"
  # Without a config, the default print stays plain (an absolute path, not '{').
  local plain
  plain="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -name a.txt)"
  expect_ne "{" "${plain:0:1}"
}

test::local_xffrc_in_the_tree_is_ignored() {
  # The project layer was dropped (Option B, 2026-07-06): a .xffrc sitting in the search tree is
  # NOT config. It never loads, never warns, and cannot change the run. (Per-directory rules are an
  # ignore concern -- .gitignore / .xffignore -- not config; config is system + user + --xffrc only.)
  local proj
  proj="$(test_tmpdir localrc)"
  mkdir -p "${proj}"
  : >"${proj}/a.txt"
  printf 'common: --format=jsonl\n' >"${proj}/.xffrc" # a would-be project file; must have no effect
  local xff out
  xff="$(_xff_bin)"
  out="$(cd "${proj}" && XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" . -name a.txt 2>&1)"
  expect_not_matches '\{' "${out}"      # --format=jsonl NOT applied -> no object brace
  expect_not_matches 'ignored' "${out}" # no project-config note (the mechanism is gone)
}

test::xffrc_flag_loads_an_explicit_file() {
  # An explicit --xffrc=FILE is loaded (naming it is the consent) as its own tier, surfacing in
  # --explain with `xffrc` provenance.
  local rc="${TEST_TMPDIR}/explicit.rc"
  printf 'common: --color=never\n' >"${rc}"
  local out
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --xffrc="${rc}" --explain)"
  local lines=()
  local line
  while IFS= read -r line; do lines+=("${line}"); done <<<"${out}"
  expect_contains "$(printf 'xffrc\t--color=never')" "${lines[@]}"
}

test::config_expands_at_the_selector_position() {
  local cfg="${TEST_TMPDIR}/ordered_config"
  printf 'common: --jobs=1\nplain: --color=never\n' >"${cfg}"
  local out expected
  out="$(XFF_CONFIG="${cfg}" "$(_xff_bin)" --color=always --config=plain --sort --explain)"
  expected="$(printf 'user\t--jobs=1\ncli\t--color=always\ncli\t--config=plain\nuser\t--color=never\ncli\t--sort')"
  expect_output_contains "${expected}" "${out}"
}

test::no_user_config_requires_permission_from_a_present_user_file() {
  local cfg="${TEST_TMPDIR}/user_skip_denied"
  printf 'common: --format=jsonl\n' >"${cfg}"
  local out rc
  out="$(XFF_CONFIG="${cfg}" "$(_xff_bin)" --no-user-config --explain 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains '--allow-no-user-config' "${out}"
}

test::authorized_no_config_suppresses_user_defaults_but_keeps_explicit_xffrc() {
  local cfg="${TEST_TMPDIR}/user_skip_allowed"
  local explicit="${TEST_TMPDIR}/explicit_with_no_config"
  printf 'common: --allow-no-user-config --format=jsonl\n' >"${cfg}"
  printf 'common: --color=never\n' >"${explicit}"
  local out
  out="$(XFF_CONFIG="${cfg}" "$(_xff_bin)" --no-config --xffrc="${explicit}" --explain)"
  expect_not_matches 'user[[:space:]]+--format=jsonl' "${out}"
  expect_matches 'xffrc[[:space:]]+--color=never' "${out}"
}

test::xffrc_dangerous_line_is_inert_unless_armed() {
  # The --xffrc tier is non-arming: a sensitive -exec carried by the file is dropped (inert) with a
  # "needs --allow-exec" note unless --allow-exec is passed from a trusted tier (here, the CLI). A
  # safe line on the same file still applies.
  local rc="${TEST_TMPDIR}/arm.rc"
  printf 'common: --color=never\ncommon: -exec echo {} ;\n' >"${rc}"
  local out
  # Unarmed: the -exec is dropped; the safe --color line survives with xffrc provenance.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --xffrc="${rc}" --explain 2>&1)"
  expect_output_contains 'needs --allow-exec' "${out}"
  expect_matches 'xffrc[[:space:]]+--color=never' "${out}"
  # Armed from the CLI: the -exec is honored, so the "needs --allow-exec" drop note is gone.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --allow-exec --xffrc="${rc}" --explain 2>&1)"
  expect_output_not_contains 'needs --allow-exec' "${out}"
}

test_runner
