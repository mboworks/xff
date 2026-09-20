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

# Regenerate the committed full and lean build notices. Component notices self-register in the binary;
# //xff/cli:xff_notice_test fails until NOTICE.md matches this output.

set -euo pipefail

cd "$(dirname "$0")"
bazel run --config=xff_docs //xff/cli:xff_full -- --help=notice --help-format=markdown >NOTICE.md
bazel run --config=xff_docs //xff/cli:xff -- --help=notice --help-format=markdown >NOTICE.lean.md
echo "Wrote NOTICE.md and NOTICE.lean.md"
