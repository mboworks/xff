#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

# Generate the Markdown and standalone HTML references as one indivisible pair.
# Callers must publish both outputs from this invocation; this keeps release HTML
# tied to the same binary and source-of-truth model as the generated XFF.md.

set -euo pipefail

if [[ "$#" -ne 3 ]]; then
  echo "usage: $0 XFF_BINARY MARKDOWN_OUTPUT HTML_OUTPUT" >&2
  exit 2
fi

binary="$1"
markdown_output="$2"
html_output="$3"
markdown_tmp=""
html_tmp=""

cleanup() {
  [[ -z "${markdown_tmp}" ]] || rm -f "${markdown_tmp}"
  [[ -z "${html_tmp}" ]] || rm -f "${html_tmp}"
}
trap cleanup EXIT

markdown_tmp="$(mktemp "${markdown_output}.tmp.XXXXXX")"
html_tmp="$(mktemp "${html_output}.tmp.XXXXXX")"
"${binary}" --help=full:markdown >"${markdown_tmp}"
"${binary}" --help=full:html >"${html_tmp}"

[[ -s "${markdown_tmp}" ]] || {
  echo "generate_reference: empty Markdown output" >&2
  exit 1
}
[[ -s "${html_tmp}" ]] || {
  echo "generate_reference: empty HTML output" >&2
  exit 1
}
[[ "$(head -n 1 "${html_tmp}")" == '<!doctype html>' ]] \
  || {
    echo "generate_reference: HTML output is not a standalone document" >&2
    exit 1
  }

mv "${markdown_tmp}" "${markdown_output}"
mv "${html_tmp}" "${html_output}"
markdown_tmp=""
html_tmp=""
