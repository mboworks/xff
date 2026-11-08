# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Memory fixtures release only resources whose ownership still matches."""

import hashlib
import json
from pathlib import Path
import plistlib
import tempfile
import unittest
from unittest import mock

import benchmark_extended_storage as storage


class BenchmarkExtendedStorageTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve() / 'collection'
        self.device = '/dev/disk987'
        self.attached = False
        self.image = 'ram://4194304'
        self.uuid = 'synthetic-owned-uuid'
        self.commands = []

    def output(self, command, **kwargs):
        self.commands.append(command)
        if command[:2] == ['hdiutil', 'attach']:
            self.attached = True
            return self.device + '\n'
        if command == ['hdiutil', 'info', '-plist']:
            images = [{'image-path': self.image, 'system-entities': [{'dev-entry': self.device}]}] if self.attached else []
            return plistlib.dumps({'images': images})
        if command[:3] == ['diskutil', 'info', '-plist']:
            return plistlib.dumps({'DeviceNode': self.device, 'MountPoint': str(self.root / 'ram-volume'),
                                   'VolumeUUID': self.uuid})
        raise AssertionError(command)

    def run_command(self, command, **kwargs):
        self.commands.append(command)
        if command == ['hdiutil', 'detach', self.device]:
            self.attached = False
        return mock.Mock(returncode=0)

    def mac_mocks(self):
        return (mock.patch.object(storage.sys, 'platform', 'darwin'),
                mock.patch.object(storage.subprocess, 'check_output', side_effect=self.output),
                mock.patch.object(storage.subprocess, 'run', side_effect=self.run_command))

    def test_mac_formats_only_the_new_owned_ram_image_and_records_verified_release(self):
        platform, output, run = self.mac_mocks()
        with platform, output, run:
            with storage.fixture(self.root) as (fixture, receipt):
                self.assertEqual(fixture, self.root / 'ram-volume/fixture')
                self.assertEqual(receipt['uuid'], self.uuid)
                self.assertTrue(self.attached)
            self.assertFalse(self.attached)
        commands = self.commands
        self.assertLess(commands.index(['hdiutil', 'info', '-plist']),
                        commands.index(['newfs_hfs', '-s', '-v', 'XffExtended', self.device]))
        self.assertEqual(json.loads((self.root / 'storage-release.json').read_text())['uuid'], self.uuid)

    def test_mac_body_failure_still_releases_owned_device(self):
        platform, output, run = self.mac_mocks()
        with platform, output, run, self.assertRaisesRegex(RuntimeError, 'measurement failed'):
            with storage.fixture(self.root):
                raise RuntimeError('measurement failed')
        self.assertFalse(self.attached)
        self.assertTrue((self.root / 'storage-release.json').is_file())

    def test_mac_changed_volume_identity_refuses_detach(self):
        platform, output, run = self.mac_mocks()
        with platform, output, run, self.assertRaisesRegex(ValueError, 'ownership changed'):
            with storage.fixture(self.root):
                self.uuid = 'someone-else'
        self.assertNotIn(['hdiutil', 'detach', self.device], self.commands)
        self.assertFalse((self.root / 'storage-release.json').exists())

    def test_mac_non_ram_device_is_never_formatted_or_detached(self):
        platform, output, run = self.mac_mocks()
        self.image = '/someone/elses/disk.dmg'
        with platform, output, run, self.assertRaisesRegex(ValueError, 'newly owned RAM image'):
            with storage.fixture(self.root):
                self.fail('must not yield an unowned device')
        self.assertFalse(any(command[0] == 'newfs_hfs' for command in self.commands))
        self.assertNotIn(['hdiutil', 'detach', self.device], self.commands)

    def test_linux_collision_preserves_existing_directory(self):
        shm = Path(self.temporary.name) / 'shm'
        shm.mkdir()
        parent = shm / ('xff-extended-' + hashlib.sha256(str(self.root).encode()).hexdigest()[:12])
        parent.mkdir()
        (parent / 'foreign').write_text('preserve')
        path = lambda value: shm if value == '/dev/shm' else Path(value)
        with mock.patch.object(storage.sys, 'platform', 'linux'), mock.patch.object(storage, 'Path', side_effect=path), \
                mock.patch.object(storage.subprocess, 'check_output', return_value='tmpfs\n'), \
                self.assertRaises(FileExistsError):
            with storage.fixture(self.root):
                self.fail('must not adopt an existing directory')
        self.assertEqual((parent / 'foreign').read_text(), 'preserve')

    def test_linux_new_fixture_cleanup_and_borrowed_fixture_preservation_on_error(self):
        shm = Path(self.temporary.name) / 'shm'
        shm.mkdir()
        path = lambda value: shm if value == '/dev/shm' else Path(value)
        with mock.patch.object(storage.sys, 'platform', 'linux'), mock.patch.object(storage, 'Path', side_effect=path), \
                mock.patch.object(storage.subprocess, 'check_output', return_value='tmpfs\n'):
            with self.assertRaisesRegex(RuntimeError, 'failed'):
                with storage.fixture(self.root) as (fixture, _):
                    fixture.mkdir()
                    (fixture / 'owned').write_text('owned')
                    raise RuntimeError('failed')
            self.assertFalse(fixture.parent.exists())
            borrowed = shm / 'borrowed'
            borrowed.mkdir()
            (borrowed / 'preserve').write_text('shared fixture')
            with self.assertRaisesRegex(RuntimeError, 'failed'):
                with storage.fixture(self.root, borrowed):
                    raise RuntimeError('failed')
            self.assertEqual((borrowed / 'preserve').read_text(), 'shared fixture')
            self.assertTrue(json.loads((self.root / 'storage-release.json').read_text())['preserved'])

    def test_disk_filesystems_cannot_be_used_as_memory_fixtures(self):
        self.root.mkdir()
        with mock.patch.object(storage.sys, 'platform', 'linux'), \
                mock.patch.object(storage.subprocess, 'check_output', return_value='ext4\n'), \
                self.assertRaisesRegex(ValueError, 'verified tmpfs'):
            with storage.fixture(self.root, self.root):
                self.fail('must not measure disk fixtures')


if __name__ == '__main__':
    unittest.main()
