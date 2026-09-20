// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#include <string_view>

#include "xff/license/notice.h"

namespace {

constexpr std::string_view kSection = "PCRE2 (@xff_pcre2)";

const xff::license::Registrar kPcre2Extra{{
    .section = kSection,
    .section_lead = true,
    .component = "xff PCRE2 extra (@xff_pcre2)",
    .spdx = "Apache-2.0",
    .text = "Copyright M. Boerger, the MBO Works authors. Licensed under the Apache License, Version 2.0.\n\n"
            "Provides the -regextype=pcre2 backend by linking PCRE2 and SLJIT, whose notices follow.",
}};
const xff::license::Registrar kPcre2{{
    .section = kSection,
    .component = "PCRE2",
    .spdx = "BSD-3-Clause WITH PCRE2-exception",
    .text = "Copyright (c) 1997-2007 University of Cambridge; 2007-2024 Philip Hazel; "
            "2010-2024 Zoltan Herczeg; and contributors. All rights reserved.",
}};
const xff::license::Registrar kSljit{{
    .section = kSection,
    .component = "SLJIT",
    .spdx = "BSD-2-Clause",
    .text = "Copyright (c) 2009-2024 Zoltan Herczeg. All rights reserved.",
}};

}  // namespace
