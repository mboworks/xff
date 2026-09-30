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

# Run the formatter from the pinned LLVM distribution. Bazel fetches this tool on
# demand without compiling XFF; no PATH fallback can silently select another version.
set -euo pipefail

CLANG_FORMAT="$(bazel cquery --config=clang --ui_event_filters=-info --noshow_progress \
  --output=files @llvm_toolchain_llvm//:clang-format)"
if [[ "${CLANG_FORMAT}" != /* ]]; then
  EXEC_ROOT="$(bazel info execution_root)"
  CLANG_FORMAT="${EXEC_ROOT}/${CLANG_FORMAT}"
fi
exec "${CLANG_FORMAT}" "${@}"
