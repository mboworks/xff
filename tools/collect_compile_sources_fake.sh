#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

if [[ "${1:-}" != query ]]; then
  exit 2
fi
case "${2:-}" in
  *//first/...*)
    printf '%s\n' '@@//xff/engine/run.cc' '@@third_party+//ignored.cc'
    ;;
  *//second/...*)
    printf '%s\n' '@@xff_archive+//:backend.cc'
    ;;
  *//empty/...*) ;;
  *//broken/...*)
    printf '%s\n' '@@//xff/partial.cc'
    printf '%s\n' 'controlled query failure' >&2
    exit 17
    ;;
  *) exit 2 ;;
esac
