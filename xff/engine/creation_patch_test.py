# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Apply diff patches and verify direction, transformed paths, bytes, links and modes."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from python.runfiles import runfiles


class CreationPatchTest(unittest.TestCase):
    def test_git_apply_round_trip(self):
        binary = runfiles.Create().Rlocation(EXECUTABLE)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'source'
            source.mkdir()
            entries = {'empty': b'', 'text': b'first\nsecond\n', 'no-newline': b'last',
                       'crlf': b'a\r\nb\r\n', 'exec': b'#!/bin/sh\nexit 0\n',
                       'binary': bytes(range(256)) * 300,
                       'space \t\n"\\name': b'odd name\n', 'sub/file': b'nested\n'}
            for name, data in entries.items():
                path = source / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            (source / 'exec').chmod(0o755)
            (source / 'link').symlink_to('text')
            env = dict(os.environ, XFF_TEST_SYSTEM_CONFIG=str(root / 'absent-system'),
                       XFF_TEST_USER_CONFIG=str(root / 'absent-user'))
            outputs = []
            for suffix in ([], ['/dev/null']):
                result = subprocess.run([binary, '--no-config', str(source), '-diff', *suffix],
                                        capture_output=True, env=env, check=False)
                self.assertEqual(result.returncode, 0, result.stderr)
                outputs.append(result.stdout)
            self.assertEqual(*outputs)
            self.assertIn(b'GIT binary patch', outputs[0])
            target = root / 'target'
            target.mkdir()
            applied = subprocess.run(['git', 'apply', '--whitespace=nowarn', '-'], input=outputs[0],
                                     cwd=target, capture_output=True, check=False)
            self.assertEqual(applied.returncode, 0, applied.stderr)
            for name, data in entries.items():
                self.assertEqual((target / name).read_bytes(), data, name)
            self.assertEqual(os.readlink(target / 'link'), 'text')
            self.assertEqual((target / 'exec').stat().st_mode & 0o111, 0o111)
            self.assertEqual((target / 'text').stat().st_mode & 0o111, 0)
            for suffix in ([], ['/dev/null']):
                removed = subprocess.run([binary, '--no-config', str(source), '-rdiff', *suffix],
                                         capture_output=True, env=env, check=False)
                self.assertEqual(removed.returncode, 0, removed.stderr)
                self.assertIn(b'deleted file mode', removed.stdout)
                # Check forward deletion and reverse restoration, including binary payloads.
                applied = subprocess.run(['git', 'apply', '--whitespace=nowarn', '-'],
                                         input=removed.stdout, cwd=target,
                                         capture_output=True, check=False)
                self.assertEqual(applied.returncode, 0, applied.stderr)
                self.assertEqual([p for p in target.rglob('*') if not p.is_dir()], [])
                restored = subprocess.run(['git', 'apply', '--reverse', '--whitespace=nowarn', '-'],
                                          input=removed.stdout, cwd=target,
                                          capture_output=True, check=False)
                self.assertEqual(restored.returncode, 0, restored.stderr)
                for name, data in entries.items():
                    self.assertEqual((target / name).read_bytes(), data, name)
                self.assertEqual(os.readlink(target / 'link'), 'text')
                self.assertEqual((target / 'exec').stat().st_mode & 0o111, 0o111)
            # A file operand uses its basename; optional operands do not swallow globals/operators.
            result = subprocess.run([binary, str(source / 'empty'), '-diff', '--safe', '-o', '-false'],
                                    capture_output=True, env=env, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(b'"b/empty"', result.stdout)
            silent = subprocess.run([binary, str(source), '-diff:none'],
                                    capture_output=True, env=env, check=False)
            self.assertEqual(silent.returncode, 0, silent.stderr)
            self.assertEqual(silent.stdout, b'')

    def run_xff(self, *args):
        binary = runfiles.Create().Rlocation(EXECUTABLE)
        return subprocess.run([binary, '--no-config', *map(str, args)],
                              capture_output=True, check=False)

    def apply_patch(self, directory, data, strip=1):
        applied = subprocess.run(['git', 'apply', f'-p{strip}', '--whitespace=nowarn', '-'],
                                 input=data, cwd=directory, capture_output=True, check=False)
        self.assertEqual(applied.returncode, 0, applied.stderr)

    def test_explicit_target_direction_and_file_comparison(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            old = root / 'old.txt'
            new = root / 'new.txt'
            old.write_bytes(b'old\n')
            new.write_bytes(b'new\n')
            # Both action sides use the found file's relative name, not TARGET's location.
            forward = self.run_xff(new, '-diff', old)
            reverse = self.run_xff(new, '-rdiff', old)
            for result in (forward, reverse):
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(b'--- a/new.txt', result.stdout)
                self.assertIn(b'+++ b/new.txt', result.stdout)
            self.assertIn(b'-old\n+new\n', forward.stdout)
            self.assertIn(b'-new\n+old\n', reverse.stdout)
            target = root / 'apply'
            target.mkdir()
            (target / 'new.txt').write_bytes(old.read_bytes())
            self.apply_patch(target, forward.stdout)
            self.assertEqual((target / 'new.txt').read_bytes(), new.read_bytes())
            self.apply_patch(target, reverse.stdout)
            self.assertEqual((target / 'new.txt').read_bytes(), old.read_bytes())
            compared = self.run_xff('--compare=diff', old, new)
            self.assertEqual(compared.returncode, 0, compared.stderr)
            self.assertIn(b'--- a/old.txt', compared.stdout)
            self.assertIn(b'+++ b/new.txt', compared.stdout)
            self.assertIn(b'-old\n+new\n', compared.stdout)
            same = self.run_xff(new, '-rdiff', new, '-print')
            self.assertEqual(same.returncode, 0, same.stderr)
            self.assertIn(str(new).encode(), same.stdout)
            for primary in ('-diff:none', '-rdiff:none'):
                silent = self.run_xff(new, primary, old)
                self.assertEqual(silent.returncode, 0, silent.stderr)
                self.assertEqual(silent.stdout, b'')

    def test_mapped_creation_deletion_and_comparison_apply(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            old = root / 'old'
            new = root / 'new'
            odd_name = 'space ' + chr(9) + chr(10) + chr(34) + chr(92) + 'file.in'
            for tree, content in ((old, b'old\n'), (new, b'new\n')):
                (tree / 'vendor/src').mkdir(parents=True)
                (tree / 'vendor/src/file.in').write_bytes(content)
                (tree / 'vendor/src' / odd_name).write_bytes(content)
            flags = ['--diff-left-prefix=', '--diff-right-prefix=',
                     '--diff-left-strip=1', '--diff-right-strip=1',
                     '--diff-left-root=placeholder/', '--diff-right-root=placeholder',
                     '--diff-left-rewrite=s|^placeholder/|project/|;s|[.]in$||',
                     '--diff-right-rewrite=s|^placeholder/|project/|',
                     '--diff-right-rewrite=s|[.]in$||']
            target = root / 'apply'
            target.mkdir()
            created = self.run_xff(new, '-diff', *flags)
            self.assertEqual(created.returncode, 0, created.stderr)
            self.assertIn(b'+++ "project/src/file"', created.stdout)
            self.apply_patch(target, created.stdout, strip=0)
            output = target / 'project/src/file'
            odd_output = target / 'project/src' / odd_name[:-3]
            self.assertEqual(output.read_bytes(), b'new\n')
            self.assertEqual(odd_output.read_bytes(), b'new\n')
            removed = self.run_xff(new, '-rdiff', *flags)
            self.assertEqual(removed.returncode, 0, removed.stderr)
            self.assertIn(b'--- "project/src/file"\n+++ /dev/null', removed.stdout)
            self.apply_patch(target, removed.stdout, strip=0)
            self.assertFalse(output.exists())
            self.assertFalse(odd_output.exists())
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(b'old\n')
            odd_output.write_bytes(b'old\n')
            compared = self.run_xff('--compare=diff', old, new, *flags)
            self.assertEqual(compared.returncode, 0, compared.stderr)
            self.assertIn(b'--- project/src/file\n+++ project/src/file', compared.stdout)
            self.apply_patch(target, compared.stdout, strip=0)
            self.assertEqual(output.read_bytes(), b'new\n')
            self.assertEqual(odd_output.read_bytes(), b'new\n')
            # TARGET templates select physical data; mappings act only on patch paths.
            mirror = self.run_xff(new, '-type', 'f', '-diff', f'{old}/{{relpath}}', *flags)
            self.assertEqual(mirror.returncode, 0, mirror.stderr)
            self.assertIn(b'--- project/src/file\n+++ project/src/file', mirror.stdout)
            self.assertIn(b'-old\n+new\n', mirror.stdout)

    def test_invalid_rules_before_empty_walk_and_runtime_path_errors(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            empty = root / 'empty'
            empty.mkdir()
            for option in ('--diff-left-strip=-1', '--diff-right-rewrite=s|[|x|',
                           r'--diff-left-rewrite=s|x|\1|', '--diff-left-root=../escape',
                           '--diff-right-prefix=/'):
                for mode in (('-diff',), ('--compare=diff', empty)):
                    result = self.run_xff(empty, *mode, '-type', 'f', option)
                    self.assertEqual(result.returncode, 2, (option, result.stderr))
                    self.assertEqual(result.stdout, b'')
            file = root / 'file'
            file.write_bytes(b'x')
            for primary in ('-diff', '-rdiff'):
                for option in ('--diff-left-strip=1', '--diff-right-rewrite=s|.*|../escape|'):
                    failed = self.run_xff(file, primary, option, '--skip-unsupported', '-o', '-print')
                    self.assertEqual(failed.returncode, 2, failed.stderr)
                    self.assertEqual(failed.stdout, b'')
                    self.assertNotIn(b'incomplete dry run', failed.stderr)
            # A field template rendering to the sentinel has absent-side semantics too.
            templated = self.run_xff(file, '--define=TARGET=/dev/null', '-rdiff', '{def.TARGET}')
            self.assertEqual(templated.returncode, 0, templated.stderr)
            self.assertIn(b'deleted file mode', templated.stdout)

    def test_output_collisions_and_independent_action_domains(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'source'
            for parent in ('one', 'two'):
                (source / parent).mkdir(parents=True)
                (source / parent / 'file').write_bytes(b'different\n')
            for primary, side in (('-diff', 'right'), ('-rdiff', 'left')):
                collided = self.run_xff(source, '-type', 'f', primary,
                                        f'--diff-{side}-strip=1')
                self.assertEqual(collided.returncode, 2, collided.stderr)
                self.assertIn(b'collision', collided.stderr)
            empty = root / 'empty'
            empty.mkdir()
            collided = self.run_xff('--compare=diff', empty, source, '-type', 'f', '--diff-right-strip=1')
            self.assertEqual(collided.returncode, 2, collided.stderr)
            self.assertIn(b'collision', collided.stderr)
            file = source / 'one/file'
            independent = self.run_xff(file, '-diff', ',', '-rdiff')
            self.assertEqual(independent.returncode, 0, independent.stderr)
            self.assertIn(b'new file mode', independent.stdout)
            self.assertIn(b'deleted file mode', independent.stdout)

    def test_diagnostics_and_context_headers_use_the_same_mapping(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            left = root / 'left'
            right = root / 'right'
            left.mkdir()
            right.mkdir()
            (left / 'file').write_bytes(b'x\0old')
            (right / 'file').write_bytes(b'x\0new')
            flags = ['--diff-left-root=old', '--diff-right-root=new',
                     '--diff-left-prefix=L/', '--diff-right-prefix=R/']
            compared = self.run_xff('--compare=diff', left, right, '-type', 'f', *flags)
            self.assertEqual(compared.returncode, 0, compared.stderr)
            self.assertIn(b'Binary files L/old/file and R/new/file differ', compared.stdout)
            for primary in ('-diff', '-rdiff'):
                result = self.run_xff(right / 'file', primary, left / 'file', *flags)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(b'Binary files L/old/file and R/new/file differ', result.stderr)
            (left / 'file').write_bytes(b'old\n')
            (right / 'file').write_bytes(b'new\n')
            context = self.run_xff(right / 'file', '-rdiff:c', left / 'file', *flags)
            self.assertEqual(context.returncode, 0, context.stderr)
            self.assertIn(b'*** L/old/file\n--- R/new/file', context.stdout)
            (left / 'link').symlink_to('old-target')
            (right / 'link').symlink_to('new-target')
            links = self.run_xff('--compare=diff', left, right, '-type', 'l', *flags)
            self.assertEqual(links.returncode, 0, links.stderr)
            self.assertIn(b'Files L/old/link and R/new/link differ', links.stdout)
            status = self.run_xff('--compare=status', left, right, '-type', 'l', *flags)
            self.assertEqual(status.returncode, 0, status.stderr)
            self.assertEqual(status.stdout, b'different\tlink\n')

    def test_normalized_equality_does_not_consume_patch_paths(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            old = root / 'old'
            new = root / 'new'
            cases = [('trail', 'same   ' + chr(10), 'same' + chr(10)),
                     ('case', 'SAME', 'same')]
            for normalization, before, after in cases:
                old.write_text(before)
                new.write_text(after)
                for primary in ('-diff', '-rdiff'):
                    result = self.run_xff(new, primary, old, f'--diff-ignore={normalization}',
                                          '--diff-right-strip=1', '-print')
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(result.stdout.strip(), str(new).encode())

    def test_config_rewrites_accumulate_and_scalar_cli_values_override(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            file = root / 'file.in'
            file.write_bytes(b'new\n')
            config = root / 'xffrc'
            config.write_text('--diff-right-root=configured --diff-right-prefix=CONFIG/ '
                              '--diff-right-rewrite="s|^cli/|project/|"\n')
            result = self.run_xff(file, '-diff', f'--xffrc={config}',
                                  '--diff-right-root=cli', '--diff-right-prefix=',
                                  '--diff-right-rewrite=s|[.]in$||')
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(b'+++ "project/file"', result.stdout)


if __name__ == '__main__':
    EXECUTABLE = sys.argv.pop(1)
    unittest.main()
