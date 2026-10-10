#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
# Native M5 Pro handoff. Usage: bash tools/native_scheduler_handoff.sh FIXTURE_PARENT
# Only creates fresh output/build/fixture directories. Never changes the caller's
# checkout, installs packages, publishes results, or edits host security settings.
set -euo pipefail

if [[ $# != 1 || ! -d $1 ]]; then
  printf '%s\n' 'Usage: bash tools/native_scheduler_handoff.sh EXISTING_FIXTURE_DIRECTORY' >&2
  exit 2
fi
if [[ $(uname -s) != Darwin ]]; then
  printf '%s\n' 'Run this handoff on the native M5 Pro.' >&2
  exit 2
fi
for task_tool in git bazel python3.13 find rg fzf sort; do
  command -v "$task_tool" >/dev/null
done
task_script_directory=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
task_repo=$(git -C "$task_script_directory" rev-parse --show-toplevel)
task_fixture_parent=$(cd "$1" && pwd)
task_output=$(mktemp -d "${TMPDIR:-/tmp}/xff-native-m5-audit.XXXXXX")
printf 'Retain all artifacts under: %s\n' "$task_output"
printf '%s\n' 'Stop other compilation, profiling and heavy workloads before collection.'
git -C "$task_repo" fetch origin audit/native-scheduler-candidate
python3.13 "$task_script_directory/native_scheduler_prepare.py" \
  --repo="$task_repo" --root="$task_output/run" 2>&1 | tee "$task_output/prepare.log"
df -k "$task_fixture_parent" >"$task_output/fixture-df.txt"
mount >"$task_output/host-mounts.txt"
# This command may not accept a directory within a volume. Retain diagnostics;
# neither successful diskutil output nor a storage label proves slow-storage behavior.
if ! diskutil info -plist "$task_fixture_parent" >"$task_output/fixture-diskutil.plist" \
  2>"$task_output/fixture-diskutil.stderr"; then
  printf '%s\n' 'diskutil could not identify that path; storage verification remains open.' >&2
fi
python3.13 "$task_script_directory/native_scheduler_collect.py" \
  --root="$task_output/run" --fixture-parent="$task_fixture_parent" \
  --storage-label="user-selected fixture directory: $task_fixture_parent" \
  2>&1 | tee "$task_output/collect.log"
printf 'Collection is terminal. Share the complete directory: %s\n' "$task_output"
printf '%s\n' 'Do not run Instruments concurrently with timing. Native sampling/allocation,'
printf '%s\n' 'boundary workloads and verified slow-storage A/B remain separate acceptance gates.'
