# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Resolve declared repository data under Bazel, or checkout data for direct tests."""

import os
from pathlib import Path


def repository_file(relative: str) -> Path:
    if "TEST_SRCDIR" in os.environ:
        return Path(os.environ["TEST_SRCDIR"], os.environ["TEST_WORKSPACE"], relative)
    return Path(__file__).resolve().parent.parent / relative
