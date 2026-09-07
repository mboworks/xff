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

#include "xff/cli/manpage.h"

#include <string>
#include <string_view>

#include "xff/cli/help_backend.h"
#include "xff/cli/help_build.h"
#include "xff/cli/roff_backend.h"

namespace xff::cli {

std::string ManPage(std::string_view program_name) {
  RoffBackend backend;
  Document doc = BuildReference();
  doc.name = program_name;
  RenderDocument(doc, backend);
  return backend.Take();
}

}  // namespace xff::cli
