#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

# Turn the two unstripped Bazel release outputs into separate, symmetric distributions. Each raw
# executable is stripped, and its own .tar.zst retains both that executable and its matching debug
# symbol file.
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

if [[ "${platform}" == "linux" ]]; then
  objcopy="$(find_tool "${RELEASE_OBJCOPY:-}" llvm-objcopy objcopy)"
  strip_tool="$(find_tool "${RELEASE_STRIP:-}" llvm-strip strip)"
else
  strip_tool="$(find_tool "${RELEASE_STRIP:-}" strip)"
  codesign_tool="$(find_tool "${RELEASE_CODESIGN:-}" codesign)"
fi
zstd_tool="$(find_tool "${RELEASE_ZSTD:-}" zstd)"

stage_binary() {
  local name="$1"
  local source="$2"
  local bundle_name="${name}-${platform}-${arch}"
  local staging="${dist}/.${bundle_name}-staging"
  local bundle="${staging}/${bundle_name}"
  local symbols="${bundle}/debug"
  local debug_file="${symbols}/${name}.debug"
  local archive="${dist}/${bundle_name}.tar.zst"
  local public="${dist}/${bundle_name}"
  mkdir -p "${symbols}"
  install -m 0755 "${source}" "${bundle}/${name}"

  if [[ "${platform}" == "linux" ]]; then
    "${objcopy}" --only-keep-debug "${bundle}/${name}" "${debug_file}"
    "${strip_tool}" --strip-all "${bundle}/${name}"
    "${objcopy}" --add-gnu-debuglink="${debug_file}" "${bundle}/${name}"
  else
    # dsymutil cannot recover line tables from lld's temporary ThinLTO object after the link. Keep
    # the complete linker-signed executable as the symbol file; it retains the local symbol table
    # removed from the downloadable copy and can be supplied directly to LLDB or atos.
    install -m 0755 "${bundle}/${name}" "${debug_file}"
    "${strip_tool}" -S -x "${bundle}/${name}"
    "${codesign_tool}" --force --sign - "${bundle}/${name}"
  fi

  COPYFILE_DISABLE=1 tar -cf - -C "${staging}" "${bundle_name}" \
    | "${zstd_tool}" -19 -T0 --quiet --force -o "${archive}"
  install -m 0755 "${bundle}/${name}" "${public}"
  printf '%s\n' "${public}" "${archive}"
}

stage_binary xff "${lean}"
stage_binary xff_full "${full}"
