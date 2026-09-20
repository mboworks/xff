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

#ifndef XFF_LICENSE_LICENSE_H_
#define XFF_LICENSE_LICENSE_H_

#include <string>
#include <string_view>

#include "xff/license/notice.h"  // Notice, Register, Registrar, Notices() (the registration seam)

namespace xff::license {

// xff's copyright + license grant: the "who owns this, and under what license" statement (project,
// copyright line, Apache-2.0 grant). This is Apache 2.0's own APPENDIX ("How to apply the License to
// your work") boilerplate filled in for xff, so it is what makes the bare license text a COMPLETE
// statement of how the work is licensed. One SOT: it heads both --help=notice and --help=license, so
// the two can never state a different owner.
std::string_view CopyrightNotice();

// The prose introducing the component manifest. Shared by the plain-text notice renderer and the
// structured, width-aware help topic so the two surfaces cannot describe the manifest differently.
std::string_view NoticeIntroduction();

// xff's own license, the Apache License 2.0, verbatim (xff holds the copyright). Reproduced so a
// single-file binary is self-contained; the text is generated from the repo LICENSE file (see the
// //xff/license:license_text_gen genrule), which stays canonical. This is the generic license body
// only; CopyrightNotice() carries the copyright that completes it.
std::string_view LicenseText();

// The assembled third-party notice: an xff attribution header followed by each registered component
// (sorted). The help model also renders these notices; NOTICE.md uses its Markdown output. Full license
// bodies are registered separately by SPDX id and rendered by `--help=license=COMPONENT`.
std::string NoticeText();

}  // namespace xff::license

#endif  // XFF_LICENSE_LICENSE_H_
