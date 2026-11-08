# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Own disposable memory fixtures and record their creation and release."""

from contextlib import contextmanager
from datetime import datetime, timezone
import json
import hashlib
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys


def write(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def disk_info(mount):
    return plistlib.loads(subprocess.check_output(['diskutil', 'info', '-plist', str(mount)]))


def verify_image(device, image):
    images = plistlib.loads(subprocess.check_output(['hdiutil', 'info', '-plist']))['images']
    found = [entry for entry in images if entry.get('image-path') == image
             and any(item.get('dev-entry') == device for item in entry.get('system-entities', []))]
    if len(found) != 1:
        raise ValueError('device does not match the newly owned RAM image')


def verify_volume(receipt):
    verify_image(receipt['device'], receipt['image'])
    info = disk_info(receipt['mount'])
    if (info.get('DeviceNode') != receipt['device'] or info.get('VolumeUUID') != receipt['uuid']
            or info.get('MountPoint') != receipt['mount']):
        raise ValueError('RAM volume ownership changed; refusing to detach')


@contextmanager
def fixture(root, existing=None):
    """Create a new fixture; never reformat an existing volume or remove another fixture."""
    root = root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    if existing is not None:
        if sys.platform != 'linux':
            raise ValueError('existing fixture reuse is Linux-only local validation')
        existing = existing.resolve(strict=True)
        filesystem = subprocess.check_output(['findmnt', '-n', '-o', 'FSTYPE', '-T', str(existing)], text=True).strip()
        if filesystem != 'tmpfs' or not existing.is_dir():
            raise ValueError('existing extended fixture must be a verified tmpfs directory')
        receipt = {'filesystem': filesystem, 'storage': 'memory', 'fixture': str(existing), 'borrowed': True}
        write(root / 'storage.json', receipt)
        try:
            yield existing, receipt
        finally:
            write(root / 'storage-release.json', dict(receipt, preserved=True,
                  released_at=datetime.now(timezone.utc).isoformat()))
        return
    if sys.platform == 'linux':
        filesystem = subprocess.check_output(['findmnt', '-n', '-o', 'FSTYPE', '-T', '/dev/shm'], text=True).strip()
        if filesystem != 'tmpfs':
            raise ValueError('extended Linux fixtures require verified tmpfs')
        # Stable paths keep repeated CI invocations comparable. mkdir refuses a
        # collision; only this context's newly created directory may be removed.
        parent = Path('/dev/shm') / ('xff-extended-' + hashlib.sha256(str(root).encode()).hexdigest()[:12])
        parent.mkdir()
        owned = parent.stat()
        receipt = {'filesystem': filesystem, 'storage': 'memory', 'parent': str(parent)}
        write(root / 'storage.json', receipt)
        try:
            yield parent / 'fixture', receipt
        finally:
            current = parent.lstat()
            if parent.is_symlink() or (current.st_dev, current.st_ino) != (owned.st_dev, owned.st_ino):
                raise ValueError('fixture ownership changed; refusing removal')
            shutil.rmtree(parent)
            write(root / 'storage-release.json', dict(receipt, released_at=datetime.now(timezone.utc).isoformat()))
        return
    if sys.platform != 'darwin':
        raise ValueError('extended fixtures support Linux and macOS')
    image = 'ram://4194304'  # 2 GiB; the full recipe uses approximately 1 GiB.
    result = subprocess.check_output(['hdiutil', 'attach', '-nomount', image], text=True).strip()
    if not re.fullmatch(r'/dev/disk[0-9]+', result):
        raise ValueError('ambiguous newly attached RAM device: ' + result)
    device = result
    verify_image(device, image)
    # Record ownership before formatting; failure cleanup still checks the RAM image.
    receipt = {'device': device, 'image': image, 'storage': 'memory', 'filesystem': 'case-sensitive HFS+',
               'created_at': datetime.now(timezone.utc).isoformat()}
    write(root / 'storage.json', receipt)
    try:
        subprocess.run(['newfs_hfs', '-s', '-v', 'XffExtended', device], check=True)
        mount = root / 'ram-volume'
        mount.mkdir()
        subprocess.run(['diskutil', 'mount', '-mountPoint', str(mount), device], check=True)
        info = disk_info(mount)
        if info.get('DeviceNode') != device or info.get('MountPoint') != str(mount) or not info.get('VolumeUUID'):
            raise ValueError('new RAM volume identity is missing')
        receipt.update(mount=str(mount), uuid=info['VolumeUUID'])
        write(root / 'storage.json', receipt)
        verify_volume(receipt)
        yield mount / 'fixture', receipt
    finally:
        if 'uuid' in receipt:
            verify_volume(receipt)
        else:
            verify_image(device, image)
        subprocess.run(['hdiutil', 'detach', device], check=True)
        images = plistlib.loads(subprocess.check_output(['hdiutil', 'info', '-plist']))['images']
        if any(any(item.get('dev-entry') == device for item in entry.get('system-entities', [])) for entry in images):
            raise ValueError('owned RAM device remains after detach')
        if 'mount' in receipt and Path(receipt['mount']).is_mount():
            raise ValueError('owned RAM mount remains after detach')
        write(root / 'storage-release.json', dict(receipt, released_at=datetime.now(timezone.utc).isoformat()))
