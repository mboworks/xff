# Share collection-owned path storage (2026-11-01)

## Decision

Retain the candidate for a follow-up PR: copy the full path once, then reuse exact matching suffix/prefix views for the basename/root. Copy either field independently when its value is not contained at that boundary. This avoids a new interner, root cache, wrapper, synchronization or reference-counted text object. Both time and memory improve on useful large cases across macOS and Linux; small costs remain explicit.

The reference is merged main `16e70cb97de93c7ab4ae8c9e990e1842d50cc528`, including #1040 fixed 1 MiB blocks. This is an additional ownership optimization, not a remeasurement of arena block sizes.

## Contract and correctness

The collection arena owns the copied path and retains stable storage through append growth and moves. A reused view never borrows the walker or caller buffer. Prefix/suffix checks establish byte equality only; they do not interpret path ancestry or change filtering. Name and root fall back to owned copies for arbitrary nonmatching inputs. Logical row/byte budget accounting is unchanged even when fields share physical storage.

Both variants pass release `collect_test` and `run_test` on both platforms. Existing cases cover caller destruction, empty/length-delimited values, budgets, moves, summaries and engine integration. Add an explicit distinct-name/root lifetime case. Extend the existing growth test from 150-byte to 600-byte paths so its two copies of 1,025 paths actually exceed the new 1 MiB block size; otherwise the historical arena-growth assertion no longer exercises a second block.

## Measurement contract

Mac: local Apple M5 Pro, warm APFS, no CPU affinity. Linux: drogon Ryzen 9950X KVM guest, tmpfs, affinity restricted per requested allowance. Both build `--config=clang_release` with LLVM 23 and ThinLTO. All builds/tests on a host finish before its timing begins.

Native matrix: 26 cases, three sessions, discarded warmup per case/variant/session, nine interleaved measurements retaining the fastest seven. Cases include empty, 10/1,000/10,000/100,000 files, short/long paths, one/three workers, one/four named collections, and no-collection controls. Every native invocation verifies count/size output independently. Both matrices total 3,120 successful invocations including warmups.

Kernel matrix: 12 cases, three per-case shuffled sessions, nine repetitions retaining seven real-time samples; 10/1,000/100,000 entries, root lengths 1/128, one/four independent collection threads. Fixture setup is outside timing; construction, insertion, enumeration and destruction are inside. Kernel checksums prevent dead-code removal but are not independent per-iteration output oracles.

Percentages are median paired-session elapsed changes; negative is faster. Absolute times and RSS are summarized independently and need not yield the same percentage. RSS uses all 27 peak samples per variant/case, not only the fastest seven.

## Complete-command results

| Host  | Paths / files / workers / collections | Main ms | Views ms |  Change | Main MiB | Views MiB |
| ----- | ------------------------------------- | ------: | -------: | ------: | -------: | --------: |
| macos | short / 10 / 1 / 1                    |   4.346 |    4.318 |  -0.19% |     3.88 |      3.89 |
| macos | short / 100,000 / 3 / 1               | 109.151 |  109.872 |  -1.66% |    40.03 |     34.34 |
| macos | long / 100,000 / 1 / 1                | 367.909 |  362.691 |  -1.65% |    96.14 |     63.42 |
| macos | long / 100,000 / 3 / 1                | 147.050 |  145.101 |  -1.79% |   100.25 |     66.39 |
| macos | short / 100,000 / 3 / 4               | 137.509 |  131.596 |  -5.23% |   139.16 |    116.20 |
| macos | long / 100,000 / 3 / 4                | 195.058 |  179.718 |  -8.08% |   364.36 |    230.77 |
| linux | short / 10 / 1 / 1                    |   0.629 |    0.626 |  -0.74% |     5.42 |      5.40 |
| linux | short / 100,000 / 3 / 1               |  43.489 |   42.167 |  -3.02% |    38.53 |     33.05 |
| linux | long / 100,000 / 1 / 1                | 133.210 |  126.330 |  -5.34% |    92.57 |     59.62 |
| linux | long / 100,000 / 3 / 1                |  58.487 |   53.165 |  -9.10% |    97.55 |     64.36 |
| linux | short / 100,000 / 3 / 4               |  74.893 |   71.104 |  -5.15% |   132.68 |    111.20 |
| linux | long / 100,000 / 3 / 4                | 131.486 |   95.863 | -26.57% |   357.05 |    225.41 |

## Kernel and storage results

| Host  | Entries / root length / threads | Elapsed change |
| ----- | ------------------------------- | -------------: |
| macos | 10 / 1 / 1                      |         +2.29% |
| macos | 1,000 / 1 / 1                   |         -6.94% |
| macos | 100,000 / 1 / 1                 |         -6.57% |
| macos | 100,000 / 128 / 1               |        -25.68% |
| macos | 100,000 / 128 / 4               |        -25.64% |
| linux | 10 / 1 / 1                      |         -2.56% |
| linux | 1,000 / 1 / 1                   |         +4.08% |
| linux | 100,000 / 1 / 1                 |         -1.97% |
| linux | 100,000 / 128 / 1               |        -17.22% |
| linux | 100,000 / 128 / 4               |        -24.99% |

Separate untimed accounting agrees on both platforms. For 100,000 long-root entries, used text falls from 28,677,780 to 14,388,890 bytes (49.8% less), arena blocks from 28 to 14, and reserved text from 28 to 14 MiB. Entry records and filesystem owners are still real objects. Empty collections remain allocation-free. RSS reductions depend on the other retained state and must not be advertised as a universal 50% process reduction.

## Limits and counterexamples

Worst native paired medians are +1.33% on Linux (1,000 short-path files/one worker) and +1.64% on macOS (ten long-path files/one worker). Kernel counterexamples include Linux 1,000 short-root entries/one thread at +4.08% and macOS ten long-root entries/four threads at +5.26%. Do not claim every small case improves or assign tiny unaffected-control changes to the ownership code.

The four-thread kernel constructs four separate collections; it is not evidence of parallel insertion into one collection. Production collection is coordinator-owned. This change does not optimize all traversal, raw listing, regex, archive decoding or output paths. Both policies retain VFS and safety semantics. Additional real storage, cold-cache and unusual path distributions remain outside this contract.

## Raw evidence

[Complete JSON evidence](collection-text-sharing-20261101.json.gz): 135,725 bytes, SHA-256 `b45fc37ff76fc9d81006d8fafbeab53b2ea33c2d133616e85e0082130a368d88`. It retains raw samples, independent native oracles, source patches, scripts, binary hashes and separate accounting/exception-action summaries.
