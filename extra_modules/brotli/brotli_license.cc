// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <string_view>

#include "xff/license/notice.h"

namespace xff::brotli {
namespace {

constexpr std::string_view kSection = "Brotli archive compression (@xff_brotli)";

const license::Registrar kBrotliExtra{{
    .section = kSection,
    .section_lead = true,
    .component = "xff Brotli extra (@xff_brotli)",
    .spdx = "Apache-2.0",
    .text = "Copyright M. Boerger, the MBO Works authors. Licensed under the Apache License, Version 2.0.\n\n"
            "Adds raw Brotli streams and Brotli-compressed tar archives to the archive extra.",
}};

const license::Registrar kBrotliLibrary{{
    .section = kSection,
    .component = "Brotli",
    .spdx = "MIT",
    .text = "Copyright (c) 2009, 2010, 2013-2016 by the Brotli Authors.",
}};

}  // namespace
}  // namespace xff::brotli
