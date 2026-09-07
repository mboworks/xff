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
# Execution tests for the user-facing examples. The `--help=cookbook` recipes (SOT:
# xff/cli/help.cc RenderCookbook) are RUN here against a fixture tree, not merely rendered,
# so a copy-paste of any recipe actually works - this is what would have caught an invalid
# example. A guard case (test::cookbook_recipes_are_all_tested) fails if a recipe is added
# or reworded without a matching case here, so examples ship tested, never just shown.
#
# The two git-blame recipes need `git`; they degrade to a logged skip where git is absent.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

_xff_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/xff"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff -path '*xff/cli/xff' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

XFF_BIN="$(_xff_bin)"
readonly XFF_BIN
readonly TAB=$'\t'

# Isolate from any real user/system config (XFF_CONFIG points at a nonexistent path), and keep
# git from reading a developer's global identity. `xff ...` then reads exactly as the recipes do.
export GIT_CONFIG_NOSYSTEM=1
xff() { XFF_CONFIG="${TEST_TMPDIR}/none" "${XFF_BIN}" "$@"; }

FIX="$(test_tmpdir fixture)"
readonly FIX
GITFIX="$(test_tmpdir gitfix)"
readonly GITFIX

# Build the fixture tree once (idempotent). A mix of extensions/types for --summary=ext, a C++
# file with a TODO for -grep, an old and a fresh *.tmp for -mtime, a text file with no final
# newline for ! -eofnl, and a clearly-largest file for the "ten largest" sort.
_ensure_fixtures() {
  [[ -f "${FIX}/a.txt" ]] && return 0
  mkdir -p "${FIX}/src"
  printf 'abc\n' >"${FIX}/a.txt" # text, ends with a newline
  printf 'log line\n' >"${FIX}/b.log"
  printf 'no final newline' >"${FIX}/no_nl.txt" # text, NO final newline
  printf '# title\n' >"${FIX}/small.md"
  printf 'x%.0s' {1..2000} >"${FIX}/big.bin"                       # clearly the largest file
  printf '// TODO: fix this\nint main() {}\n' >"${FIX}/src/foo.cc" # C++, contains TODO
  printf 'int x;\n' >"${FIX}/src/bar.h"
  printf 'tmp\n' >"${FIX}/old.tmp"
  printf 'tmp\n' >"${FIX}/fresh.tmp"
  touch -t 200001010000 "${FIX}/old.tmp" # > 7 days old (portable -t form)

  # Best-effort git repo for the two git-blame recipes; any failure leaves _have_git false.
  if command -v git >/dev/null 2>&1; then
    mkdir -p "${GITFIX}"
    (
      cd "${GITFIX}"
      git init -q
      git config user.email 'author@example.com'
      git config user.name 'Example Author'
      git config commit.gpgsign false
      printf 'line one\nline two\nline three\n' >code.txt
      git add -A
      git commit -q -m init
    ) >/dev/null 2>&1 || true
  fi
  return 0
}

_have_git() {
  command -v git >/dev/null 2>&1 || return 1
  [[ -d "${GITFIX}/.git" ]] || return 1
  git -C "${GITFIX}" rev-parse HEAD >/dev/null 2>&1
}

# --- The guard: no recipe ships without a test here --------------------------------------------

test::cookbook_recipes_are_all_tested() {
  # GUARD (do not delete): every `--help=cookbook` recipe must have an execution case in this
  # file. Recipe command lines are the indented `xff ...` lines (notes/tasks never start with
  # `xff`). If you add or change a recipe in xff/cli/help.cc RenderCookbook, add or update the
  # matching test::recipe_* case below and keep this list in sync - examples are run, not just
  # rendered. The count catches additions/removals; the per-recipe tokens catch rewordings.
  local cookbook
  cookbook="$(xff --help=cookbook 2>&1)"

  # Count the recipe command lines (the indented `xff ...` lines) with pure bash - the
  # no-shell-grep-in-bashtests guard rightly forbids a hand-rolled `grep` in a *_test.sh, and
  # this is extraction, not an assertion (STYLE_CPP.md "Shell / binary-level tests").
  local count=0 line
  while IFS= read -r line; do
    [[ "${line}" =~ ^[[:space:]]+xff[[:space:]] ]] && count=$((count + 1))
  done <<<"${cookbook}"
  expect_eq "9" "${count}" # a new/removed recipe: add/remove a test::recipe_* case and update this

  # One distinctive token per tested recipe (keep aligned with the cases below).
  expect_output_contains 'sort -rn | head' "${cookbook}"   # ten largest files
  expect_output_contains '--summary=ext' "${cookbook}"     # disk use per file type
  expect_output_contains '-mtime +7' "${cookbook}"         # delete stale temp files
  expect_output_contains '-grep' "${cookbook}"             # search code content by language
  expect_output_contains 'git blame' "${cookbook}"         # per-file git-blame line counts
  expect_output_contains '-capturedir:blame' "${cookbook}" # author line counts, natively
  expect_output_contains '-hash:sha256' "${cookbook}"      # checksum manifest for a tree
  expect_output_contains '--compare=diff' "${cookbook}"    # patch between repository trees
  expect_output_contains '--format=jsonl' "${cookbook}"    # recently changed files as rows
}

# --- One case per cookbook recipe (each RUNS the recipe against the fixture) --------------------

test::recipe_ten_largest_files() {
  _ensure_fixtures
  local raw rc
  raw="$(cd "${FIX}" && xff . -type f -printf '%s\t%p\n' 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  local top size
  top="$(printf '%s\n' "${raw}" | sort -rn | head -1)"
  expect_output_contains 'big.bin' "${top}" # the largest file sorts to the top
  size="${top%%"${TAB}"*}"
  expect_matches '^[0-9]+$' "${size}" # `%s` produced a numeric size, `%p` the path
}

test::recipe_disk_use_per_file_type() {
  _ensure_fixtures
  local out rc
  out="$(cd "${FIX}" && xff . -type f --summary=ext 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_output_not_contains 'src/foo.cc' "${out}" # --summary replaces the per-file listing
  expect_output_contains 'txt' "${out}"            # grouped by extension
}

test::recipe_delete_stale_temp_files_safely() {
  _ensure_fixtures
  local out rc
  out="$(cd "${FIX}" && xff . -type f -name '*.tmp' -mtime +7 -delete --dry-run 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_output_contains 'old.tmp' "${out}"         # the > 7-day file is a candidate
  expect_output_not_contains 'fresh.tmp' "${out}"   # the fresh file is excluded by -mtime +7
  expect_output_contains 'old.tmp' "$(ls "${FIX}")" # --dry-run removed nothing
}

test::recipe_search_code_content_by_language() {
  _ensure_fixtures
  local out rc
  out="$(cd "${FIX}" && xff src -lang 'C*' -grep 'TODO' 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_output_contains 'TODO' "${out}"
  expect_matches 'foo\.cc:[0-9]+:' "${out}" # path:lineno:text form
}

test::recipe_git_blame_author_line_counts() {
  _ensure_fixtures
  if ! _have_git; then
    echo "skip: git unavailable - git-blame recipe not exercised" >&2
    return 0
  fi
  local raw rc
  raw="$(cd "${GITFIX}" && xff . -text -exec git blame --line-porcelain {} \; 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  # `--line-porcelain` emits `author <name>` lines; the recipe's `grep '^author ' | sort |
  # uniq -c | sort -rn` tail is standard shell aggregation (not xff), and a hand-rolled shell
  # `grep` is disallowed in a bashtest, so we assert on xff's raw per-line output instead.
  expect_output_contains 'author Example Author' "${raw}"
}

test::recipe_author_line_counts_natively() {
  _ensure_fixtures
  if ! _have_git; then
    echo "skip: git unavailable - native git-blame recipe not exercised" >&2
    return 0
  fi
  local out rc
  out="$(cd "${GITFIX}" && xff -g . -text -capturedir:blame git blame --line-porcelain {} \; \
    --summary='{capture.blame:m/^author (.+)$/\1/}' 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_output_contains 'Example Author' "${out}" # m// extraction tallies lines per author
}

test::recipe_checksum_manifest_for_a_tree() {
  _ensure_fixtures
  local out rc
  out="$(cd "${FIX}" && xff . -type f -hash:sha256 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  local first digest
  first="$(printf '%s\n' "${out}" | head -1)"
  digest="${first%% *}" # `<digest>  <path>` -> the digest
  expect_eq "64" "${#digest}"
  expect_matches '^[0-9a-f]+$' "${digest}" # sha256 hex
}

test::recipe_patch_between_repository_trees() {
  local trees old_tree new_tree out rc
  trees="$(test_tmpdir compare_trees)"
  old_tree="${trees}/old-tree"
  new_tree="${trees}/new-tree"
  mkdir -p "${old_tree}" "${new_tree}"
  printf 'before\n' >"${old_tree}/changed.txt"
  printf 'after\n' >"${new_tree}/changed.txt"
  printf 'ignored.txt\n' >"${old_tree}/.gitignore"
  printf 'ignored.txt\n' >"${new_tree}/.gitignore"
  printf 'old noise\n' >"${old_tree}/ignored.txt"
  printf 'new noise\n' >"${new_tree}/ignored.txt"
  out="$(xff -g+ --compare=diff "${old_tree}" "${new_tree}" 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_output_contains '--- a/changed.txt' "${out}"
  expect_output_contains '+++ b/changed.txt' "${out}"
  expect_output_not_contains 'ignored.txt' "${out}" # each tree applied its own .gitignore
}

test::recipe_recently_changed_as_machine_rows() {
  _ensure_fixtures
  local out rc
  out="$(cd "${FIX}" && xff . -type f -mtime -1 --format=jsonl 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  local first
  first="$(printf '%s\n' "${out}" | head -1)"
  expect_matches '^\{' "${first}"         # one JSON object per line
  expect_output_contains 'a.txt' "${out}" # a freshly created file is within the last day
}

# --- A README example that is not (yet) a cookbook recipe ---------------------------------------

test::readme_missing_final_newline_linter() {
  # The README "Missing Newline Code Linting" example: text files with no trailing newline.
  _ensure_fixtures
  local out rc
  out="$(cd "${FIX}" && xff . -text ! -eofnl -print 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_output_contains 'no_nl.txt' "${out}" # text file lacking a final newline
  expect_output_not_contains 'a.txt' "${out}" # a.txt ends with a newline, so it is excluded
}

test_runner
