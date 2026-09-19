// SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
// SPDX-License-Identifier: Apache-2.0

#ifndef XFF_PARSER_DIAGNOSTICS_H_
#define XFF_PARSER_DIAGNOSTICS_H_

#include <string_view>

namespace xff::parser {

// The unknown expression token, only while option parsing is active. Lets CLI
// diagnostics consult their registry without scanning argv or parsing error prose.
inline constexpr std::string_view kUnknownPredicatePayload = "xff.parser/unknown-predicate";

}  // namespace xff::parser

#endif  // XFF_PARSER_DIAGNOSTICS_H_
