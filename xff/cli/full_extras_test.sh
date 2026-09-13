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
# Verifies that the extended `xff_full` binary actually LINKS every build extra that
# `--config=xff_full` enables - none silently dropped by a bad `select`, a missing `alwayslink`, or a
# backend that fails to self-register. It reads the per-extra availability straight from
# `xff --help=extras` and asserts each enabled extra reports "built into this binary".
#
# This target is `manual` (it links the extras + fetches @pcre2), so wildcard builds skip it; every
# CI test job names it explicitly and runs it under `--config=xff_full` (only the `minimal` job opts
# out of the extras). So its assertions are unconditional on purpose. A plain lean build of xff_full
# would correctly report these extras off - not this test's concern (full_binary_test covers the
# config-agnostic behaviour). As a new extra is added to `--config=xff_full`, add its line here so
# the guarantee grows with the set.

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

# The committed mini container in the runfiles. XFF_MINI_TAR is the rootpath the BUILD file passes;
# a test runs with its runfiles as the working directory, so it resolves as-is - except when the
# path is relative to a different root, in which case the search below finds it.
_mini_tar() {
  if [[ -f "${XFF_MINI_TAR:-}" ]]; then
    echo "${XFF_MINI_TAR}"
    return
  fi
  find "${TEST_SRCDIR}" -type f -name mini.tar 2>/dev/null | head -1
}

_mini_sqfs() {
  if [[ -f "${XFF_MINI_SQFS:-}" ]]; then
    echo "${XFF_MINI_SQFS}"
    return
  fi
  find "${TEST_SRCDIR}" -type f -name mini.sqfs 2>/dev/null | head -1
}

# True when this build cannot run a mount, so the mounting cases below report why and return. MSan
# false-positives on anything it did not instrument, and a mount runs through the dlopened SYSTEM
# libfuse3 - so every byte libfuse writes reads back as uninitialized and the run dies inside
# `fuse_opt_parse`. The C++ server test skips for the same reason under `#if MEMORY_SANITIZER`;
# XFF_MSAN is that macro's build-flag twin, set by the BUILD file under `--config=msan`.
_skip_under_msan() {
  if [[ -z "${XFF_MSAN:-}" ]]; then
    return 1
  fi
  skip_test "MSan cannot model the uninstrumented system libfuse3"
}

_xff_full_bin() {
  local bin="${TEST_SRCDIR}/${TEST_WORKSPACE}/xff/cli/testing/xff_full"
  if [[ ! -x "${bin}" ]]; then
    bin="$(find "${TEST_SRCDIR}" -type f -name xff_full -path '*xff/cli/testing/xff_full' 2>/dev/null | head -1)"
  fi
  echo "${bin}"
}

test::xff_full_links_the_pcre2_extra() {
  # PCRE2 (--//xff:xff_pcre, on under --config=xff_full) must be linked and reported built in - never
  # dropped. Match the bracketed status right after the name (only spaces between, no newline), so it
  # binds to pcre2's own line: a lean binary shows "[not built in ...]" there and this fails. (Bash
  # `[[ =~ ]]` lets `.` cross newlines, so a loose `pcre2.*built in` would bleed into another extra.)
  local out
  out="$("$(_xff_full_bin)" --help=extras 2>&1)"
  expect_matches "pcre2 +\[built into this binary\]" "${out}"
}

test::xff_full_links_the_brotli_extra() {
  local out
  out="$("$(_xff_full_bin)" --help=extras 2>&1)"
  expect_matches "brotli +\[built into this binary\]" "${out}"
}

test::xff_full_links_the_squashfs_extra() {
  local image out
  out="$("$(_xff_full_bin)" --help=extras 2>&1)"
  expect_matches "squashfs +\[built into this binary\]" "${out}"

  image="$(_mini_sqfs)"
  out="$("$(_xff_full_bin)" -L --archive=roots "${image}" -type f -content 'hello from')"
  expect_output_contains "mini.sqfs!hello.txt" "${out}"
}

test::xff_full_links_and_walks_the_asar_extra() {
  local out root archive json
  out="$("$(_xff_full_bin)" --help=extras 2>&1)"
  expect_matches "asar +\[built into this binary\]" "${out}"

  # One canonical Chromium Pickle size, one Pickle-wrapped JSON header, then the file bytes. The
  # fixture is spelled independently here so this is an end-to-end format test, not a round trip
  # through an xff writer (ASAR is deliberately read-only).
  root="$(test_tmpdir asar)"
  archive="${root}/app.asar"
  json='{"files":{"hello.txt":{"size":5,"offset":"0"}}}'
  printf '\x04\x00\x00\x00\x38\x00\x00\x00\x34\x00\x00\x00\x2f\x00\x00\x00%s\x00hello' \
    "${json}" >"${archive}"
  out="$("$(_xff_full_bin)" --archive=roots "${archive}" -type f -content hello)"
  expect_output_contains "app.asar!hello.txt" "${out}"
}

test::archive_mount_runs_the_action_over_a_real_container() {
  # A real container (the committed mini.tar), through the mount path. Mounting is a per-MACHINE
  # capability, so the assertion is what holds EVERYWHERE: the action runs and the child sees the
  # member's real 13 bytes - served from the mount where the machine can mount, from a temporary
  # copy where it cannot. -L because the fixture reaches the test through runfiles, which are
  # symlinks: unfollowed, the container is not a regular file and nothing dives into it.
  _skip_under_msan && return 0
  local out
  out="$("$(_xff_full_bin)" -L --archive=all --archive-mount --archive-extract \
    "$(_mini_tar)" -type f -name hello.txt -exec wc -c {} \; 2>&1)"
  # The whole output goes to the log: this case depends on machine capabilities, and bashtest
  # ellipsizes the text it reports on failure - which turned a CI failure here into a guess.
  echo "xff said: ${out}"
  expect_output_contains '13' "${out}"
  expect_output_not_contains 'cannot run on an archive member' "${out}"
}

test::archive_mount_alone_mounts_where_it_can_and_refuses_where_it_cannot() {
  # Without --archive-extract there is no fallback, so the outcome depends on the MACHINE: mount and
  # run, or refuse. That is not an excuse for a matcher that accepts either - the environment
  # already declares which machine this is. The Bazel FUSE-test config supplies the private marker
  # exactly where mounting must work, so each case gets its own exact assertion and neither can
  # silently turn into the other.
  _skip_under_msan && return 0
  local out
  out="$("$(_xff_full_bin)" -L --archive=all --archive-mount \
    "$(_mini_tar)" -type f -name hello.txt -exec wc -c {} \; 2>&1)" || true
  # Same reason as the case above: bashtest ellipsizes the text it reports, so the log carries the
  # whole thing or a CI failure here is a guess.
  echo "xff said: ${out}"
  if [[ -n "${XFF_FUSE_TESTS_REQUIRED:-}" ]]; then
    # Mounting works here: the child must read the member IN PLACE, with no fallback available.
    expect_output_contains '13' "${out}"
    expect_output_not_contains 'cannot run on an archive member' "${out}"
  else
    # Mounting cannot work here, so the action is refused - and the message names both ways out,
    # since either flag would have made the command run.
    expect_output_contains 'use --archive-mount to run it on the member in place' "${out}"
    expect_output_contains 'or --archive-extract to run it on a temporary copy' "${out}"
  fi
}

test::the_notice_lists_every_linked_extra_and_the_direct_codecs() {
  # The extras line is derived from EnabledExtras() (it used to check only archive, so a binary
  # with pcre2 linked under-reported itself), and zlib/bzip2 are components in their OWN right:
  # the phar reader inflates members with them directly, not through libarchive.
  local out
  out="$("$(_xff_full_bin)" --help=notice 2>&1)"
  local extras_line enabled_extras extra
  local -a extras
  extras_line="${out%%$'\n'*}"
  enabled_extras=" ${extras_line#Build extras compiled into this binary: } "
  enabled_extras="${enabled_extras//,/}"

  # The first line and the extension headings come from different registries. Check them in both
  # directions: a newly enabled extra must register its own notice, and a stale/superfluous
  # extension notice must not claim code this binary did not compile.
  IFS=',' read -r -a extras <<<"${extras_line#Build extras compiled into this binary: }"
  for extra in "${extras[@]}"; do
    extra="${extra// /}"
    extra="${extra//-/_}"
    expect_output_contains "(@xff_${extra})" "${out}"
  done
  while IFS= read -r extra; do
    extra="${extra//_/-}"
    expect_matches " ${extra} " "${enabled_extras}"
  done < <(sed -n 's/^Build extension:.*(@xff_\([^)]*\)).*/\1/p' <<<"${out}")

  expect_output_contains 'Build extension: FUSE (@xff_fuse)' "${out}"
  expect_output_contains 'Build extension: PCRE2 (@xff_pcre2)' "${out}"
  expect_output_contains 'Build extension: Archive (@xff_archive)' "${out}"
  expect_output_contains 'Build extension: Electron ASAR (@xff_asar)' "${out}"
  expect_output_contains 'Build extension: Brotli archive compression (@xff_brotli)' "${out}"
  expect_output_contains 'Brotli  [MIT]' "${out}"
  expect_output_contains 'zlib  [Zlib]' "${out}"
  expect_output_contains 'bzip2  [bzip2-1.0.6]' "${out}"
  expect_output_contains 'liblzma (XZ Utils)  [LicenseRef-Public-Domain]' "${out}"
  expect_output_contains 'LZ4 library  [BSD-2-Clause]' "${out}"
  expect_output_contains 'Zstandard  [BSD-3-Clause]' "${out}"
  expect_output_contains 'PCRE2  [BSD-3-Clause WITH PCRE2-exception]' "${out}"
  expect_output_contains 'SLJIT  [BSD-2-Clause]' "${out}"
}

test::the_full_binary_embeds_each_extensions_license_bodies() {
  local out
  out="$("$(_xff_full_bin)" --help=license=PCRE2 2>&1)"
  expect_output_contains 'BSD-3-Clause WITH PCRE2-exception' "${out}"
  expect_output_contains 'EXEMPTION FOR BINARY LIBRARY-LIKE PACKAGES' "${out}"
  out="$("$(_xff_full_bin)" '--help=license=liblzma (XZ Utils)' 2>&1)"
  expect_output_contains 'LicenseRef-Public-Domain' "${out}"
  expect_output_contains 'do whatever you want' "${out}"
  out="$("$(_xff_full_bin)" --help=license=bzip2 2>&1)"
  expect_output_contains 'bzip2/libbzip2 version 1.0.8' "${out}"
  out="$("$(_xff_full_bin)" --help=license=Brotli 2>&1)"
  expect_output_contains 'MIT' "${out}"
  expect_output_contains 'Permission is hereby granted, free of charge' "${out}"
}

test_runner
