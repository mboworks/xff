#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Fail publication when Pages reports success but still serves an older benchmark index."""

import argparse
import hashlib
from pathlib import Path
import time
import urllib.error
import urllib.request


def verify(expected, url, attempts=20, delay=15):
    digest = hashlib.sha256(expected).hexdigest()
    for attempt in range(attempts):
        request = urllib.request.Request(url + '?publication=' + digest,
                                         headers={'Cache-Control': 'no-cache'})
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                if hashlib.sha256(response.read()).hexdigest() == digest:
                    print('Verified live benchmark index: ' + digest, flush=True)
                    return
        except (OSError, urllib.error.URLError) as error:
            print(str(error), flush=True)
        print(f'Waiting for benchmark publication ({attempt + 1}/{attempts})', flush=True)
        if attempt + 1 < attempts:
            time.sleep(delay)
    raise ValueError('Pages is not serving the published benchmark index')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--index', type=Path, required=True)
    parser.add_argument('--url', required=True)
    args = parser.parse_args()
    verify(args.index.read_bytes(), args.url)
