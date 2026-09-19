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
  root="$(test_tmpdir shards)"
  printf aa >"${root}/data-00000-of-00002"
  printf bb >"${root}/data-00001-of-00002"
  echo "${root}"
}

_expect_total() {
  python3 -c '
import json,sys
rows=[json.loads(line) for line in sys.stdin]
assert any(r.get("group")=="total" and r.get("count")==int(sys.argv[1]) and r.get("bytes")==int(sys.argv[2]) for r in rows), rows
' "$1" "$2" <<<"$3"
}

test::physical_and_logical_count_examples() {
  local root out
  root="$(_tree)"
  out="$("$(_xff_bin)" "${root}" -type f --summary --format=jsonl)"
  _expect_total 2 4 "${out}" || return
  out="$("$(_xff_bin)" "${root}" -type f --shards --summary --format=jsonl)"
  _expect_total 1 4 "${out}" || return
  out="$("$(_xff_bin)" "${root}" -type f -collect --shards --summary --format=jsonl)"
  _expect_total 1 4 "${out}" || return
  out="$("$(_xff_bin)" "${root}" -type f -shard-status complete --summary --format=jsonl)"
  _expect_total 2 4 "${out}"
}

test::size_predicates_and_actions_remain_physical() {
  local root out
  root="$(_tree)"
  out="$("$(_xff_bin)" "${root}" -type f -size 2c --shards --summary --format=jsonl)"
  _expect_total 1 4 "${out}" || return
  out="$("$(_xff_bin)" "${root}" -type f -size 4c --shards --summary --format=jsonl)"
  _expect_total 0 0 "${out}" || return
  out="$("$(_xff_bin)" "${root}" --sort=name -type f -printf '%f:%s\n' --shards)"
  expect_eq "$(printf 'data-00000-of-00002:2\ndata-00001-of-00002:2\n%s/data-00000-of-00002' "${root}")" "${out}" || return
  out="$("$(_xff_bin)" "${root}" -type f --shards '--summary={size}:{shard}' --format=jsonl)"
  expect_output_contains '"group":"4:2"' "${out}"
}

test::classification_and_collection_order_examples() {
  local root out
  root="$(_tree)"
  out="$("$(_xff_bin)" "${root}" -type f -name '*00000*' -shard-status incomplete -print)"
  expect_eq "${root}/data-00000-of-00002" "${out}" || return
  out="$("$(_xff_bin)" "${root}" -type f -shard-status complete -name '*00000*' -print)"
  expect_eq "${root}/data-00000-of-00002" "${out}" || return
  out="$("$(_xff_bin)" "${root}" --sort=name -type f -collect -first 1 --shards --summary --format=jsonl)"
  _expect_total 1 4 "${out}" || return
  out="$("$(_xff_bin)" "${root}" --sort=name -type f -first 1 -collect --shards --summary --format=jsonl)"
  _expect_total 1 2 "${out}"
}

test::missing_and_duplicate_members() {
  local root out status
  root="$(_tree)"
  rm "${root}/data-00001-of-00002"
  out="$("$(_xff_bin)" "${root}" -type f --shards --shards-show=count)"
  expect_output_contains 'INCOMPLETE' "${out}" || return
  out="$("$(_xff_bin)" "${root}" -type f --shards --summary --format=jsonl)"
  _expect_total 1 2 "${out}" || return
  mv "${root}/data-00000-of-00002" "${root}/data-00000-of-00002.aaaaaaaa"
  printf cccc >"${root}/data-00000-of-00002.bbbbbbbb"
  printf bb >"${root}/data-00001-of-00002"
  out="$("$(_xff_bin)" "${root}" -type f --shards --summary --format=jsonl)"
  _expect_total 1 4 "${out}" || return
  out="$("$(_xff_bin)" "${root}" -type f -shard-status superfluous -print)"
  expect_eq "${root}/data-00000-of-00002.bbbbbbbb" "${out}" || return
  status=0
  out="$("$(_xff_bin)" "${root}" -type f --shards --shards-dedup=error 2>&1)" || status=$?
  [[ "${status}" != 0 ]] || fail 'duplicate was accepted' || return
  expect_output_contains 'duplicate' "${out}"
}

test::custom_naming_example() {
  local root out
  root="$(test_tmpdir custom)"
  printf aa >"${root}/img_001_v3.raw"
  printf bb >"${root}/img_002_v3.raw"
  out="$("$(_xff_bin)" "${root}" -type f --shards \
    '--shard-pattern=(?P<stem>img)_(?P<index>[0-9]+)_v(?P<dup>[0-9]+)\.raw' --summary --format=jsonl)"
  _expect_total 1 4 "${out}"
}

test::comparison_keeps_physical_members() {
  local left right out
  left="$(_tree)"
  right="$(_tree)"
  printf cc >"${right}/data-00001-of-00002"
  out="$("$(_xff_bin)" "${left}" "${right}" --compare=summary -type f --shards \
    --summary=ext --summary-scope=compare --format=jsonl)"
  python3 -c '
import json,sys
rows=[json.loads(line) for line in sys.stdin]
rows=[r for r in rows if r.get("group")=="total" and "left-total" in r]
assert len(rows)==1, rows
for side in ("left-total","right-total"):
    assert rows[0][side]["count"]==2 and rows[0][side]["bytes"]==4, rows
' <<<"${out}"
}

test_runner "$@"
