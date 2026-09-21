# Comparisons with other tools

The post-merge benchmark report includes task-specific comparisons with `find`, `rg`, and `fzf`.
These are informational measurements, not a speed ranking or a merge gate. Each task checks its
output against an independently generated manifest before measurement and after every timed run.
A mismatch, unexpected exit status, diagnostic, or timeout fails collection rather than publishing
an apparently successful empty result.

## Tasks and equivalence

| Task                   | Participants           | Contract                                                           |
| ---------------------- | ---------------------- | ------------------------------------------------------------------ |
| File enumeration       | xff, find, rg          | All regular files, including hidden and ignored names              |
| Basename `*.txt`       | xff, find, rg          | Case-sensitive suffix selection                                    |
| Content selection      | xff, rg                | Literal, case-sensitive `needle` and no-match `absent_marker`      |
| Discovery plus fuzzy   | xff, find piped to fzf | Case-sensitive path subsequences `itm`, `hdn`, and no-match `zzzz` |
| Precomputed-list fuzzy | fzf                    | Same candidate paths and queries; excludes discovery               |

All results are unordered multisets of NUL-delimited relative paths. Verification checks duplicates
as well as membership. Roots are traversed without following file or directory symlinks. There are
no result limits, archive containers, binary contents, or excluded hidden/ignored files. xff and rg
request one worker; find is naturally sequential. Sorting, color, pagers, rg configuration, and fzf
default options are disabled. xff requests `--no-config`; a mandatory host configuration that changes
the result causes validation to fail rather than silently changing the benchmark population.

Each shape contains 2,000 generated files plus a `.gitignore`, one file symlink and one directory
symlink. The broad fixture has one directory; the deep fixture distributes files over up to 40 nested
levels. Generated contents use a fixed ASCII pattern with different extensions and match densities.
No third-party corpus is downloaded. The exact commands, tool versions, executable hashes, host,
fixture parameters, expected counts, expected-result hashes, and raw observations are retained in
`report.json`. Relative paths keep queries and expected-result hashes independent of temporary paths.

The fuzzy queries exercise plain ASCII subsequence acceptance in xff's `fzf` model and fzf's
noninteractive `--filter` mode. They do not claim equivalent scoring, ranking, extended-query syntax,
or interactive responsiveness. The standalone list task intentionally has no xff counterpart:
xff's CLI performs discovery, whereas fzf accepts an already generated list. Its time must not be
compared directly with xff's discovery time. The end-to-end comparison includes both `find` and `fzf`.
See [fzf's own option definitions](https://github.com/junegunn/fzf/blob/master/man/man1/fzf.1) for
filtering, NUL framing, and sorting controls.

## Measurement and interpretation

Five repetitions rotate the participant order within each task. A separate correctness run precedes
those samples. Fixtures are freshly written, then reused without flushing OS caches; these are
cache-reuse measurements, not cold-storage results. Process startup is included. The report shows
elapsed time, first stdout latency, user/system CPU time, RSS, and output bytes, with median, range,
sample standard deviation, and sample count. No output means first-output latency is unavailable.
There are no automatic cross-tool or cross-scope speed ratios.

Each invocation gets a fresh measurement worker. Commands run directly without a shell, with stdout
drained through a pipe and stderr directed to a temporary file. Output validation happens after the
measured interval. For the `find`/`fzf` pipeline, both child exit codes and both CPU usages are recorded.
Single-process RSS is its high-water mark. Pipeline memory is the **sum of individual high-water
marks**, an upper bound on their simultaneous peak, and is labelled separately. It must not be
presented as a measured process-tree peak. These commands are leaf processes; arbitrary subprocess
spawning is outside this harness's accounting contract. Each worker has a 60-second deadline, and a
timeout terminates its process group.

The hosted job installs GNU findutils, ripgrep, and fzf and records their actual installed versions.
Local macOS runs identify BSD find by the OS version and executable hash. Missing tools are explicit
skips locally; the hosted job requires all tools. Changes in tool versions, build configuration,
fixture size, hardware, or OS require interpreting a new measurement contract, not claiming a
regression from unlike runs.

## Running and testing

Build xff with `--config=clang_release`. Add comparisons to a history report with:

```sh
python3 tools/benchmark_compare.py --binary=/path/to/optimized/xff \
  --report=benchmark-report.json --require-tools
```

For a small local harness check, use `--files=12 --depth=3 --repetitions=1`; this is not performance
evidence. Without `--require-tools`, unavailable competitors are reported as skips. The driver also
has a Bazel `py_binary` target, `//tools:benchmark_compare`.

```sh
bazel test //tools:benchmark_compare_test //tools:benchmark_history_test
```

Correctness tests belong to normal PR testing. The informational measurements are added to the
existing main-only benchmark workflow and its existing publication/retention system; no separate
PR benchmark workflow or additional cache generation is introduced. Historical reports without
comparisons remain readable. Release links resolve to the exact tagged commit's retained report.
