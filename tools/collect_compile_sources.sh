#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

# Emit only a complete set of successful first-party source queries. A headers-only
# query can legitimately be empty, but a failed query must not publish a partial list.
set -euo pipefail

if [[ "$#" -eq 0 ]]; then
  printf '%s\n' 'usage: collect_compile_sources.sh BAZEL_PATTERN...' >&2
  exit 2
fi

query_output="$(mktemp)"
collected="$(mktemp)"
trap 'rm -f "${query_output}" "${collected}"' EXIT

for pattern in "${@}"; do
  if ! bazel query "filter(\"\\.cc$\", kind(\"source file\", deps(kind(\"cc_.* rule\", ${pattern}))))" >"${query_output}"; then
    printf '%s\n' "ERROR: source query failed for ${pattern}" >&2
    exit 1
  fi
  # Unlike grep, awk succeeds when a valid query contains no matching sources.
  # Genuine read/write errors still fail under errexit.
  awk '$0 ~ "^@@(//|xff_)" { print }' "${query_output}" >>"${collected}"
done

cat "${collected}"
