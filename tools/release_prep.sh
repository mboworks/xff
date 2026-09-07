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
# Release preparation for xff, invoked by .github/workflows/release.yml on a tag
# push. xff uses the override-on-release version model: the git tag is the
# version source of truth, the repository stays at the 0.0.0 sentinel, and this
# script stamps the tag's version into every version location at build time. It:
#
#   1. verifies the tag matches the top CHANGELOG.md heading (so a release always
#      has notes and the version is deliberate);
#   2. stamps <tag> into every in-repo MODULE.bazel version, every intra-repo
#      bazel_dep on one of our modules, and the `xff --version` literal in
#      xff/cli/main.cc;
#   3. runs tools/check_module_versions.py <tag>, so a location the stamp missed
#      fails the release loudly instead of shipping a stale version;
#   4. prints the tag's CHANGELOG.md section, installation and verification
#      instructions, and stable versioned documentation links to stdout (used
#      as the GitHub release body).
#
# Usage: tools/release_prep.sh <tag>   (v-prefixed, e.g. v1.2.3; may also come
# from GITHUB_REF_NAME). The stamped version drops the `v`.

set -euo pipefail

die() {
  echo "release_prep: ERROR: ${*}" >&2
  exit 1
}

# The tag is v-prefixed (e.g. v1.2.3), but the stamped version is always bare: a
# Bazel `module(version=)` and `xff --version` cannot carry a `v`. Strip it.
TAG="${1:-${GITHUB_REF_NAME:-}}"
[ -n "${TAG}" ] || die "no tag given (pass as an argument or set GITHUB_REF_NAME)"
VERSION="${TAG#v}"
[[ "${VERSION}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "tag '${TAG}' is not a (v)X.Y.Z version"

# Work from the repository root (this script lives in tools/).
cd "$(dirname "${BASH_SOURCE[0]}")/.."

# 1. The version must match the newest (top) CHANGELOG heading.
CHANGELOG_VERSION="$(sed -rne 's,^# ([0-9]+([.][0-9]+)+.*)$,\1,p' <CHANGELOG.md | head -n1)"
[ "${CHANGELOG_VERSION}" = "${VERSION}" ] \
  || die "version '${VERSION}' does not match the top CHANGELOG.md heading '${CHANGELOG_VERSION}'"

# 2. Stamp the 0.0.0 sentinel -> VERSION at every version location. Only our own
#    modules and intra-repo bazel_deps carry version = "0.0.0"; third-party
#    bazel_deps have real versions, so this never rewrites them.
while IFS= read -r module; do
  perl -pi -e "s/version = \"0\\.0\\.0\"/version = \"${VERSION}\"/g" "${module}"
done < <(find . -name MODULE.bazel -not -path './bazel-*' -not -path '*/external/*')
# The binary's --version literal: `std::cout << "xff 0.0.0\n";`.
perl -pi -e "s/xff 0\\.0\\.0/xff ${VERSION}/g" xff/cli/main.cc

# 3. Fail the release if any version location was missed.
python3 tools/check_module_versions.py "${VERSION}"

# 4. Emit the version's CHANGELOG section (everything under `# <version>` up to
#    the next version heading) plus the retained release resources as the release
#    body. The Pages publisher consumes this run's coverage artifact after the
#    release succeeds, so these URLs become live shortly after publication.
awk -v tag="${VERSION}" '
  $0 ~ ("^# " tag "([[:space:]]|$)") { grab = 1; next }
  grab && /^# [0-9]/ { exit }
  grab { print }
' CHANGELOG.md
cat <<'EOF'
## Install

Run either or both install blocks: the lean binary is installed as `xff`, and the all-features
binary as `xff_full`. Ensure `${HOME}/.local/bin` is in `PATH`; the generated manual pages are
installed below `${HOME}/.local/share/man`.

Release files without an archive suffix, such as `xff-linux-x86_64`, are the actual stripped
platform executables and can be downloaded and installed directly. Each matching `.tar.zst`
contains that same stripped executable together with its separate debug-information file.

### Install on Linux x86_64:

```sh
EOF
printf '%s\n' XFF_VERSION="${TAG}"
cat <<'EOF'
XFF_PATH_BIN="${HOME}/.local/bin"
XFF_PATH_MAN="${HOME}/.local/share/man/man1"
XFF_PATH_TEMP="$(mktemp -d "${TMPDIR:-/tmp}/xff-install.XXXXXX")"
trap 'rm -rf "${XFF_PATH_TEMP}"' EXIT
mkdir -p "${XFF_PATH_BIN}"
mkdir -p "${XFF_PATH_MAN}"
cd "${XFF_PATH_TEMP}"
curl -fLO "https://github.com/mboworks/xff/releases/download/${XFF_VERSION}/SHA256SUMS"

# Install xff.
curl -fLO "https://github.com/mboworks/xff/releases/download/${XFF_VERSION}/xff-linux-x86_64"
grep ' xff-linux-x86_64$' SHA256SUMS | sha256sum -c -
install -m 0755 xff-linux-x86_64 "${XFF_PATH_BIN}/xff"
"${XFF_PATH_BIN}/xff" --pager=never --man > "${XFF_PATH_MAN}/xff.1"

# Install xff_full.
curl -fLO "https://github.com/mboworks/xff/releases/download/${XFF_VERSION}/xff_full-linux-x86_64"
grep ' xff_full-linux-x86_64$' SHA256SUMS | sha256sum -c -
install -m 0755 xff_full-linux-x86_64 "${XFF_PATH_BIN}/xff_full"
"${XFF_PATH_BIN}/xff_full" --pager=never --man > "${XFF_PATH_MAN}/xff_full.1"
```

### Install on macOS:

```sh
EOF
printf '%s\n' XFF_VERSION="${TAG}"
cat <<'EOF'
XFF_PATH_BIN="${HOME}/.local/bin"
XFF_PATH_MAN="${HOME}/.local/share/man/man1"
XFF_PATH_TEMP="$(mktemp -d "${TMPDIR:-/tmp}/xff-install.XXXXXX")"
trap 'rm -rf "${XFF_PATH_TEMP}"' EXIT
mkdir -p "${XFF_PATH_BIN}"
mkdir -p "${XFF_PATH_MAN}"
cd "${XFF_PATH_TEMP}"
curl -fLO "https://github.com/mboworks/xff/releases/download/${XFF_VERSION}/SHA256SUMS"

# Install xff.
curl -fLO "https://github.com/mboworks/xff/releases/download/${XFF_VERSION}/xff-macos-arm64"
grep ' xff-macos-arm64$' SHA256SUMS | shasum -a 256 -c -
install -m 0755 xff-macos-arm64 "${XFF_PATH_BIN}/xff"
"${XFF_PATH_BIN}/xff" --pager=never --man > "${XFF_PATH_MAN}/xff.1"

# Install xff_full.
curl -fLO "https://github.com/mboworks/xff/releases/download/${XFF_VERSION}/xff_full-macos-arm64"
grep ' xff_full-macos-arm64$' SHA256SUMS | shasum -a 256 -c -
install -m 0755 xff_full-macos-arm64 "${XFF_PATH_BIN}/xff_full"
"${XFF_PATH_BIN}/xff_full" --pager=never --man > "${XFF_PATH_MAN}/xff_full.1"
```

EOF
printf '\n'
bash tools/release_notes.sh "${TAG}"
