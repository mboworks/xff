# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Extras linked by the full production and test CLIs."""

FULL_DEPS = select({
    "//xff:xff_apple_on": ["@xff_apple//:apple_register_cc"],
    "//conditions:default": [],
}) + select({
    "//xff:xff_archive_on": ["@xff_archive//:archive_register_cc"],
    "//conditions:default": [],
}) + select({
    "//xff:xff_asar_on": ["@xff_asar//:asar_register_cc"],
    "//conditions:default": [],
}) + select({
    "//xff:xff_brotli_on": ["@xff_brotli//:brotli_register_cc"],
    "//conditions:default": [],
}) + select({
    "//xff:xff_fuse_on": ["@xff_fuse//:fuse_register_cc"],
    "//conditions:default": [],
}) + select({
    "//xff:xff_language_db_on": ["@xff_language_db//:language_db_register_cc"],
    "//conditions:default": [],
}) + select({
    "//xff:xff_mime_db_on": ["@xff_mime_db//:mime_db_register_cc"],
    "//conditions:default": [],
}) + select({
    "//xff:xff_pcre_on": ["@xff_pcre2//:pcre2_backend_cc"],
    "//conditions:default": [],
}) + select({
    "//xff:xff_squashfs_on": ["@xff_squashfs//:squashfs_register_cc"],
    "//conditions:default": [],
})
