#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
# Generate unsigned fixtures using macOS's production packaging tools and our own text only.
set -euo pipefail
fixture_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/test_data"
scratch="$(mktemp -d)"
trap 'rm -rf "${scratch}"' EXIT
contents="${scratch}/root/Applications/Example.app/Contents"
mkdir -p "${contents}"
printf 'portable package\n' >"${contents}/info.txt"
cat >"${contents}/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>org.mboworks.xff.fixture</string>
<key>CFBundleVersion</key><string>1.0</string>
</dict></plist>
PLIST
/usr/bin/pkgbuild --root "${scratch}/root" --identifier org.mboworks.xff.fixture \
  --version 1.0 --install-location / "${scratch}/native-component.pkg"
/usr/bin/productbuild --package "${scratch}/native-component.pkg" "${scratch}/native-product.pkg"
cp "${scratch}/native-component.pkg" "${scratch}/native-product.pkg" "${fixture_dir}/"
