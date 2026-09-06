#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

# shellcheck disable=SC1090,SC1091,SC2154
source "${mboworks_bashtest}"

stager="${TEST_SRCDIR}/${TEST_WORKSPACE}/tools/stage_release_artifacts.sh"

make_fixture() {
  local root="$1"
  mkdir -p "${root}/bin"
  cat >"${root}/xff" <<'EOF'
#!/usr/bin/env bash
echo "lean"
EOF
  cat >"${root}/xff_full" <<'EOF'
#!/usr/bin/env bash
echo "full"
EOF
  cat >"${root}/bin/strip" <<'EOF'
#!/usr/bin/env bash
printf '\n# stripped\n' >>"${!#}"
EOF
  cat >"${root}/bin/objcopy" <<'EOF'
#!/usr/bin/env bash
if [[ "$1" == "--only-keep-debug" ]]; then
  cp "$2" "$3"
else
  printf '\n# debuglink\n' >>"${!#}"
fi
EOF
  cat >"${root}/bin/codesign" <<'EOF'
#!/usr/bin/env bash
printf '\n# signed\n' >>"${!#}"
EOF
  chmod +x "${root}/xff" "${root}/xff_full" "${root}/bin/strip" \
    "${root}/bin/objcopy" "${root}/bin/codesign"
}

test::stages_linux_binaries_and_separate_debug_files() {
  local root output listing
  root="$(test_tmpdir release-linux)"
  make_fixture "${root}"
  output="$(RELEASE_OBJCOPY="${root}/bin/objcopy" RELEASE_STRIP="${root}/bin/strip" \
    "${stager}" linux x86_64 "${root}/xff" "${root}/xff_full" "${root}/dist")"

  expect_output_contains "${root}/dist/.xff-linux-x86_64-staging/xff-linux-x86_64/xff" "${output}"
  expect_eq "lean" "$("${root}/dist/.xff-linux-x86_64-staging/xff-linux-x86_64/xff")"
  expect_eq "full" "$("${root}/dist/.xff-linux-x86_64-staging/xff-linux-x86_64/xff_full")"
  listing="$(tar -tzf "${root}/dist/xff-linux-x86_64.tar.gz")"
  expect_output_contains "xff-linux-x86_64/xff" "${listing}"
  expect_output_contains "xff-linux-x86_64/xff_full" "${listing}"
  expect_output_contains "xff-linux-x86_64/debug/xff.debug" "${listing}"
  expect_output_contains "xff-linux-x86_64/debug/xff_full.debug" "${listing}"
}

test::stages_macos_binaries_and_preserves_unstripped_copies() {
  local root listing
  root="$(test_tmpdir release-macos)"
  make_fixture "${root}"
  RELEASE_STRIP="${root}/bin/strip" RELEASE_CODESIGN="${root}/bin/codesign" \
    "${stager}" macos arm64 "${root}/xff" "${root}/xff_full" "${root}/dist" >/dev/null

  expect_eq "lean" "$("${root}/dist/.xff-macos-arm64-staging/xff-macos-arm64/xff")"
  expect_eq "full" "$("${root}/dist/.xff-macos-arm64-staging/xff-macos-arm64/xff_full")"
  listing="$(tar -tzf "${root}/dist/xff-macos-arm64.tar.gz")"
  expect_output_contains "xff-macos-arm64/xff" "${listing}"
  expect_output_contains "xff-macos-arm64/xff_full" "${listing}"
  expect_output_contains "xff-macos-arm64/debug/xff.debug" "${listing}"
  expect_output_contains "xff-macos-arm64/debug/xff_full.debug" "${listing}"
}

test::rejects_unknown_platforms() {
  local root output rc
  root="$(test_tmpdir release-platform)"
  make_fixture "${root}"
  output="$("${stager}" windows x86_64 "${root}/xff" "${root}/xff_full" "${root}/dist" 2>&1)" && rc=0 || rc=$?
  expect_eq "2" "${rc}"
  expect_output_contains "unsupported platform: windows" "${output}"
}
