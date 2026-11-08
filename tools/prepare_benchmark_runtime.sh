#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

# Ubuntu 24.04 supplies the production executable's glibc floor. Historical LLVM
# distributions additionally need ICU 70, obtained from signed Ubuntu metadata.
set -euo pipefail

runtime_root="$(realpath -m "${1:?usage: prepare_benchmark_runtime.sh RECEIPT_DIRECTORY}")"
mkdir -p "${runtime_root}"
python3 - <<'PY_OS'
import os
import platform

release = platform.freedesktop_os_release()
if release.get('ID') != 'ubuntu' or release.get('VERSION_ID') != '24.04':
    raise SystemExit('historical Linux backfill requires Ubuntu 24.04')
libc = os.confstr('CS_GNU_LIBC_VERSION')
if not libc or tuple(map(int, libc.split()[-1].split('.'))) < (2, 38):
    raise SystemExit('historical production executables require glibc >= 2.38')
PY_OS
if [[ "$(dpkg --print-architecture)" != amd64 ]]; then
  echo 'historical Linux backfill requires amd64' >&2
  exit 1
fi

sudo apt-get update
sudo apt-get install -y findutils ripgrep fzf libxml2

# Keep the older distribution's package indexes separate from system sources and
# caches. Only the explicitly downloaded ICU package is installed into the runner.
mkdir -p "${runtime_root}/apt-lists/partial"
cat >"${runtime_root}/sources.list" <<'APT_SOURCES'
deb [arch=amd64 signed-by=/usr/share/keyrings/ubuntu-archive-keyring.gpg] https://archive.ubuntu.com/ubuntu jammy main
deb [arch=amd64 signed-by=/usr/share/keyrings/ubuntu-archive-keyring.gpg] https://archive.ubuntu.com/ubuntu jammy-updates main
deb [arch=amd64 signed-by=/usr/share/keyrings/ubuntu-archive-keyring.gpg] https://security.ubuntu.com/ubuntu jammy-security main
APT_SOURCES
apt_options=(
  -o "Dir::Etc::sourcelist=${runtime_root}/sources.list"
  -o 'Dir::Etc::sourceparts=-'
  -o "Dir::State::Lists=${runtime_root}/apt-lists"
  -o 'Dir::Cache::pkgcache='
  -o 'Dir::Cache::srcpkgcache='
)
sudo apt-get "${apt_options[@]}" update
(cd "${runtime_root}" && apt-get "${apt_options[@]}" download libicu70)
packages=("${runtime_root}"/libicu70_*.deb)
if ((${#packages[@]} != 1)) || [[ ! -f "${packages[0]}" ]]; then
  echo 'expected exactly one signed-index ICU 70 package download' >&2
  exit 1
fi
sha256sum "${packages[0]}" >"${runtime_root}/package-sha256.txt"
sudo apt-get install -y "${packages[0]}"

python3 - "${runtime_root}" "$0" <<'PY_RECEIPT'
import ctypes
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

root = Path(sys.argv[1])
names = ('libicui18n.so.70', 'libicuuc.so.70', 'libicudata.so.70', 'libxml2.so.2', 'libc.so.6')
for name in names:
    ctypes.CDLL(name, mode=os.RTLD_NOW)
paths = {Path(line.split()[-1]).resolve() for line in Path('/proc/self/maps').read_text().splitlines()
         if line.split()[-1].startswith('/') and any(name in line.split()[-1] for name in names)}
receipt = {'preparer_sha256': hashlib.sha256(Path(sys.argv[2]).read_bytes()).hexdigest(),
           'os_release': platform.freedesktop_os_release(), 'glibc': os.confstr('CS_GNU_LIBC_VERSION'),
           'architecture': platform.machine(),
           'packages': subprocess.check_output(['dpkg-query', '-W', '-f=${Package}\t${Version}\t${Architecture}\n',
                                                'libicu70', 'libxml2', 'libc6'], text=True).splitlines(),
           'libraries': {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(paths)},
           'package_sha256': (root / 'package-sha256.txt').read_text().strip(),
           'sources': (root / 'sources.list').read_text().splitlines()}
(root / 'runtime-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
print(json.dumps(receipt, indent=2))
PY_RECEIPT
