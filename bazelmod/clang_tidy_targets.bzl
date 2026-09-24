# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Manual targets that must still be indexed for clang-tidy.

An aquery wildcard excludes `manual` targets before a tag filter could find them. The `clang-tidy`
tag states intent, this explicit list makes the targets reachable, and the pre-commit policy check
keeps the two sets identical.
"""

CLANG_TIDY_MANUAL_TARGETS = [
    "//xff/engine:read_accounting_test",
    "//xff/matching/regex:regex_benchmark",
    "//xff/matching/similarity:near_duplicate_benchmark",
    "//xff/matching/similarity:similarity_benchmark",
    "//xff/parser:parser_benchmark",
]
