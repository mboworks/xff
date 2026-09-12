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
# Binary-level test of the strict find style: `--config=find` accepts only find's
# own vocabulary and rejects xff extensions (e.g. -println), while the default xff
# style accepts everything. Exercises cli/main.cc's ActiveStyle + EnforceStyle
# wiring end to end (mboworks/bashtest).

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

# A directory holding a single file, isolated from any ambient config.
_tree() {
  local dir
  dir="$(test_tmpdir "$1")"
  : >"${dir}/a.txt"
  echo "${dir}"
}

test::strict_find_style_rejects_xff_extensions() {
  local dir
  dir="$(_tree reject)"
  # -println is an xff extension; under --config=find it is rejected before the
  # run starts, with a self-documenting error and exit code 2.
  local out rc
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --config=find "${dir}" -println 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  local lines=()
  local line
  while IFS= read -r line; do lines+=("${line}"); done <<<"${out}"
  expect_contains \
    "xff: '-println' is an xff extension, not available under the find style (--config=find); use --config=xff" \
    "${lines[@]}"
}

test::strict_find_style_accepts_find_vocabulary() {
  local dir
  dir="$(_tree findvocab)"
  # A pure-find expression runs normally under --config=find (exit 0).
  local out rc
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --config=find "${dir}" -name a.txt 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
}

test::xff_style_accepts_xff_extensions() {
  local dir
  dir="$(_tree accept)"
  local xff
  xff="$(_xff_bin)"
  # The default (no --config) modern style runs -println fine.
  local rc
  XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" "${dir}" -name a.txt -println >/dev/null 2>&1 && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  # And explicitly under --config=xff.
  XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" --config=xff "${dir}" -name a.txt -println >/dev/null 2>&1 && rc=0 || rc=$?
  expect_eq "0" "${rc}"
}

# A tree with a .gitignore and a .ignore, plus a file each would exclude and one kept.
_ignore_tree() {
  local dir
  dir="$(test_tmpdir "$1")"
  mkdir -p "${dir}/build"
  printf 'build/\n' >"${dir}/.gitignore"
  printf '*.tmp\n' >"${dir}/.ignore"
  : >"${dir}/build/out.o" # excluded by .gitignore
  : >"${dir}/junk.tmp"    # excluded by .ignore
  : >"${dir}/keep.txt"    # kept
  echo "${dir}"
}

test::rg_style_respects_ignore_files_by_default() {
  local dir out
  dir="$(_ignore_tree rgignore)"
  # --config=rg turns on .gitignore + .ignore with no flags (ripgrep's headline default).
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --config=rg "${dir}" -type f 2>&1)"
  expect_output_contains "keep.txt" "${out}"
  expect_output_not_contains "out.o" "${out}"    # .gitignore build/
  expect_output_not_contains "junk.tmp" "${out}" # .ignore *.tmp
  # The find style leaves ignore files off, so it sees everything.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --config=find "${dir}" -type f 2>&1)"
  expect_output_contains "out.o" "${out}"
  expect_output_contains "junk.tmp" "${out}"
}

test::rg_style_accepts_xff_extensions() {
  local dir rc
  dir="$(_tree rgvocab)"
  # rg uses the full xff vocabulary; only the strict find style rejects extensions.
  XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --config=rg "${dir}" -name a.txt -println >/dev/null 2>&1 && rc=0 || rc=$?
  expect_eq "0" "${rc}"
}

test::argv0_fd_alias_is_a_plain_name_not_opinionated() {
  local dir out
  dir="$(_ignore_tree argv0fd)"
  # xfd was dropped and fd is not a built-in style: through an `fd`-named symlink, xff is the base
  # with no opinionated defaults (no magic remap to rg), so ignore files are NOT respected.
  ln -sf "$(_xff_bin)" "${TEST_TMPDIR}/fd"
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "${TEST_TMPDIR}/fd" "${dir}" -type f 2>&1)"
  expect_output_contains "keep.txt" "${out}"
  expect_output_contains "out.o" "${out}" # .gitignore NOT applied -> fd is a plain name (xff base)
}

test::argv0_custom_alias_activates_same_named_config() {
  local dir
  dir="$(test_tmpdir argv0alias)"
  : >"${dir}/a.txt"
  # A user config defines a NAMED block `mytool:` (not a preset). Invoked through a `mytool`
  # symlink, argv[0] selects that named config with no --config, so its --format=jsonl applies.
  local cfg="${TEST_TMPDIR}/argv0alias_cfg"
  printf 'mytool: --format=jsonl\n' >"${cfg}"
  ln -sf "$(_xff_bin)" "${TEST_TMPDIR}/mytool"
  local out
  out="$(XFF_CONFIG="${cfg}" "${TEST_TMPDIR}/mytool" "${dir}" -name a.txt 2>&1)"
  expect_output_contains "{" "${out}" # --format=jsonl from the mytool: block -> a JSON object
  # A plain xff run does not activate mytool:, so output stays plain (no JSON object).
  out="$(XFF_CONFIG="${cfg}" "$(_xff_bin)" "${dir}" -name a.txt 2>&1)"
  expect_output_not_contains "{" "${out}"
}

# A tree with a hidden dotfile alongside a visible one.
_hidden_tree() {
  local dir
  dir="$(test_tmpdir "$1")"
  : >"${dir}/.secret"
  : >"${dir}/visible.txt"
  echo "${dir}"
}

test::hidden_dotfiles_skipped_by_opinionated_styles() {
  local dir out xff
  dir="$(_hidden_tree hid)"
  xff="$(_xff_bin)"
  # find / xff (conservative) show dotfiles; rg (opinionated) skips them.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" --config=xff "${dir}" -type f 2>&1)"
  expect_output_contains ".secret" "${out}"
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" --config=rg "${dir}" -type f 2>&1)"
  expect_output_contains "visible.txt" "${out}"
  expect_output_not_contains ".secret" "${out}"
  # --hidden opts the opinionated style back in; --no-hidden opts the conservative out.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" --config=rg --hidden "${dir}" -type f 2>&1)"
  expect_output_contains ".secret" "${out}"
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" --config=xff --no-hidden "${dir}" -type f 2>&1)"
  expect_output_not_contains ".secret" "${out}"
}

test::case_smart_and_overrides() {
  local dir out xff
  dir="$(test_tmpdir casesmart)"
  : >"${dir}/README.md"
  xff="$(_xff_bin)"
  # Use the find style for globs (no FS-native folding), so the case flags are the only
  # case input on any platform. smart: an all-lowercase pattern folds; one with an
  # uppercase letter stays exact.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" --config=find --case=smart "${dir}" -name 'readme*' 2>&1)"
  expect_output_contains "README.md" "${out}"
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" --config=find --case=smart "${dir}" -name 'Readme*' 2>&1)"
  expect_output_not_contains "README.md" "${out}" # uppercase in pattern -> exact
  # sensitive (find default): a lowercase pattern does not match the uppercase file.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" --config=find "${dir}" -name 'readme*' 2>&1)"
  expect_output_not_contains "README.md" "${out}"
  # -i forces insensitive.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" --config=find -i "${dir}" -name 'readme*' 2>&1)"
  expect_output_contains "README.md" "${out}"
  # rg defaults to smart; a lowercase regex folds (regex ignores FS-native, so portable),
  # and -s- forces sensitive back off.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" --config=rg "${dir}" -regex '.*readme.*' 2>&1)"
  expect_output_contains "README.md" "${out}"
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "${xff}" --config=rg -s- "${dir}" -regex '.*readme.*' 2>&1)"
  expect_output_not_contains "README.md" "${out}"
}

test::help_styles_shows_the_flavor_comparison() {
  local out
  out="$("$(_xff_bin)" --help=styles 2>&1)"
  # The three style columns (find/xff/rg) and the key behavior rows are present; xfd was dropped.
  expect_output_contains "rg" "${out}"
  expect_output_not_contains "xfd" "${out}"
  expect_output_contains "hidden dotfiles" "${out}"
  expect_output_contains "letter case" "${out}"
  # Two-tier: the differing behaviors lead under a "Where the styles differ:" heading.
  expect_output_contains "Where the styles differ:" "${out}"
  # The syntax contract explains both dash-count scope and the deliberately narrow short-alias
  # policy. This is the boundary behind the spellings, not merely a list of current tokens.
  expect_output_contains "Flag and primary conventions:" "${out}"
  expect_output_contains "--flag          configures the whole run" "${out}"
  expect_output_contains "--flag=VALUE    assigns that global's value" "${out}"
  expect_output_contains "-primary:QUAL   qualifies that one expression node" "${out}"
  expect_output_contains "--define=NAME=VALUE" "${out}"
  expect_output_contains "-fuzzy:fzf:80% foo" "${out}"
  expect_output_contains "between -exec and its closing ; or +" "${out}"
  expect_output_contains "Short aliases are exceptional" "${out}"
  expect_output_contains "frequent symmetric basename/whole-path glob pair" "${out}"
  expect_output_contains "does not imply -r, -x, -c, or -f" "${out}"
  # hidden: find/xff show, rg skips (one row, so the whole-text regex stays on that line).
  expect_matches 'hidden dotfiles.*show.*show.*skip' "${out}"
  # The tail maps other finders' spellings onto xff's, which is the other half of "which flavor am I
  # in": every claim here is a real xff spelling, so a rename would break this test rather than
  # silently mislead someone arriving from fd.
  expect_output_contains "Coming from fd:" "${out}"
  expect_output_contains "-name PATTERN" "${out}"
  expect_output_contains "the primary IS the choice" "${out}" # why there is no --glob
  expect_output_contains "--no-hidden" "${out}"
  expect_output_contains "Coming from ripgrep:" "${out}"
}

test::explain_adds_the_current_flavor_column() {
  local out
  # --explain prints the flavor table with a `current` column resolved for this run.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" --config=rg --explain 2>&1)"
  expect_output_contains "current" "${out}"
  expect_output_contains "Relevant to this run:" "${out}" # the two-tier lead heading
  expect_matches 'letter case.*smart' "${out}"            # rg resolves case -> smart
}

test::no_ignore_wins_in_the_explain_flavor_column() {
  local out
  local NL=$'\n'
  # The ignore-files facet resolves the -g ternary: -g+ forces on (the row ends with the
  # current column), and -u / --no-ignore is the master switch that overrules it to off.
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" -g+ --explain 2>&1)"
  expect_matches "ignore files[^${NL}]*on[[:space:]]*(\$|${NL})" "${out}"
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" -g+ -u --explain 2>&1)"
  expect_matches "ignore files[^${NL}]*off[[:space:]]*(\$|${NL})" "${out}"
}

test::argv0_find_alias_defaults_to_strict_style() {
  local dir
  dir="$(_tree argv0)"
  # Invoked through a `find`-named symlink, the strict find style is the default
  # (no --config needed): an xff extension is rejected with exit 2.
  ln -sf "$(_xff_bin)" "${TEST_TMPDIR}/find"
  local out rc
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "${TEST_TMPDIR}/find" "${dir}" -println 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  local lines=()
  local line
  while IFS= read -r line; do lines+=("${line}"); done <<<"${out}"
  expect_contains \
    "xff: '-println' is an xff extension, not available under the find style (--config=find); use --config=xff" \
    "${lines[@]}"
}

test::argv0_xff_alias_defaults_to_modern_style() {
  local dir
  dir="$(_tree argv0xff)"
  # Invoked as xff (its real name), the modern style is the default: -println runs.
  local rc
  XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -name a.txt -println >/dev/null 2>&1 && rc=0 || rc=$?
  expect_eq "0" "${rc}"
}

test::config_multiple_predicates_are_evaluated() {
  local dir out config
  dir="$(test_tmpdir configpredicates)"
  : >"${dir}/foo"
  : >"${dir}/bar"
  : >"${dir}/other"
  config="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/testdata/config_validation/multiple/user.rc"
  out="$(XFF_CONFIG="${config}" "$(_xff_bin)" --config=both "${dir}" -type f -printf '%f\n')"
  expect_eq "" "${out}"
  out="$(XFF_CONFIG="${config}" "$(_xff_bin)" --config=either "${dir}" -type f -printf '%f\n')"
  expect_output_contains "foo" "${out}"
  expect_output_contains "bar" "${out}"
  expect_output_not_contains "other" "${out}"
  out="$(XFF_CONFIG="${config}" "$(_xff_bin)" --config=mixed "${dir}" -type f -printf '%f\n')"
  expect_eq "foo" "${out}"
}

test::type_choices_reject_unknown_values() {
  local dir out rc
  dir="$(_tree invalidtype)"
  out="$(XFF_CONFIG="${TEST_TMPDIR}/none" "$(_xff_bin)" "${dir}" -type garbage 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "unknown value 'garbage' for -type" "${out}"
}

test_runner
