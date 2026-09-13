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

# Generate `compile_commands.json` by extracting the compile commands in the exact
# configuration we develop in: `--config=clang-tidy` (see .bazelrc), which layers
# the hermetic LLVM/clang toolchain (`--config=clang`) and an explicit libc++ on
# top. Because the extractor's internal `aquery` runs in that same config, every
# recorded command carries the standard library, include paths and feature macros
# the `--config=clang` builds use - so clang-tidy / clangd parse exactly what the
# real build does, not whatever toolchain bazel would otherwise autodetect (which
# differs per platform: libc++ on macOS but the system libstdc++ on Linux, the mix
# that silently corrupted clang-tidy's type/member analysis).
#
# The outer `bazel run` and the extractor's internal `aquery` both use
# `--config=clang-tidy`. The outer copy avoids rebuilding the extractor with the default toolchain
# immediately after the clang-tidy probe builds; it does not propagate into the internal query, so
# the runtime copy must remain after `--`. The tool forwards every non-`--bcce-*` runtime argument
# to that query.

set -euo pipefail

function die() {
  echo "ERROR: ${*}" 1>&2
  exit 1
}

OUTPUT_BASE="$(bazel info output_base 2>/dev/null || true)"
[ -n "${OUTPUT_BASE}" ] || die "'bazel info output_base' failed; is bazel on PATH?"

# The hermetic LLVM install (headers + libs) lives in the `_llvm_llvm` repo; the
# plain `_llvm` repo only re-exports clang-format/clang-tidy/clangd.
declare -a CLANG_LOCS=(
  "${OUTPUT_BASE}/external/toolchains_llvm++llvm+llvm_toolchain_llvm_llvm/bin/clang"
  "${OUTPUT_BASE}/external/toolchains_llvm~~llvm~llvm_toolchain_llvm_llvm/bin/clang"
  "${OUTPUT_BASE}/external/llvm_toolchain_llvm/bin/clang"
)

CLANG=""
function resolve_clang() {
  for LOC in "${CLANG_LOCS[@]}"; do
    if [ -x "${LOC}" ]; then
      CLANG="${LOC}"
      return
    fi
  done
}

resolve_clang
if [ -z "${CLANG}" ]; then
  # A fresh checkout (or CI runner) has not materialized the toolchain yet: it is
  # only fetched once something is actually built with `--config=clang`. The header
  # build below (also `--config=clang-tidy`) triggers that, so just build first.
  echo "Hermetic clang not present; fetching the toolchain via a probe build ..." 1>&2
  bazel build --config=clang-tidy //tools:show_compiler >/dev/null \
    || die "probe build '//tools:show_compiler --config=clang-tidy' failed; cannot fetch the LLVM toolchain"
  resolve_clang
fi
[ -n "${CLANG}" ] || die "Cannot find the hermetic clang even after a '--config=clang-tidy' build"

# Sources reachable both normally and through a build-machine tool are compiled
# twice (target + exec configuration), and both commands would be emitted. Keep
# only the target-configuration one: clang-tidy works per entry, so the exec copy
# is duplicate linting for a near-identical result. Files compiled ONLY in the
# exec configuration keep their command, so nothing leaves the compile DB.
declare -a BCCE_ARGS=(
  "--bcce-prefer-target-config"
  # External actions cannot include workspace headers. Avoid synthetically
  # preprocessing every third-party source merely to rediscover its external
  # headers: configured projects such as XZ intentionally reject that mode.
  # Their source commands remain in the database and are still lintable.
  "--bcce-exclude-headers=external"
)

# Name the hermetic clang BINARY (not the toolchain's `cc_wrapper.sh`, which is
# what the extracted command line starts with). clang-tidy runs its own clang to
# parse, but reads this leading argument to derive the driver's target triple and
# resource directory (its built-in headers). A shell-script "compiler" leaves it
# with the wrong builtins, so the macOS SDK / libc++ headers fail to parse. Use the
# language-neutral `clang` driver rather than `clang++`: source extensions and the
# extracted `-std` flags still select the language, while third-party C translation
# units remain C instead of being forced through the C++ driver. All OTHER flags
# still come from `--config=clang-tidy`; only the compiler token is substituted.
BCCE_ARGS+=("--bcce-compiler=${CLANG}")

# The hermetic clang carries its own libc++ but no system C headers: without the
# SDK sysroot its <locale> support headers fail on `'time.h' file not found`. The
# bazel `--config=clang` toolchain supplies this itself, but the extracted commands
# do not carry it, so make it explicit here. Linux needs no such flag.
if [ "$(uname -s)" = "Darwin" ]; then
  SDKROOT_PATH="$(xcrun --show-sdk-path 2>/dev/null || true)"
  [ -n "${SDKROOT_PATH}" ] || die "'xcrun --show-sdk-path' failed; install the Xcode command line tools"
  BCCE_ARGS+=("--bcce-copt=-isysroot${SDKROOT_PATH}")
fi

# Materialize the generated / virtual-include headers the recorded commands will
# reference (e.g. xff/license/notice.h, xff/matching/regex/backend.h from the local
# @xff_extras_api module, served through bazel-out `_virtual_includes` symlink
# forests that exist only once their cc_library is built). The aquery records those
# include paths but does not build them, so a fresh checkout / CI runner has the
# paths but not the files and clang-tidy aborts with `'xff/.../foo.h' file not
# found`. Build them in the SAME config the aquery runs in (`--config=clang-tidy`),
# so the forests land exactly where the DB points; a fresh checkout / CI runner also
# fetches the hermetic toolchain here.
echo "Building generated / virtual-include headers in --config=clang-tidy so the compile DB resolves ..." 1>&2
bazel build --config=clang-tidy //... >/dev/null \
  || die "'bazel build --config=clang-tidy //...' failed; cannot materialize the headers the compile DB references"

# Wildcards omit manual targets, but the compile database explicitly includes every manual target
# tagged `clang-tidy` (enforced by the pre-commit hook). Build that same list so external and generated
# headers exist before clang-tidy consumes their commands.
declare -a CLANG_TIDY_MANUAL_TARGETS=()
while IFS= read -r TARGET; do
  CLANG_TIDY_MANUAL_TARGETS+=("${TARGET}")
done < <(sed -n 's|^ *"\(//[^"]*\)",$|\1|p' bazelmod/clang_tidy_targets.bzl)
if [ "${#CLANG_TIDY_MANUAL_TARGETS[@]}" -gt 0 ]; then
  bazel build --config=clang-tidy "${CLANG_TIDY_MANUAL_TARGETS[@]}" >/dev/null \
    || die "building manual clang-tidy targets failed"
fi

# The composable extras are separate modules, so `//...` does not reach them: without this build
# their virtual-include forests are missing exactly as the core's would be, and clang-tidy cannot
# resolve an extra's own header. The list is DERIVED from bazelmod/extras.MODULE.bazel
# (tools/extras.py), the same
# source //:refresh_compile_commands uses - a hardcoded list here is how @xff_fuse shipped with a
# compile DB whose headers did not exist (PR #534's CI). No --//xff:xff_<extra> flag: that gates
# whether the CORE links an extra, not whether the extra itself builds (see //BUILD.bazel).
# These wildcards also request explicit generated-source targets, such as PCRE2's
# chartables_source. Keeping those as top-level outputs materializes them even when
# compiled library outputs come from cache; their diff tests guard the upstream copies.
for EXTRA_TARGETS in $(tools/extras.py --wildcards); do
  echo "Building ${EXTRA_TARGETS} so the compile DB resolves its headers ..." 1>&2
  bazel build --config=clang-tidy "${EXTRA_TARGETS}" >/dev/null \
    || die "'bazel build --config=clang-tidy ${EXTRA_TARGETS}' failed"
done

# //:refresh_compile_commands, not the extractor's stock :refresh_all - the latter covers `//...`
# only, which silently omits every extras source file. See the target's comment in //BUILD.bazel.
#
bazel run --config=clang-tidy //:refresh_compile_commands -- --config=clang-tidy "${BCCE_ARGS[@]}" \
  || die "refreshing compile_commands.json failed"

# Post-process the extracted database (tools/fix_compile_commands.py documents both passes):
# the composable extras are separate bazel modules, so their sources are recorded under the execroot
# spelling `external/xff_archive+/...` while every tool asks about `extra_modules/archive/...` - a
# lookup is by path, so those entries were unreachable and clang-tidy could not resolve an extra's own
# header. On macOS it also drops the SDK libc++ `-nostdinc++ -cxx-isystem` pair, which mismatches the
# hermetic clang++ that clang-tidy parses with.
# The set every first-party source must be in. clang-tidy lints only what the database LISTS, so a
# source bazel compiles but the extractor missed is silently unlinted - which already happened once to
# every extras translation unit (see //:refresh_compile_commands) and looked like a clean run. The
# query is the authority on "compiled", not a find over the tree: a file in no cc target is a
# different problem. The comparison goes through the same source-path remap as the rewrite pass, or the
# extras all look missing (their labels say `external/xff_archive+/...`, the database says
# `extra_modules/archive/...`).
EXPECTED_SOURCES="$(mktemp)"
trap 'rm -f "${EXPECTED_SOURCES}"' EXIT
for pattern in "//xff/..." $(python3 tools/extras.py --wildcards) "@xff_extras_api//..."; do
  bazel query "filter(\"\\.cc$\", kind(\"source file\", deps(kind(\"cc_.* rule\", ${pattern}))))" 2>/dev/null \
    | grep -E '^@@(//|xff_)' >>"${EXPECTED_SOURCES}" || true
done

python3 tools/fix_compile_commands.py compile_commands.json MODULE.bazel bazelmod/extras.MODULE.bazel \
  --system="$(uname -s)" \
  --expect-sources="${EXPECTED_SOURCES}" \
  || die "post-processing compile_commands.json failed"

echo "OK"
