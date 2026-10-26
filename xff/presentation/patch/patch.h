// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0
#ifndef XFF_PRESENTATION_PATCH_PATCH_H_
#define XFF_PRESENTATION_PATCH_PATCH_H_

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "xff/presentation/fields/fields.h"

namespace xff::patch {
// Output-only lexical mapping. Resolve validates and compiles before traversal.
struct SidePaths {
  std::size_t strip = 0;
  std::string root;
  std::string prefix;
  std::vector<fields::RewriteProgram> rewrites;

  absl::StatusOr<std::string> Map(std::string_view logical_path) const;
};

struct Paths {
  SidePaths left{.prefix = "a/"};
  SidePaths right{.prefix = "b/"};

  static absl::StatusOr<Paths> Resolve(absl::Span<const std::string> globals);
};

struct Names {
  std::string left;
  std::string right;
};

// One coordinator-owned collision domain. Reclaiming a label for the same physical
// entry is valid; distinct entries on the same side must have distinct labels.
class Destinations {
 public:
  absl::Status Claim(const Names& names, std::string_view left_source, std::string_view right_source);

 private:
  std::map<std::string, std::string> left_;
  std::map<std::string, std::string> right_;
};

// Quote a complete label for a patch header, preserving its underlying bytes.
std::string Quote(std::string_view path);
std::string HeaderName(std::string_view path);

// Render one Git creation patch. Path must be relative without traversal components.
// Mode is 100644, 100755, or 120000; content is the link target for a symlink.
absl::StatusOr<std::string> Creation(std::string_view path, std::string_view content, std::string_view mode);

// Both labels are already mapped; /dev/null is introduced only in content headers.
// reverse=true renders deletion, including swapped blob IDs and binary payloads.
absl::StatusOr<std::string> AbsentSide(
    const Names& names,
    std::string_view content,
    std::string_view mode,
    bool reverse);
}  // namespace xff::patch
#endif  // XFF_PRESENTATION_PATCH_PATCH_H_
