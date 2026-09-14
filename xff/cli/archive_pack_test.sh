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
# End-to-end test of --pack: the walk BUILDS an archive instead of listing. The writer has its own
# unit tests in the extra; what only this level can prove is the wiring - that the member names come
# out relative to the search root, that the sink really replaces the listing, that the pre-walk
# checks fire before anything is written, and that the archive the walk meets is not packed into
# itself. Read back with the system `tar`, not with xff, so a bug shared by both readers cannot hide.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

# expect_matches compares against the WHOLE text, so a per-line anchor needs a real newline.
NL=$'\n'

_xff_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/testing/xff_full"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff_full -path '*xff/cli/testing/xff_full' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

# A small tree: `src/a.cc`, `src/sub/b.cc` and `src/c.txt`, so an expression can select a subset and
# the nested file proves the relative naming.
_tree() {
  local tree
  tree="$(test_tmpdir tree)"
  mkdir -p "${tree}/src/sub"
  echo "first" >"${tree}/src/a.cc"
  echo "second" >"${tree}/src/sub/b.cc"
  echo "other" >"${tree}/src/c.txt"
  echo "${tree}"
}

test::packed_members_are_named_relative_to_their_search_root() {
  local root out members
  root="$(_tree)"
  out="$("$(_xff_bin)" "${root}/src" -name '*.cc' --pack="${root}/out.tar" 2>&1)"
  expect_eq "" "${out}" # a sink: the listing is replaced, and a plain pack says nothing
  members="$(tar -tf "${root}/out.tar")"
  # `a.cc`, not `${root}/src/a.cc`: an absolute path baked into an archive unpacks somewhere nobody
  # asked for, so the root the entry was found under is stripped.
  expect_output_contains "a.cc" "${members}"
  expect_output_contains "sub/b.cc" "${members}"
  expect_output_not_contains "c.txt" "${members}"
  expect_output_not_contains "${root}" "${members}"
}

test::the_search_root_directory_is_not_stored_beside_its_own_children() {
  local root members
  root="$(_tree)"
  members="$("$(_xff_bin)" "${root}/src" --pack="${root}/out.tar" >/dev/null && tar -tf "${root}/out.tar")"
  # Children are named relative to the root, so a `src` entry alongside them would extract as an
  # empty stray directory. Nested directories DO travel: they have a name relative to the root.
  expect_output_contains "a.cc" "${members}"
  expect_output_contains "sub/" "${members}"
  expect_not_matches "(^|${NL})src/?($|${NL})" "${members}"
}

test::a_root_that_is_a_file_keeps_its_basename() {
  local root members
  root="$(_tree)"
  members="$("$(_xff_bin)" "${root}/src/a.cc" --pack="${root}/one.tar" >/dev/null && tar -tf "${root}/one.tar")"
  # Unlike a directory root, a file named on the command line IS content, so it is stored.
  expect_eq "a.cc" "${members}"
}

test::an_explicit_print_still_lists_what_goes_in() {
  local root out
  root="$(_tree)"
  out="$("$(_xff_bin)" "${root}/src" -name '*.cc' -print --pack="${root}/out.tar")"
  # The sink suppresses the IMPLICIT print only; an action the user wrote still runs.
  expect_output_contains "${root}/src/a.cc" "${out}"
  expect_output_contains "${root}/src/sub/b.cc" "${out}"
}

test::the_member_content_survives_the_round_trip() {
  local root
  root="$(_tree)"
  "$(_xff_bin)" "${root}/src" -name 'a.cc' --pack="${root}/out.tar.gz"
  expect_eq "first" "$(tar -xOzf "${root}/out.tar.gz" a.cc)"
}

test::brotli_tar_defaults_to_rfc9841_and_is_immediately_diveable() {
  local root magic members
  root="$(_tree)"
  "$(_xff_bin)" "${root}/src" -name '*.cc' --pack="${root}/out.tar.br"
  magic="$(od -An -tx1 -N4 "${root}/out.tar.br" | tr -d ' ')"
  expect_eq "910a4252" "${magic}"
  members="$("$(_xff_bin)" -z+ "${root}/out.tar.br" -type f -printf '%f\n')"
  expect_output_contains "a.cc" "${members}"
  expect_output_contains "b.cc" "${members}"
}

test::brotli_raw_is_an_explicit_legacy_interoperability_mode() {
  local root magic members
  root="$(_tree)"
  "$(_xff_bin)" "${root}/src" -name '*.cc' --pack="${root}/out.tbr" --pack-option=framing=raw
  magic="$(od -An -tx1 -N4 "${root}/out.tbr" | tr -d ' ')"
  expect_not_matches "^910a4252$" "${magic}"
  members="$("$(_xff_bin)" -z+ "${root}/out.tbr" -type f -printf '%f\n')"
  expect_output_contains "a.cc" "${members}"
  expect_output_contains "b.cc" "${members}"
}

test::an_output_name_with_no_writable_format_fails_before_the_walk() {
  local root status out
  root="$(_tree)"
  status=0
  out="$("$(_xff_bin)" "${root}/src" --pack="${root}/out.rar" 2>&1)" || status=$?
  expect_eq 2 "${status}"
  expect_output_contains "cannot tell the archive format" "${out}"
  expect_output_contains "tar.gz" "${out}" # the accepted set is named, not left to be guessed
  expect_eq "0" "$(find "${root}" -maxdepth 1 -name 'out.*' | wc -l | tr -d ' ')"
}

test::a_compression_level_the_format_rejects_is_a_usage_error() {
  local root status out
  root="$(_tree)"
  status=0
  out="$("$(_xff_bin)" "${root}/src" --pack="${root}/out.tar.gz" --pack-level=99 2>&1)" || status=$?
  expect_eq 2 "${status}"
  expect_output_contains "accepts 0-9" "${out}"
  # And a plain tar has nothing to set a level on, which is an error rather than a silent no-op; the
  # message names the formats that DO take it instead of leaving the reader to guess.
  status=0
  out="$("$(_xff_bin)" "${root}/src" --pack="${root}/plain.tar" --pack-level=9 2>&1)" || status=$?
  expect_eq 2 "${status}"
  expect_output_contains "does not apply to tar" "${out}"
  expect_output_contains "tar.gz" "${out}"
}

test::an_unknown_pack_option_is_refused_before_the_walk() {
  local root status out
  root="$(_tree)"
  status=0
  out="$("$(_xff_bin)" "${root}/src" --pack="${root}/out.tar.gz" --pack-option=squish=9 2>&1)" || status=$?
  expect_eq 2 "${status}"
  expect_output_contains "unknown pack option 'squish'" "${out}"
  # The accepted vocabulary is named, and nothing was written.
  expect_output_contains "level" "${out}"
  expect_eq "0" "$(find "${root}" -maxdepth 1 -name 'out.tar.gz' | wc -l | tr -d ' ')"
}

test::a_malformed_pack_option_is_a_usage_error() {
  local root status out
  root="$(_tree)"
  status=0
  out="$("$(_xff_bin)" "${root}/src" --pack="${root}/out.tar.gz" --pack-option=level 2>&1)" || status=$?
  expect_eq 2 "${status}"
  expect_output_contains "expected NAME=VALUE" "${out}"
}

test::a_pack_option_that_does_not_apply_to_the_format_is_refused() {
  local root status out
  root="$(_tree)"
  status=0
  out="$("$(_xff_bin)" "${root}/src" --pack="${root}/out.tar.gz" --pack-option=zip64=yes 2>&1)" || status=$?
  expect_eq 2 "${status}"
  expect_output_contains "does not apply to tar.gz" "${out}"
  expect_output_contains "zip" "${out}"
}

test::a_pack_option_reaches_the_writer() {
  local root first second
  root="$(_tree)"
  # timestamp=no is the one option with an exactly checkable effect: without the header timestamp two
  # runs over the same input are byte-identical, which is what a reproducible build needs.
  "$(_xff_bin)" "${root}/src" --pack="${root}/r1.tar.gz" --pack-option=timestamp=no
  sleep 1
  "$(_xff_bin)" "${root}/src" --pack="${root}/r2.tar.gz" --pack-option=timestamp=no
  first="$(cksum <"${root}/r1.tar.gz")"
  second="$(cksum <"${root}/r2.tar.gz")"
  expect_eq "${first}" "${second}"
}

test::pack_level_is_the_same_thing_as_the_level_option() {
  local root sugar spelled
  root="$(_tree)"
  head -c 200000 /dev/zero | tr '\0' 'a' >"${root}/src/big.txt"
  "$(_xff_bin)" "${root}/src" -name 'big.txt' --pack="${root}/sugar.tar.gz" --pack-level=9
  "$(_xff_bin)" "${root}/src" -name 'big.txt' --pack="${root}/spelled.tar.gz" --pack-option=level=9
  sugar="$(wc -c <"${root}/sugar.tar.gz")"
  spelled="$(wc -c <"${root}/spelled.tar.gz")"
  expect_eq "${sugar}" "${spelled}"
}

test::the_last_value_for_a_pack_option_wins() {
  local root mixed plain
  root="$(_tree)"
  head -c 200000 /dev/zero | tr '\0' 'a' >"${root}/src/big.txt"
  "$(_xff_bin)" "${root}/src" -name 'big.txt' --pack="${root}/mixed.tar.gz" --pack-option=level=1 --pack-option=level=9
  "$(_xff_bin)" "${root}/src" -name 'big.txt' --pack="${root}/plain.tar.gz" --pack-option=level=9
  mixed="$(wc -c <"${root}/mixed.tar.gz")"
  plain="$(wc -c <"${root}/plain.tar.gz")"
  expect_eq "${plain}" "${mixed}"
}

test::a_json_pack_option_file_uses_the_same_vocabulary_and_ordering() {
  local root options mixed plain file_last expected json_zip inline_zip
  root="$(_tree)"
  head -c 200000 /dev/zero | tr '\0' 'a' >"${root}/src/big.txt"
  options="${root}/options.json"
  printf '%s\n' '{"level": 1, "timestamp": false}' >"${options}"
  "$(_xff_bin)" "${root}/src" -name 'big.txt' --pack="${root}/mixed.tar.gz" \
    --pack-option="@${options}" --pack-option=level=9
  "$(_xff_bin)" "${root}/src" -name 'big.txt' --pack="${root}/plain.tar.gz" \
    --pack-option=timestamp=no --pack-option=level=9
  mixed="$(cksum <"${root}/mixed.tar.gz")"
  plain="$(cksum <"${root}/plain.tar.gz")"
  expect_eq "${plain}" "${mixed}"

  # Expansion happens exactly where the @file occurs, not in a separate precedence tier.
  printf '%s\n' '{"level": 9, "timestamp": false}' >"${options}"
  "$(_xff_bin)" "${root}/src" -name 'big.txt' --pack="${root}/file-last.tar.gz" \
    --pack-option=level=1 --pack-option="@${options}"
  file_last="$(cksum <"${root}/file-last.tar.gz")"
  expected="$(cksum <"${root}/plain.tar.gz")"
  expect_eq "${expected}" "${file_last}"

  # String values use the same backend-specific vocabulary as an inline option too.
  printf '%s\n' '{"compression": "deflate", "zip64": false}' >"${options}"
  "$(_xff_bin)" "${root}/src" -name 'big.txt' --pack="${root}/json.zip" --pack-option="@${options}"
  "$(_xff_bin)" "${root}/src" -name 'big.txt' --pack="${root}/inline.zip" \
    --pack-option=compression=deflate --pack-option=zip64=no
  json_zip="$(cksum <"${root}/json.zip")"
  inline_zip="$(cksum <"${root}/inline.zip")"
  expect_eq "${inline_zip}" "${json_zip}"
}

test::a_bad_json_pack_option_file_fails_before_the_walk() {
  local root options status out
  root="$(_tree)"
  options="${root}/options.json"
  printf '%s\n' '{"level": [9]}' >"${options}"
  status=0
  out="$("$(_xff_bin)" "${root}/src" --pack="${root}/out.tar.gz" --pack-option="@${options}" 2>&1)" || status=$?
  expect_eq 2 "${status}"
  expect_output_contains "value for 'level' must be a string, integer, or boolean" "${out}"
  expect_eq "0" "$(find "${root}" -maxdepth 1 -name 'out.tar.gz' | wc -l | tr -d ' ')"
}

test::json_pack_option_file_shape_and_read_errors_are_specific() {
  local root options status out
  root="$(_tree)"
  options="${root}/options.json"

  printf '%s\n' '{bad json' >"${options}"
  status=0
  out="$("$(_xff_bin)" "${root}/src" --pack="${root}/out.tar.gz" --pack-option="@${options}" 2>&1)" || status=$?
  expect_eq 2 "${status}"
  expect_output_contains "is not valid JSON" "${out}"

  printf '%s\n' '[{"level": 9}]' >"${options}"
  status=0
  out="$("$(_xff_bin)" "${root}/src" --pack="${root}/out.tar.gz" --pack-option="@${options}" 2>&1)" || status=$?
  expect_eq 2 "${status}"
  expect_output_contains "must contain one JSON object" "${out}"

  status=0
  out="$("$(_xff_bin)" "${root}/src" --pack="${root}/out.tar.gz" --pack-option="@${root}/missing.json" 2>&1)" || status=$?
  expect_eq 2 "${status}"
  expect_output_contains "cannot read pack-option file" "${out}"

  status=0
  out="$("$(_xff_bin)" "${root}/src" --pack="${root}/out.tar.gz" --pack-option=@ 2>&1)" || status=$?
  expect_eq 2 "${status}"
  expect_output_contains "expected a file after --pack-option=@" "${out}"
}

test::json_pack_option_names_use_the_linked_writer_vocabulary() {
  local root options status out
  root="$(_tree)"
  options="${root}/options.json"
  printf '%s\n' '{"squish": 9}' >"${options}"
  status=0
  out="$("$(_xff_bin)" "${root}/src" --pack="${root}/out.tar.gz" --pack-option="@${options}" 2>&1)" || status=$?
  expect_eq 2 "${status}"
  expect_output_contains "unknown pack option 'squish'" "${out}"
  expect_output_contains "level" "${out}"
}

test::the_archive_help_lists_the_vocabulary_the_binary_accepts() {
  local out
  out="$("$(_xff_bin)" --help=archive --width=100)"
  # Generated from the linked writer's own table, so this is what proves the two cannot drift.
  expect_output_contains "--pack-option=NAME=VALUE" "${out}"
  expect_output_contains "timestamp=yes|no" "${out}"
  expect_output_contains "zip64=yes|no" "${out}"
  expect_output_contains "framing=rfc9841|raw" "${out}"
}

test::a_level_that_works_produces_a_smaller_archive() {
  local root fast small
  root="$(_tree)"
  # Compressible input, so the two levels cannot come out the same size by accident.
  head -c 200000 /dev/zero | tr '\0' 'a' >"${root}/src/big.txt"
  "$(_xff_bin)" "${root}/src" -name 'big.txt' --pack="${root}/fast.tar.gz" --pack-level=1
  "$(_xff_bin)" "${root}/src" -name 'big.txt' --pack="${root}/small.tar.gz" --pack-level=9
  fast="$(wc -c <"${root}/fast.tar.gz")"
  small="$(wc -c <"${root}/small.tar.gz")"
  # bashtest has no ordering matcher, so the comparison is the value under test.
  expect_eq 1 "$((small < fast))"
}

test::the_archive_being_written_is_not_packed_into_itself() {
  local root members
  root="$(_tree)"
  # Two runs: the first leaves an `out.tar` inside the tree, so the second MEETS it while writing it.
  "$(_xff_bin)" "${root}" -type f --pack="${root}/out.tar"
  "$(_xff_bin)" "${root}" -type f --pack="${root}/out.tar"
  members="$(tar -tf "${root}/out.tar")"
  expect_output_contains "src/a.cc" "${members}"
  expect_output_not_contains "out.tar" "${members}"
}

test::dry_run_reports_what_it_would_pack_and_writes_nothing() {
  local root out
  root="$(_tree)"
  out="$("$(_xff_bin)" "${root}/src" -name '*.cc' --pack="${root}/out.tar" --dry-run)"
  expect_output_contains "would pack 2 entries" "${out}"
  expect_eq "0" "$(find "${root}" -maxdepth 1 -name 'out.tar' | wc -l | tr -d ' ')"
}

test::an_existing_output_survives_a_failed_pack() {
  local root status
  root="$(_tree)"
  echo "mine" >"${root}/out.tar.gz"
  status=0
  "$(_xff_bin)" "${root}/src" --pack="${root}/out.tar.gz" --pack-level=99 >/dev/null 2>&1 || status=$?
  expect_eq 2 "${status}"
  expect_eq "mine" "$(cat "${root}/out.tar.gz")"
}

test::a_member_of_another_archive_is_refused_rather_than_dropped() {
  local root status out
  root="$(_tree)"
  COPYFILE_DISABLE=1 tar -c -f "${root}/in.tar" -C "${root}/src" a.cc
  status=0
  out="$("$(_xff_bin)" -z "${root}/in.tar" -type f --pack="${root}/out.tar" 2>&1)" || status=$?
  # 2, the "what you asked for cannot be done" code, not 1: nothing was written and nothing will be.
  expect_eq 2 "${status}"
  expect_output_contains "re-packing members is not supported" "${out}"
  expect_eq "0" "$(find "${root}" -maxdepth 1 -name 'out.tar' | wc -l | tr -d ' ')"
}

test::separate_policy_permits_new_archives_while_blocking_ordinary_writes() {
  local root status
  root="$(_tree)"
  cat >"${root}/policy.ini" <<'INI'
--block-policy-categories=archive
--block-file-writing
--block-file-deletion
--block-execution
--block-archive-overwrite
[unsafe]
--no-safe
--no-safe-block-file-writing
INI
  XFF_TEST_USER_CONFIG="${root}/policy.ini" "$(_xff_bin)" "${root}/src" --pack="${root}/out.tar" >/dev/null
  expect_output_contains "a.cc" "$(tar -tf "${root}/out.tar")"
  status=0
  XFF_TEST_USER_CONFIG="${root}/policy.ini" "$(_xff_bin)" "${root}/src" --config=unsafe -fprint "${root}/ordinary" >/dev/null 2>&1 || status=$?
  expect_eq 2 "${status}"
  expect_eq 0 "$(find "${root}" -maxdepth 1 -name ordinary | wc -l | tr -d ' ')"
  status=0
  XFF_TEST_USER_CONFIG="${root}/policy.ini" "$(_xff_bin)" "${root}/src" --pack="${root}/out.tar" --dry-run >/dev/null 2>&1 || status=$?
  expect_eq 2 "${status}"
  status=0
  XFF_TEST_USER_CONFIG="${root}/policy.ini" "$(_xff_bin)" "${root}/src" --pack="${root}/out.tar" >/dev/null 2>&1 || status=$?
  expect_eq 2 "${status}"
  expect_output_contains "a.cc" "$(tar -tf "${root}/out.tar")"
}

test::default_file_policy_blocks_archive_writes_and_cli_cannot_switch_policy() {
  local root status
  root="$(_tree)"
  echo '--block-file-writing' >"${root}/policy.ini"
  status=0
  XFF_TEST_USER_CONFIG="${root}/policy.ini" "$(_xff_bin)" "${root}/src" --pack="${root}/out.tar" >/dev/null 2>&1 || status=$?
  expect_eq 2 "${status}"
  status=0
  XFF_TEST_USER_CONFIG="${root}/policy.ini" "$(_xff_bin)" "${root}/src" --block-policy-categories=archive --pack="${root}/out.tar" >/dev/null 2>&1 || status=$?
  expect_eq 2 "${status}"
  expect_eq 0 "$(find "${root}" -maxdepth 1 -name out.tar | wc -l | tr -d ' ')"
}

test::archive_content_deletion_remains_independent_of_ordinary_deletion() {
  local root members status
  root="$(_tree)"
  "$(_xff_bin)" "${root}/src" --pack="${root}/out.tar" >/dev/null
  cat >"${root}/policy.ini" <<'INI'
--block-policy-categories=archive
--block-file-writing
--block-file-deletion
INI
  XFF_TEST_USER_CONFIG="${root}/policy.ini" "$(_xff_bin)" "${root}/out.tar" --archive --archive-delete -name a.cc -delete >/dev/null
  members="$(tar -tf "${root}/out.tar")"
  expect_output_not_contains "a.cc" "${members}"
  expect_output_contains "c.txt" "${members}"
  echo '--block-archive-content-deletion' >>"${root}/policy.ini"
  status=0
  XFF_TEST_USER_CONFIG="${root}/policy.ini" "$(_xff_bin)" "${root}/out.tar" --archive --archive-delete -name c.txt -delete --skip-unsupported >/dev/null 2>&1 || status=$?
  expect_eq 2 "${status}"
  expect_output_contains "c.txt" "$(tar -tf "${root}/out.tar")"
}

test_runner

test::output_scope_guards_archive_publication() {
  local root cfg status
  root="$(_tree)"
  root="$(cd "${root}" && pwd -P)"
  mkdir -p "${root}/output"
  cfg="${root}/user.ini"
  cat >"${cfg}" <<INI
--block-policy-categories=archive,output
--output-root=${root}/output
--block-file-writing
--block-output-file-overwrite
INI
  XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" "${root}/src" --pack="${root}/output/new.tar"
  tar -tf "${root}/output/new.tar" >/dev/null
  status=0
  XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" "${root}/src" --pack="${root}/outside.tar" >/dev/null 2>&1 || status=$?
  expect_eq "2" "${status}"
  [[ ! -e "${root}/outside.tar" ]] || fail "archive escaped the output policy"
  status=0
  XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" "${root}/src" --pack="${root}/output/new.tar" >/dev/null 2>&1 || status=$?
  expect_eq "2" "${status}"
  XFF_TEST_USER_CONFIG="${cfg}" "$(_xff_bin)" "${root}/src" --pack="${root}/output/preview.tar" --dry-run >/dev/null
  [[ ! -e "${root}/output/preview.tar" ]] || fail "dry run published archive"
}
