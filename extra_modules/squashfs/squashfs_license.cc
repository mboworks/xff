// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <string_view>

#include "xff/license/notice.h"

namespace xff::squashfs {
namespace {

constexpr std::string_view kSection = "SquashFS archive format (@xff_squashfs)";

const license::Registrar kSquashfsExtra{{
    .section = kSection,
    .section_lead = true,
    .component = "xff SquashFS extra (@xff_squashfs)",
    .spdx = "Apache-2.0",
    .text = "Copyright M. Boerger, the MBO Works authors. Licensed under the Apache License, Version 2.0.\n\n"
            "Adds independent SquashFS, Snap, and AppImage container reading to xff.",
}};

const license::Registrar kLibsqsh{{
    .section = kSection,
    .component = "libsqsh",
    .spdx = "BSD-2-Clause",
    .text = "Copyright (c) 2023-2024 Enno Boland. All rights reserved.",
}};

const license::Registrar kCextras{{
    .section = kSection,
    .component = "cextras",
    .spdx = "BSD-2-Clause",
    .text = "Copyright (c) 2023 Enno Boland. All rights reserved.",
}};

const license::Registrar kZlib{{
    .section = kSection,
    .component = "zlib",
    .spdx = "Zlib",
    .text = "Copyright (c) 1995-2022 Jean-loup Gailly and Mark Adler.",
}};

const license::Registrar kLiblzma{{
    .section = kSection,
    .component = "liblzma (XZ Utils)",
    .spdx = "LicenseRef-Public-Domain",
    .text = "The liblzma source in XZ Utils 5.4.5 has been put into the public domain.",
}};

const license::Registrar kLz4{{
    .section = kSection,
    .component = "LZ4 library",
    .spdx = "BSD-2-Clause",
    .text = "Copyright (c) 2011-2020 Yann Collet. All rights reserved.",
}};

const license::Registrar kZstd{{
    .section = kSection,
    .component = "Zstandard",
    .spdx = "BSD-3-Clause",
    .text = "Copyright (c) Meta Platforms, Inc. and affiliates. All rights reserved.",
}};

}  // namespace
}  // namespace xff::squashfs
