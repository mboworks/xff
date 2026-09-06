#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

# Turn the two unstripped Bazel release outputs into one self-contained platform archive. Its
# executables are stripped and its debug/ directory retains their matching symbol files.
#
# Usage: stage_release_artifacts.sh PLATFORM ARCH LEAN_BINARY FULL_BINARY DIST_DIR

set -euo pipefail

die() {
  echo "stage_release_artifacts: ERROR: ${*}" >&2
  exit 2
}

find_tool() {
  local configured="$1"
  shift
  if [[ -n "${configured}" ]]; then
    [[ -x "${configured}" ]] || die "configured tool is not executable: ${configured}"
    printf '%s\n' "${configured}"
    return
  fi
  local candidate
  for candidate in "$@"; do
    if command -v "${candidate}" >/dev/null 2>&1; then
      command -v "${candidate}"
      return
    fi
  done
  die "required tool not found: $*"
}

[[ "$#" -eq 5 ]] || die "usage: $0 PLATFORM ARCH LEAN_BINARY FULL_BINARY DIST_DIR"
platform="$1"
arch="$2"
lean="$3"
full="$4"
dist="$5"

[[ "${platform}" == "linux" || "${platform}" == "macos" ]] || die "unsupported platform: ${platform}"
[[ -n "${arch}" && "${arch}" != */* ]] || die "invalid architecture: ${arch}"
[[ -x "${lean}" ]] || die "lean binary is not executable: ${lean}"
[[ -x "${full}" ]] || die "full binary is not executable: ${full}"

mkdir -p "${dist}"
bundle_name="xff-${platform}-${arch}"
bundle="${dist}/.${bundle_name}-staging/${bundle_name}"
symbols="${bundle}/debug"
mkdir -p "${symbols}"

lean_name="xff"
full_name="xff_full"
install -m 0755 "${lean}" "${bundle}/${lean_name}"
install -m 0755 "${full}" "${bundle}/${full_name}"

if [[ "${platform}" == "linux" ]]; then
  objcopy="$(find_tool "${RELEASE_OBJCOPY:-}" llvm-objcopy objcopy)"
  strip_tool="$(find_tool "${RELEASE_STRIP:-}" llvm-strip strip)"
  for name in "${lean_name}" "${full_name}"; do
    debug_file="${symbols}/${name}.debug"
    "${objcopy}" --only-keep-debug "${bundle}/${name}" "${debug_file}"
    "${strip_tool}" --strip-all "${bundle}/${name}"
    "${objcopy}" --add-gnu-debuglink="${debug_file}" "${bundle}/${name}"
  done
else
  strip_tool="$(find_tool "${RELEASE_STRIP:-}" strip)"
  codesign_tool="$(find_tool "${RELEASE_CODESIGN:-}" codesign)"
  for name in "${lean_name}" "${full_name}"; do
    # dsymutil cannot recover line tables from lld's temporary ThinLTO object after the link. Keep
    # the complete linker-signed executable as the symbol file; it retains the local symbol table
    # removed from the downloadable copy and can be supplied directly to LLDB or atos.
    install -m 0755 "${bundle}/${name}" "${symbols}/${name}.debug"
    "${strip_tool}" -S -x "${bundle}/${name}"
    "${codesign_tool}" --force --sign - "${bundle}/${name}"
  done
fi

archive="${dist}/${bundle_name}.tar.gz"
COPYFILE_DISABLE=1 tar -czf "${archive}" -C "${dist}/.${bundle_name}-staging" "${bundle_name}"
printf '%s\n' "${bundle}/${lean_name}" "${bundle}/${full_name}" "${archive}"
