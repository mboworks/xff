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
  # Unsectioned defaults always apply; a named config (myx:) applies only when --config=myx is active; an
  # inactive named config (other:) stays inert. (Bare preset selectors like `xff:` are rejected -
  # see explain_rejects_preset_overloading_config.)
  printf -- '--sort\n\n[myx]\n--hidden\n[other]\n--no-hidden' >"${cfg}"
  local out
  out="$(XFF_CONFIG="${cfg}" "$(_xff_bin)" --config=myx --explain)"
  local lines=()
  local line
  while IFS= read -r line; do lines+=("${line}"); done <<<"${out}"

  # Unsectioned defaults apply always; myx: applies because --config=myx is active.
  expect_contains "$(printf 'user\t--sort')" "${lines[@]}"
  expect_contains "$(printf -- 'user\t--hidden')" "${lines[@]}"
  # The other: line is inert (its named config is not active): --no-hidden must not surface.
  expect_not_contains "$(printf 'user\t--no-hidden')" "${lines[@]}"
  # The CLI selector is echoed with cli provenance.
  expect_contains "$(printf 'cli\t--config=myx')" "${lines[@]}"
  # The source trace reports the consulted user config (found).
  expect_contains "$(printf 'source\tuser\tfound\t%s' "${cfg}")" "${lines[@]}"
}

test::explain_rejects_preset_overloading_config() {
  # A config file may not attach behavior to a built-in preset: a bare `xff:` / `find:` selector
  # (no named config) is dropped in every layer, so a plain preset run stays reproducible.
  local cfg="${TEST_TMPDIR}/xff_overload_config"
  printf -- '--sort\n\n[xff]\n--hidden' >"${cfg}"
  local dir
  dir="$(test_tmpdir ov)"
  mkdir -p "${dir}"
  : >"${dir}/a.txt"
  local out
  # A normal run warns on stderr that the bare-preset line was dropped.
  out="$(XFF_CONFIG="${cfg}" "$(_xff_bin)" --config=xff "${dir}" -name a.txt 2>&1)"
  expect_matches 'cannot change a preset' "${out}"
  # --explain records the drop in its trace, does not resolve the bare-preset flag, keeps defaults.
  out="$(XFF_CONFIG="${cfg}" "$(_xff_bin)" --config=xff --explain 2>&1)"
  expect_output_contains "'[xff]' in the user" "${out}"
  expect_not_matches 'user[[:space:]]--hidden' "${out}"
  expect_matches 'user[[:space:]]--sort' "${out}"
}

test::config_applies_to_the_run() {
  local dir
  dir="$(test_tmpdir tree)"
  mkdir -p "${dir}"
  : >"${dir}/a.txt"
  local cfg="${TEST_TMPDIR}/xff_apply_config"
  printf -- '--format=jsonl\n' >"${cfg}"
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
  printf -- '--format=jsonl\n' >"${proj}/.xffrc" # a would-be project file; must have no effect
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
  printf -- '--color=never\n' >"${rc}"
  local out
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --xffrc="${rc}" --explain)"
  local lines=()
  local line
  while IFS= read -r line; do lines+=("${line}"); done <<<"${out}"
  expect_contains "$(printf 'xffrc\t--color=never')" "${lines[@]}"
}

test::config_expands_at_the_selector_position() {
  local cfg="${TEST_TMPDIR}/ordered_config"
  printf -- '--jobs=1\n\n[plain]\n--color=never' >"${cfg}"
  local out expected
  out="$(XFF_CONFIG="${cfg}" "$(_xff_bin)" --color=always --config=plain --sort --explain)"
  expected="$(printf 'user\t--jobs=1\ncli\t--color=always\ncli\t--config=plain\nuser\t--color=never\ncli\t--sort')"
  expect_output_contains "${expected}" "${out}"
}

test::no_user_config_requires_permission_from_a_present_user_file() {
  local cfg="${TEST_TMPDIR}/user_skip_denied"
  printf -- '--format=jsonl\n' >"${cfg}"
  local out rc
  out="$(XFF_CONFIG="${cfg}" "$(_xff_bin)" --no-user-config --explain 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains '--no-require-user-config' "${out}"
}

test::authorized_no_user_config_suppresses_user_defaults_but_keeps_explicit_xffrc() {
  local cfg="${TEST_TMPDIR}/user_skip_allowed"
  local explicit="${TEST_TMPDIR}/explicit_with_no_config"
  printf -- '%s\n' '--no-require-user-config' '--format=jsonl' >"${cfg}"
  printf -- '--color=never\n' >"${explicit}"
  local out
  out="$(XFF_CONFIG="${cfg}" "$(_xff_bin)" --no-user-config --xffrc="${explicit}" --explain)"
  expect_not_matches 'user[[:space:]]+--format=jsonl' "${out}"
  expect_matches 'xffrc[[:space:]]+--color=never' "${out}"
}

test::combined_skip_obeys_user_requirement_and_keeps_explicit_config() {
  local cfg="${TEST_TMPDIR}/combined_user_skip"
  local explicit="${TEST_TMPDIR}/combined_explicit"
  local out rc
  printf -- '%s\n' '--require-user-config' '--format=jsonl' >"${cfg}"
  out="$(XFF_CONFIG="${cfg}" "$(_xff_bin)" --no-config --explain 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains '--no-require-user-config' "${out}"
  printf -- '%s\n' '--no-require-user-config' '--format=jsonl' >"${cfg}"
  printf -- '%s\n' '--color=never' >"${explicit}"
  out="$(XFF_CONFIG="${cfg}" "$(_xff_bin)" --no-config --xffrc="${explicit}" --explain)"
  expect_not_matches 'user[[:space:]]+--format=jsonl' "${out}"
  expect_matches 'xffrc[[:space:]]+--color=never' "${out}"
}

test::require_controls_are_config_only() {
  local flag out rc
  for flag in --require-system-config --no-require-system-config --require-user-config --no-require-user-config; do
    out="$("$(_xff_bin)" "${flag}" --explain 2>&1)" && rc=0 || rc=$?
    expect_eq "2" "${rc}"
    expect_output_contains "${flag}" "${out}"
  done
}

test::config_local_override_warns_but_last_value_still_wins() {
  local cfg="${TEST_TMPDIR}/local_override"
  printf -- '--color=always\n--color=never\n' >"${cfg}"
  local out
  out="$(XFF_CONFIG="${cfg}" "$(_xff_bin)" --explain 2>&1)"
  expect_output_contains 'warning: setting --color is overridden within user config globals' "${out}"
  expect_matches 'user[[:space:]]+--color=always' "${out}"
  expect_matches 'user[[:space:]]+--color=never' "${out}"
}

test::xffrc_dangerous_line_is_inert_unless_armed() {
  # The --xffrc tier is non-arming: a sensitive -exec carried by the file is dropped (inert) with a
  # "needs --allow-exec" note unless --allow-exec is passed from a trusted tier (here, the CLI). A
  # safe line on the same file still applies.
  local rc="${TEST_TMPDIR}/arm.rc"
  printf -- '--color=never\n-exec echo {} ;\n' >"${rc}"
  local out
  # Unarmed: the -exec is dropped; the safe --color line survives with xffrc provenance.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --xffrc="${rc}" --explain 2>&1)"
  expect_output_contains 'needs --allow-exec' "${out}"
  expect_matches 'xffrc[[:space:]]+--color=never' "${out}"
  # Armed from the CLI: the -exec is honored, so the "needs --allow-exec" drop note is gone.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --allow-exec --xffrc="${rc}" --explain 2>&1)"
  expect_output_not_contains 'needs --allow-exec' "${out}"
}

test::repeated_user_section_is_rejected_when_selected() {
  local cfg out rc
  cfg="${TEST_TMPDIR}/repeated-user.ini"
  printf '[dev]\n--hidden\n[other]\n--color=auto\n[dev]\n--color=never\n' >"${cfg}"
  out="$(XFF_CONFIG="${cfg}" "$(_xff_bin)" --config=dev --explain 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "${cfg}:5" "${out}"
  expect_output_contains 'declared more than once' "${out}"
  expect_output_contains 'selected config [dev] is disabled' "${out}"
}

test::same_name_is_refined_across_user_and_explicit_files() {
  local cfg first second out
  cfg="${TEST_TMPDIR}/refine-user.ini"
  first="${TEST_TMPDIR}/refine-first.xffrc"
  second="${TEST_TMPDIR}/refine-second.xffrc"
  printf '[dev]\n--color=auto\n--config=checks\n' >"${cfg}"
  printf '[dev]\n--color=always\n[checks]\n--hidden\n' >"${first}"
  printf '[dev]\n--color=never\n' >"${second}"
  out="$(XFF_CONFIG="${cfg}" "$(_xff_bin)" --config=dev --xffrc="${first}" --xffrc="${second}" --explain 2>&1)"
  expect_output_contains "$(printf 'user\t--color=auto')" "${out}"
  expect_output_contains "$(printf 'xffrc\t--color=always')" "${out}"
  expect_output_contains "$(printf 'xffrc\t--color=never')" "${out}"
  expect_output_contains "$(printf 'xffrc\t--hidden')" "${out}"
  expect_output_not_contains 'declared more than once' "${out}"
}

test::repeated_explicit_section_is_rejected_through_composition() {
  local cfg file out rc
  cfg="${TEST_TMPDIR}/compose-user.ini"
  file="${TEST_TMPDIR}/repeated.xffrc"
  printf '[dev]\n--config=checks\n' >"${cfg}"
  printf '[checks]\n--hidden\n[checks]\n' >"${file}"
  out="$(XFF_CONFIG="${cfg}" "$(_xff_bin)" --config=dev --xffrc="${file}" --explain 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "${file}:3" "${out}"
  expect_output_contains 'selected config [checks] is disabled' "${out}"
}

test_runner
