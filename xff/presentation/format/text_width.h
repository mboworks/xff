// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_PRESENTATION_FORMAT_TEXT_WIDTH_H_
#define XFF_PRESENTATION_FORMAT_TEXT_WIDTH_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace xff::format {

// Estimated terminal columns using Unicode grapheme boundaries and character widths.
// Intended for plain text, without terminal control sequences.
std::size_t TextColumns(std::string_view text);

// Wrap without splitting an extended grapheme cluster. Width zero means unwrapped;
// a cluster wider than the requested width remains intact on its own line.
std::vector<std::string> WrapColumns(std::string_view text, std::size_t width);

}  // namespace xff::format

#endif  // XFF_PRESENTATION_FORMAT_TEXT_WIDTH_H_
