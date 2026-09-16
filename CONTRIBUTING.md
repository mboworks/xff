# Intro

All contributions are generally welcome as long as they fit in with the concepts and goals of this repository and as long as the [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) is being respected.

# Code Rules

All code must adhere to the [RULES.md](RULES.md) and mostly follows the [Google style](https://google.github.io/styleguide/). Where it diverges, clang-tidy rules are in effect as much as possible.

## Coverage

The production coverage job derives its ratings, HTML colours, and CI gates from
[`coverage_policy.json`](coverage_policy.json). See [`docs/coverage.md`](docs/coverage.md) before
changing thresholds or adding a category override; weakened overrides require a documented reason.

# Run pre-commit

All changes will be verified by the pre-commit rules. In order to check these before committing changes install the tool:

```sh
pre-commit install --hook-type pre-commit --hook-type pre-push
```

Once installed the verification can be triggered for all files as follows:

```sh
pre-commit run -a
```

Without the `-a` only the modified and staged files will be checked.

## Local clang-tidy scope

The normal commit and push hooks run clang-tidy on their changed C++ files. Changed headers
select the affected translation units. A single coordinator defaults to the detected CPU count minus one
(minimum one), capped by the number of selected files. This leaves one CPU available for other work. Set `CLANG_TIDY_JOBS` to a positive integer to override
that count, or leave it unset (`auto`) to use CPU detection. GitHub CI explicitly supplies its
runner CPU count; there is no fixed two-worker local limit.

When Trunk manages Git hooks, the repository's Trunk actions invoke this same pre-commit hook.
Both Trunk actions explicitly forward `CLANG_TIDY_JOBS`, so a per-command worker override reaches
the coordinator (for example `CLANG_TIDY_JOBS=8 git push`).
The push action combines changed paths from the pushed refs into one invocation. Existing Trunk
formatting and checks remain enabled; do not replace `core.hooksPath` to install competing hooks.

Generate or refresh the compile database with `./compile_commands-update.sh`, including after
adding sources or changing build configuration. Without the database or clang-tidy 22 or newer,
the local hook reports a skip; the dedicated CI job supplies both and enforces the check. A requested
source missing from an existing database fails with a refresh instruction instead of silently
omitting that file.

Check selected files, including previously committed files that failed CI:

```sh
pre-commit run clang-tidy --files xff_extras_api/mutations_test.cc xff_extras_api/fuse_backend_test.cc
```

A full scan is a separate, explicit invocation (not part of a normal commit or push):

```sh
pre-commit run clang-tidy --all-files --hook-stage manual
```

As with other hooks, `pre-commit run -a` explicitly selects all files. Use `--files` for a focused
verification; omit it to check staged changes.

Generated dependency sources needed during compile-command extraction must be explicit build
outputs in the owning extra. The PCRE2 extra exposes `chartables_source` for this purpose;
`@xff_pcre2//:chartables_test` compares it byte-for-byte with PCRE2's versioned
`pcre2_chartables.c.dist`. The compile-database preparation builds the extra's wildcard targets,
so the generated source is requested even when the compiled library is cached.

## CI build caches

Bazel's compiled-output disk cache is separate from its repository download cache.
CI restores the latest main cache for each runner OS, architecture, and build configuration.
Only main writes refreshed generations; PRs and release tags restore without saving. GCC also
uses a compiled-output cache. Release builds share the production configuration's main cache.

GitHub cache entries are immutable. Every main run therefore saves under a new run/attempt key,
with prefix restoration to reuse the previous generation. During migration, existing legacy
compiled-output caches remain a fallback until the first new generation is saved. A GitHub "cache hit" only means the
archive was restored; Bazel's final process statistics show whether build actions actually hit
that cache. Dependency and compiler-option changes can still require rebuilding.

Before upload, CI stops Bazel and evicts the largest disk-cache files until the action-cache and
content-store payload is at most 600,000,000 bytes by default. ASan and TSan use
2,600,000,000 bytes; MSan uses 4,000,000,000 bytes to retain more instrumented outputs. These are uncompressed limits; GitHub
storage usage reflects compressed uploads. Measure those uploads and total repository usage
after main refreshes the caches before increasing other configurations. This is a synchronous upload bound, not an
idle-time garbage-collection setting. Evicted outputs become normal cache misses; this bounded
cache does not promise to retain every object or executable. Size-first eviction favors retaining
many smaller compilation outputs over large linked executables; ties evict older files first.
This is a size heuristic, not action-type classification: large object files can also be evicted.
The weekly deep-fuzz workflow shares the ordinary fuzz cache and main-only refresh policy.
LLVM downloads remain uncached.
Cleanup retains ASan/TSan uploads up to 1,000,000,000 compressed bytes and MSan uploads up to
1,200,000,000 bytes; other entries keep
the existing 700,000,000-byte ceiling. The repository storage limit remains 10 GB.

After each successful main upload, the save action checks the GitHub inventory for that exact
replacement key and its compressed size. Only then does it delete strictly older main generations
for the same OS, architecture, and configuration. Missing, empty, or oversized replacements leave
the previous generations intact; concurrent newer uploads are also preserved. Inventory and
deletion failures leave cleanup to the fallback workflow. Compiler jobs need Actions write
permission for deletion; PRs and tags never invoke the save/retirement action.

The trusted cache-maintenance workflow removes closed-PR and tag-scoped caches first, then
superseded generations per configuration and ref, and oversized entries. It retains the newest
usable generation if a newer upload exceeds the ceiling. Open-PR caches from older workflows are
not removed merely because they belong to a PR; new PR runs do not write such caches.

Main's `done` job reports each measured cache's pre-trim and retained uncompressed sizes,
compressed upload size, and eviction count. It also totals all repository cache entries against
the 10 GB budget, including any overlapping generations still present. Missing uploads or job
measurements are identified rather than reported as zero. Small measurement artifacts expire
after one day and are not part of the cache-storage total.

Locally, use consistent compiler, extras, and linker options when comparing incremental builds.
An analysis-cache reset alone does not mean compilation was repeated. A personal `--disk_cache`
setting can preserve action outputs across configuration switches and checkouts; the CI cache
change does not alter local Bazel settings.
