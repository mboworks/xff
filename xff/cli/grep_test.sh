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
# End-to-end test of the -grep action (#79): xff prints each content line matching
# a regex as path:line:text (grep's piped form), skips binaries, composes with the
# find predicate set, and is an xff extension the find style rejects.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

# `test_tmpdir` allocates each tree under bashtest's managed scratch root. Its random suffix keeps
# test names out of printed paths, where a name could accidentally satisfy a negative assertion.

# A real newline for line-anchored expect_matches patterns: bashtest's expect_matches
# matches the whole text ([[ =~ ]]), so `^`/`$` anchor the whole output, not a line.
# `(^|${NL})X` and `X($|${NL})` restore grep's per-line anchoring (a real newline,
# because `\n` is not a portable ERE escape).
NL=$'\n'

_xff_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/xff"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff -path '*xff/cli/xff' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

# Runs xff, asserting it exited 0 (surfacing output on a nonzero exit) and echoing
# its output.
_run() {
  local out rc
  out="$("$(_xff_bin)" "$@" 2>&1)" && rc=0 || rc=$?
  if [[ "${rc}" != "0" ]]; then
    echo "xff $* exited ${rc}; output:"
    printf '%s\n' "${out}"
  fi
  expect_eq "0" "${rc}"
  printf '%s' "${out}"
}

_make_tree() {
  local path
  path="$(test_tmpdir tree)"
  printf 'first TODO line\nsecond line\nanother TODO here\n' >"${path}/a.txt"
  printf 'nothing to match here\n' >"${path}/b.txt"
  printf 'ELF\0hidden TODO\n' >"${path}/c.bin" # a NUL -> binary -> skipped
  echo "${path}"
}

test::grep_prints_path_line_text_for_each_match() {
  local root out
  root="$(_make_tree)"
  out="$(_run "${root}" -type f -grep 'TODO')"
  expect_matches "/a\.txt:1:first TODO line(\$|${NL})" "${out}"
  expect_matches "/a\.txt:3:another TODO here(\$|${NL})" "${out}"
  expect_not_matches 'second line' "${out}" # a non-matching line is not printed
}

test::grep_uses_regex() {
  local root out
  root="$(_make_tree)"
  out="$(_run "${root}" -type f -grep 'T.DO')" # '.' is a regex wildcard
  expect_matches "/a\.txt:1:first TODO line(\$|${NL})" "${out}"
}

test::grep_skips_binary_files() {
  local root out
  root="$(_make_tree)"
  out="$(_run "${root}" -type f -grep 'TODO')"
  expect_not_matches 'c\.bin' "${out}" # the binary's TODO is not reported
}

test::grep_composes_with_find_predicates() {
  local root out
  root="$(_make_tree)"
  out="$(_run "${root}" -name 'a.txt' -grep 'TODO')"
  expect_matches "/a\.txt:1:first TODO line(\$|${NL})" "${out}"
  expect_not_matches '/b\.txt' "${out}"
}

test::grep_is_rejected_by_the_find_style() {
  local root out rc
  root="$(_make_tree)"
  out="$("$(_xff_bin)" --config=find "${root}" -grep 'TODO' 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}" # an xff extension is a usage error under --config=find
  expect_matches 'xff extension' "${out}"
}

test::regextype_exact_matches_literally() {
  local root out
  root="$(test_tmpdir tree)"
  mkdir -p "${root}"
  printf 'price 3.50\nprice 3X50\n' >"${root}/p.txt"
  out="$(_run --regextype=EXACT "${root}" -type f -grep '3.50')" # '.' is literal under EXACT
  expect_matches "/p\.txt:1:price 3\.50(\$|${NL})" "${out}"
  expect_not_matches '3X50' "${out}" # the regex-wildcard match is gone
}

test::config_regextype_binds_the_expression_with_the_final_grammar() {
  local root cfg out
  root="$(test_tmpdir tree)"
  cfg="${TEST_TMPDIR}/exact_config"
  mkdir -p "${root}"
  printf 'price 3.50\nprice 3X50\n' >"${root}/p.txt"
  printf -- '--regextype=EXACT\n' >"${cfg}"
  out="$(XFF_CONFIG="${cfg}" _run "${root}" -type f -grep '3.50')"
  expect_matches "/p\.txt:1:price 3\.50(\$|${NL})" "${out}"
  expect_not_matches '3X50' "${out}"
}

test::re2_overrides_a_configured_grammar() {
  local root cfg out
  root="$(test_tmpdir tree)"
  cfg="${TEST_TMPDIR}/literal_config"
  mkdir -p "${root}"
  printf 'price 3X50\n' >"${root}/p.txt"
  printf -- '--regextype=EXACT\n' >"${cfg}"
  out="$(XFF_CONFIG="${cfg}" _run --re2 "${root}" -type f -grep '3.50')"
  expect_matches '3X50' "${out}"
}

test::bsd_e_uses_the_configured_extended_grammar() {
  local root cfg out
  root="$(test_tmpdir tree)"
  cfg="${TEST_TMPDIR}/bsd_e_config"
  mkdir -p "${root}"
  printf 'price 3.50\nprice 3X50\n' >"${root}/p.txt"
  printf -- '--regextype=EXACT\n' >"${cfg}"
  out="$(XFF_CONFIG="${cfg}" _run -E "${root}" -type f -grep '3.50')"
  expect_matches '3\.50' "${out}"
  expect_not_matches '3X50' "${out}"
}

test::pcre_reports_the_missing_lean_build_extra() {
  local root out rc
  root="$(test_tmpdir tree)"
  mkdir -p "${root}"
  out="$("$(_xff_bin)" --pcre "${root}" -type f -grep x 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_matches 'no pcre2 support' "${out}"
}

test::regextype_shglob_expands_brace_alternation() {
  local root out
  root="$(test_tmpdir tree)"
  mkdir -p "${root}"
  printf 'a TODO line\na FIXME line\na plain line\n' >"${root}/n.txt"
  # SHGLOB adds brace alternation over GLOB: {TODO,FIXME} matches either keyword.
  out="$(_run --regextype=SHGLOB "${root}" -type f -grep '{TODO,FIXME}')"
  expect_matches "/n\.txt:1:a TODO line(\$|${NL})" "${out}"
  expect_matches "/n\.txt:2:a FIXME line(\$|${NL})" "${out}"
  expect_not_matches 'plain line' "${out}" # a non-matching line is not printed
}

test::regextype_shglob_expands_integer_and_letter_sequences() {
  local root out
  root="$(test_tmpdir tree)"
  mkdir -p "${root}"
  printf 'part-03.a\npart-02.b\npart-01.c\npart-3.a\npart-00.a\npart-02.d\n' >"${root}/n.txt"
  out="$(_run --regextype=SHGLOB "${root}" -type f -grep 'part-{03..01}.{a..c}')"
  expect_matches '/n\.txt:1:part-03\.a' "${out}"
  expect_matches '/n\.txt:2:part-02\.b' "${out}"
  expect_matches '/n\.txt:3:part-01\.c' "${out}"
  expect_not_matches 'part-3\.a' "${out}"
  expect_not_matches 'part-00\.a' "${out}"
  expect_not_matches 'part-02\.d' "${out}"
}

test::regextype_glob_keeps_braces_literal() {
  local root out
  root="$(test_tmpdir tree)"
  mkdir -p "${root}"
  # Under plain GLOB the braces are literal, so the alternation above matches nothing here.
  printf 'a TODO line\nliteral {TODO,FIXME} here\n' >"${root}/n.txt"
  out="$(_run --regextype=GLOB "${root}" -type f -grep '{TODO,FIXME}')"
  expect_matches "literal \{TODO,FIXME\} here" "${out}" # matches the literal braces
  expect_not_matches ':1:a TODO line' "${out}"          # not the bare keyword
}

test::regextype_reserved_value_is_a_usage_error() {
  local root out rc
  root="$(_make_tree)"
  out="$("$(_xff_bin)" --regextype=MATCH "${root}" -grep 'TODO' 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}" # MATCH is still reserved (#85)
  expect_matches 'not supported yet' "${out}"
}

test::regextype_pcre2_not_built_in_is_a_usage_error() {
  # PCRE2 is a build-time extra; this (lean) binary does not link it, so --regextype=PCRE2 is a
  # usage error (exit 2), never a silent RE2 fallback. A full build accepts it.
  local root out rc
  root="$(_make_tree)"
  out="$("$(_xff_bin)" --regextype=PCRE2 "${root}" -grep 'TODO' 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_matches 'not built into this binary' "${out}"
}

test::grep_format_overrides_the_default_output() {
  local root out
  root="$(_make_tree)"
  # -grep:FORMAT renders a field template ({line}/{text} + the entry vocabulary).
  out="$(_run "${root}" -name 'a.txt' -grep:'{line}|{text}' 'TODO')"
  expect_matches "(^|${NL})1\|first TODO line(\$|${NL})" "${out}"
  expect_matches "(^|${NL})3\|another TODO here(\$|${NL})" "${out}"
  expect_not_matches ':' "${out}" # the default path:line:text colons are gone
}

test::grep_match_field_extracts_only_the_match() {
  local root out
  root="$(test_tmpdir tree)"
  mkdir -p "${root}"
  printf 'error E42 here\nwarn E7\n' >"${root}/log.txt"
  # {match} is grep -o: just the matched substring, {column} its 1-based start.
  out="$(_run "${root}" -name 'log.txt' -grep:'{column}:{match}' 'E[0-9]+')"
  expect_matches "(^|${NL})7:E42(\$|${NL})" "${out}" # E42 at column 7 of "error E42 here"
  expect_matches "(^|${NL})6:E7(\$|${NL})" "${out}"  # E7 at column 6 of "warn E7"
}

test::grep_count_prints_per_file_match_line_count() {
  local root out
  root="$(test_tmpdir tree)"
  mkdir -p "${root}"
  printf 'TODO 1\nx\nTODO 2\n' >"${root}/a.txt"
  printf 'no hits\n' >"${root}/b.txt"
  # --count (a leading global): path:count per file with matches; 0-match files omitted.
  out="$(_run --count "${root}" -type f -grep 'TODO')"
  expect_matches "/a\.txt:2(\$|${NL})" "${out}"
  expect_not_matches 'b\.txt' "${out}" # no matches -> not listed
  expect_not_matches 'TODO' "${out}"   # the lines themselves are not printed
}

test::grep_context_prints_surrounding_lines() {
  local root out
  root="$(_make_tree)"
  # --context=1 (leading global, grep -C1): the non-matching "second line" now shows as context
  # with '-' separators between the two TODO matches (':' separators); the windows merge.
  out="$(_run --context=1 "${root}" -name a.txt -grep 'TODO')"
  expect_matches "/a\.txt:1:first TODO line(\$|${NL})" "${out}" # match line, colon
  expect_matches "/a\.txt-2-second line(\$|${NL})" "${out}"     # context line, dash
  expect_matches "/a\.txt:3:another TODO here(\$|${NL})" "${out}"
}

test_runner
