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

"""Golden-output tests for the `xff` binary: one committed expected-output file per case.

`xff_golden_cases` takes a single input generator (`setup`, a script that populates a fixture in
the current directory) and a `cases` dict mapping a label to `(args, golden)`: `args` is the whole
xff command line to run over the fixture (a single string, shell-split; the binary and a `--sort`
for deterministic traversal are prepended), and `golden` is its committed expected stdout. Per case
the built `xff` runs over a fresh copy of the fixture, its stdout is optionally normalized (a `sed`
pipeline for volatile bits such as a diff header timestamp), and the result is diffed against the
golden with mbo's `diff_test` (a mismatch prints the offending diff).

This is the output analogue of `mbo::testing::EqualsText` for C++: it locks the *entire* rendered
output of a command, so it reads far better than a pile of substring probes and one file shows
exactly what a command produces. The `args` string is arbitrary, so the same harness covers name
globs, type filters, regex, `-printf`, `--format=...`, `-diff`, and so on -- add a case, add a
golden.

This is the single-mode, many-cases harness. Its sibling, `//xff/golden:golden.bzl`'s `xff_golden`,
is dual-mode: it runs one `args` under both `--config=find` and `--config=xff` to pin find-vs-xff
parity. Use `xff_golden` to assert two styles agree (or diverge) on shared primaries; use
`xff_golden_cases` to lock the exact output of many arbitrary (often xff-only) commands.

Example:

    xff_golden_cases(
        name = "walk_golden",
        setup = "testdata/walk/setup.sh",
        cases = {
            "type_f":    ("a -type f", "testdata/walk/golden/type_f.txt"),
            "name_glob": ("a -type f -name '*.txt'", "testdata/walk/golden/name_glob.txt"),
        },
    )
"""

load("@mboworks_mbo//mbo/diff:diff.bzl", "diff_test")

# Populate the fixture in a temp dir, then run `xff <flags>` over it in mode-stable order, pipe the
# stdout through the optional normalizer, and capture it. Placeholders are substituted with str
# .replace (not .format) so `{relpath}` / `{name}` field templates in `args` survive verbatim; the
# `__...__` tokens avoid `@@` (Bazel canonical-repo syntax, which buildifier flags) and `{}` (field
# templates). The test entry point injects absent trusted config paths so the run is hermetic.
_CMD = (
    "tmp=$$(mktemp -d) && cp $(location __SETUP__) $$tmp/setup.sh && " +
    "bin=$$(pwd)/$(location //xff/cli/testing:xff) && " +
    "( cd $$tmp && bash setup.sh && $$bin __FLAGS__ ) __NORM__ > $@"
)

def xff_golden_cases(name, setup, cases, sort = "tree", normalize = None):
    """Golden-output tests of arbitrary `xff` commands over a shared fixture, one golden per case.

    Args:
        name: Base name; each case becomes `<name>_<label>_test` (and a `<name>_<label>_gen`
            genrule producing `<name>_<label>.actual`).
        setup: The input generator (a script that populates the fixture in the current directory).
        cases: Dict {label: (args, golden)}. `label` names the case (a target-name fragment);
            `args` is the xff command line run over the fixture (one string, shell-split -- include
            your own roots, flags, and quoting); `golden` is the committed expected-stdout file.
        sort: The `--sort` mode prepended for deterministic traversal (default "tree"). Pass an
            empty string for a case set that establishes its own ordering.
        normalize: Optional list of `sed -E` scripts applied to stdout before the diff, for volatile
            output (e.g. a `-diff` header timestamp). Each is passed as a separate `-e` (so it must
            not contain a single quote). Default: no normalization.
    """
    sort_flag = ("--sort=" + sort + " ") if sort else ""
    norm = ""
    if normalize:
        norm = "| sed -E " + " ".join(["-e '%s'" % expr for expr in normalize])
    for label, (args, golden) in cases.items():
        actual = "{}_{}.actual".format(name, label)
        cmd = (
            _CMD
                .replace("__SETUP__", setup)
                .replace("__FLAGS__", sort_flag + args)
                .replace("__NORM__", norm)
        )
        native.genrule(
            name = "{}_{}_gen".format(name, label),
            testonly = True,
            # The MSan suppression file is a source input under --config=msan so it exists at its
            # workspace-relative path in this action's sandbox: a genrule runs with the execroot as
            # its working directory, which is what MSAN_OPTIONS=suppressions= (set in .bazelrc) names.
            # Without it the instrumented `xff` this action RUNS would report the known absl
            # flag-registry false positive and fail the generation step, not a test.
            srcs = [setup] + select({
                "//xff:xff_msan_enabled": ["//tools:msan_suppressions"],
                "//conditions:default": [],
            }),
            outs = [actual],
            tools = ["//xff/cli/testing:xff"],
            cmd = cmd,
        )
        diff_test(
            name = "{}_{}_test".format(name, label),
            file_old = golden,
            file_new = actual,
        )
