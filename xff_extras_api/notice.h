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

#ifndef XFF_LICENSE_NOTICE_H_
#define XFF_LICENSE_NOTICE_H_

#include <string_view>
#include <vector>

namespace xff::license {

// One linked component's notice: the name, its SPDX license id, and the copyright / notice line to
// reproduce. A component may be xff's own extension code or a third-party library. `text` points at
// a static string literal (lives for the process). This is the SOT for the notice content; the repo
// NOTICE.md file is generated from / checked against it.
struct Notice {
  // Empty for the main binary; a stable label for a build extension otherwise. Notices sort by
  // section first, keeping an extension and the libraries it brings into the binary together.
  std::string_view section;
  // The extension's own notice sorts before its libraries inside the section.
  bool section_lead = false;
  std::string_view component;
  std::string_view spdx;
  // Blank lines separate attribution from optional per-component explanation paragraphs.
  std::string_view text;
};

// Records `notice` in the process-wide set. Called once per component from a file-scope Registrar,
// so a component is registered exactly when its translation unit is linked in (core deps always;
// each build-extra from its own TU). Static-init only; not thread-safe.
void Register(Notice notice);

// Registers on construction. Declare one at namespace scope in the component's TU:
//   const xff::license::Registrar kFooNotice{{.component = "foo", .spdx = "MIT", .text = "..."}};
struct Registrar {
  explicit Registrar(Notice notice) { Register(notice); }
};

// Every registered notice, sorted by section, section lead, then component name so the output is
// deterministic regardless of static-init order across translation units.
std::vector<Notice> Notices();

// A license BODY, verbatim, keyed by its SPDX identifier - the key is the identifier rather than
// the component because components share licenses (libarchive and lz4 are both BSD-2-Clause) and a
// license text is defined by which license it is, not by who uses it. A component's own copyright
// line stays in its Notice; this is the terms that line points at.
struct LicenseBody {
  std::string_view spdx;
  std::string_view text;
};

// Records a license body. Static-init only, like Register. Registering the same SPDX twice keeps
// the FIRST: two components naming one license are the expected case, and where the texts somehow
// differ, silently preferring the later one would be worse than being deterministic.
void RegisterLicenseBody(LicenseBody body);

// Registers on construction. Declare one at namespace scope in the TU that embeds the text:
//   const xff::license::LicenseBodyRegistrar kBsd2{{.spdx = "BSD-2-Clause", .text = kBsd2Text}};
struct LicenseBodyRegistrar {
  explicit LicenseBodyRegistrar(LicenseBody body) { RegisterLicenseBody(body); }
};

// Every registered license body, sorted by SPDX id for the same determinism reason as Notices().
std::vector<LicenseBody> LicenseBodies();

// The body for `spdx`, or empty when this binary carries no text for it. Empty is a normal answer
// rather than an error: a component may name a license whose text nothing linked in has embedded,
// and saying so plainly beats implying the license does not apply.
std::string_view LicenseBodyFor(std::string_view spdx);

}  // namespace xff::license

#endif  // XFF_LICENSE_NOTICE_H_
