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
# End-to-end test of --summary output: a right-aligned human table by default
# (grouped digits) and stable one-object-per-row --format=jsonl machine output.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

# A real newline for line-anchored expect_matches patterns: expect_matches matches
# the whole text ([[ =~ ]]), so `^`/`$` anchor the whole output, not a line.
# `(^|${NL})X` and `X($|${NL})` restore grep's per-line anchoring.
NL=$'\n'

_xff_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/testing/xff"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff -path '*xff/cli/testing/xff' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

# Runs xff and returns its combined output, asserting it exited 0 (surfacing the
# output on a nonzero exit so a sanitizer abort is diagnosable rather than hidden).
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

_new_tree() {
  test_tmpdir tree
}

# Tree: a.txt (1234 bytes), b.txt (10 bytes), c.md (5 bytes).
_make_tree() {
  local tree
  tree="$(_new_tree)"
  head -c 1234 /dev/zero >"${tree}/a.txt"
  head -c 10 /dev/zero >"${tree}/b.txt"
  head -c 5 /dev/zero >"${tree}/c.md"
  echo "${tree}"
}

test::summary_default_is_human_and_right_aligned_in_xff() {
  local root out
  root="$(_make_tree)"
  out="$(_run --summary=ext "${root}" -type f)"
  # xff style defaults to human sizes in SI: txt (2 files, 1244 bytes) -> kB, md (5) -> B,
  # with the count right of the label.
  expect_matches 'txt +2 +[0-9.]+ kB' "${out}"
  expect_matches 'total +3 +[0-9.]+ kB' "${out}"
  # A byte size renders as the integer with the fraction columns blanked (so points line
  # up under the scaled rows), hence several spaces before the left-aligned suffix.
  expect_matches 'md +1 +5 +B' "${out}"
}

test::summary_human_off_shows_grouped_bytes() {
  local root out
  root="$(_make_tree)"
  out="$(_run --summary=ext --human=off "${root}" -type f)"
  # --human=off forces raw grouped bytes (the machine-ish view), right-aligned.
  expect_matches 'txt +2 +1,244' "${out}"
  expect_matches 'total +3 +1,249' "${out}"
}

test::summary_jsonl_emits_one_object_per_row() {
  local root out
  root="$(_make_tree)"
  out="$(_run --summary=ext --format=jsonl "${root}" -type f)"
  expect_matches '\{"group":"txt","count":2,"bytes":1244\}' "${out}"
  expect_matches '\{"group":"total","count":3,"bytes":1249\}' "${out}"
}

test::summary_overall_is_a_single_total_row() {
  local root out
  root="$(_make_tree)"
  out="$(_run --summary --format=jsonl "${root}" -type f)"
  expect_matches '\{"group":"total","count":3,"bytes":1249\}' "${out}"
}

test::summary_hash_groups_identical_files_into_one_bucket() {
  local root out
  root="$(_new_tree)"
  printf 'abc' >"${root}/a" # sha256(abc) = ba7816...
  printf 'abc' >"${root}/b" # identical content -> same digest bucket
  printf 'xyz' >"${root}/c" # a different digest
  out="$(_run --summary=hash --format=jsonl "${root}" -type f)"
  # The two identical files collapse into one bucket with count 2 (the dedup view); the
  # group key is the file's sha256 digest, reusing the {hash} field.
  expect_matches '\{"group":"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","count":2,"bytes":6\}' "${out}"
  expect_matches '\{"group":"total","count":3,"bytes":9\}' "${out}"
}

test::summary_hash_verification_tallies_passes_and_failures() {
  local root out
  root="$(_new_tree)"
  printf 'abc' >"${root}/good"
  printf 'xyz' >"${root}/bad"
  out="$(_run --summary=hash-verification --format=jsonl "${root}" -type f -hasheq \
    ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad)"
  expect_matches '\{"group":"failed","count":1,"bytes":3\}' "${out}"
  expect_matches '\{"group":"verified","count":1,"bytes":3\}' "${out}"
  expect_matches '\{"group":"total","count":2,"bytes":6\}' "${out}"
}

# A 5,872,025-byte file to exercise human size units (5.6 MiB / 5.9 MB).
_make_big() {
  local tree
  tree="$(_new_tree)"
  head -c 5872025 /dev/zero >"${tree}/big.bin"
  echo "${tree}"
}

test::human_iec_renders_binary_units() {
  local root out
  root="$(_make_big)"
  out="$(_run --summary --human=iec "${root}" -type f)"
  expect_matches '5\.6[0-9]* MiB' "${out}" # --human=iec = binary (MiB); precision-agnostic
}

test::human_si_renders_decimal_units() {
  local root out
  root="$(_make_big)"
  out="$(_run --summary --human=si "${root}" -type f)"
  expect_matches '5\.[0-9]+ MB' "${out}" # --human=si = decimal (MB, not MiB); precision-agnostic
}

test::human_default_and_si_alias_are_decimal() {
  local root out
  root="$(_make_big)"
  # Bare --human defaults to SI (decimal MB, not MiB); --si is its alias.
  out="$(_run --summary --human "${root}" -type f)"
  expect_matches '5\.[0-9]+ MB' "${out}"
  expect_not_matches 'MiB' "${out}"
  out="$(_run --summary --si "${root}" -type f)"
  expect_matches '5\.[0-9]+ MB' "${out}"
}

test::summary_precision_sets_fraction_digits() {
  local root out
  root="$(_make_big)"
  # --summary-precision=N sets the scaled-size fraction digits (default 2).
  out="$(_run --summary --human=iec --summary-precision=4 "${root}" -type f)"
  expect_matches '5\.[0-9]{4} MiB' "${out}" # exactly four fraction digits
  # 0 => integer, no decimal point.
  out="$(_run --summary --human=iec --summary-precision=0 "${root}" -type f)"
  expect_matches '[0-9]+ MiB' "${out}"
  expect_not_matches '\. *MiB|[0-9]\.[0-9]+ MiB' "${out}"
}

test::human_does_not_change_jsonl_bytes() {
  # jsonl is the machine path: exact bytes regardless of --human.
  local root out
  root="$(_make_big)"
  out="$(_run --summary --human --format=jsonl "${root}" -type f)"
  expect_output_contains '"bytes":5872025' "${out}"
}

test::summary_top_keeps_the_largest_groups_by_size() {
  # txt (a.txt+b.txt = 1244 B) is larger than md (c.md = 5 B); --top=1 keeps txt and
  # drops md, while the total row still counts every group.
  local root out
  root="$(_make_tree)"
  out="$(_run --summary=ext --top=1 --human=off "${root}" -type f)"
  expect_matches 'txt +2 +1,244' "${out}"
  expect_matches 'total +3 +1,249' "${out}"
  expect_not_matches "(^|${NL})md " "${out}" # the smallest group is dropped
}

test::summary_template_key_groups_by_a_field_value() {
  # A {template} key groups per matched entry, like the built-in categories -- here by extension.
  local root out
  root="$(_make_tree)"
  out="$(_run --summary='{ext}' --human=off "${root}" -type f)"
  expect_matches 'txt +2 +1,244' "${out}"
  expect_matches "(^|${NL})md +1 +5" "${out}"
}

test::summary_m_extraction_key_counts_per_extracted_line() {
  # The driving case: run a command per file, break its multi-line output into a value stream with
  # an m// extraction, and count per extracted key. Here a stand-in for `git blame --line-porcelain`
  # emits repeated `author X` lines; --summary tallies lines per author across the tree.
  local root out
  root="$(_new_tree)"
  : >"${root}/only.txt"
  out="$(_run --summary='{capture.a:m/^author (.+)$/\1/}' "${root}" -type f \
    -capture:a sh -c 'printf "author Bob\nauthor-mail x\nauthor Ann\nauthor Bob\n"' \;)"
  expect_matches "(^|${NL})Bob +2" "${out}" # two author-Bob lines
  expect_matches "(^|${NL})Ann +1" "${out}"
  expect_matches "(^|${NL})total +3" "${out}" # three matching lines in total
}

test::summary_m_chain_extracts_then_normalizes() {
  # A ;-chained m// key: the first command extracts the author, the second normalizes it
  # (spaces -> underscores) per line, then --summary counts the normalized keys.
  local root out
  root="$(_new_tree)"
  : >"${root}/only.txt"
  out="$(_run --summary='{capture.a:m/^author (.+)$/\1/;s/ /_/g}' "${root}" -type f \
    -capture:a sh -c 'printf "author Bob Smith\nauthor Bob Smith\nauthor Ann Lee\n"' \;)"
  expect_matches "(^|${NL})Bob_Smith +2" "${out}"
  expect_matches "(^|${NL})Ann_Lee +1" "${out}"
}

test::m_extraction_rejected_in_a_scalar_render_context() {
  # An m// extraction is a value stream, valid only as a --summary key. A per-entry scalar context
  # (-printf / --template / an -exec arg) rejects it with a usage error instead of newline-joining.
  local root out rc
  root="$(_new_tree)"
  : >"${root}/a.txt"
  out="$("$(_xff_bin)" "${root}" -type f -printf '%{name:m/./\0/}\n' 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_matches 'only valid as a --summary key' "${out}"
  out="$("$(_xff_bin)" --template='{name:m/./\0/}' "${root}" -type f 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
}

test::summary_global_may_follow_the_roots_and_expression() {
  # A --long global is position-independent (#145): --summary works after the roots AND after the
  # expression (the WTH-killer), not only leading. A --flag inside an -exec command stays literal.
  local root out
  root="$(_new_tree)"
  : >"${root}/a.txt"
  : >"${root}/b.log"
  out="$(_run "${root}" -type f --summary=ext)" # global at the very end, after the expression
  expect_matches "(^|${NL})txt " "${out}"
  expect_matches "(^|${NL})log " "${out}"
  out="$(_run "${root}" --summary=ext -type f)" # global after the root, before the expression
  expect_matches "(^|${NL})txt " "${out}"
}

test::m_reducer_makes_the_extraction_scalar_valid() {
  # A terminal join(...) reducer collapses the value stream to one scalar, so the SAME m// is now
  # valid in a scalar context (--template / -printf) -- the explicit opt-in the #136 error asks for.
  # Bare m// (no reducer) still errors (covered above).
  local root out
  root="$(_new_tree)"
  : >"${root}/only.txt"
  out="$(_run --template='authors={capture.a:m/^author (.+)$/\1/;join(, )}' "${root}" -type f \
    -capture:a sh -c 'printf "author Bob\nx\nauthor Ann\nauthor Bob\n"' \;)"
  expect_matches 'authors=Bob, Ann, Bob' "${out}"
}

test::summary_mixed_extraction_template_is_a_usage_error() {
  # A key template that mixes an m// extraction with other text has no single key -> exit 2.
  local root out rc
  root="$(_new_tree)"
  : >"${root}/only.txt"
  out="$("$(_xff_bin)" --summary='{capture.a:m/./\0/}{name}' "${root}" -type f \
    -capture:a sh -c 'echo hi' \; 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_matches 'not a mix' "${out}"
}

test::multiple_summary_flags_emit_independent_tables() {
  # --summary is repeatable (like --histogram): each occurrence is its own table, printed in order,
  # separated by a blank line. --top applies to every table.
  local root out
  root="$(_new_tree)"
  : >"${root}/a.txt"
  : >"${root}/b.txt"
  : >"${root}/c.log"
  out="$(_run "${root}" -type f --summary=ext --summary=type)"
  expect_matches "(^|${NL})txt +2" "${out}"  # the ext table
  expect_matches "(^|${NL})log +1" "${out}"  # the ext table
  expect_matches "(^|${NL})file +3" "${out}" # the type table (all three are regular files)
}

test::compare_summary_follows_selected_results() {
  local root out
  root="$(_new_tree)"
  mkdir "${root}/left" "${root}/right"
  printf left >"${root}/left/left"
  printf right >"${root}/right/right"
  printf same >"${root}/left/same"
  printf same >"${root}/right/same"
  printf old >"${root}/left/different"
  printf new >"${root}/right/different"
  out="$(_run --compare "${root}/left" "${root}/right" --summary --compare-select=all)"
  expect_matches "^different[[:space:]]+different${NL}left-only[[:space:]]+left${NL}right-only[[:space:]]+right${NL}identical[[:space:]]+same${NL}left-only +1 +25.00%${NL}right-only +1 +25.00%${NL}different +1 +25.00%${NL}identical +1 +25.00%${NL}total +4 +100.00%" "${out}"
  out="$(_run --compare "${root}/left" "${root}/right" --summary=compare --compare-select=different --format=jsonl)"
  expect_output_contains '{"group":"different","count":1,"percent":25.00}' "${out}"
  expect_output_contains '{"group":"identical","count":1,"percent":25.00}' "${out}"
  expect_output_contains '{"group":"total","count":4,"percent":100.00}' "${out}"
  out="$(_run --compare "${root}/left" "${root}/right" --summary=type --summary --compare-select=all --format=jsonl)"
  expect_output_contains '{"group":"identical","count":1,"percent":25.00}' "${out}"
  expect_output_contains '{"group":"total","count":4,"percent":100.00}' "${out}"
  out="$(_run --compare "${root}/left" "${root}/right" --summary=compare --compare-select=left-only,right-only,different --summary-precision=1 --format=jsonl)"
  expect_output_contains '{"group":"left-only","count":1,"percent":25.0}' "${out}"
  expect_output_contains '{"group":"total","count":4,"percent":100.0}' "${out}"
  out="$(_run --compare=diff "${root}/left" "${root}/right" --summary --compare-select=different --format=jsonl)"
  expect_output_contains '{"group":"different","count":1,"percent":25.00}' "${out}"
  expect_output_contains '{"group":"total","count":4,"percent":100.00}' "${out}"
  local explicit
  explicit="$(_run --compare=status "${root}/left" "${root}/right" --compare-select=none --summary=compare)"
  out="$(_run --compare=summary "${root}/left" "${root}/right")"
  expect_eq "${explicit}" "${out}"
  out="$(_run --compare-select=all --compare=summary "${root}/left" "${root}/right")"
  expect_eq "${explicit}" "${out}"
  out="$(_run --compare=summary "${root}/left" "${root}/right" --compare-select=all)"
  expect_matches "^different[[:space:]]+different" "${out}"
  expect_matches 'total +4 +100.00%' "${out}"
  out="$(_run --compare=summary "${root}/left" "${root}/right" --summary=none)"
  expect_eq '' "${out}"
  out="$(_run --summary=none --compare=summary "${root}/left" "${root}/right")"
  expect_eq "${explicit}" "${out}"
  local selection
  for selection in none ''; do
    out="$(_run --compare "${root}/left" "${root}/right" --summary "--compare-select=${selection}" --format=jsonl)"
    expect_eq '{"group":"left-only","count":1,"percent":25.00}
{"group":"right-only","count":1,"percent":25.00}
{"group":"different","count":1,"percent":25.00}
{"group":"identical","count":1,"percent":25.00}
{"group":"total","count":4,"percent":100.00}' "${out}"
    out="$(_run --compare "${root}/left" "${root}/right" "--compare-select=${selection}")"
    expect_eq '' "${out}"
  done
  out="$(_run --compare "${root}/left" "${root}/right" --summary --summary=none --compare-select=all)"
  expect_not_matches 'total' "${out}"
}

test::compare_summary_empty_trees_has_zero_total() {
  local root out
  root="$(_new_tree)"
  mkdir "${root}/left" "${root}/right"
  out="$(_run --compare "${root}/left" "${root}/right" --summary --format=jsonl)"
  expect_output_contains '{"group":"total","count":0,"percent":0.00}' "${out}"
}

test::compare_summary_requires_compare_mode() {
  local root out rc
  root="$(_new_tree)"
  out="$("$(_xff_bin)" "${root}" --summary=compare 2>&1)" && rc=0 || rc=$?
  expect_eq 2 "${rc}"
  expect_output_contains 'requires --compare' "${out}"
}

test::summary_scopes_multiple_roots() {
  local root out
  root="$(_new_tree)"
  mkdir "${root}/one" "${root}/two" "${root}/three"
  printf abc >"${root}/one/a.txt"
  printf de >"${root}/two/b.txt"
  printf f >"${root}/three/c.cc"
  out="$(_run "${root}/one" "${root}/two" "${root}/three" -type f --summary --format=jsonl)"
  expect_eq '{"group":"total","count":3,"bytes":6}' "${out}"
  out="$(_run "${root}/one" "${root}/two" "${root}/three" -type f --summary --summary-scope=root --format=jsonl)"
  expect_output_contains "\"root\":\"${root}/one\",\"group\":\"total\",\"count\":1,\"bytes\":3" "${out}"
  expect_output_contains "\"root\":\"${root}/two\",\"group\":\"total\",\"count\":1,\"bytes\":2" "${out}"
  expect_output_contains "\"root\":\"${root}/three\",\"group\":\"total\",\"count\":1,\"bytes\":1" "${out}"
}

test::summary_scopes_comparison_aliases() {
  local root out explicit
  root="$(_new_tree)"
  mkdir "${root}/left" "${root}/right"
  printf abc >"${root}/left/only.txt"
  printf defg >"${root}/right/extra.cc"
  printf same >"${root}/left/equal.txt"
  printf same >"${root}/right/equal.txt"
  printf x >"${root}/left/change.txt"
  printf yz >"${root}/right/change.txt"
  out="$(_run --compare=summary "${root}/left" "${root}/right" -type f --summary=overall --summary-scope=compare --format=jsonl)"
  expect_output_contains "\"scope\":\"left-only\",\"root\":\"${root}/left\",\"group\":\"total\",\"count\":1,\"bytes\":3" "${out}"
  expect_output_contains "\"scope\":\"different\",\"root\":\"${root}/left\",\"group\":\"total\",\"count\":1,\"bytes\":1" "${out}"
  expect_output_contains "\"scope\":\"different\",\"root\":\"${root}/right\",\"group\":\"total\",\"count\":1,\"bytes\":2" "${out}"
  explicit="$(_run --compare=summary "${root}/left" "${root}/right" -type f --summary=overall --summary-scope=left,right,diff,identical,compare --format=jsonl)"
  expect_eq "${out}" "${explicit}"
  out="$(_run --compare=summary "${root}/left" "${root}/right" -type f --summary=overall --summary-scope=all --format=jsonl)"
  expect_output_contains '"scope":"all","root":"","group":"total","count":6,"bytes":18' "${out}"
}

test::summary_scope_rejects_invalid_context() {
  local root out rc
  root="$(_new_tree)"
  out="$("$(_xff_bin)" "${root}" --summary --summary-scope=diff 2>&1)" && rc=0 || rc=$?
  expect_eq 2 "${rc}"
  expect_output_contains 'require --compare' "${out}"
  out="$("$(_xff_bin)" "${root}" --summary --summary-scope=garbage 2>&1)" && rc=0 || rc=$?
  expect_eq 2 "${rc}"
}

test::summary_scope_empty_root_and_replacement() {
  local root out
  root="$(_new_tree)"
  out="$(_run "${root}" -type f --summary --summary-scope=root --format=jsonl)"
  expect_eq "{\"scope\":\"root\",\"root\":\"${root}\",\"group\":\"total\",\"count\":0}" "${out}"
  printf abc >"${root}/file.txt"
  out="$(_run "${root}" -type f --summary --summary-scope=root --summary-scope=all --format=jsonl)"
  expect_eq '{"scope":"all","root":"","group":"total","count":1,"bytes":3}' "${out}"
}

test::summary_scope_template_and_collection() {
  local root out
  root="$(_new_tree)"
  mkdir "${root}/one" "${root}/two"
  printf abc >"${root}/one/a.txt"
  printf de >"${root}/two/b.txt"
  out="$(_run "${root}/one" "${root}/two" -type f -collect:files --summary='{ext}' --summary-scope=root --format=jsonl)"
  expect_output_contains "\"root\":\"${root}/one\",\"group\":\"txt\",\"count\":1,\"bytes\":3" "${out}"
  expect_output_contains "\"root\":\"${root}/two\",\"group\":\"txt\",\"count\":1,\"bytes\":2" "${out}"
}

test::summary_scope_requires_active_file_summary() {
  local root out rc scope
  root="$(_new_tree)"
  mkdir "${root}/left" "${root}/right"
  printf abc >"${root}/left/file.txt"
  for scope in all left,right; do
    out="$("$(_xff_bin)" --compare=summary "${root}/left" "${root}/right" "--summary-scope=${scope}" 2>&1)" && rc=0 || rc=$?
    expect_eq 2 "${rc}"
    expect_output_contains 'requires an active file summary' "${out}"
    expect_output_contains '--compare=summary expands to --summary=compare' "${out}"
    out="$(_run --compare=summary "${root}/left" "${root}/right" "--summary-scope=${scope}" --summary --format=jsonl)"
    expect_output_contains '"scope":' "${out}"
  done
  out="$("$(_xff_bin)" "${root}" --summary-scope=root -exec echo SHOULD_NOT_RUN \; 2>&1)" && rc=0 || rc=$?
  expect_eq 2 "${rc}"
  expect_output_contains 'requires an active file summary' "${out}"
  expect_not_matches 'SHOULD_NOT_RUN' "${out}"
  out="$("$(_xff_bin)" --compare "${root}/left" "${root}/right" --summary=compare --summary-scope=all 2>&1)" && rc=0 || rc=$?
  expect_eq 2 "${rc}"
  out="$("$(_xff_bin)" "${root}" --summary --summary=none --summary-scope=root 2>&1)" && rc=0 || rc=$?
  expect_eq 2 "${rc}"
  out="$(_run "${root}" -type f --summary=none --summary-scope=root --summary=ext --format=jsonl)"
  expect_output_contains '"scope":"root"' "${out}"
}

test::summary_scope_plain_labels_and_top_ties() {
  local root out
  root="$(_new_tree)"
  mkdir "${root}/one" "${root}/two"
  touch "${root}/one/a.txt" "${root}/one/b.txt" "${root}/one/c.cc" "${root}/one/d.h"
  printf abc >"${root}/two/data.bin"
  out="$(_run "${root}/one" -type f --summary=ext --summary-scope=root --top=2 --human=off)"
  expect_output_contains "Summary scope: root (${root}/one)" "${out}"
  expect_matches "txt +2${NL}cc +1${NL}total +4" "${out}"
  expect_not_matches "(^|${NL})h +1" "${out}"
  out="$(_run "${root}/one" "${root}/two" -type f --summary --summary-scope=all --human=off)"
  expect_output_contains 'Summary scope: all' "${out}"
  expect_matches "total +5 +3" "${out}"
  out="$(_run --compare=summary "${root}/one" "${root}/two" -type f --summary=overall --summary-scope=left,right --human=off)"
  expect_output_contains "Summary scope: left-only (${root}/one)" "${out}"
  expect_output_contains "Summary scope: right-only (${root}/two)" "${out}"
  expect_matches "total +1 +3" "${out}"
}

test::summary_scope_empty_list_is_an_error() {
  local root out rc
  root="$(_new_tree)"
  mkdir "${root}/left" "${root}/right"
  out="$("$(_xff_bin)" --compare=summary "${root}/left" "${root}/right" --summary=overall --summary-scope= 2>&1)" && rc=0 || rc=$?
  expect_eq 2 "${rc}"
  expect_output_contains "unknown summary scope ''" "${out}"
}

test_runner
