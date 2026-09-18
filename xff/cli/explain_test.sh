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
# from XFF_TEST_USER_CONFIG, tagging each flag with provenance and omitting inactive-style
# lines. Exercises the real cli/main.cc wiring end to end (mboworks/bashtest).

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

# Path to the built xff binary in the test runfiles.
_xff_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/testing/xff"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff -path '*xff/cli/testing/xff' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

test::config_quoted_patterns_match_the_same_files_as_cli_arguments() {
  local dir cfg explicit cli configured
  dir="$(test_tmpdir quoted_config)"
  cfg="${dir}/user.ini"
  explicit="${dir}/task.xffrc"
  mkdir -p "${dir}/files"
  : >"${dir}/files/#hash name"
  : >"${dir}/files/plain"
  cat >"${cfg}" <<'INI'
# A comment
[quoted] # Another comment
-name '#hash name' # Literal hash inside quotes
INI
  cp "${cfg}" "${explicit}"
  cli="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}/files" -name '#hash name')"
  configured="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" "${dir}/files" --config=quoted)"
  expect_eq "${cli}" "${configured}"
  configured="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}/files" --xffrc="${explicit}" --config=quoted)"
  expect_eq "${cli}" "${configured}"
}

test::config_comments_do_not_hide_a_quoted_exec_terminator() {
  local cfg out
  cfg="${TEST_TMPDIR}/quoted_exec.xffrc"
  cat >"${cfg}" <<'INI'
-exec /usr/bin/printf '%s' '#literal' \; ; Comment after terminator
INI
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${cfg}" --xffrc="${cfg}" --allow-exec)"
  expect_eq '#literal' "${out}"
}

test::explain_reflects_effective_config() {
  local cfg="${TEST_TMPDIR}/xff_config"
  # Unsectioned defaults always apply; a named config (myx:) applies only when --config=myx is active; an
  # inactive named config (other:) stays inert. (Bare preset selectors like `xff:` are rejected -
  # see explain_rejects_preset_overloading_config.)
  printf -- '--sort\n\n[myx]\n--hidden\n[other]\n--no-hidden' >"${cfg}"
  local out
  out="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" --config=myx --explain)"
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
  out="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" --config=xff "${dir}" -name a.txt 2>&1)"
  expect_matches 'cannot change a preset' "${out}"
  # --explain records the drop in its trace, does not resolve the bare-preset flag, keeps defaults.
  out="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" --config=xff --explain 2>"${dir}/explain.err")"
  [[ ! -s "${dir}/explain.err" ]] || fail 'explain repeated its dropped-line trace as execution warnings'
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
  with_cfg="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" "${dir}" -name a.txt)"
  expect_eq "{" "${with_cfg:0:1}"
  # Without a config, the default print stays plain (an absolute path, not '{').
  local plain
  plain="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -name a.txt)"
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
  out="$(cd "${proj}" && XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "${xff}" . -name a.txt 2>&1)"
  expect_not_matches '\{' "${out}"      # --format=jsonl NOT applied -> no object brace
  expect_not_matches 'ignored' "${out}" # no project-config note (the mechanism is gone)
}

test::xffrc_flag_loads_an_explicit_file() {
  # An explicit --xffrc=FILE is loaded (naming it is the consent) as its own tier, surfacing in
  # --explain with `xffrc` provenance.
  local rc="${TEST_TMPDIR}/explicit.rc"
  printf -- '--color=never\n' >"${rc}"
  local out
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --xffrc="${rc}" --explain)"
  local lines=()
  local line
  while IFS= read -r line; do lines+=("${line}"); done <<<"${out}"
  expect_contains "$(printf 'xffrc\t--color=never')" "${lines[@]}"
}

test::explicit_ini_globals_apply_without_selecting_named_sections() {
  local cfg="${TEST_TMPDIR}/explicit_sections.xffrc"
  local out
  printf -- '%s\n' '--hidden' '[quiet]' '--color=never' >"${cfg}"
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --xffrc="${cfg}" --explain)"
  expect_matches 'xffrc[[:space:]]+--hidden' "${out}"
  expect_not_matches 'xffrc[[:space:]]+--color=never' "${out}"
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --xffrc="${cfg}" --config=quiet --explain)"
  expect_matches 'xffrc[[:space:]]+--hidden' "${out}"
  expect_matches 'xffrc[[:space:]]+--color=never' "${out}"
}

test::config_expands_at_the_selector_position() {
  local cfg="${TEST_TMPDIR}/ordered_config"
  printf -- '--jobs=1\n\n[plain]\n--color=never' >"${cfg}"
  local out expected
  out="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" --color=always --config=plain --sort --explain)"
  expected="$(printf 'user\t--jobs=1\ncli\t--color=always\ncli\t--config=plain\n# at %s:4 [plain]\nuser\t--color=never\ncli\t--sort' "${cfg}")"
  expect_output_contains "${expected}" "${out}"
}

test::no_user_config_preserves_required_user_globals() {
  local cfg="${TEST_TMPDIR}/user_skip_denied"
  printf -- '--format=jsonl\n' >"${cfg}"
  local out rc
  out="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" --no-user-config --explain 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_matches 'user[[:space:]]+--format=jsonl' "${out}"
}

test::authorized_no_user_config_suppresses_user_defaults_but_keeps_explicit_xffrc() {
  local cfg="${TEST_TMPDIR}/user_skip_allowed"
  local explicit="${TEST_TMPDIR}/explicit_with_no_config"
  printf -- '%s\n' '--no-require-user-globals' '--format=jsonl' >"${cfg}"
  printf -- '--color=never\n' >"${explicit}"
  local out
  out="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" --no-user-config --xffrc="${explicit}" --explain)"
  expect_not_matches 'user[[:space:]]+--format=jsonl' "${out}"
  expect_matches 'xffrc[[:space:]]+--color=never' "${out}"
}

test::combined_skip_obeys_user_requirement_and_keeps_explicit_config() {
  local cfg="${TEST_TMPDIR}/combined_user_skip"
  local explicit="${TEST_TMPDIR}/combined_explicit"
  local out rc
  printf -- '%s\n' '--require-user-globals' '--format=jsonl' >"${cfg}"
  out="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" --no-config --explain 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_matches 'user[[:space:]]+--format=jsonl' "${out}"
  printf -- '%s\n' '--no-require-user-globals' '--format=jsonl' >"${cfg}"
  printf -- '%s\n' '--color=never' >"${explicit}"
  out="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" --no-config --xffrc="${explicit}" --explain)"
  expect_not_matches 'user[[:space:]]+--format=jsonl' "${out}"
  expect_matches 'xffrc[[:space:]]+--color=never' "${out}"
}

test::require_controls_are_config_only() {
  local flag out rc
  for flag in --require-system-globals --no-require-system-globals --require-user-globals --no-require-user-globals; do
    out="$("$(_xff_bin)" "${flag}" --explain 2>&1)" && rc=0 || rc=$?
    expect_eq "2" "${rc}"
    expect_output_contains "${flag}" "${out}"
  done
}

test::config_local_override_warns_but_last_value_still_wins() {
  local cfg="${TEST_TMPDIR}/local_override"
  printf -- '--color=always\n--color=never\n' >"${cfg}"
  local out
  out="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" --explain 2>&1)"
  expect_output_contains 'warning: setting --color is overridden within user config globals' "${out}"
  expect_matches 'user[[:space:]]+--color=always' "${out}"
  expect_matches 'user[[:space:]]+--color=never' "${out}"
}

test::xffrc_dangerous_line_is_inert_unless_armed() {
  # The --xffrc tier is non-arming: a sensitive -exec carried by the file is dropped (inert) with a
  # "needs --allow-exec" note unless --allow-exec is passed from a trusted tier (here, the CLI). A
  # safe line on the same file still applies.
  local rc="${TEST_TMPDIR}/arm.rc"
  printf -- '--color=never\n-exec echo {} \\;\n' >"${rc}"
  local out
  # Unarmed: the -exec is dropped; the safe --color line survives with xffrc provenance.
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --xffrc="${rc}" --explain 2>&1)"
  expect_output_contains 'needs --allow-exec' "${out}"
  expect_matches 'xffrc[[:space:]]+--color=never' "${out}"
  # Armed from the CLI: the -exec is honored, so the "needs --allow-exec" drop note is gone.
  out="$(XFF_TEST_USER_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --allow-exec --xffrc="${rc}" --explain 2>&1)"
  expect_output_not_contains 'needs --allow-exec' "${out}"
}

test::repeated_user_section_is_rejected_when_selected() {
  local cfg out rc
  cfg="${TEST_TMPDIR}/repeated-user.ini"
  printf '[dev]\n--hidden\n[other]\n--color=auto\n[dev]\n--color=never\n' >"${cfg}"
  out="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" --config=dev --explain 2>&1)" && rc=0 || rc=$?
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
  out="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" --config=dev --xffrc="${first}" --xffrc="${second}" --explain 2>&1)"
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
  out="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" --config=dev --xffrc="${file}" --explain 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "${file}:3" "${out}"
  expect_output_contains 'selected config [checks] is disabled' "${out}"
}

test::directory_policy_limits_output_and_protects_roots() {
  local dir cfg out status
  dir="$(test_tmpdir directory_policy)"
  dir="$(cd "${dir}" && pwd -P)"
  cfg="${dir}/user.ini"
  mkdir -p "${dir}/source" "${dir}/output/nested" "${dir}/scratch"
  printf 'source\n' >"${dir}/source/file"
  cat >"${cfg}" <<INI
--block-policy-categories=temp,output
--temp-root=${dir}/scratch
--output-root=${dir}/output
--block-file-writing
--block-file-deletion
--block-directory-deletion
--block-output-file-overwrite
[write]
-fprint ${dir}/output/nested/list
INI
  XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" "${dir}/source" --config=write
  expect_eq "${dir}/source/file" "$(tail -1 "${dir}/output/nested/list")"
  XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" "${dir}/source" -fprint "${dir}/scratch/list"
  expect_eq "${dir}/source/file" "$(tail -1 "${dir}/scratch/list")"
  status=0
  XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" "${dir}/source" --config=write >/dev/null 2>&1 || status=$?
  expect_eq "2" "${status}"
  status=0
  XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" "${dir}/source" -fprint "${dir}/outside" >/dev/null 2>&1 || status=$?
  expect_eq "2" "${status}"
  [[ ! -e "${dir}/outside" ]] || fail "created outside output scope"
  out="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" "${dir}/source" -fprint "${dir}/output/preview" --dry-run)"
  expect_matches 'would create .*preview' "${out}"
  [[ ! -e "${dir}/output/preview" ]] || fail "dry run wrote output"
  status=0
  XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" "${dir}/output" -maxdepth 0 -delete >/dev/null 2>&1 || status=$?
  expect_eq "2" "${status}"
  [[ -d "${dir}/output" ]] || fail "removed protected root"
  status=0
  XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" . --output-root="${dir}" >/dev/null 2>&1 || status=$?
  expect_eq "2" "${status}"
}

test::rc_modes_and_named_sections_use_real_files() {
  local dir cfg out
  dir="$(test_tmpdir rc_modes)"
  cfg="${dir}/user.ini"
  mkdir -p "${dir}/root/sub"
  printf '[checks]\n-type f\n' >"${dir}/root/.xffrc"
  printf '[checks]\n--color=never\n' >"${dir}/root/sub/.xffrc"
  : >"${cfg}"
  out="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" "${dir}/root" --explain)"
  expect_output_contains "$(printf 'rc-mode\toff')" "${out}"
  expect_output_not_contains "${dir}/root/.xffrc" "${out}"
  out="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" "${dir}/root" --rc --config=checks --explain)"
  expect_output_contains "$(printf 'rc-mode\troots')" "${out}"
  expect_output_contains "${dir}/root/.xffrc" "${out}"
  expect_output_not_contains "${dir}/root/sub/.xffrc" "${out}"
  expect_output_contains "$(printf 'xffrc\t-type')" "${out}"
  out="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" "${dir}/root" --rc+ --config=checks --explain)"
  expect_output_contains "$(printf 'rc-mode\trecursive')" "${out}"
  expect_output_contains "${dir}/root/sub/.xffrc" "${out}"
  expect_output_contains "$(printf 'xffrc\t--color=never')" "${out}"
  out="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" "${dir}/root" --rc+ --rc- --explain)"
  expect_output_not_contains "${dir}/root/.xffrc" "${out}"
}

test::rc_globals_fail_before_writes_and_need_a_trusted_grant() {
  local dir cfg out status
  dir="$(test_tmpdir rc_globals)"
  cfg="${dir}/user.ini"
  mkdir -p "${dir}/root"
  printf -- '--hidden\n[quiet]\n--color=never\n' >"${dir}/root/.xffrc"
  : >"${cfg}"
  status=0
  out="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" "${dir}/root" --rc -fprint "${dir}/output" 2>&1)" || status=$?
  expect_eq 2 "${status}"
  expect_output_contains 'globals require --allow-rc-globals' "${out}"
  expect_output_contains "${dir}/root/.xffrc" "${out}"
  [[ ! -e "${dir}/output" ]] || fail "wrote output after rejecting config"
  printf -- '--rc\n--allow-rc-globals\n--no-require-user-globals\n' >"${cfg}"
  out="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" "${dir}/root" --config=quiet --explain)"
  expect_output_contains "$(printf 'xffrc\t--hidden')" "${out}"
  expect_output_contains "$(printf 'xffrc\t--color=never')" "${out}"
  out="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" "${dir}/root" --no-config --rc+ --explain)"
  expect_output_contains "$(printf 'rc-mode\toff')" "${out}"
  out="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" "${dir}/root" --no-config --xffrc="${dir}/root/.xffrc" --explain)"
  expect_output_contains "$(printf 'xffrc\t--hidden')" "${out}"
  status=0
  out="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" --allow-rc-globals --explain 2>&1)" || status=$?
  expect_eq 2 "${status}"
  expect_output_contains 'config-only' "${out}"
}

test::xffrc_admission_uses_selector_order_for_explicit_and_discovered_files() {
  local dir cfg file mode out status
  dir="$(test_tmpdir admission_order)"
  cfg="${dir}/user.ini"
  mkdir -p "${dir}/root"
  file="${dir}/root/.xffrc"
  printf '[deny]\n--no-allow-xffrc\n[allow]\n--allow-xffrc\n[wrapper]\n--config=deny\n' >"${cfg}"
  printf '[checks]\n--hidden\n' >"${file}"
  for mode in "--xffrc=${file}" --rc --rc+; do
    status=0
    out="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" "${dir}/root" --config=allow --config=deny "${mode}" --config=checks --explain 2>&1)" || status=$?
    expect_eq 2 "${status}"
    expect_output_contains '.xffrc loading is disabled' "${out}"
    out="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" "${dir}/root" --config=deny --config=allow "${mode}" --config=checks --explain)"
    expect_output_contains "$(printf 'xffrc\t--hidden')" "${out}"
    status=0
    out="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" "${dir}/root" --config=allow --config=wrapper "${mode}" --explain 2>&1)" || status=$?
    expect_eq 2 "${status}"
    expect_output_contains '.xffrc loading is disabled' "${out}"
  done
  # A malformed explicit file must not be parsed after admission has been denied.
  printf "'unfinished\n" >"${file}"
  status=0
  out="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" "${dir}/root" --config=allow --config=deny --xffrc="${file}" --explain 2>&1)" || status=$?
  expect_eq 2 "${status}"
  expect_output_contains '.xffrc loading is disabled' "${out}"
  expect_output_not_contains 'unterminated' "${out}"
}

_expect_flavor_current() {
  python3 -c '
import sys
behavior, expected = sys.argv[1:]
rows = [line.split() for line in sys.stdin if line.startswith(behavior + " ")]
assert len(rows) == 1 and rows[0][-1] == expected, (behavior, expected, rows)
' "$1" "$2" <<<"$3"
}

test::explain_current_flavors_include_automatic_config_defaults() {
  local dir system user out
  dir="$(test_tmpdir effective_defaults)"
  system="${dir}/system.ini"
  user="${dir}/user.ini"
  printf '%s\n' '--no-hidden' >"${system}"
  printf '%s\n' '--case=insensitive' >"${user}"
  out="$(XFF_TEST_SYSTEM_CONFIG="${system}" XFF_TEST_USER_CONFIG="${user}" "$(_xff_bin)" --explain)"
  _expect_flavor_current 'hidden dotfiles' skip "${out}" || return
  _expect_flavor_current 'letter case' insensitive "${out}"
}

test::explain_current_flavors_follow_selector_and_cli_order() {
  local dir user out
  dir="$(test_tmpdir effective_selectors)"
  user="${dir}/user.ini"
  printf '%s\n' '[outer]' '--config=inner' '[inner]' '--case=insensitive' >"${user}"
  out="$(XFF_TEST_USER_CONFIG="${user}" "$(_xff_bin)" --case=sensitive --config=outer --explain)"
  _expect_flavor_current 'letter case' insensitive "${out}" || return
  out="$(XFF_TEST_USER_CONFIG="${user}" "$(_xff_bin)" --config=outer --case=sensitive --explain)"
  _expect_flavor_current 'letter case' sensitive "${out}"
}

test::explain_current_flavors_include_explicit_files_and_obey_skips() {
  local dir user explicit out
  dir="$(test_tmpdir effective_explicit)"
  user="${dir}/user.ini"
  explicit="${dir}/project.rc"
  printf '%s\n' '--no-require-user-globals' '--no-hidden' >"${user}"
  printf '%s\n' '--case=insensitive' >"${explicit}"
  out="$(XFF_TEST_USER_CONFIG="${user}" "$(_xff_bin)" --no-user-config --xffrc="${explicit}" --explain)"
  _expect_flavor_current 'hidden dotfiles' show "${out}" || return
  _expect_flavor_current 'letter case' insensitive "${out}" || return
  printf '%s\n' '--require-user-globals' '--no-hidden' >"${user}"
  out="$(XFF_TEST_USER_CONFIG="${user}" "$(_xff_bin)" --no-user-config --explain)"
  _expect_flavor_current 'hidden dotfiles' skip "${out}"
}

test::explain_composes_actions_without_running_them_or_hoisting_arguments() {
  local dir user out
  dir="$(test_tmpdir effective_actions)"
  user="${dir}/user.ini"
  printf '%s\n' '[actions]' '-exec echo --case=insensitive \;' '-delete' >"${user}"
  : >"${dir}/keep.txt"
  out="$(XFF_TEST_USER_CONFIG="${user}" "$(_xff_bin)" "${dir}/keep.txt" --config=actions --explain)"
  _expect_flavor_current 'letter case' sensitive "${out}" || return
  [[ -f "${dir}/keep.txt" ]] || fail 'explain executed a configured action'
}

test::execution_warns_for_unarmed_files_and_untrusted_profile_selection() {
  local dir user explicit target out
  dir="$(test_tmpdir dropped_actions)"
  user="${dir}/user.ini"
  explicit="${dir}/project.rc"
  target="${dir}/keep.txt"
  printf '%s\n' '[remove]' '-delete' >"${user}"
  : >"${target}"

  printf '%s\n' '-delete' >"${explicit}"
  out="$(XFF_TEST_USER_CONFIG="${user}" "$(_xff_bin)" "${target}" --xffrc="${explicit}" 2>&1)"
  expect_output_contains 'inert unless armed with --allow-exec' "${out}"
  [[ -f "${target}" ]] || fail 'an unarmed explicit file deleted its target'

  printf '%s\n' '--config=remove' >"${explicit}"
  out="$(XFF_TEST_USER_CONFIG="${user}" "$(_xff_bin)" "${target}" --xffrc="${explicit}" 2>&1)"
  expect_output_contains 'denied by config policy' "${out}"
  [[ -f "${target}" ]] || fail 'an untrusted profile selection deleted its target'
}

test::effective_safety_explains_admin_blocks_profiles_and_recursive_roots() {
  local dir system user out
  dir="$(test_tmpdir effective_safety)"
  dir="$(cd "${dir}" && pwd -P)"
  system="${dir}/system.ini"
  user="${dir}/user.ini"
  mkdir -p "${dir}/temp/output"
  cat >"${system}" <<'INI'
--block-policy-categories=archive,temp,output
--temp-root=${TMPDIR}/temp
--output-root=${TMPDIR}/temp/output
--block-file-writing
--safe
[locked]
--block-execution
INI
  cat >"${user}" <<'INI'
--no-safe-block-file-writing
--no-safe-block-temp-file-writing
--no-safe-block-output-file-writing
[relaxed]
--no-safe-block-execution
INI
  out="$(TMPDIR="${dir}" XFF_TEST_SYSTEM_CONFIG="${system}" XFF_TEST_USER_CONFIG="${user}" \
    "$(_xff_bin)" --config=locked --config=relaxed --explain)"
  expect_output_contains $'policy\tsystem\tarchive,temp,output' "${out}" || return
  expect_output_contains $'safety\tfile-writing\tblock\tblock\tallow\tunconditional block' "${out}" || return
  expect_output_contains "system ${system}:4 (--block-file-writing)" "${out}" || return
  expect_output_contains "user ${user}:1 (--no-safe-block-file-writing)" "${out}" || return
  expect_output_contains $'safety\texecution\tblock\tblock\tallow\tunconditional block' "${out}" || return
  expect_output_contains "system ${system}:7 [locked] (--block-execution)" "${out}" || return
  expect_output_contains "user ${user}:5 [relaxed] (--no-safe-block-execution)" "${out}" || return
  expect_output_contains $'safety\ttemp-file-writing\tallow\tnone\tallow\tprofile permits' "${out}" || return
  expect_output_contains $'safety\toutput-file-writing\tallow\tnone\tallow\tprofile permits' "${out}" || return
  expect_output_contains "$(printf 'root\ttemp\t%s/temp\t' "${dir}")" "${out}" || return
  expect_output_contains "$(printf 'root\toutput\t%s/temp/output\t' "${dir}")" "${out}" || return
  expect_output_contains 'overlapping scope restrictions combine' "${out}" || return
  out="$(TMPDIR="${dir}" XFF_TEST_SYSTEM_CONFIG="${system}" XFF_TEST_USER_CONFIG="${user}" \
    "$(_xff_bin)" --no-system-config --no-safe --explain)"
  expect_output_contains $'safety\tfile-writing\tblock\tblock\tallow\tunconditional block' "${out}" || return
  expect_output_contains $'safety\texecution\tallow\tnone\tblock\tinactive profile' "${out}" || return
  expect_eq 'no' "$([[ ${out} == *'[locked]'* ]] && echo yes || echo no)"
}

test::explain_profile_inventory_keeps_validation_reasons_and_skip_state() {
  local dir user system out rc
  dir="$(test_tmpdir profile_inventory)"
  user="${dir}/user.ini"
  system="${dir}/system.ini"
  cat >"${user}" <<'INI'
--color=auto
[reports]
--summary=ext
[bad]
-type garbage
[dependent]
--config=bad
INI
  printf '%s\n' '[project]' "-name '*.cc'" >"${system}"
  out="$(XFF_TEST_USER_CONFIG="${user}" XFF_TEST_SYSTEM_CONFIG="${system}" \
    "$(_xff_bin)" --config=reports --no-system-config --explain 2>&1)"
  expect_output_contains $'profile\treports\tuser\tavailable\tselected\tdeclares=globals' "${out}" || return
  expect_output_contains $'profile\tproject\tsystem\tskipped' "${out}" || return
  expect_output_contains $'profile\tbad\tuser\tdisabled' "${out}" || return
  expect_output_contains 'references disabled config [bad]' "${out}" || return
  expect_output_contains "# at ${user}:3 [reports]" "${out}" || return
  rc=0
  out="$(XFF_TEST_USER_CONFIG="${user}" XFF_TEST_SYSTEM_CONFIG="${system}" \
    "$(_xff_bin)" --config=bad --explain 2>&1)" || rc=$?
  expect_eq 2 "${rc}" || return
  expect_output_contains 'selected config [bad] is disabled' "${out}"
}

test::explain_project_policy_and_environment_recipes() {
  local dir user system project out
  dir="$(test_tmpdir inspection_recipes)"
  user="${dir}/user.ini"
  system="${dir}/system.ini"
  project="${dir}/project.xffrc"
  cat >"${system}" <<'INI'
--require-system-globals
--block-execution
--block-file-deletion
INI
  cat >"${user}" <<'INI'
--block-policy-categories=output
--output-root="${HOME:?HOME must be set}/xff-results"
INI
  cat >"${project}" <<'INI'
[sources]
-type f -name '*.cc'
[dangerous]
-exec printf should-not-run \;
INI
  out="$(HOME="${dir}" XFF_TEST_USER_CONFIG="${user}" XFF_TEST_SYSTEM_CONFIG="${system}" \
    "$(_xff_bin)" . --xffrc="${project}" --config=sources --config=dangerous --no-system-config --no-safe --explain)"
  expect_output_contains $'profile\tsources\txffrc\tavailable\tselected\tdeclares=predicates' "${out}" || return
  expect_output_contains $'profile\tdangerous\txffrc\tavailable\tselected\tdeclares=actions' "${out}" || return
  expect_output_contains $'safety\texecution\tblock\tblock' "${out}" || return
  expect_output_contains $'safety\tfile-deletion\tblock\tblock' "${out}" || return
  expect_output_contains "$(printf 'root\toutput\t%s/xff-results\t' "${dir}")" "${out}" || return
  expect_output_contains 'dropped' "${out}"
}

test::explain_reports_inactive_cli_modifiers_without_rejecting_them() {
  local out
  out="$("$(_xff_bin)" . --count --explain)"
  expect_output_contains $'inactive-modifier\t--count\trequires a -grep action' "${out}" || return
  out="$("$(_xff_bin)" left right --compare=diff --diff-format=y --explain)"
  expect_output_contains 'tree diffs use unified output' "${out}" || return
  out="$("$(_xff_bin)" . --shards --shards-show=count --summary=ext --explain)"
  expect_output_contains $'inactive-modifier\t--shards-show=count' "${out}" || return
  out="$("$(_xff_bin)" . --shards --shards-show=count --summary=ext --summary=none --explain)"
  expect_output_not_contains 'inactive-modifier' "${out}"
}

test::explain_modifier_checks_respect_configured_consumers_and_dormant_defaults() {
  local dir user out
  dir="$(test_tmpdir modifiers)"
  user="${dir}/user.ini"
  cat >"${user}" <<'INI'
[search]
-grep x
[defaults]
--count
INI
  out="$(XFF_TEST_USER_CONFIG="${user}" "$(_xff_bin)" . --count --config=search --explain)"
  expect_output_not_contains 'inactive-modifier' "${out}" || return
  out="$(XFF_TEST_USER_CONFIG="${user}" "$(_xff_bin)" . --config=defaults --explain)"
  expect_output_not_contains 'inactive-modifier' "${out}" || return
  out="$(XFF_TEST_USER_CONFIG="${user}" "$(_xff_bin)" . --count --config=defaults --explain)"
  expect_output_not_contains 'inactive-modifier' "${out}" || return
  out="$("$(_xff_bin)" . -exec echo --count \; --explain)"
  expect_output_not_contains 'inactive-modifier' "${out}"
}

test::explain_resources_use_selected_config_without_traversal() {
  local dir cfg out
  dir="$(test_tmpdir resource_config)"
  cfg="${dir}/user.ini"
  cat >"${cfg}" <<'INI'
[resource-view]
--jobs=3
--format=aligned
--columns=hash,lines
--buffer=2KiB
INI
  out="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" "${dir}/absent" --config=resource-view --explain)"
  expect_output_contains $'directory-workers-per-walk\t3' "${out}" || return
  expect_output_contains $'content-field-occurrences\t2' "${out}" || return
  expect_output_contains $'listing-column-buffer\t2048 cell bytes' "${out}" || return
  out="$(XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" "${dir}/absent" --config=resource-view --columns=name --jobs=1 --explain)"
  expect_output_contains $'directory-workers-per-walk\t1' "${out}" || return
  expect_output_contains $'content-field-occurrences\t0' "${out}" || return
}

test_runner
