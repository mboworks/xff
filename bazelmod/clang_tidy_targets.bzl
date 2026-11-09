# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Manual targets that must still be indexed for clang-tidy.

An aquery wildcard excludes `manual` targets before a tag filter could find them. The `clang-tidy`
tag states intent, this explicit list makes the targets reachable, and the pre-commit policy check
keeps the two sets identical.
"""

CLANG_TIDY_MANUAL_TARGETS = [
    "@xff_archive//:archive_openbytes_benchmark",
    "//tools:archive_directory_benchmark",
    "//tools:archive_index_benchmark",
    "//tools:archive_member_key_benchmark",
    "//tools:archive_source_benchmark",
    "//tools:read_source_benchmark",
    "//tools:time_format_benchmark",
    "//xff/cli:globals_benchmark",
    "//xff/content:line_scan_benchmark",
    "//xff/engine:read_accounting_test",
    "//xff/engine:compare_benchmark",
    "//xff/engine:executor_benchmark",
    "//xff/engine:rg_benchmark",
    "//xff/engine:match_tail_benchmark",
    "//xff/engine:expression_benchmark",
    "//xff/engine:expression_run_benchmark",
    "//xff/engine:name_listing_benchmark",
    "//xff/matching/language:default_cache_benchmark",
    "//xff/matching/regex:regex_benchmark",
    "//xff/matching/similarity:near_duplicate_benchmark",
    "//xff/matching/similarity:similarity_benchmark",
    "//xff/parser:parser_benchmark",
    "//xff/presentation/render:output_benchmark",
]
