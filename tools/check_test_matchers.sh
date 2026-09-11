#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Guard (pre-commit): C++ code asserts with EXPECT_THAT / ASSERT_THAT and a gmock matcher. Scalar,
# string, floating-point, tolerance, and boolean convenience macros are forbidden in both their
# EXPECT_ and ASSERT_ forms.
# Matchers compose and print far better failures: a container mismatch names the element, a
# multi-line string diffs line by line through mbo::testing::EqualsText, and a status carries
# its code and message. See STYLE_CPP.md ("Assertions: matchers only").
#
set -euo pipefail

readonly PATTERN='\b(ASSERT|EXPECT)_(TRUE|FALSE|EQ|NE|LT|LE|GT|GE|STREQ|STRNE|STRCASEEQ|STRCASENE|FLOAT_EQ|DOUBLE_EQ|NEAR)[[:space:]]*\('
readonly QUALIFIED_MATCHER_PATTERN='(::mbo::)?testing::[A-Z][A-Za-z0-9_]*\('
readonly ALLOWED_QUALIFIED_UTILITY_PATTERN='testing::(TempDir|Test|TestWithParam|Values)\('

check_status=0
for file in "$@"; do
  # Strip comments so prose ("prefer EXPECT_THAT over EXPECT_EQ") is not flagged; a stripped
  # `//` inside a string literal can only cause a miss, never a false positive.
  hits="$(sed 's|//.*||' "${file}" | grep -nE "${PATTERN}" || true)"
  if [[ -n "${hits}" ]]; then
    echo "${file}: use EXPECT_THAT / ASSERT_THAT with a matcher (IsTrue / IsFalse for booleans,"
    echo "  EqualsText for multi-line text,"
    echo "  ElementsAre / SizeIs for containers, IsOkAndHolds / StatusIs for status) - see STYLE_CPP.md:"
    while IFS= read -r hit; do
      echo "  ${file}:${hit}"
    done <<<"${hits}"
    check_status=1
  fi

  qualified_hits="$(sed 's|//.*||' "${file}" | grep -nE "${QUALIFIED_MATCHER_PATTERN}" \
    | grep -vE "${ALLOWED_QUALIFIED_UTILITY_PATTERN}" || true)"
  if [[ -n "${qualified_hits}" ]]; then
    echo "${file}: bring matchers into scope with a using declaration; do not qualify them inline"
    echo "  in EXPECT_THAT / ASSERT_THAT expressions - see STYLE_CPP.md:"
    while IFS= read -r hit; do
      echo "  ${file}:${hit}"
    done <<<"${qualified_hits}"
    check_status=1
  fi
done
exit "${check_status}"
