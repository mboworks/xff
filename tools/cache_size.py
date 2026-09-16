#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0
"""Select closed-PR, tag, superseded, and oversized Actions caches for cleanup."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections.abc import Iterable
from pathlib import Path
from typing import Any


MAX_CACHE_BYTES = 700_000_000
SANITIZER_MAX_CACHE_BYTES = 1_000_000_000


def oversized_cache_ids(caches: Iterable[dict[str, Any]]) -> list[int]:
  """Returns IDs at the ordinary ceiling or above the sanitizer allowance."""
  result = []
  for cache in caches:
    sanitizer = re.fullmatch(
        r"bazel-actions-v2-[^-]+-[^-]+-(?:asan|tsan|msan)-[0-9a-f]{64}-\d+-\d+",
        cache.get("key", ""))
    oversized = (cache["sizeInBytes"] > SANITIZER_MAX_CACHE_BYTES if sanitizer
                 else cache["sizeInBytes"] >= MAX_CACHE_BYTES)
    if oversized:
      result.append(cache["id"])
  return result


def obsolete_cache_ids(caches: list[dict[str, Any]], closed_prs: set[int]) -> list[int]:
  """Remove closed PRs and tags first, then obsolete generations and oversized entries."""
  closed = []
  candidates = []
  for cache in caches:
    match = re.fullmatch(r"refs/pull/(\d+)/(?:merge|head)", cache["ref"])
    if ((match and int(match[1]) in closed_prs)
        or cache["ref"].startswith(("refs/tags/", "refs/heads/refs/tags/"))):
      closed.append(cache["id"])
    else:
      candidates.append(cache)
  oversized = oversized_cache_ids(candidates)
  generations = {}
  for cache in candidates:
    if cache["id"] in oversized:
      continue
    match = re.fullmatch(r"(bazel-actions-v2-.+)-[0-9a-f]{64}-\d+-\d+", cache["key"])
    if match:
      generations.setdefault((cache["ref"], match[1]), []).append(cache)
  superseded = []
  for entries in generations.values():
    entries.sort(key=lambda item: (item["createdAt"], item["id"]), reverse=True)
    superseded.extend(entry["id"] for entry in entries[1:])
  return closed + superseded + oversized


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--closed-prs", type=Path, required=True,
                      help="Paginated GitHub closed-PR JSON from gh api --paginate --slurp")
  args = parser.parse_args()
  caches = json.load(sys.stdin)
  if not isinstance(caches, list):
    raise ValueError("GitHub cache JSON must be an array")
  closed_prs = {pr["number"] for page in json.loads(args.closed_prs.read_text()) for pr in page}
  for cache_id in obsolete_cache_ids(caches, closed_prs):
    print(cache_id)
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
