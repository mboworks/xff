// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "xff/license/license.h"

#include <string>

#include "absl/strings/str_cat.h"

namespace xff::license {
namespace {

// The always-linked core dependencies. Kept in this TU (the one that defines NoticeText) so the
// linker never drops them: NoticeText is referenced, so this TU -- and its registrars -- are pulled
// in. A build-extra registers from its own TU instead, so it appears exactly when it is linked.
const Registrar kAbseil{
    {.component = "Abseil (C++)",
     .spdx = "Apache-2.0",
     .text = "Copyright The Abseil Authors. Licensed under the Apache License, Version 2.0."}};
const Registrar kRe2{
    {.component = "RE2",
     .spdx = "BSD-3-Clause",
     .text = "Copyright (c) 2009 The RE2 Authors. Redistribution permitted under the BSD-3-Clause license."}};
const Registrar kUtf8proc{
    {.component = "utf8proc",
     .spdx = "LicenseRef-utf8proc",
     .text = "Copyright (c) 2014-2021 Steven G. Johnson, Jiahao Chen, Tony Kelman, Jonas Fonseca, and contributors. "
             "Copyright (c) 2009, 2013 Public Software Group e. V. Unicode data: Copyright (c) 1991-2007 Unicode, Inc. "
             "MIT library license and Unicode data terms; see the complete bundled upstream license."}};
const Registrar kMbo{
    {.component = "mboworks/mbo",
     .spdx = "Apache-2.0",
     .text = "Copyright MBO Works. Licensed under the Apache License, Version 2.0."}};
const Registrar kNlohmannJson{
    {.component = "JSON for Modern C++ (nlohmann/json)",
     .spdx = "MIT",
     .text = "Copyright (c) 2013-2025 Niels Lohmann"}};

// xff's own license file IS the Apache-2.0 text, so the same bytes answer for every Apache-2.0
// component above. Registered here rather than in the generated TU so the generated file stays a
// single verbatim string and nothing else.
const LicenseBodyRegistrar kApache2{{.spdx = "Apache-2.0", .text = LicenseText()}};

}  // namespace

std::string_view CopyrightNotice() {
  return "xff - eXtended File Find\n"
         "Copyright M. Boerger, the MBO Works authors\n"
         "Licensed under the Apache License, Version 2.0.\n";
}

std::string_view NoticeIntroduction() {
  return "The main program and each linked build extension list their components below; all are under\n"
         "permissive licenses (no copyleft). The notice-retention obligation is met by reproducing each\n"
         "name, SPDX license identifier, and copyright line.\n";
}

std::string NoticeText() {
  std::string out = absl::StrCat(CopyrightNotice(), "\n", NoticeIntroduction());
  std::string_view section;
  for (const Notice& notice : Notices()) {
    if (notice.section != section) {
      section = notice.section;
      absl::StrAppend(&out, "\n--- Build extension: ", section, " ---\n");
    }
    absl::StrAppend(&out, "\n", notice.component, "  [", notice.spdx, "]\n  ", notice.text, "\n");
  }
  return out;
}

}  // namespace xff::license
