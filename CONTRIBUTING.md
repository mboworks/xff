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
