# xff - Test and Verification Plan

> Companion to [implementation-plan.md](implementation-plan.md). Project rule:
> tests at every changed boundary; a one-off manual check is evidence for an
> investigation, not a durable regression guard.
>
> Status: implemented verification map · 2026-09-10

## Test layers

### Registries and generated documentation

- Registry tests enforce lookup/alias identity, summary shape, and the declared
  operator, binding, safety, and style classifications that drive behavior.
- Global-option tests validate spellings, aliases, values, relationships, and
  generated help coverage.
- Help backend tests cover console, Markdown, standalone HTML, and roff output,
  including stable anchors, escaping, tables, links, terminal width, dark mode,
  and accessibility structure where applicable.
- `XFF.md`, release HTML, and NOTICE content are generated from the all-extras
  binary. Drift tests rebuild them rather than trusting hand-edited output;
  separately, every rendered cookbook recipe has an executable integration case.

### Parser and values

- Parser tests cover roots, position-independent `--` globals, leading
  compatibility globals, the `--` boundary, expression precedence, primary
  arity, attached bindings, `-exec` terminators, and diagnostics.
- Style enforcement tests prove that `find` accepts the intended GNU/BSD/POSIX
  expression union while rejecting xff extensions; explicit whole-run globals
  remain available.
- Shared value parsers receive table-driven valid, boundary, overflow, and
  malformed-input coverage independently of traversal.

### Configuration and policy

- Real system INI fixtures cover independent invalid global lines, atomic named-section disablement,
  transitive references, plain section names, and the scope and uniqueness of skip-control pairs.
- Global execution and explicit-file prohibitions remain authoritative over lower-trust arming and
  permission grants, including when automatic defaults are suppressed.
- Discovery tests cover system and user locations, environment precedence,
  explicitly named `--xffrc` files, missing/unreadable files, granular config-source suppression,
  permission enforcement, and `--no-config`.
- Resolution tests cover selector order, invocation-name defaults, `_full`
  normalization, repeated values, provenance, and CLI precedence.
- Policy tests cover authoritative global prohibitions, preset-overload
  rejection, explicit-file arming, mixed safety classes, drop reasons, and
  diagnostics.
- Configuration fuzzing composes arbitrary INI and xffrc input with the policy
  gate and asserts its safety invariants.

### Filesystems, matching, and content

- VFS contract tests cover local metadata, symlinks, content ranges, errors, and
  thread-safe use by traversal workers.
- Ignore tests cover Git semantics, nested rule stacks, explicit ignore files,
  include/exclude precedence, hidden entries, VCS directories, and repository
  discovery.
- Matching packages test regex grammars and limits, fuzzy/similarity behavior,
  MIME/language databases and overlays, hashes, dates, sizes, and text/binary
  content rules.
- Optional implementations test their own format readers, malformed-input
  limits, registration, and unavailable-backend errors.

### Engine and command behavior

- Engine fixtures cover traversal, symlink modes, depth and filesystem
  boundaries, expression short-circuiting, actions, execution batching,
  reductions, archive operations, comparison, and exit status.
- Ordering tests cover every `--sort` mode, root ordering, `-depth`, read-ahead,
  `--sort=score`, and format buffering independently.
- Parallel tests cover worker-count boundaries, cancellation, deterministic
  ordered output, concurrent child limits, and race-sensitive paths.
- Comparison tests cover left-only, right-only, identical, type/content/link
  differences, binary data, selection, patch output, expression filtering,
  ignore/symlink policy, errors, and path encoding.
- CLI/bashtests cover actual process I/O, terminal behavior, child commands,
  file-producing actions, generated man/help output, configuration files, and
  final exit codes.

### Compatibility and examples

- Conformance tests run xff against the platform's real find: GNU find on Linux
  and BSD find on macOS. Dialect-specific vocabulary is platform-gated;
  intentional contradiction resolutions use xff-specific expectations.
- Every rendered cookbook recipe has a matching executable case. A help example
  cannot ship merely because its text renders correctly.
- Golden tests are reserved for stable externally visible records and use
  semantic matchers where byte-for-byte output is not the contract.

## Local validation sequence

Start narrow and widen after the focused test passes:

1. run the owning package's unit/integration target;
2. regenerate derived documentation or data when its source changes;
3. run pre-commit for every changed file;
4. run `bazel test //...` for the core;
5. add the targets from `tools/extras.py --wildcards` for the complete modular
   graph.

Use hermetic Clang for release-representative results. Run the applicable
sanitizer configuration for memory, undefined-behavior, or concurrency changes.

## Pull-request CI

The required workflow provides distinct evidence rather than treating one green
build as sufficient:

| Job                    | Evidence                                                                                                                                |
| ---------------------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| `pre-commit` / `trunk` | Repository policy, formatting, static checks, generated metadata, and workflow lint.                                                    |
| `test (ubuntu-latest)` | Complete core and extras graph under hermetic Clang/libc++, production ThinLTO, required Linux FUSE tests, and staged release binaries. |
| `test (macos-latest)`  | The same production configuration and staged binaries on macOS.                                                                         |
| `gcc`                  | Compilation compatibility with the supported GCC build.                                                                                 |
| `minimal`              | Lean core still builds after the removable extras modules are physically absent.                                                        |
| `asan`                 | AddressSanitizer plus UndefinedBehaviorSanitizer.                                                                                       |
| `tsan`                 | ThreadSanitizer over the parallel implementation.                                                                                       |
| `msan`                 | Linux MemorySanitizer with the instrumented libc++ toolchain.                                                                           |
| `coverage`             | LLVM source coverage, category thresholds, and changed-line/branch enforcement.                                                         |
| `fuzz`                 | Bounded campaigns for every discovered fuzz target plus committed-corpus replay in ordinary tests.                                      |
| `xff-md`               | The checked-in reference and NOTICE match the all-extras generator.                                                                     |
| `clang-tidy`           | Changed C/C++ translation units checked against the generated hermetic compile database.                                                |
| `Release site tests`   | Release notes, install snippets, links, and configured documentation generation.                                                        |
| `done`                 | Aggregate gate proving every required dependency completed successfully.                                                                |

Linux FUSE coverage is an explicit runner promise: those tests fail instead of
silently skipping when the mount path is unavailable. The default platform jobs
stage and execute the binaries produced by their single ThinLTO Bazel test run;
they do not perform a second Bazel build or test invocation.

## Release verification

The release workflow independently builds both explicit executable targets for
each platform, stages stripped executables and split debug information, exercises
the staged binaries, constructs `.tar.zst` archives, and produces `SHA256SUMS`.
Publication and attestations happen only after the platform artifact jobs and
release metadata checks succeed.
