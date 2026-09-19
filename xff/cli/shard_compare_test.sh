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
  root="$(test_tmpdir shard_compare)"
  mkdir -p "${root}/left" "${root}/right"
  printf aa >"${root}/left/data-00000-of-00002"
  printf bb >"${root}/left/data-00001-of-00002"
  printf aa >"${root}/right/data-00000-of-00002"
  printf cc >"${root}/right/data-00001-of-00002"
  echo "${root}"
}

_expect_json_row() {
  python3 -c '
import json,sys
expected=json.loads(sys.argv[1])
rows=[json.loads(line) for line in sys.stdin]
def matches(actual,wanted):
    return all(key in actual and (matches(actual[key],value) if isinstance(value,dict) else actual[key]==value)
               for key,value in wanted.items())
assert any(matches(row,expected) for row in rows), (expected,rows)
' "$1" <<<"$2"
}

test::mixed_set_categories_use_each_physical_pair() {
  local root out
  root="$(_tree)"
  out="$("$(_xff_bin)" --compare=summary "${root}/left" "${root}/right" -type f --shards \
    --summary=ext --summary-scope=identical,different --format=jsonl)"
  _expect_json_row '{"type":"file","group":"identical","count":1,"bytes":4}' "${out}" || return
  _expect_json_row '{"type":"file","group":"different","count":1,"bytes":4}' "${out}" || return
  _expect_json_row '{"scope":"identical,different","group":"total","identical":{"count":1,"bytes":4},"different":{"count":1,"bytes":4}}' "${out}"
}

test::side_totals_and_combined_totals_remain_physical() {
  local root out
  root="$(_tree)"
  out="$("$(_xff_bin)" --compare=summary "${root}/left" "${root}/right" -type f --shards \
    --summary=ext --format=jsonl)"
  _expect_json_row '{"scope":"left-total,right-total","group":"total","left-total":{"count":2,"bytes":4},"right-total":{"count":2,"bytes":4}}' "${out}" || return
  out="$("$(_xff_bin)" --compare=summary "${root}/left" "${root}/right" -type f --shards \
    --summary=ext --summary-scope=all --format=jsonl)"
  _expect_json_row '{"scope":"all","group":"total","count":4,"bytes":8}' "${out}"
}

test::comparison_status_has_no_extra_collapsed_listing() {
  local root out
  root="$(_tree)"
  out="$("$(_xff_bin)" --compare "${root}/left" "${root}/right" -type f --shards --compare-select=all)"
  expect_eq $'identical\tdata-00000-of-00002\ndifferent\tdata-00001-of-00002' "${out}"
}

test::ordinary_scans_still_count_logical_sets() {
  local root out
  root="$(_tree)"
  out="$("$(_xff_bin)" "${root}/left" -type f --shards --summary --format=jsonl)"
  _expect_json_row '{"group":"total","count":1,"bytes":4}' "${out}"
}

test::comparison_keeps_status_predicates_and_validates_scheme_selection() {
  local root out status
  root="$(_tree)"
  printf x >"${root}/left/other.001"
  printf x >"${root}/right/other.001"
  out="$("$(_xff_bin)" --compare=summary "${root}/left" "${root}/right" --shards=of \
    -type f -shard-status complete --format=jsonl)"
  _expect_json_row '{"type":"all","group":"total","count":2,"bytes":8}' "${out}" || return
  status=0
  out="$("$(_xff_bin)" --compare "${root}/left" "${root}/right" --shards=bogus 2>&1)" || status=$?
  expect_eq 2 "${status}" || return
  expect_output_contains 'unknown --shards scheme' "${out}"
}

test::comparison_validates_custom_patterns_and_keeps_them_for_status_predicates() {
  local root out status side
  root="$(_tree)"
  status=0
  out="$("$(_xff_bin)" --compare "${root}/left" "${root}/right" --shards \
    '--shard-pattern=(?P<stem>.*)' 2>&1)" || status=$?
  expect_eq 2 "${status}" || return
  expect_output_contains '--shard-pattern' "${out}" || return
  for side in left right; do
    printf x >"${root}/${side}/img_001_v3.raw"
    printf y >"${root}/${side}/img_002_v3.raw"
  done
  out="$("$(_xff_bin)" --compare=summary "${root}/left" "${root}/right" --shards=of \
    '--shard-pattern=(?P<stem>img)_(?P<index>[0-9]+)_v(?P<dup>[0-9]+)\.raw' \
    -name '*.raw' -shard-status complete --format=jsonl)"
  _expect_json_row '{"type":"all","group":"total","count":2,"bytes":4}' "${out}"
}

test_runner
