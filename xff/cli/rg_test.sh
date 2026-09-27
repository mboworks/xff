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
# End-to-end rg frontend contract, independent of installed rg or ambient configuration.
set -euo pipefail
# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

_bin() { echo "${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/testing/xff"; }
_check() {
  local expected_rc="$1" expected="$2" out rc
  shift 2
  out="$("$(_bin)" "$@" 2>&1)" && rc=0 || rc=$?
  expect_eq "${expected_rc}" "${rc}"
  expect_eq "${expected}" "${out}"
}
_tree() {
  local root
  root="$(test_tmpdir tree)"
  printf 'TODO one\nother\nTODO two TODO\n' >"${root}/a.cc"
  printf 'other\n' >"${root}/b.txt"
  : >"${root}/empty"
  echo "${root}"
}

test::rg_invocation_selects_short_options_help_and_errors() {
  local root out rc
  root="$(_tree)"
  ln -s "$(_bin)" "${root}/rg"
  out="$("${root}/rg" -nio todo "${root}/a.cc")"
  expect_eq $'1:TODO\n3:TODO\n3:TODO' "${out}"
  out="$("${root}/rg" --help)"
  expect_output_contains 'RIPGREP-STYLE SEARCHES' "${out}"
  expect_eq 'xff 0.0.0' "$("${root}/rg" -V)"
  out="$("${root}/rg" -e 2>&1)" && rc=0 || rc=$?
  expect_eq 2 "${rc}"
  expect_output_contains 'requires' "${out}"
  out="$("${root}/rg" absent "${root}/a.cc" 2>&1)" && rc=0 || rc=$?
  expect_eq 1 "${rc}"
  expect_eq '' "${out}"
}

test::rg_invocation_preserves_config_and_native_filter_boundaries() {
  local root out
  root="$(_tree)"
  ln -s "$(_bin)" "${root}/rg"
  out="$("${root}/rg" -I TODO "${root}" --xff -name '*.cc')"
  expect_eq $'TODO one\nTODO two TODO' "${out}"
  out="$("${root}/rg" -n TODO "${root}/a.cc" --config=xff)"
  expect_eq $'1:TODO one\n3:TODO two TODO' "${out}"
  printf '%s\n' '--line-number' >"${root}/user.ini"
  out="$(XFF_TEST_USER_CONFIG="${root}/user.ini" "${root}/rg" TODO "${root}/a.cc")"
  expect_eq $'1:TODO one\n3:TODO two TODO' "${out}"
  out="$("${root}/rg" TODO "${root}/a.cc" --no-match-output)"
  expect_eq "${root}/a.cc" "${out}"
}

test::rg_invocation_preserves_stdin_and_literal_mode_words() {
  local root out
  root="$(test_tmpdir invocation)"
  ln -s "$(_bin)" "${root}/rg"
  out="$(printf 'hit\nmiss\n' | "${root}/rg" hit)"
  expect_eq hit "${out}"
  printf '%s\n' '--xff' '+' 'help' >"${root}/words"
  expect_eq '--xff' "$("${root}/rg" -e --xff "${root}/words")"
  expect_eq '--xff' "$("${root}/rg" -- --xff "${root}/words")"
  expect_eq '+' "$("${root}/rg" -F + "${root}/words")"
  expect_eq 'help' "$("${root}/rg" help "${root}/words")"
}

test::native_subcommand_names_are_valid_rg_patterns() {
  local root pattern
  root="$(_tree)"
  printf 'help\nversion\n' >"${root}/words"
  for pattern in help version; do
    _check 0 "${pattern}" --rg "${pattern}" "${root}/words"
    _check 0 "${pattern}" --rg -e "${pattern}" "${root}/words"
  done
}

test::lines_case_prefixes_and_portions() {
  local root
  root="$(_tree)"
  _check 0 $'TODO one\nTODO two TODO' --rg TODO "${root}/a.cc"
  _check 0 $'1:TODO one\n3:TODO two TODO' --rg -n TODO "${root}/a.cc"
  _check 0 $'TODO\nTODO\nTODO' --rg -o TODO "${root}/a.cc"
  _check 0 $'TODO\nTODO\nTODO' --rg -io todo "${root}/a.cc"
  _check 1 '' --rg todo "${root}/a.cc"
  _check 0 $'TODO one\nTODO two TODO' --rg -S todo "${root}/a.cc"
  _check 0 "${root}/a.cc:TODO one"$'\n'"${root}/a.cc:TODO two TODO" --rg -H TODO "${root}/a.cc"
  _check 0 $'TODO one\nTODO two TODO' --rg -HI TODO "${root}/a.cc"
  _check 0 $'TODO one\nTODO two TODO' --rg -nN TODO "${root}/a.cc"
}

test::inversion_counts_filenames_and_exit_status() {
  local root
  root="$(_tree)"
  _check 0 other --rg -v TODO "${root}/a.cc"
  _check 0 other --rg -v TODO "${root}/b.txt"
  _check 0 other --rg -vo TODO "${root}/b.txt"
  _check 0 2 --rg -c TODO "${root}/a.cc"
  _check 0 3 --rg --count-matches TODO "${root}/a.cc"
  _check 0 3 --rg -co TODO "${root}/a.cc"
  _check 1 '' --rg -c TODO "${root}/b.txt"
  _check 0 "${root}/a.cc" --rg -l TODO "${root}/a.cc"
  _check 0 "${root}/b.txt" --rg --files-without-match TODO "${root}/b.txt"
  _check 0 "${root}/empty" --rg --files-without-match TODO "${root}/empty"
  _check 1 '' --rg -l TODO "${root}/empty"
  _check 0 '' --rg -q TODO "${root}/a.cc"
  _check 1 '' --rg -q missing "${root}/a.cc"
}

test::pattern_union_empty_pattern_files_and_fixed_strings() {
  local root
  root="$(_tree)"
  _check 0 $'TODO one\nother\nTODO two TODO' --rg -e TODO -e other "${root}/a.cc"
  _check 0 $'TODO one\nTODO two TODO' --rg -e TODO -e TODO "${root}/a.cc"
  _check 1 '' --rg -v -e TODO -e other "${root}/a.cc"
  printf 'TODO\r\nother' >"${root}/patterns"
  _check 0 $'TODO one\nother\nTODO two TODO' --rg -f "${root}/patterns" "${root}/a.cc"
  _check 1 '' --rg -f "${root}/empty" "${root}/a.cc"
  _check 0 "${root}/a.cc" --rg -f "${root}/empty" --files-without-match "${root}/a.cc"
  _check 0 $'TODO one\nother\nTODO two TODO' --rg -e '' "${root}/a.cc"
  _check 0 other --rg -x other "${root}/a.cc"
  _check 1 '' --rg -Fx TODO "${root}/a.cc"
  _check 0 $'TODO one\nTODO two TODO' --rg -w TODO "${root}/a.cc"
  _check 1 '' --rg -w TOD "${root}/a.cc"
  _check 1 '' --rg -F T.DO "${root}/a.cc"
}

test::filters_do_not_change_output_patterns() {
  local root
  root="$(_tree)"
  _check 0 $'TODO one\nTODO two TODO' --rg -I TODO "${root}" --xff -name '*.cc' -rxc other
  _check 1 '' --rg -I TODO "${root}" --xff -name '*.txt'
  _check 0 other --rg -Iv TODO "${root}" --xff -name '*.txt'
  _check 0 $'TODO one\nTODO two TODO' --rg -I TODO "${root}" --xff -name '*.cc' + -name '*.txt'
  _check 0 "${root}/a.cc" --rg TODO "${root}" --no-match-output
  _check 0 $'TODO one\nTODO two TODO' --rg -I -g '*.cc' TODO "${root}"
  _check 1 '' --rg -I -g '!*.cc' TODO "${root}"
}

test::positive_globs_override_hidden_entries_and_matching_ancestors() {
  local root file
  local files=(plain.txt .hidden.txt .secret/inside.txt .git/inside.txt ignored/inside.txt)
  root="$(test_tmpdir globs)"
  mkdir -p "${root}/.secret" "${root}/.git" "${root}/ignored"
  for file in "${files[@]}"; do
    printf 'hit\n' >"${root}/${file}"
  done
  printf 'ignored/\n' >"${root}/.ignore"
  _check 0 "${root}/.git/inside.txt"$'\n'"${root}/.hidden.txt"$'\n'"${root}/.secret/inside.txt"$'\n'"${root}/ignored/inside.txt"$'\n'"${root}/plain.txt" \
    --rg -l -g '*' hit "${root}" --sort=tree
  _check 0 "${root}/.hidden.txt"$'\n'"${root}/plain.txt" \
    --rg -l -g '*.txt' hit "${root}" --sort=tree
  _check 0 "${root}/.hidden.txt"$'\n'"${root}/plain.txt" \
    --rg -l -g '!*.txt' -g '*.txt' --no-hidden hit "${root}" --sort=tree
  _check 1 '' --rg -l -g '*' -g '!*.txt' hit "${root}"
  _check 1 '' --rg -l -g '.secret/*.txt' hit "${root}"
}

test::stdin_explicit_implicit_and_pattern_input() {
  local root
  root="$(_tree)"
  printf 'hit\nmiss\n' | _check 0 hit --rg hit
  printf 'hit\nmiss\n' | _check 0 hit --rg hit -
  printf 'TODO\n' | _check 0 $'TODO one\nTODO two TODO' --rg -f - "${root}/a.cc"
  printf 'none\n' | _check 1 '' --rg hit -
}

test::stdin_pattern_ownership_is_exclusive_and_keeps_directory_defaults() {
  local root out
  root="$(_tree)"
  printf 'hit\n' | _check 2 'xff: --rg cannot read patterns and search content from stdin simultaneously' --rg -f - -
  out="$(
    cd "${root}"
    printf 'TODO\n' | "$(_bin)" --rg -f -
  )"
  expect_output_contains 'a.cc:TODO one' "${out}"
  expect_output_contains 'a.cc:TODO two TODO' "${out}"
}

test::binary_empty_matches_context_and_max_columns() {
  local root out rc
  root="$(_tree)"
  printf 'before\0TODO\n' >"${root}/binary"
  _check 1 '' --rg TODO "${root}/binary"
  # Only matching keeps binary bytes out of the shell capture.
  _check 0 TODO --rg -ao TODO "${root}/binary"
  _check 0 '' --rg -o '^' "${root}/a.cc"
  _check 0 $'TODO one\nother\nTODO two TODO' --rg -C1 TODO "${root}/a.cc"
  _check 0 $'TODO one\nother\nTODO two TODO' --rg -A1 -B1 TODO "${root}/a.cc"
  _check 0 $'[Omitted long matching line]\n[Omitted long matching line]' --rg -M3 TODO "${root}/a.cc"
  out="$("$(_bin)" --rg -e '[' "${root}" 2>&1)" && rc=0 || rc=$?
  expect_eq 2 "${rc}"
  expect_matches 'xff:' "${out}"
}

test::only_matching_applies_max_columns_to_each_portion() {
  local root
  root="$(_tree)"
  printf 'hit abcdef abcdef\n' >"${root}/long"
  _check 0 $'hit\n[Omitted long matching line]\n[Omitted long matching line]' \
    --rg -o -M3 'hit|abcdef' "${root}/long"
  _check 0 'hit abcdef abcdef' --rg -M0 hit "${root}/long"
}

test::zero_threads_are_automatic_but_invalid_counts_still_fail() {
  local root out rc
  root="$(_tree)"
  _check 0 other --rg -j0 other "${root}/b.txt"
  _check 0 other --rg --threads=0 other "${root}/b.txt"
  out="$("$(_bin)" --rg --threads=-1 other "${root}/b.txt" 2>&1)" && rc=0 || rc=$?
  expect_eq 2 "${rc}"
  expect_output_contains 'expected a positive integer' "${out}"
  out="$("$(_bin)" --jobs=0 "${root}" 2>&1)" && rc=0 || rc=$?
  expect_eq 2 "${rc}"
  expect_output_contains 'expected a positive integer' "${out}"
}

test::explain_keeps_search_selection_active_without_line_output() {
  local root out
  root="$(_tree)"
  out="$("$(_bin)" --rg TODO "${root}" --files-without-match --no-match-output --explain)"
  expect_output_not_contains 'inactive-modifier' "${out}"
  out="$("$(_bin)" --rg TODO "${root}" --invert-match --summary --explain)"
  expect_output_not_contains 'inactive-modifier' "${out}"
}

test::errors_are_not_silent_or_action_fallbacks() {
  local root out rc
  root="$(_tree)"
  out="$("$(_bin)" --rg TODO "${root}" --xff -delete 2>&1)" && rc=0 || rc=$?
  expect_eq 2 "${rc}"
  expect_matches 'not actions' "${out}"
  expect_eq yes "$(if [[ -f "${root}/a.cc" ]]; then echo yes; fi)"
  out="$("$(_bin)" --rg -f "${root}/missing" "${root}" 2>&1)" && rc=0 || rc=$?
  expect_eq 2 "${rc}"
  out="$("$(_bin)" --rg TODO "${root}/missing" 2>&1)" && rc=0 || rc=$?
  expect_eq 2 "${rc}"
}

test::complete_ini_files_keep_native_syntax_and_mandatory_globals() {
  local root out rc
  root="$(_tree)"
  printf '%s\n' '--require-system-globals' '--block-file-deletion' '[search]' '--line-number' '-name *.cc' >"${root}/system.ini"
  printf '%s\n' '[search]' '--only-matching' >"${root}/user.ini"
  out="$(XFF_TEST_SYSTEM_CONFIG="${root}/system.ini" XFF_TEST_USER_CONFIG="${root}/user.ini" "$(_bin)" --rg --config=search -I TODO "${root}")"
  expect_eq $'1:TODO\n3:TODO\n3:TODO' "${out}"
  printf '%s\n' '--rg' >"${root}/system.ini"
  out="$(XFF_TEST_SYSTEM_CONFIG="${root}/system.ini" "$(_bin)" --rg TODO "${root}" 2>&1)" && rc=0 || rc=$?
  expect_eq 2 "${rc}"
  expect_matches 'command.line' "${out}"
}

test::plus_or_uses_xff_style_and_keeps_find_compatibility() {
  local root out rc
  root="$(_tree)"
  _check 0 "${root}/a.cc" --xff "${root}/a.cc" -false + -true
  _check 0 "${root}/a.cc" "${root}/a.cc" -false + -true
  _check 0 "${root}/a.cc" --config=find "${root}/a.cc" -false -o -true
  out="$("$(_bin)" --config=find "${root}/a.cc" -false + -true 2>&1)" && rc=0 || rc=$?
  expect_eq 2 "${rc}"
  expect_output_contains "'+' is an xff extension" "${out}"
  _check 0 "${root}/a.cc" --config=find --config=xff "${root}/a.cc" -false + -true
  printf '%s\n' '[selection]' '-false + -true' >"${root}/user.ini"
  out="$(XFF_TEST_USER_CONFIG="${root}/user.ini" "$(_bin)" --config=find --config=selection "${root}/a.cc" 2>&1)" && rc=0 || rc=$?
  expect_eq 2 "${rc}"
  expect_output_contains "'+' is an xff extension" "${out}"
}

test::types_custom_definitions_and_listing() {
  local root out
  root="$(_tree)"
  _check 0 $'TODO one\nTODO two TODO' --rg -I -tcpp TODO "${root}"
  _check 1 '' --rg -tcpp -g '*.txt' TODO "${root}"
  _check 0 $'TODO one\nTODO two TODO' --rg -I --type-add 'local:*.cc' -tlocal TODO "${root}"
  out="$("$(_bin)" --rg --type-list)"
  expect_output_contains 'cpp: ' "${out}"
  expect_output_contains 'py: ' "${out}"
}

test::multiline_context_counts_and_heading_overrides() {
  local root
  root="$(_tree)"
  printf 'pre\nalpha one\nbeta two\npost\n' >"${root}/multi"
  _check 0 $'2:alpha one\n3:beta two' --rg -Un 'alpha[^\n]*\nbeta' "${root}/multi"
  _check 0 $'1-pre\n2:alpha one\n3:beta two\n4-post' --rg -Un -C1 'alpha[^\n]*\nbeta' "${root}/multi"
  _check 0 '1' --rg -Uc 'alpha[^\n]*\nbeta' "${root}/multi"
  _check 0 $'2:1:alpha one\n3:1:beta two' --rg -U --column 'alpha[^\n]*\nbeta' "${root}/multi"
  _check 0 $'alpha one\nbeta two' --rg -U --heading --no-heading 'alpha[^\n]*\nbeta' "${root}/multi"
}

test::explicit_search_text_modes_pass_through_cli_configuration() {
  local root
  root="$(_tree)"
  _check 0 $'TODO one\nTODO two TODO' --rg --unicode TODO "${root}/a.cc"
  _check 0 $'TODO one\nTODO two TODO' --rg --no-unicode TODO "${root}/a.cc"
  _check 0 $'TODO one\nTODO two TODO' --rg --no-unicode --unicode TODO "${root}/a.cc"
}

test::type_membership_uses_shared_language_overlays() {
  local root out rc
  root="$(test_tmpdir shared-types)"
  mkdir -p "${root}/code"
  printf 'hit header\n' >"${root}/code/header.h"
  printf 'hit exact\n' >"${root}/code/Build[1]"
  printf 'hit ordinary\n' >"${root}/code/ordinary.PY"
  printf 'hit specific\n' >"${root}/code/module.special.py"
  cat >"${root}/types.json" <<'JSON'
{
  "Only": {"extensions": ["h"], "filenames": ["Build[1]"], "aliases": ["project", "shared"]},
  "Companion": {"shared_extensions": ["h"], "aliases": ["shared"]},
  "Specific": {"extensions": ["special.py"]}
}
JSON
  _check 0 $'hit exact\nhit header' --rg -I -tproject hit "${root}/code" --sort=dir --lang-db="${root}/types.json"
  _check 0 'hit header' --rg -I -tCompanion hit "${root}/code" --lang-db="${root}/types.json"
  _check 1 '' --rg -I -tcpp hit "${root}/code" --lang-db="${root}/types.json"
  _check 0 'hit ordinary' --rg -I -tpy hit "${root}/code" --lang-db="${root}/types.json"
  _check 0 'header.h|Only' --lang-db="${root}/types.json" "${root}/code" -lang Companion -printf '%f|%{lang}\n'
  out="$("$(_bin)" --rg -tshared hit "${root}/code" --lang-db="${root}/types.json" 2>&1)" && rc=0 || rc=$?
  expect_eq 2 "${rc}"
  expect_output_contains "ambiguous file type alias 'shared'" "${out}"
}

test::shared_type_catalog_observes_ini_and_cli_order_in_both_modes() {
  local root out
  root="$(test_tmpdir shared-catalog)"
  mkdir "${root}/code"
  printf 'one\n' >"${root}/code/a.one"
  printf 'two\n' >"${root}/code/a.two"
  printf 'three\n' >"${root}/code/a.three"
  printf 'source\n' >"${root}/code/a.cc"
  printf '%s\n' '--type-add=local:*.one' >"${root}/system.ini"
  cat >"${root}/user.ini" <<'INI'
--type-clear=local --type-add=local:*.two
[narrow]
--type-clear=local --type-add=local:*.three
INI
  out="$(XFF_TEST_SYSTEM_CONFIG="${root}/system.ini" XFF_TEST_USER_CONFIG="${root}/user.ini" \
    "$(_bin)" --file-type=local "${root}/code" -printf '%f\n')"
  expect_eq a.two "${out}"
  out="$(XFF_TEST_SYSTEM_CONFIG="${root}/system.ini" XFF_TEST_USER_CONFIG="${root}/user.ini" \
    "$(_bin)" --rg -I -tlocal . "${root}/code")"
  expect_eq two "${out}"
  out="$(XFF_TEST_SYSTEM_CONFIG="${root}/system.ini" XFF_TEST_USER_CONFIG="${root}/user.ini" \
    "$(_bin)" --config=narrow --file-type=local "${root}/code" -printf '%f\n')"
  expect_eq a.three "${out}"
  out="$(XFF_TEST_SYSTEM_CONFIG="${root}/system.ini" XFF_TEST_USER_CONFIG="${root}/user.ini" \
    "$(_bin)" --rg --config=narrow -I -tlocal . "${root}/code")"
  expect_eq three "${out}"
  out="$(XFF_TEST_SYSTEM_CONFIG="${root}/system.ini" XFF_TEST_USER_CONFIG="${root}/user.ini" \
    "$(_bin)" --config=narrow --type-clear=local '--type-add=local:*.cc' \
    --file-type=local "${root}/code" -lang C++ -printf '%f|%{lang}\n')"
  expect_eq 'a.cc|C++' "${out}"
  out="$(XFF_TEST_SYSTEM_CONFIG="${root}/system.ini" XFF_TEST_USER_CONFIG="${root}/user.ini" \
    "$(_bin)" --rg --config=narrow --type-clear=local '--type-add=local:*.cc' \
    -I -tlocal . "${root}/code" --xff -lang C++)"
  expect_eq source "${out}"
}

test::mode_switches_keep_native_and_rg_short_options_distinct() {
  local root
  root="$(_tree)"
  _check 0 $'TODO\nTODO\nTODO' --rg TODO "${root}" --xff -type f \
    --rg -tcpp -o --xff -name '*.cc' + -name '*.h' --rg -I
  _check 0 "${root}/a.cc" --file-type=cpp "${root}" -type f
}

test::native_type_aliases_share_cli_and_ini_catalog_selection() {
  local root out
  root="$(_tree)"
  _check 0 "${root}/a.cc" -t cpp "${root}" -type f
  _check 0 "${root}/a.cc" -t=cpp "${root}" -type f
  _check 0 "$(printf '%s\n' "${root}/b.txt" "${root}/empty")" -Tcpp "${root}" -type f
  _check 0 "${root}/a.cc" -Tcpp -tcpp "${root}" -type f
  _check 0 "${root}/a.cc" '--type-add=help:*.cc' -t help "${root}" -type f
  printf '%s\n' '-t cpp' '[exclude]' '-Tcpp' >"${root}/user.ini"
  out="$(XFF_TEST_USER_CONFIG="${root}/user.ini" "$(_bin)" "${root}" -type f)"
  expect_eq "${root}/a.cc" "${out}"
  out="$(XFF_TEST_USER_CONFIG="${root}/user.ini" "$(_bin)" --config=exclude "${root}" -type f)"
  expect_eq '' "${out}"
}

test::find_style_rejects_native_type_aliases_from_cli_and_ini() {
  local root out rc
  root="$(_tree)"
  out="$("$(_bin)" --config=find -tcpp "${root}" 2>&1)" && rc=0 || rc="${?}"
  expect_eq 2 "${rc}"
  expect_output_contains 'not available in find mode' "${out}"
  out="$("$(_bin)" --config=find -Tcpp "${root}" --explain 2>&1)" && rc=0 || rc="${?}"
  expect_eq 2 "${rc}"
  expect_output_contains 'not available in find mode' "${out}"
  printf '%s\n' '--config=find' '-t cpp' >"${root}/user.ini"
  out="$(XFF_TEST_USER_CONFIG="${root}/user.ini" "$(_bin)" "${root}" 2>&1)" && rc=0 || rc="${?}"
  expect_eq 2 "${rc}"
  expect_output_contains 'not available in find mode' "${out}"
  cp "$(_bin)" "${root}/find"
  out="$("${root}/find" -tcpp "${root}" 2>&1)" && rc=0 || rc="${?}"
  expect_eq 2 "${rc}"
  expect_output_contains 'not available in find mode' "${out}"
  out="$("${root}/find" -tcpp --help 2>&1)" && rc=0 || rc="${?}"
  expect_eq 2 "${rc}"
  expect_output_contains 'not available in find mode' "${out}"
}

test::mode_reentry_preserves_follow_and_configuration_order() {
  local root out
  root="$(test_tmpdir follow)"
  mkdir "${root}/scan"
  printf 'hit\n' >"${root}/scan/only.txt"
  _check 0 hit --rg -I hit "${root}/scan" --xff -type f --rg -L
  printf '%s\n' '[physical]' '-P' >"${root}/user.ini"
  out="$(XFF_TEST_USER_CONFIG="${root}/user.ini" "$(_bin)" --rg -I hit "${root}/scan" \
    --xff -type f --config=physical --rg -L --explain \
    | awk -F '\t' '$2 == "-L" || $2 == "-P" { print $2 }')"
  expect_eq $'-P\n-L' "${out}"
  out="$(XFF_TEST_USER_CONFIG="${root}/user.ini" "$(_bin)" --rg -I hit "${root}/scan" \
    --xff -type f --rg -L --xff --config=physical --explain \
    | awk -F '\t' '$2 == "-L" || $2 == "-P" { print $2 }')"
  expect_eq $'-L\n-P' "${out}"
}

test::explicit_files_bypass_discovery_but_not_native_filters() {
  local root
  root="$(_tree)"
  _check 0 other --rg -tcpp other "${root}/b.txt"
  _check 0 other --rg '-g!*.txt' other "${root}/b.txt"
  _check 1 '' --rg -tcpp other "${root}/b.txt" --xff -name '*.cc'
  _check 0 "${root}/b.txt" --file-type=cpp "${root}/b.txt"
}

test_runner
