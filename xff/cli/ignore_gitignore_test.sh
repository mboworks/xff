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
# End-to-end test of -g / --gitignore: per-directory .gitignore files, opt-in (off
# by default, find-compatible), stacked per directory, and disabled by -u. Bare -g is
# the AUTO ternary (respect .gitignore only inside a git repo); -g+/=on force it on
# regardless, -g-/=off force it off. _make_tree plants an empty .git so bare -g sees a repo.

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

_new_tree() {
  test_tmpdir tree
}

# Tree: keep.cc, a.o, sub/b.o, sub/keep.h.  <root>/.gitignore => *.o.  An empty .git
# marks the tree as a git repo, so bare -g (auto) turns .gitignore on. The empty .git
# directory contributes no -type f entries, so it does not perturb the assertions.
_make_tree() {
  local tree
  tree="$(_new_tree)"
  mkdir -p "${tree}/sub" "${tree}/.git"
  touch "${tree}/keep.cc" "${tree}/a.o" "${tree}/sub/b.o" "${tree}/sub/keep.h"
  printf '*.o\n' >"${tree}/.gitignore"
  echo "${tree}"
}

# The same tree WITHOUT a .git, so it is not a git repo (bare -g auto stays off).
_make_tree_no_repo() {
  local tree
  tree="$(_new_tree)"
  mkdir -p "${tree}/sub"
  touch "${tree}/keep.cc" "${tree}/a.o" "${tree}/sub/b.o" "${tree}/sub/keep.h"
  printf '*.o\n' >"${tree}/.gitignore"
  echo "${tree}"
}

test::gitignore_off_by_default() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" "${root}" -type f 2>&1)"
  expect_matches "(^|${NL}|/)a\.o(\$|${NL})" "${out}" # .gitignore not consulted without -g
}

test::dash_g_auto_respects_gitignore_recursively_in_a_repo() {
  local root out
  root="$(_make_tree)" # has .git -> auto turns on
  out="$("$(_xff_bin)" -g "${root}" -type f 2>&1)"
  expect_not_matches "(^|${NL}|/)a\.o(\$|${NL})" "${out}"     # dropped at the root
  expect_not_matches "(^|${NL}|/)sub/b\.o(\$|${NL})" "${out}" # and in the subdirectory
  expect_matches "(^|${NL}|/)keep\.cc(\$|${NL})" "${out}"
  expect_matches "(^|${NL}|/)sub/keep\.h(\$|${NL})" "${out}"
}

test::dash_g_auto_is_off_outside_a_repo() {
  local root out
  root="$(_make_tree_no_repo)" # no .git -> auto stays off, .gitignore ignored
  out="$("$(_xff_bin)" -g "${root}" -type f 2>&1)"
  expect_matches "(^|${NL}|/)a\.o(\$|${NL})" "${out}" # not in a repo: bare -g is a no-op
}

test::gitignore_on_forces_even_outside_a_repo() {
  local root out
  root="$(_make_tree_no_repo)" # no .git, but =on forces regardless
  out="$("$(_xff_bin)" --gitignore=on "${root}" -type f 2>&1)"
  expect_not_matches "(^|${NL}|/)a\.o(\$|${NL})" "${out}"
  expect_matches "(^|${NL}|/)keep\.cc(\$|${NL})" "${out}"
}

test::gitignore_takes_the_whole_shared_value_vocabulary() {
  # One vocabulary across flags: =on / =yes / =true / =1 all force it, =off / =no / =false / =0 all
  # disable it, and =auto is the bare -g behaviour spelled out. Before this the flag compared the
  # two literal strings on / off, so =yes silently did nothing.
  local root out spelling
  root="$(_make_tree_no_repo)" # no .git, so only a forcing value can hide a.o
  for spelling in on yes true 1; do
    out="$("$(_xff_bin)" "--gitignore=${spelling}" "${root}" -type f 2>&1)"
    expect_not_matches "(^|${NL}|/)a\.o(\$|${NL})" "${out}"
  done
  for spelling in off no false 0 auto; do
    out="$("$(_xff_bin)" "--gitignore=${spelling}" "${root}" -type f 2>&1)"
    expect_matches "(^|${NL}|/)a\.o(\$|${NL})" "${out}" # not forced: outside a repo nothing is ignored
  done
}

test::gitignore_off_value_disables() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" --gitignore=off "${root}" -type f 2>&1)"
  expect_matches "(^|${NL}|/)a\.o(\$|${NL})" "${out}"
}

test::dash_g_plus_forces_on_outside_a_repo() {
  local root out
  root="$(_make_tree_no_repo)" # no .git; -g+ is the short form of =on (force on)
  out="$("$(_xff_bin)" -g+ "${root}" -type f 2>&1)"
  expect_not_matches "(^|${NL}|/)a\.o(\$|${NL})" "${out}"
  expect_matches "(^|${NL}|/)keep\.cc(\$|${NL})" "${out}"
}

test::dash_g_minus_forces_off_in_a_repo() {
  local root out
  root="$(_make_tree)" # has .git, so bare -g would auto-on; -g- (=off) forces it off
  out="$("$(_xff_bin)" -g- "${root}" -type f 2>&1)"
  expect_matches "(^|${NL}|/)a\.o(\$|${NL})" "${out}" # -g- overrides the repo auto-on
}

test::no_ignore_master_switch_overrides_dash_g() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" -g -u "${root}" -type f 2>&1)"
  expect_matches "(^|${NL}|/)a\.o(\$|${NL})" "${out}" # -u force-disables even with -g
  out="$("$(_xff_bin)" -u -g+ "${root}" -type f 2>&1)"
  # ... and even a forced -g+, regardless of position: -u is a master switch, not a
  # participant in the -g last-wins scan.
  expect_matches "(^|${NL}|/)a\.o(\$|${NL})" "${out}"
}

test::ignore_vcs_respects_gitignore_like_dash_g() {
  local root out
  root="$(_make_tree)" # repo + .gitignore=*.o; --ignore-vcs == auto (respect in a repo)
  out="$("$(_xff_bin)" --ignore-vcs "${root}" -type f 2>&1)"
  expect_not_matches "(^|${NL}|/)a\.o(\$|${NL})" "${out}"
  expect_not_matches "(^|${NL}|/)sub/b\.o(\$|${NL})" "${out}"
  expect_matches "(^|${NL}|/)keep\.cc(\$|${NL})" "${out}"
}

test::ignore_vcs_flags_are_last_wins() {
  local root out
  root="$(_make_tree)"
  # -g turns the VCS layer on; a trailing --no-ignore-vcs wins -> a.o shows again.
  out="$("$(_xff_bin)" -g --no-ignore-vcs "${root}" -type f 2>&1)"
  expect_matches "(^|${NL}|/)a\.o(\$|${NL})" "${out}"
  root="$(_make_tree)"
  # ... and the reverse order re-enables it (one last-occurrence-wins scan with -g / --gitignore).
  out="$("$(_xff_bin)" --no-ignore-vcs --ignore-vcs "${root}" -type f 2>&1)"
  expect_not_matches "(^|${NL}|/)a\.o(\$|${NL})" "${out}"
}

# Repo with BOTH a .gitignore (the VCS layer, *.o) and a .ignore (the --ignore-files layer, *.log),
# so the two axes are separable: keep.cc, a.o (gitignored), b.log (.ignore'd).
_make_tree_vcs_and_ignore() {
  local tree
  tree="$(_new_tree)"
  mkdir -p "${tree}/.git"
  touch "${tree}/keep.cc" "${tree}/a.o" "${tree}/b.log"
  printf '*.o\n' >"${tree}/.gitignore"
  printf '*.log\n' >"${tree}/.ignore"
  echo "${tree}"
}

test::no_ignore_vcs_drops_only_the_vcs_layer_keeping_ignore_files() {
  local root out
  root="$(_make_tree_vcs_and_ignore)"
  # --no-ignore-vcs drops the VCS (.gitignore) layer, but --ignore-files (.ignore) stays on: a.o
  # reappears while b.log stays dropped. That is exactly the difference from -u / --no-ignore.
  out="$("$(_xff_bin)" --no-ignore-vcs --ignore-files "${root}" -type f 2>&1)"
  expect_matches "(^|${NL}|/)a\.o(\$|${NL})" "${out}"       # VCS layer off
  expect_not_matches "(^|${NL}|/)b\.log(\$|${NL})" "${out}" # .ignore layer still on
  expect_matches "(^|${NL}|/)keep\.cc(\$|${NL})" "${out}"
}

test::no_ignore_master_switch_is_broader_than_no_ignore_vcs() {
  local root out
  root="$(_make_tree_vcs_and_ignore)"
  # -u / --no-ignore turns off EVERY ignore source, so both a.o and b.log show (the broader switch).
  out="$("$(_xff_bin)" -u --ignore-files "${root}" -type f 2>&1)"
  expect_matches "(^|${NL}|/)a\.o(\$|${NL})" "${out}"   # .gitignore off
  expect_matches "(^|${NL}|/)b\.log(\$|${NL})" "${out}" # .ignore off too (unlike --no-ignore-vcs)
}

test::nested_gitignore_scopes_to_its_subtree() {
  local root out
  root="$(_new_tree)"
  mkdir -p "${root}/sub"
  touch "${root}/top.tmp" "${root}/sub/inner.tmp"
  printf '*.tmp\n' >"${root}/sub/.gitignore"                    # only in sub/
  out="$("$(_xff_bin)" --gitignore=on "${root}" -type f 2>&1)"  # =on: exercise nesting without a .git
  expect_not_matches "(^|${NL}|/)inner\.tmp(\$|${NL})" "${out}" # sub/.gitignore applies here
  expect_matches "(^|${NL}|/)top\.tmp(\$|${NL})" "${out}"       # but not above its directory
}

test::git_info_exclude_is_honored() {
  local root out
  root="$(_new_tree)"
  mkdir -p "${root}/.git/info" # a real .git dir -> repo root
  printf '*.tmp\n' >"${root}/.git/info/exclude"
  touch "${root}/keep.cc" "${root}/drop.tmp"
  out="$("$(_xff_bin)" -g "${root}" -type f 2>&1)"
  expect_not_matches "(^|${NL}|/)drop\.tmp(\$|${NL})" "${out}" # .git/info/exclude drops it
  expect_matches "(^|${NL}|/)keep\.cc(\$|${NL})" "${out}"
}

test::dash_g_from_a_subdir_honors_repo_root_ignores() {
  # Search root is a SUBDIR of the repo: git/rg/fd honor the repo-root .gitignore and
  # .git/info/exclude even though they sit ABOVE the search root.
  local root out
  root="$(_new_tree)"
  mkdir -p "${root}/.git/info" "${root}/sub"
  printf '*.log\n' >"${root}/.gitignore"        # repo-root .gitignore (above the search root)
  printf '*.tmp\n' >"${root}/.git/info/exclude" # repo-level exclude
  touch "${root}/sub/a.log" "${root}/sub/b.tmp" "${root}/sub/keep.cc"
  out="$("$(_xff_bin)" -g "${root}/sub" -type f 2>&1)"
  expect_not_matches "(^|${NL}|/)a\.log(\$|${NL})" "${out}" # repo-root .gitignore reaches down
  expect_not_matches "(^|${NL}|/)b\.tmp(\$|${NL})" "${out}" # .git/info/exclude reaches down
  expect_matches "(^|${NL}|/)keep\.cc(\$|${NL})" "${out}"
}

test::default_global_git_ignore_is_honored() {
  # git's default global ignore ($XDG_CONFIG_HOME/git/ignore, else ~/.config/git/ignore)
  # is the lowest ignore layer. Drive it via a throwaway HOME (XDG unset).
  local root home out
  root="$(_new_tree)"
  home="$(_new_tree)"
  mkdir -p "${root}/.git" "${home}/.config/git"
  touch "${root}/keep.cc" "${root}/drop.bak"
  printf '*.bak\n' >"${home}/.config/git/ignore"
  out="$(env -u XDG_CONFIG_HOME HOME="${home}" "$(_xff_bin)" -g "${root}" -type f 2>&1)"
  expect_not_matches "(^|${NL}|/)drop\.bak(\$|${NL})" "${out}" # ~/.config/git/ignore applies
  expect_matches "(^|${NL}|/)keep\.cc(\$|${NL})" "${out}"
}

test::core_excludesfile_is_honored() {
  # core.excludesFile in ~/.gitconfig points at a custom global ignore file.
  local root home out
  root="$(_new_tree)"
  home="$(_new_tree)"
  mkdir -p "${root}/.git"
  touch "${root}/keep.cc" "${root}/drop.bak"
  printf '*.bak\n' >"${home}/my_ignore"
  printf '[core]\n\texcludesfile = %s/my_ignore\n' "${home}" >"${home}/.gitconfig"
  out="$(env -u XDG_CONFIG_HOME HOME="${home}" "$(_xff_bin)" -g "${root}" -type f 2>&1)"
  expect_not_matches "(^|${NL}|/)drop\.bak(\$|${NL})" "${out}" # core.excludesFile applies
  expect_matches "(^|${NL}|/)keep\.cc(\$|${NL})" "${out}"
}

test::help_topic_documents_gitignore() {
  # Check the exit status separately from the content, so a crashed subprocess (rc != 0,
  # e.g. a sanitizer abort) is distinguishable from a genuine "substring missing"; the
  # content matcher prints the captured output on failure, so no manual diagnostic is needed.
  local out rc
  out="$("$(_xff_bin)" --help=-g 2>&1)" && rc=0 || rc=$?
  expect_eq "0" "${rc}"
  expect_output_contains 'gitignore' "${out}"
}

# Tree: .gitkeep + keep.cc + build.o, with .gitignore = `*` (ignore everything). A .gitkeep is a
# placeholder that keeps its directory in the repo, so xff always keeps it against the gitignore
# layers (#120); the rest is ignored. --hidden shows the dotfile so the exemption is observable.
_make_gitkeep_tree() {
  local tree
  tree="$(_new_tree)"
  mkdir -p "${tree}/.git"
  touch "${tree}/.gitkeep" "${tree}/keep.cc" "${tree}/build.o"
  printf '*\n' >"${tree}/.gitignore"
  echo "${tree}"
}

test::gitignore_always_keeps_gitkeep() {
  local root out
  root="$(_make_gitkeep_tree)"
  out="$("$(_xff_bin)" -g+ --hidden "${root}" -type f 2>&1)"
  expect_matches "(^|${NL}|/)\.gitkeep(\$|${NL})" "${out}"    # exempt from `*`, always kept
  expect_not_matches "(^|${NL}|/)keep\.cc(\$|${NL})" "${out}" # ignored by `*`
  expect_not_matches "(^|${NL}|/)build\.o(\$|${NL})" "${out}" # ignored by `*`
}

test::explicit_exclude_overrides_gitkeep_exemption() {
  local root out
  root="$(_make_gitkeep_tree)"
  # The gitignore exemption keeps .gitkeep, but an explicit --exclude still wins over it.
  out="$("$(_xff_bin)" -g+ --hidden --exclude='.gitkeep' "${root}" -type f 2>&1)"
  expect_not_matches "(^|${NL}|/)\.gitkeep(\$|${NL})" "${out}"
}

# A repo whose .git carries plumbing, alongside a non-git hidden dotfile the user keeps
# (.bazelrc), the tracked .gitignore itself, and a gitignored file. git never lists .git in a
# .gitignore (it excludes its own plumbing implicitly), so -g must drop the whole .git tree while
# leaving the user's other hidden files alone.
_make_repo_with_dotfiles() {
  local tree
  tree="$(_new_tree)"
  mkdir -p "${tree}/.git/hooks" "${tree}/build"
  printf 'ref: refs/heads/main\n' >"${tree}/.git/HEAD"
  : >"${tree}/.git/hooks/pre-commit.sample"
  printf 'build/\n' >"${tree}/.gitignore"
  printf 'build --config=foo\n' >"${tree}/.bazelrc"
  : >"${tree}/build/out.o"
  : >"${tree}/keep.cc"
  echo "${tree}"
}

test::dash_g_excludes_gits_own_metadata_tree() {
  local root out
  root="$(_make_repo_with_dotfiles)" # has .git -> bare -g auto-on
  out="$("$(_xff_bin)" -g "${root}" 2>&1)"
  expect_not_matches "(^|${NL}|/)\.git(/|\$|${NL})" "${out}" # git's own dir + all its contents gone
  expect_matches "(^|${NL}|/)\.bazelrc(\$|${NL})" "${out}"   # a non-git hidden file the user keeps: shown
  expect_matches "(^|${NL}|/)\.gitignore(\$|${NL})" "${out}" # the tracked .gitignore file itself: shown
  expect_matches "(^|${NL}|/)keep\.cc(\$|${NL})" "${out}"
  expect_not_matches "(^|${NL}|/)out\.o(\$|${NL})" "${out}" # build/ ignored by .gitignore
}

test::git_metadata_exclusion_is_independent_of_hidden() {
  local root out
  root="$(_make_repo_with_dotfiles)"
  # -g is git mode, not a hidden toggle: even with --hidden forced on, .git stays out (it is git
  # plumbing, not a mere dotfile) while the user's own dotfiles show.
  out="$("$(_xff_bin)" --hidden -g "${root}" 2>&1)"
  expect_not_matches "(^|${NL}|/)\.git(/|\$|${NL})" "${out}"
  expect_matches "(^|${NL}|/)\.bazelrc(\$|${NL})" "${out}"
}

test::git_metadata_shown_when_gitignore_off() {
  local root out
  root="$(_make_repo_with_dotfiles)"
  # Gitignore off (-g-, find-compatible): xff shows everything, including git's .git tree.
  out="$("$(_xff_bin)" -g- "${root}" 2>&1)"
  expect_matches "(^|${NL}|/)\.git(/|\$|${NL})" "${out}"
}

# A tree holding several VCS metadata dirs plus a non-VCS dotfile, so --skip-vcs's scope is testable.
_make_multi_vcs_tree() {
  local tree
  tree="$(_new_tree)"
  mkdir -p "${tree}/.git/x" "${tree}/.hg/y" "${tree}/.svn" "${tree}/src"
  : >"${tree}/.git/config"
  : >"${tree}/.hg/store"
  : >"${tree}/.bazelrc"
  : >"${tree}/src/a.cc"
  echo "${tree}"
}

test::skip_vcs_bare_prunes_every_known_vcs() {
  local root out
  root="$(_make_multi_vcs_tree)"
  out="$("$(_xff_bin)" --skip-vcs "${root}" 2>&1)"
  expect_not_matches "(^|${NL}|/)\.git(/|\$|${NL})" "${out}"
  expect_not_matches "(^|${NL}|/)\.hg(/|\$|${NL})" "${out}"
  expect_not_matches "(^|${NL}|/)\.svn(/|\$|${NL})" "${out}"
  expect_matches "(^|${NL}|/)\.bazelrc(\$|${NL})" "${out}" # a non-VCS dotfile is untouched
  expect_matches "(^|${NL}|/)a\.cc(\$|${NL})" "${out}"
}

test::skip_vcs_list_is_an_explicit_subset() {
  local root out
  root="$(_make_multi_vcs_tree)"
  # An explicit list prunes exactly those; the others stay. No -g needed - it is standalone.
  out="$("$(_xff_bin)" --skip-vcs=git,hg "${root}" 2>&1)"
  expect_not_matches "(^|${NL}|/)\.git(/|\$|${NL})" "${out}"
  expect_not_matches "(^|${NL}|/)\.hg(/|\$|${NL})" "${out}"
  expect_matches "(^|${NL}|/)\.svn(/|\$|${NL})" "${out}" # not listed -> kept
}

test::skip_vcs_hg_only_leaves_git_when_gitignore_off() {
  local root out
  root="$(_make_multi_vcs_tree)"
  # --skip-vcs=hg without -g: only .hg goes; .git stays (no gitignore -> .git default).
  out="$("$(_xff_bin)" --skip-vcs=hg "${root}" 2>&1)"
  expect_not_matches "(^|${NL}|/)\.hg(/|\$|${NL})" "${out}"
  expect_matches "(^|${NL}|/)\.git(/|\$|${NL})" "${out}"
}

test::no_skip_vcs_opts_out_of_the_dash_g_git_default() {
  local root out
  root="$(_make_multi_vcs_tree)"
  # -g would imply --skip-vcs=git; --no-skip-vcs overrides it, so .git shows again.
  out="$("$(_xff_bin)" -g+ --no-skip-vcs "${root}" 2>&1)"
  expect_matches "(^|${NL}|/)\.git(/|\$|${NL})" "${out}"
}

test::skip_vcs_unknown_token_is_a_usage_error() {
  local root out rc
  root="$(_make_multi_vcs_tree)"
  out="$("$(_xff_bin)" --skip-vcs=bogus "${root}" 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_matches "unknown --skip-vcs value 'bogus'" "${out}"
}

test_runner
