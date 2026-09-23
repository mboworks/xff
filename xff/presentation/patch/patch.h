// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#ifndef XFF_PRESENTATION_PATCH_PATCH_H_
#define XFF_PRESENTATION_PATCH_PATCH_H_

#include <string>
#include <string_view>

#include "absl/status/statusor.h"

namespace xff::patch {
// Render one Git creation patch. Path must be relative without traversal components.
// Mode is 100644, 100755, or 120000; content is the link target for a symlink.
absl::StatusOr<std::string> Creation(std::string_view path, std::string_view content, std::string_view mode);
}  // namespace xff::patch
#endif  // XFF_PRESENTATION_PATCH_PATCH_H_
