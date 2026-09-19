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
# Binary-level test of --color=auto|always|never. Drives the real binary because
# the color decision reads isatty(stdout): the test captures stdout into a
# variable (a pipe, not a tty), so auto stays plain and only --color=always forces
# the ANSI escapes. Anchors on the presence/absence of the directory color escape.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

# The theme variables are inputs under test, so they are cleared once here and set per case. A
# themed shell (a macOS $LSCOLORS in particular) would otherwise change what the unthemed cases see.
unset LS_COLORS LSCOLORS

_xff_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/testing/xff"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff -path '*xff/cli/testing/xff' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

# The ANSI CSI escape for a directory (bold blue) and the SGR reset, as literal
# bytes so grep sees the real ESC (\033). bash 3.2 ANSI-C quoting ($'...').
DIR_COLOR=$'\033\\[1;34m'
RESET=$'\033\\[0m'

# A fresh tree per test with one subdirectory (reliably colorable as a directory).
# `test_tmpdir` allocates each tree under bashtest's managed scratch root. Its random suffix keeps
# test names out of printed paths, where a name could accidentally satisfy a negative assertion.
_make_tree() {
  local path
  path="$(test_tmpdir tree)"
  mkdir -p "${path}/sub"
  printf 'x\n' >"${path}/a.txt"
  echo "${path}"
}

test::color_always_wraps_directories_in_ansi() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" --color=always "${root}" -type d 2>&1)"
  expect_matches "${DIR_COLOR}" "${out}"
  expect_matches "${RESET}" "${out}"
}

test::color_never_emits_no_escapes() {
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" --color=never "${root}" -type d 2>&1)"
  expect_not_matches "${DIR_COLOR}" "${out}"
}

test::color_auto_is_plain_when_stdout_is_not_a_tty() {
  # Default (auto): captured stdout is a pipe, so no color even for a directory.
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" "${root}" -type d 2>&1)"
  expect_not_matches "${DIR_COLOR}" "${out}"
}

test::color_always_leaves_plain_files_uncolored() {
  # A non-executable regular file gets no color escape even under --color=always.
  local root out
  root="$(_make_tree)"
  out="$("$(_xff_bin)" --color=always "${root}" -name a.txt 2>&1)"
  expect_not_matches $'\033\\[' "${out}"
}

test::the_ls_theme_is_the_default_palette() {
  # The colours a user expects are the ones their terminal is themed with, so $LS_COLORS is read by
  # default (`auto`, i.e. ls OR xff): `di=01;35` here must beat xff's own bold blue.
  local root out
  root="$(_make_tree)"
  out="$(LS_COLORS='di=01;35' "$(_xff_bin)" --color=always "${root}" -type d 2>&1)"
  expect_matches $'\033\\[01;35m' "${out}"
}

test::an_extension_entry_from_the_theme_colours_a_plain_file() {
  # xff's own scheme has nothing per-extension, so this is only possible through the theme - and it is
  # the case a themed terminal notices first.
  local root out
  root="$(_make_tree)"
  out="$(LS_COLORS='*.txt=33' "$(_xff_bin)" --color=always "${root}" -name a.txt 2>&1)"
  expect_matches $'\033\\[33m' "${out}"
}

test::color_scheme_xff_ignores_the_theme() {
  # The way back: --color-scheme=xff uses the built-in scheme even with a theme set.
  local root out
  root="$(_make_tree)"
  out="$(LS_COLORS='di=01;35' "$(_xff_bin)" --color=always --color-scheme=xff "${root}" -type d 2>&1)"
  expect_matches "${DIR_COLOR}" "${out}"
  expect_not_matches $'\033\\[01;35m' "${out}"
}

test::ls_and_xff_fills_in_what_the_theme_omits() {
  # The per-KEY merge, which is NOT the default: a theme naming only directories keeps xff's colour
  # for symlinks. Under the default (ls OR xff) the same theme is the whole answer.
  local root out
  root="$(_make_tree)"
  ln -s a.txt "${root}/link"
  for spelling in "ls-and-xff" "merged"; do # the algebra name and the plain word are one value
    out="$(LS_COLORS='di=01;35' "$(_xff_bin)" --color=always "--color-scheme=${spelling}" "${root}" -type l 2>&1)"
    expect_matches $'\033\\[1;36m' "${out}" # xff's symlink colour survives
  done
  out="$(LS_COLORS='di=01;35' "$(_xff_bin)" --color=always "${root}" -type l 2>&1)"
  expect_not_matches $'\033\\[' "${out}" # the default takes the theme whole
}

test::ls_alone_and_the_merge_differ_on_what_the_theme_omits() {
  # The two readings of "use ls colours", side by side on the same theme: `ls` leaves a symlink the
  # theme never mentions uncoloured (as a real ls does), `ls-and-xff` keeps xff's colour for it.
  local root out
  root="$(_make_tree)"
  ln -s a.txt "${root}/link"
  out="$(LS_COLORS='di=01;35' "$(_xff_bin)" --color=always --color-scheme=ls "${root}" -type l 2>&1)"
  expect_not_matches $'\033\\[' "${out}"
  out="$(LS_COLORS='di=01;35' "$(_xff_bin)" --color=always --color-scheme=ls-and-xff "${root}" -type l 2>&1)"
  expect_matches $'\033\\[1;36m' "${out}"
}

test::auto_takes_the_theme_whole_or_not_at_all() {
  # The third reading: with a theme set, `auto` is that theme alone; with none set it is xff's scheme
  # alone - a per-variable decision rather than the per-key fallback of ls+xff.
  local root out
  root="$(_make_tree)"
  ln -s a.txt "${root}/link"
  out="$(LS_COLORS='di=01;35' "$(_xff_bin)" --color=always --color-scheme=auto "${root}" -type l 2>&1)"
  expect_not_matches $'\033\\[' "${out}"
  out="$(env -u LS_COLORS "$(_xff_bin)" --color=always --color-scheme=auto "${root}" -type l 2>&1)"
  expect_matches $'\033\\[1;36m' "${out}"
  # The synonyms are one value, which is what lets a config file name the default.
  for spelling in "ls+xff" "ls-or-xff" "default"; do
    out="$(env -u LS_COLORS "$(_xff_bin)" --color=always "--color-scheme=${spelling}" "${root}" -type l 2>&1)"
    expect_matches $'\033\\[1;36m' "${out}"
  done
}

test::bsd_lscolors_themes_the_listing_on_macos() {
  # BSD's variable in BSD's spelling: 11 fg/bg letter pairs, position as the key. This is the only
  # theme a macOS user who ran no dircolors setup has, so ignoring it made "the colours ls uses"
  # false on that platform.
  local root out
  root="$(_make_tree)"
  out="$(LSCOLORS='ExFxCxDxBxegedabagacad' "$(_xff_bin)" --color=always "${root}" -type d 2>&1)"
  expect_matches $'\033\\[1;34m' "${out}" # `Ex` -> bold blue, the macOS default directory colour
  # $LS_COLORS is the richer format, so it wins wherever both are set.
  out="$(LS_COLORS='di=01;35' LSCOLORS='ExFxCxDxBxegedabagacad' "$(_xff_bin)" --color=always "${root}" -type d 2>&1)"
  expect_matches $'\033\\[01;35m' "${out}"
  # A mis-sized value is ignored whole rather than shifting every later type by a position, which
  # leaves xff's own scheme in place under the default scheme.
  out="$(LSCOLORS='Ex' "$(_xff_bin)" --color=always "${root}" -type d 2>&1)"
  expect_matches "${DIR_COLOR}" "${out}"
}

test::help_documents_color() {
  # Self-documentation: the complete option inventory lists --color.
  expect_output_contains "--color" "$("$(_xff_bin)" --help=all 2>&1)"
  # And the palette flag, whose help has to say where the colours come from.
  out="$("$(_xff_bin)" --help=--color-scheme 2>&1)"
  expect_output_contains "LS_COLORS" "${out}"
  expect_output_contains "LSCOLORS" "${out}" # BSD / macOS spelling of the same theme
  expect_output_contains "ls+xff" "${out}"   # each reading of "ls colours" is named
  expect_output_contains "auto" "${out}"
}

test_runner
