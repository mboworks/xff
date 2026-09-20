// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <string_view>

#include "xff/license/notice.h"

namespace xff::archive {
namespace {

constexpr std::string_view kSection = "Archive (@xff_archive)";

const license::Registrar kArchiveExtra{{
    .section = kSection,
    .section_lead = true,
    .component = "xff archive extra (@xff_archive)",
    .spdx = "Apache-2.0",
    .text = "Copyright M. Boerger, the MBO Works authors. Licensed under the Apache License, Version 2.0.\n\n"
            "Provides container diving, extraction and packing, including xff's own phar reader and\n"
            "writer, by linking the libraries whose notices follow.",
}};

const license::Registrar kLibarchive{{
    .section = kSection,
    .component = "libarchive",
    .spdx = "BSD-2-Clause",
    .text = "Copyright (c) 2003-2018 libarchive contributors. All rights reserved.",
}};
const license::Registrar kExpat{{
    .section = kSection,
    .component = "Expat",
    .spdx = "MIT",
    .text = "Copyright (c) 1998-2000 Thai Open Source Software Center Ltd and Clark Cooper.\n"
            "Copyright (c) 2001-2025 Expat maintainers.",
}};
const license::Registrar kMbedDigests{{
    .section = kSection,
    .component = "Mbed TLS digest algorithms",
    .spdx = "Apache-2.0",
    .text = "Copyright The Mbed TLS Contributors. Distributed under the Apache-2.0 option.\n"
            "Used for XAR integrity checks, not signature or publisher verification.",
}};
const license::Registrar kBzip2{{
    .section = kSection,
    .component = "bzip2",
    .spdx = "bzip2-1.0.6",
    .text = "Copyright (c) 1996-2019 Julian R Seward. All rights reserved.",
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
}  // namespace xff::archive
