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
# End-to-end test of --archive DIVING, the other side of archive_test.sh: that one pins what a
# build WITHOUT the extra does, this one runs the binary that has it (the target is gated on
# --//xff:xff_archive) and asserts the three modes actually differ on a real archive.
#
# The fixtures are built here with `tar` rather than committed: the committed ones live in the
# archive extra's own module, which the core cannot depend on, and a two-file tar written at test
# time is both cheaper and self-describing.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

_xff_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/testing/xff_full"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff_full -path '*xff/cli/testing/xff_full' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

# A directory holding `a.tar` (members `one.txt` and `dir/two.txt`) plus a plain `b.txt`, so one
# tree exercises "the container", "inside the container" and "an ordinary file" at once.
_tree() {
  local tree stage
  tree="$(test_tmpdir tree)"
  stage="${tree}/stage"
  mkdir -p "${stage}/dir"
  echo "needle" >"${stage}/one.txt"
  echo "two" >"${stage}/dir/two.txt"
  # COPYFILE_DISABLE keeps macOS bsdtar from adding an AppleDouble `._name` member beside every
  # real one, which would make the member list platform-dependent.
  COPYFILE_DISABLE=1 tar -c -f "${tree}/a.tar" -C "${stage}" one.txt dir
  rm -rf "${stage}"
  : >"${tree}/b.txt"
  echo "${tree}"
}

test::a_container_named_as_a_root_lists_its_members() {
  local root out
  root="$(_tree)"
  out="$("$(_xff_bin)" --archive=roots "${root}/a.tar")"
  # Dual identity: the tar is still the file it is, and its members follow it.
  expect_output_contains "${root}/a.tar" "${out}"
  expect_output_contains "a.tar!one.txt" "${out}"
  expect_output_contains "a.tar!dir/two.txt" "${out}"
}

test::none_keeps_an_archive_a_plain_file() {
  local root out
  root="$(_tree)"
  for spelling in "--archive=none" "-z-"; do
    out="$("$(_xff_bin)" "${spelling}" "${root}/a.tar")"
    expect_output_contains "${root}/a.tar" "${out}"
    expect_output_not_contains "a.tar!" "${out}"
  done
}

test::roots_does_not_dive_into_an_archive_found_mid_walk() {
  local root out
  root="$(_tree)"
  # `roots` is "the archive I pointed you AT". Walking the DIRECTORY finds the same tar, and must
  # leave it closed - that difference is the whole point of having two modes.
  out="$("$(_xff_bin)" --archive=roots "${root}")"
  expect_output_contains "${root}/a.tar" "${out}"
  expect_output_not_contains "a.tar!" "${out}"
}

test::all_dives_into_an_archive_found_mid_walk() {
  local root out
  root="$(_tree)"
  for spelling in "--archive=all" "-z+" "--archive"; do
    out="$("$(_xff_bin)" "${spelling}" "${root}")"
    expect_output_contains "a.tar!one.txt" "${out}"
    expect_output_contains "a.tar!dir/two.txt" "${out}"
    expect_output_contains "${root}/b.txt" "${out}" # an ordinary file is untouched by diving
  done
}

test::members_are_ordinary_entries_for_the_expression() {
  local root out
  root="$(_tree)"
  # Nothing in the vocabulary knows about archives: a member is matched by the same -name / -type
  # every other entry is, which is what makes diving worth having.
  out="$("$(_xff_bin)" --archive=roots "${root}/a.tar" -type f -name '*.txt')"
  expect_output_contains "a.tar!one.txt" "${out}"
  expect_output_contains "a.tar!dir/two.txt" "${out}"
  # Exactly those two: the container is not a `.txt`, and `dir` is not a file.
  expect_eq "2" "$(wc -l <<<"${out}" | tr -d ' ')"
}

test::content_predicates_read_a_members_own_bytes() {
  local root out
  root="$(_tree)"
  # The payoff of member reads: -content and -grep search INSIDE the archive, and only the member
  # that holds the needle matches (the container is a tar, so it never matches as text).
  out="$("$(_xff_bin)" --archive=roots "${root}/a.tar" -content needle)"
  expect_output_contains "a.tar!one.txt" "${out}"
  expect_eq "1" "$(wc -l <<<"${out}" | tr -d ' ')" # and nothing else in the archive holds it
  out="$("$(_xff_bin)" --archive=roots "${root}/a.tar" -grep needle)"
  expect_output_contains "a.tar!one.txt:1:needle" "${out}"
}

test::hash_and_line_fields_render_for_a_member() {
  local root member plain
  root="$(_tree)"
  # A member's digest must be the digest of its CONTENT, so it equals the digest of a loose file
  # holding the same bytes - the check a manifest (`-hasheq`) over an archive depends on.
  echo "needle" >"${root}/copy.txt"
  member="$("$(_xff_bin)" --archive=roots "${root}/a.tar" -name 'one.txt' -printfln '%{hash} %{lines}')"
  plain="$("$(_xff_bin)" "${root}/copy.txt" -printfln '%{hash} %{lines}')"
  expect_eq "${plain}" "${member}"
}

test::pruning_the_container_keeps_the_file_and_skips_the_members() {
  local root out
  root="$(_tree)"
  out="$("$(_xff_bin)" --archive=roots "${root}/a.tar" -name 'a.tar' -prune -print)"
  expect_output_contains "${root}/a.tar" "${out}"
  expect_output_not_contains "a.tar!" "${out}"
}

test::the_member_path_separator_and_prefix_are_the_users_choice() {
  local root out
  root="$(_tree)"
  out="$("$(_xff_bin)" --archive=roots --archive-separator='#' "${root}/a.tar")"
  expect_output_contains "a.tar#one.txt" "${out}"
  out="$("$(_xff_bin)" --archive=roots --archive-prefix=URI "${root}/a.tar")"
  expect_output_contains "archive://${root}/a.tar!one.txt" "${out}"
}

test::archive_depth_bounds_nesting() {
  local root out
  root="$(_tree)"
  # A tar holding a tar. The default depth of 1 leaves the inner one a plain member; --archive-depth=2
  # opens it, and its member is then an ordinary entry the expression reads like any other.
  mkdir -p "${root}/nest"
  COPYFILE_DISABLE=1 tar -c -f "${root}/nest/inner.tar" -C "${root}" a.tar
  COPYFILE_DISABLE=1 tar -c -f "${root}/outer.tar" -C "${root}/nest" inner.tar
  out="$("$(_xff_bin)" --archive=all "${root}/outer.tar")"
  expect_output_contains "outer.tar!inner.tar" "${out}"
  expect_output_not_contains "outer.tar!inner.tar!" "${out}"
  out="$("$(_xff_bin)" --archive=all --archive-depth=3 "${root}/outer.tar")"
  expect_output_contains "outer.tar!inner.tar!a.tar" "${out}"
  # Three containers deep, the innermost member is still just an entry with readable content.
  out="$("$(_xff_bin)" --archive=all --archive-depth=3 "${root}/outer.tar" -grep needle)"
  expect_output_contains "outer.tar!inner.tar!a.tar!one.txt:1:needle" "${out}"
}

test::a_bad_archive_depth_is_a_usage_error() {
  local root out rc
  root="$(_tree)"
  # 0 most likely means "off", which --archive=none spells; guessing would be worse than saying so.
  out="$("$(_xff_bin)" --archive=all --archive-depth=0 "${root}" 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "--archive-depth" "${out}"
  out="$("$(_xff_bin)" --archive=all --archive-depth=lots "${root}" 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
}

test::all_only_opens_files_whose_name_looks_like_a_container() {
  local root out
  root="$(_tree)"
  # The gate: `blob` IS a tar, byte for byte, but nothing in its name says so. Under `all` the walk
  # leaves it closed - otherwise every file in a source tree would be opened and format-bid - while the
  # `.tar` beside it is dived as before.
  cp "${root}/a.tar" "${root}/blob"
  out="$("$(_xff_bin)" --archive=all "${root}")"
  expect_output_contains "a.tar!one.txt" "${out}"
  expect_output_contains "${root}/blob" "${out}"
  expect_output_not_contains "blob!" "${out}"
}

test::archive_any_offers_every_file_to_the_reader() {
  local root out
  root="$(_tree)"
  # The way out of the name gate, for a tree of archives named anything at all.
  cp "${root}/a.tar" "${root}/blob"
  out="$("$(_xff_bin)" --archive=all --archive-any "${root}")"
  expect_output_contains "blob!one.txt" "${out}"
  # And an ordinary file offered to the reader is still just a file, not an error.
  expect_output_contains "${root}/b.txt" "${out}"
  expect_output_not_contains "xff:" "${out}"
}

test::later_all_modes_restore_the_filename_gate() {
  local root out earlier later
  root="$(_tree)"
  cp "${root}/a.tar" "${root}/blob"
  for earlier in --archive=any --archive-any -z++ -Z++; do
    for later in --archive=all --archive -z+ -Z+; do
      out="$("$(_xff_bin)" "${earlier}" "${later}" "${root}")"
      expect_output_contains "a.tar!one.txt" "${out}"
      expect_output_not_contains "blob!" "${out}"
    done
  done
}

test::archive_sniffing_follows_selected_config_order() {
  local root out
  root="$(_tree)"
  cp "${root}/a.tar" "${root}/blob"
  cat >"${root}/modes.rc" <<'INI'
[sniff]
--archive=any
[named]
--archive=all
INI
  out="$("$(_xff_bin)" --xffrc="${root}/modes.rc" --config=sniff --config=named "${root}")"
  expect_output_contains "a.tar!one.txt" "${out}"
  expect_output_not_contains "blob!" "${out}"
  out="$("$(_xff_bin)" --xffrc="${root}/modes.rc" --config=named --config=sniff "${root}")"
  expect_output_contains "blob!one.txt" "${out}"
}

test::a_container_named_on_the_command_line_is_never_gated_by_its_name() {
  local root out
  root="$(_tree)"
  # Pointing xff AT a file is the request to look inside it, so `roots` (and `all`) open it whatever it
  # is called - the gate exists for files the walk merely met.
  cp "${root}/a.tar" "${root}/blob"
  out="$("$(_xff_bin)" --archive=roots "${root}/blob")"
  expect_output_contains "blob!one.txt" "${out}"
  out="$("$(_xff_bin)" --archive=all "${root}/blob")"
  expect_output_contains "blob!one.txt" "${out}"
}

test::a_compressed_single_file_dives_to_the_name_inside() {
  local root out
  root="$(_tree)"
  # `notes.txt.gz` is not an archive of many members: it is ONE file, compressed. Its member takes the
  # container's name minus the suffix - what `gzip -d` restores - and content search reads it.
  printf 'findable-needle\n' >"${root}/notes.txt"
  gzip -c "${root}/notes.txt" >"${root}/notes.txt.gz"
  out="$("$(_xff_bin)" --archive=roots "${root}/notes.txt.gz")"
  expect_output_contains "notes.txt.gz!notes.txt" "${out}"
  out="$("$(_xff_bin)" --archive=roots "${root}/notes.txt.gz" -grep findable-needle)"
  expect_output_contains "notes.txt.gz!notes.txt:1:findable-needle" "${out}"
  # And the MEMBER's size is the uncompressed one, since that is what the entry holds - the container
  # keeps its own (compressed) size, so both are listed and only the member's is checked here.
  out="$("$(_xff_bin)" --archive=roots "${root}/notes.txt.gz" -name 'notes.txt' -printfln '%s')"
  expect_eq "16" "${out}"
}

test::a_text_file_named_gz_is_not_a_container() {
  local root out
  root="$(_tree)"
  # The guard on the guard: libarchive's `raw` format bids on anything, so a name claiming compression
  # is not enough - a real codec must have applied. Otherwise every mis-named text file would present as
  # a one-member archive.
  printf 'just text, no gzip header\n' >"${root}/liar.gz"
  out="$("$(_xff_bin)" --archive=roots "${root}/liar.gz")"
  expect_output_contains "${root}/liar.gz" "${out}"
  expect_output_not_contains "liar.gz!" "${out}"
}

test::a_tar_gz_is_still_read_as_a_tar() {
  local root out
  root="$(_tree)"
  # The distinction that matters: `.tar.gz` is a compressed ARCHIVE (libarchive reads it whole, members
  # and all), while `.txt.gz` is a compressed FILE. Both end in `.gz`.
  mkdir -p "${root}/pack"
  printf 'inner\n' >"${root}/pack/inner.txt"
  COPYFILE_DISABLE=1 tar -czf "${root}/pack.tar.gz" -C "${root}/pack" inner.txt
  out="$("$(_xff_bin)" --archive=roots "${root}/pack.tar.gz")"
  expect_output_contains "pack.tar.gz!inner.txt" "${out}"
}

test::write_actions_on_a_member_refuse_loudly() {
  local root out rc
  root="$(_tree)"
  # A member exists only inside its container: there is nothing to unlink and no path a child process
  # can open. Both must SAY so (exit 2, naming the path) rather than exit 0 having done nothing, which
  # is what -delete used to do while -exec handed the child `a.tar!one.txt`.
  out="$("$(_xff_bin)" --archive=roots "${root}/a.tar" -name 'one.txt' -delete 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "archive member" "${out}"
  out="$("$(_xff_bin)" --archive=roots "${root}/a.tar" -name 'one.txt' -exec echo GOT {} \; 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "archive member" "${out}"
  expect_output_not_contains "GOT" "${out}" # the child never ran
  # The tar itself is a real file, so a write action on IT is not refused: `-delete` on the container is
  # a legitimate request, which is why the guard keys on the entry rather than on "diving is on".
  out="$("$(_xff_bin)" --archive=roots "${root}/a.tar" -name 'a.tar' -exec echo GOT {} \; 2>&1)"
  expect_output_contains "GOT ${root}/a.tar" "${out}"
}

test::skip_unsupported_downgrades_a_refused_member_to_a_warning() {
  local root out rc
  root="$(_tree)"
  # The standing escape hatch for an impossible task: keep walking, skip the entry, exit 0. That is what
  # makes `xff -z+ . -delete` usable over a tree that happens to contain archives.
  out="$("$(_xff_bin)" --archive=roots --skip-unsupported "${root}/a.tar" -delete 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
}

test::an_ordinary_file_that_is_not_an_archive_is_no_error() {
  local root out rc
  root="$(_tree)"
  # Every plain file the walk meets under `all` is offered to the reader and declined; that must be
  # silent, or `xff -z+ .` would report an error per ordinary file.
  out="$("$(_xff_bin)" --archive=all "${root}" 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_output_not_contains "xff:" "${out}"
}

test::a_summary_counts_the_members_of_a_dived_container_not_the_container() {
  local root members both container
  root="$(_tree)"
  # The default (`members`): diving makes one byte visible twice, so a total that adds the tar AND
  # what is in it describes no filesystem that exists. The tar is several kilobytes of blocked
  # padding and its two members are a handful of bytes, so the counts separate the modes cleanly.
  members="$("$(_xff_bin)" --archive=roots --summary "${root}/a.tar")"
  both="$("$(_xff_bin)" --archive=roots --summary --archive-aggregate=both "${root}/a.tar")"
  container="$("$(_xff_bin)" --archive=roots --summary --archive-aggregate=container "${root}/a.tar")"
  # 3 members (one.txt, dir, dir/two.txt); +1 for the tar itself under `both`; only the tar under
  # `container`, which is exactly what the same run without diving reports.
  expect_matches "total +3 " "${members}"
  expect_matches "total +4 " "${both}"
  expect_matches "total +1 " "${container}"
  expect_eq "$("$(_xff_bin)" --archive=none --summary "${root}/a.tar")" "${container}"
}

test::archive_aggregate_leaves_the_printed_entries_alone() {
  local root out
  root="$(_tree)"
  # Only the REDUCTIONS are affected. A mode that drops the container from a total must not drop it
  # from the listing, or `--summary` would silently change what a run finds.
  for mode in members container both; do
    out="$("$(_xff_bin)" --archive=roots "--archive-aggregate=${mode}" "${root}/a.tar")"
    expect_output_contains "${root}/a.tar" "${out}"
    expect_output_contains "a.tar!one.txt" "${out}"
  done
}

test::a_bad_archive_aggregate_value_is_a_usage_error() {
  local root out rc
  root="$(_tree)"
  out="$("$(_xff_bin)" --archive-aggregate=nope --summary "${root}" 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "--archive-aggregate" "${out}"
}

test::a_mid_walk_container_is_dropped_from_the_total_too() {
  local root members both
  root="$(_tree)"
  # `all` meets the tar during the walk, where its own entry is emitted with a whole listing block
  # before anything is dived - so the answer has to be known ahead of the entry, not after it.
  members="$("$(_xff_bin)" --archive=all --summary "${root}")"
  both="$("$(_xff_bin)" --archive=all --summary --archive-aggregate=both "${root}")"
  # root + b.txt + 3 members = 5, and `both` adds the tar back.
  expect_matches "total +5 " "${members}"
  expect_matches "total +6 " "${both}"
}

test::archive_extract_runs_a_child_over_a_temporary_copy_of_the_member() {
  local root out
  root="$(_tree)"
  # `one.txt` holds "needle". Without a path a child can open, -exec is a refusal (asserted above);
  # with --archive-extract the child reads the member's real bytes out of a temporary file.
  out="$("$(_xff_bin)" --archive=roots --archive-extract "${root}/a.tar" -name 'one.txt' -exec cat {} \;)"
  expect_output_contains "needle" "${out}"
  # The temporary keeps the member's own name, so a tool that keys on the extension still works, and
  # -execdir runs in the directory holding it.
  out="$("$(_xff_bin)" --archive=roots --archive-extract "${root}/a.tar" -name 'one.txt' -execdir basename {} \;)"
  expect_output_contains "one.txt" "${out}"
}

test::archive_extract_covers_the_batch_and_field_forms_too() {
  local root out
  root="$(_tree)"
  # `-exec ... +` runs after the walk, so its copies have to outlive the entry that made them; and
  # --exec-fields renders {path} as the copy, not the member path the child could not open.
  out="$("$(_xff_bin)" --archive=roots --archive-extract "${root}/a.tar" -name 'one.txt' -exec cat {} +)"
  expect_output_contains "needle" "${out}"
  out="$("$(_xff_bin)" --archive=roots --archive-extract --exec-fields "${root}/a.tar" -name 'one.txt' \
    -exec cat '{path}' \;)"
  expect_output_contains "needle" "${out}"
}

test::the_refusal_without_archive_extract_names_the_way_out() {
  local root out rc
  root="$(_tree)"
  # A refusal that does not say what to do instead is a dead end; the flag is the answer here.
  out="$("$(_xff_bin)" --archive=roots "${root}/a.tar" -name 'one.txt' -exec cat {} \; 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "--archive-extract" "${out}"
}

test::archive_extract_does_not_make_delete_possible() {
  local root out rc
  root="$(_tree)"
  # Deleting a temporary copy would be a no-op dressed as a deletion, so -delete stays a refusal
  # whatever this flag says.
  out="$("$(_xff_bin)" --archive=roots --archive-extract "${root}/a.tar" -name 'one.txt' -delete 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "read-only" "${out}"
}

test::archive_extract_leaves_nothing_behind() {
  local root extract_root out
  root="$(_tree)"
  extract_root="$(test_tmpdir extract-root)"
  # Every copy is removed when its child finishes (or when the run ends, for a batch), so a run over
  # an archive must leave its chosen temporary directory empty. Give this invocation a private
  # XDG_RUNTIME_DIR: counting `xff-*` in a process-wide TMPDIR races other work, and would not even
  # inspect /dev/shm when that preferred candidate is available. Select the members explicitly so
  # `cat` does not also print the tar container's binary bytes into Bash command substitution.
  out="$(XDG_RUNTIME_DIR="${extract_root}" "$(_xff_bin)" --archive=roots --archive-extract "${root}/a.tar" \
    -name '*.txt' -exec cat {} \;)"
  expect_output_contains "needle" "${out}"
  expect_eq "" "$(find "${extract_root}" -mindepth 1 -print -quit)"
}

test::archive_delete_rewrites_the_container_without_the_member() {
  local root out
  root="$(_tree)"
  # The member is gone from the listing afterwards, and the OTHER member is still readable - a
  # rewrite that lost content would still pass a name-only check.
  out="$("$(_xff_bin)" --archive=roots --archive-delete "${root}/a.tar" -name 'one.txt' -delete 2>&1)"
  expect_eq "" "${out}"
  out="$("$(_xff_bin)" --archive=roots "${root}/a.tar")"
  expect_output_not_contains "a.tar!one.txt" "${out}"
  expect_output_contains "a.tar!dir/two.txt" "${out}"
  out="$("$(_xff_bin)" --archive=roots "${root}/a.tar" -name 'two.txt' -grep two)"
  expect_output_contains "a.tar!dir/two.txt:1:two" "${out}"
}

test::archive_delete_needs_the_flag_and_says_so() {
  local root out rc
  root="$(_tree)"
  # Rewriting a whole archive is not something to do by default, so the refusal stands - and names
  # the way past it rather than being a dead end.
  out="$("$(_xff_bin)" --archive=roots "${root}/a.tar" -name 'one.txt' -delete 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "--archive-delete" "${out}"
  out="$("$(_xff_bin)" --archive=roots "${root}/a.tar")"
  expect_output_contains "a.tar!one.txt" "${out}" # and nothing was removed
}

test::archive_delete_under_dry_run_writes_nothing() {
  local root out
  root="$(_tree)"
  out="$("$(_xff_bin)" --archive=roots --archive-delete --dry-run "${root}/a.tar" -name 'one.txt' -delete)"
  expect_output_contains "a.tar!one.txt" "${out}" # what WOULD go
  out="$("$(_xff_bin)" --archive=roots "${root}/a.tar")"
  expect_output_contains "a.tar!one.txt" "${out}" # still there
}

test::archive_delete_refuses_a_container_it_cannot_rewrite() {
  local root out rc
  root="$(_tree)"
  # A compressed single file is read through a path libarchive cannot write back, so this has to be a
  # named refusal rather than a rewrite that silently changes the file's format.
  printf 'plain\n' >"${root}/notes.txt"
  gzip -f "${root}/notes.txt"
  out="$("$(_xff_bin)" --archive=roots --archive-delete "${root}/notes.txt.gz" -name 'notes.txt' -delete 2>&1)" \
    && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "not rewrite it" "${out}"
}

test::the_container_itself_is_still_an_ordinary_file_to_delete() {
  local root out
  root="$(_tree)"
  # `-delete` on the archive means the archive, not its members: the dual identity has to survive the
  # flag that makes members deletable.
  out="$("$(_xff_bin)" --archive=roots --archive-delete "${root}/a.tar" -name 'a.tar' -delete 2>&1)"
  expect_eq "" "${out}"
  expect_eq "0" "$(find "${root}" -name 'a.tar' | wc -l | tr -d ' ')"
}

test::the_top_read_rung_sniffs_files_a_named_gate_would_skip() {
  local root out
  root="$(_tree)"
  cp "${root}/a.tar" "${root}/noextension"
  # `-z+` opens a mid-walk file only when its NAME looks like a container; `-z++` (= --archive=any)
  # drops that gate, which is the only difference between the two top read rungs.
  out="$("$(_xff_bin)" -z+ "${root}")"
  expect_output_contains "a.tar!dir/two.txt" "${out}"
  expect_output_not_contains "noextension!dir/two.txt" "${out}"
  out="$("$(_xff_bin)" -z++ "${root}")"
  expect_output_contains "noextension!dir/two.txt" "${out}"
  out="$("$(_xff_bin)" --archive=any "${root}")"
  expect_output_contains "noextension!dir/two.txt" "${out}" # the long spelling of the same rung
}

test::the_upper_case_family_is_the_same_ladder_with_writing_armed() {
  local root out
  root="$(_tree)"
  # Case carries the CAPABILITY, the signs carry the LEVEL. `-z` refuses to touch a member; `-Z` is
  # the same rung with --archive-write, so the same command goes through.
  out="$("$(_xff_bin)" -z "${root}/a.tar" -name 'one.txt' -exec cat {} \; 2>&1)"
  expect_output_contains "cannot run on an archive member" "${out}"
  out="$("$(_xff_bin)" -Z "${root}/a.tar" -name 'one.txt' -exec cat {} \;)"
  expect_output_contains "needle" "${out}" # extraction armed by the capital alone
  out="$("$(_xff_bin)" -Z "${root}/a.tar" -name 'one.txt' -delete 2>&1)"
  expect_eq "" "${out}"
  out="$("$(_xff_bin)" --archive=roots "${root}/a.tar")"
  expect_output_not_contains "a.tar!one.txt" "${out}" # deletion armed, and it happened
}

test::the_upper_case_rungs_match_the_lower_case_ones() {
  local root out
  root="$(_tree)"
  # -Z is roots (a mid-walk archive stays closed), -Z+ is all (it opens).
  out="$("$(_xff_bin)" -Z "${root}")"
  expect_output_not_contains "a.tar!dir/two.txt" "${out}"
  out="$("$(_xff_bin)" -Z+ "${root}")"
  expect_output_contains "a.tar!dir/two.txt" "${out}"
}

test::the_capital_minus_is_a_full_reset() {
  local root out
  root="$(_tree)"
  # `-Z-` is the none rung AND a disarm, so it overrides whatever an earlier flag (or a config file)
  # asked for. Its disarm is only observable once reading is turned back on, so the case does that:
  # -Z arms, -Z- resets, -z reads again, and the member write must be refused as if -Z never ran.
  out="$("$(_xff_bin)" -Z- "${root}")"
  expect_output_not_contains "a.tar!" "${out}" # reading off
  out="$("$(_xff_bin)" -Z -Z- -z "${root}/a.tar" -name 'one.txt' -delete 2>&1)"
  expect_output_contains "cannot remove an archive member" "${out}"
  out="$("$(_xff_bin)" --archive=roots "${root}/a.tar")"
  expect_output_contains "a.tar!one.txt" "${out}" # and the member is still there
}

test::the_two_axes_resolve_independently_and_later_wins() {
  local root out
  root="$(_tree)"
  cp "${root}/a.tar" "${root}/noextension"
  # `-z+ -Z++` widens the rung to `any` and arms writing; `-z++ -Z` narrows back to roots.
  out="$("$(_xff_bin)" -z+ -Z++ "${root}")"
  expect_output_contains "noextension!dir/two.txt" "${out}"
  out="$("$(_xff_bin)" -z++ -Z "${root}")"
  expect_output_not_contains "a.tar!dir/two.txt" "${out}" # roots again
  # `-Z++ -z-` is the pack shape: writing armed, reading off, so no existing container is opened
  # (and nothing is harvested out of one).
  out="$("$(_xff_bin)" -Z++ -z- "${root}")"
  expect_output_not_contains "a.tar!" "${out}"
  expect_output_contains "a.tar" "${out}" # the container is still an ordinary file
}

test::archive_write_arms_the_flags_without_changing_the_dive_mode() {
  local root out
  root="$(_tree)"
  # The long spelling says WRITE and only that: the mode stays whatever it was, so a mid-walk archive
  # is still left closed under the default `roots`.
  out="$("$(_xff_bin)" --archive-write "${root}")"
  expect_output_not_contains "a.tar!" "${out}"
  out="$("$(_xff_bin)" --archive-write --archive=roots "${root}/a.tar" -name 'one.txt' -exec cat {} \;)"
  expect_output_contains "needle" "${out}"
}

test::only_the_plus_ladder_spells_the_umbrella() {
  local root out rc
  root="$(_tree)"
  # The ladder stops at ++, and the star form was deliberately not taken: a bare `-z*` errors in zsh
  # and silently expands in bash, so it must not be a spelling xff answers to either. The upper-case
  # family stops there too.
  for spelling in "-z+++" "-z*" "-Z+++" "-Z*"; do
    out="$("$(_xff_bin)" "${spelling}" "${root}" 2>&1)" && rc=0 || rc=$?
    expect_eq "2" "${rc}"
    expect_output_contains "unknown option" "${out}"
  done
}

test_runner
