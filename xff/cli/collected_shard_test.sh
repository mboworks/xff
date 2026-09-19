#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail
# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

_xff_bin() {
  echo "${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/testing/xff"
}

_tree() {
  local root
  root="$(test_tmpdir collected_shards)"
  printf aa >"${root}/data-00000-of-00002"
  printf bb >"${root}/data-00001-of-00002"
  echo "${root}"
}

_expect_json_row() {
  python3 -c '
import json,sys
expected=json.loads(sys.argv[1]); rows=[json.loads(line) for line in sys.stdin]
def matches(actual,wanted):
    return all(key in actual and (matches(actual[key],value) if isinstance(value,dict) else actual[key]==value)
               for key,value in wanted.items())
assert any(matches(row,expected) for row in rows), (expected,rows)
' "$1" <<<"$2"
}

test::summary_and_histogram_reduce_the_same_logical_population_once() {
  local root out
  root="$(_tree)"
  out="$("$(_xff_bin)" "${root}" -type f -collect --shards --summary --histogram=ext --format=jsonl)"
  _expect_json_row '{"group":"total","count":1,"bytes":4}' "${out}" || return
  _expect_json_row '{"bucket":"(none)","value":1}' "${out}" || return
  out="$("$(_xff_bin)" "${root}" -type f -collect --shards --histogram=ext --format=jsonl)"
  _expect_json_row '{"bucket":"(none)","value":1}' "${out}" || return
  out="$("$(_xff_bin)" "${root}" -type f -collect --shards '--summary={shard}' --format=jsonl)"
  _expect_json_row '{"group":"2","count":1,"bytes":4}' "${out}"
}

test::collection_placement_controls_the_grouped_population() {
  local root out
  root="$(_tree)"
  out="$("$(_xff_bin)" "${root}" --sort=name -type f -collect -first 1 --shards --summary --format=jsonl)"
  _expect_json_row '{"group":"total","count":1,"bytes":4}' "${out}" || return
  out="$("$(_xff_bin)" "${root}" --sort=name -type f -first 1 -collect --shards --summary --format=jsonl)"
  _expect_json_row '{"group":"total","count":1,"bytes":2}' "${out}" || return
  out="$("$(_xff_bin)" "${root}" -false -collect --shards --summary --format=jsonl)"
  _expect_json_row '{"group":"total","count":0,"bytes":0}' "${out}"
}

test::named_collections_are_independent_and_non_shard_mode_stays_physical() {
  local root out
  root="$(_tree)"
  out="$("$(_xff_bin)" "${root}" -type f -collect:one -collect:two --shards --summary --format=jsonl)"
  _expect_json_row '{"group":"total","count":2,"bytes":8}' "${out}" || return
  out="$("$(_xff_bin)" "${root}" -type f -collect --summary --format=jsonl)"
  _expect_json_row '{"group":"total","count":2,"bytes":4}' "${out}" || return
  out="$("$(_xff_bin)" "${root}" -type f -collect --shards)"
  expect_eq "${root}/data-00000-of-00002" "${out}"
}

test::incomplete_sets_deduplicate_and_ignore_out_of_range_members() {
  local root out status
  root="$(test_tmpdir incomplete)"
  printf aa >"${root}/data-00000-of-00003.aaaaaaaa"
  printf bbbb >"${root}/data-00000-of-00003.bbbbbbbb"
  printf ccc >"${root}/data-00002-of-00003"
  printf ignored >"${root}/data-00003-of-00003"
  printf x >"${root}/plain.txt"
  touch -t 202001010000 "${root}/data-00000-of-00003.aaaaaaaa"
  touch -t 202201010000 "${root}/data-00000-of-00003.bbbbbbbb"
  out="$("$(_xff_bin)" "${root}" -type f -collect --shards --summary --format=jsonl)"
  _expect_json_row '{"group":"total","count":2,"bytes":6}' "${out}" || return
  out="$("$(_xff_bin)" "${root}" -type f -collect --shards --shards-dedup=mtime --summary --format=jsonl)"
  _expect_json_row '{"group":"total","count":2,"bytes":8}' "${out}" || return
  status=0
  out="$("$(_xff_bin)" "${root}" -type f -collect --shards --shards-dedup=error --summary --format=jsonl 2>&1)" || status=$?
  [[ "${status}" != 0 ]] || fail 'duplicate collection was accepted' || return
  expect_output_contains 'duplicate' "${out}" || return
  expect_output_not_contains '"group":' "${out}"
}

test::custom_schemes_and_scheme_restrictions_apply_to_collections() {
  local root out
  root="$(test_tmpdir custom)"
  printf x >"${root}/img_001_v3.raw"
  printf y >"${root}/img_002_v3.raw"
  out="$("$(_xff_bin)" "${root}" -type f -collect --shards=of --summary --format=jsonl)"
  _expect_json_row '{"group":"total","count":2,"bytes":2}' "${out}" || return
  out="$("$(_xff_bin)" "${root}" -type f -collect --shards=of \
    '--shard-pattern=(?P<stem>img)_(?P<index>[0-9]+)_v(?P<dup>[0-9]+)\.raw' --summary --format=jsonl)"
  _expect_json_row '{"group":"total","count":1,"bytes":2}' "${out}"
}

test::root_scopes_preserve_each_collections_source_root() {
  local left right out
  left="$(_tree)"
  right="$(_tree)"
  out="$("$(_xff_bin)" "${left}" "${right}" -type f -collect --shards --summary --summary-scope=root --format=jsonl)"
  _expect_json_row "{\"root\":\"${left}\",\"group\":\"total\",\"count\":1,\"bytes\":4}" "${out}" || return
  _expect_json_row "{\"root\":\"${right}\",\"group\":\"total\",\"count\":1,\"bytes\":4}" "${out}"
}

test::comparison_collections_keep_physical_category_accounting() {
  local left right out
  left="$(_tree)"
  right="$(_tree)"
  printf cc >"${right}/data-00001-of-00002"
  out="$("$(_xff_bin)" --compare=summary "${left}" "${right}" -type f -collect --shards \
    --summary=ext --summary-scope=identical,different --format=jsonl)"
  _expect_json_row '{"scope":"identical,different","group":"total","identical":{"count":1,"bytes":4},"different":{"count":1,"bytes":4}}' "${out}"
}

test::zero_byte_shards_and_collection_overflow_remain_distinct() {
  local root out status
  root="$(_tree)"
  : >"${root}/data-00000-of-00002"
  : >"${root}/data-00001-of-00002"
  out="$("$(_xff_bin)" "${root}" -type f -collect --shards --summary --format=jsonl)"
  _expect_json_row '{"group":"total","count":1,"bytes":0}' "${out}" || return
  status=0
  out="$("$(_xff_bin)" "${root}" -type f -collect --shards --buffer=1 --summary --format=jsonl 2>&1)" || status=$?
  [[ "${status}" != 0 ]] || fail 'partial collection was summarized' || return
  expect_output_contains 'collection exceeded --buffer' "${out}" || return
  expect_output_not_contains '"group":' "${out}"
}

test::restricted_schemes_leave_other_files_and_directories_physical() {
  local root out
  root="$(_tree)"
  printf x >"${root}/other.001"
  printf y >"${root}/other.002"
  out="$("$(_xff_bin)" "${root}" -type f -collect --shards=of --summary=ext --format=jsonl)"
  _expect_json_row '{"group":"(none)","count":1,"bytes":4}' "${out}" || return
  _expect_json_row '{"group":"001","count":1,"bytes":1}' "${out}" || return
  _expect_json_row '{"group":"002","count":1,"bytes":1}' "${out}" || return
  mkdir "${root}/dir-00000-of-00001"
  out="$("$(_xff_bin)" "${root}" \( -type f -o -name dir-00000-of-00001 \) -collect \
    --shards --summary=type --format=jsonl)"
  _expect_json_row '{"group":"directory","count":1}' "${out}"
}

test::status_predicate_duplicate_errors_are_not_lost_to_collection_grouping() {
  local root out status
  root="$(test_tmpdir duplicate_status)"
  printf x >"${root}/data-00000-of-00001.aaaaaaaa"
  printf y >"${root}/data-00000-of-00001.bbbbbbbb"
  status=0
  out="$("$(_xff_bin)" "${root}" -type f -shard-status complete -collect --shards \
    --shards-dedup=error --summary --format=jsonl 2>&1)" || status=$?
  [[ "${status}" != 0 ]] || fail 'duplicate status cohort was accepted' || return
  expect_output_contains 'duplicate' "${out}"
}

test_runner
