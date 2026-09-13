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

"""Dual-mode golden tests: run xff over a fixture in find AND xff style, diff goldens.

`xff_golden` builds one bashtest that, via the shared `golden_driver.sh`:
  1. runs `setup` in a fresh temp tree (the fixture),
  2. runs the built `xff` over it once with `--config=find` and once with `--config=xff`,
     passing `args` as the expression,
  3. normalizes the output (the temp root -> `<ROOT>`; lines sorted unless `ordered`),
  4. compares each mode's output to its committed golden file.

This pins find-vs-xff behavior and catches drift a single-mode test would miss. Output
with volatile per-run fields (`-ls` inode/date) is not golden-able yet; use stable
output (`-print`/`-printf`/name listings, `--summary` counts) for now.

This is the dual-mode (two styles, one command) harness. Its sibling,
`//xff/cli:golden.bzl`'s `xff_golden_cases`, is single-mode and many-cases: a dict of
arbitrary xff command lines, each run once (default xff style) and diffed against its own
golden via mbo's `diff_test`. Use it to lock the exact output of many (often xff-only)
commands; use `xff_golden` here to assert find and xff agree (or diverge) on one command.
"""

load("@mboworks_bashtest//bashtest:bashtest.bzl", "bashtest")

def xff_golden(name, setup, args, find_golden, xff_golden, ordered = False, size = "small"):
    """A dual-mode (find + xff) golden test.

    Args:
        name:        Test target name (should end in `_test`).
        setup:       A shell script (label/file) that populates the fixture tree ($PWD).
        args:        The full xff argv; a `<ROOT>` token marks where the search root goes
                     (so globals like `--summary` can precede it). Use a real newline for a
                     -printf terminator, not `\\n` (the sh_test launcher strips the backslash).
        find_golden: Expected normalized output under `--config=find`.
        xff_golden:  Expected normalized output under `--config=xff`.
        ordered:     Keep output line order (default: sort, so readdir order is immaterial).
        size:        Test size (default "small").
    """
    bashtest(
        name = name,
        size = size,
        srcs = ["//xff/golden:golden_driver.sh"],
        args = [
            "$(rootpath {})".format(setup),
            "$(rootpath {})".format(find_golden),
            "$(rootpath {})".format(xff_golden),
            "1" if ordered else "0",
            "--",
        ] + args,
        data = [
            setup,
            find_golden,
            xff_golden,
            "//xff/cli/testing:xff",
        ],
    )
