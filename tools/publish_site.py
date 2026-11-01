#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Retry a retained-site push after branch advancement, regenerating pages before retrying."""

import argparse
from pathlib import Path
import subprocess
import sys


def git(repo, *arguments, check=True):
    result = subprocess.run(['git', '-C', str(repo), *arguments], check=False,
                            text=True, capture_output=True)
    if check and result.returncode:
        print(result.stderr, end='', file=sys.stderr)
        result.check_returncode()
    return result


def publish(repo, regenerate, *, remote='origin', branch='coverage-pages', attempts=3):
    """Preserve concurrent commits and rebuild derived pages; never force a push."""
    if attempts < 1:
        raise ValueError('publication needs at least one push attempt')
    tracking = f'refs/remotes/{remote}/{branch}'
    for attempt in range(attempts):
        base = git(repo, 'rev-parse', tracking).stdout.strip()
        result = git(repo, 'push', remote, f'HEAD:refs/heads/{branch}', check=False)
        print(result.stdout, end='')
        print(result.stderr, end='', file=sys.stderr)
        if result.returncode == 0:
            return
        if attempt + 1 == attempts:
            result.check_returncode()
        git(repo, 'fetch', '--no-tags', remote, f'+refs/heads/{branch}:{tracking}')
        updated = git(repo, 'rev-parse', tracking).stdout.strip()
        if updated == base:
            result.check_returncode()
        if git(repo, 'merge-base', '--is-ancestor', base, updated, check=False).returncode:
            raise ValueError('remote site branch history changed; refusing to replace it')
        print(f'Site branch advanced; preserving concurrent changes and rebuilding before push '
              f'{attempt + 2}/{attempts}.', flush=True)
        rebased = git(repo, 'rebase', tracking, check=False)
        print(rebased.stdout, end='')
        print(rebased.stderr, end='', file=sys.stderr)
        if rebased.returncode:
            git(repo, 'rebase', '--abort', check=False)
            rebased.check_returncode()
        regenerate()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--remote', default='origin')
    parser.add_argument('--branch', default='coverage-pages')
    parser.add_argument('--attempts', type=int, default=3)
    parser.add_argument('regenerate', nargs=argparse.REMAINDER,
                        help='trusted command that regenerates and commits pages after branch advancement')
    args = parser.parse_args(argv)
    command = args.regenerate[1:] if args.regenerate[:1] == ['--'] else args.regenerate
    if not command:
        parser.error('provide a page regeneration command after --')
    publish(args.repo, lambda: subprocess.run(command, check=True), remote=args.remote,
            branch=args.branch, attempts=args.attempts)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
