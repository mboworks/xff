# Language list reservation requalification after #1143

Keep the current runtime. Repeating the exact reservation prototype on current main still
shows small and mixed results, without a demonstrated practical command-level improvement.

## Measurement

The baseline is main `50fe2426d690156e5c388eb879b6295c4e123801`, after #1140 and #1144.
The unchanged language source matches the original baseline byte for byte. The candidate
adds only the original `result.reserve(found->size());` statement. The original private
diagnostic and direct-clock driver are used unchanged; no runtime change enters this PR.

Each host builds lean and linked-database variants under `--config=clang_release`, copies
each image immediately after its build, and retains exact baseline-copy controls. Linux
pins measurements to one allowed CPU; macOS is unpinned. Three sessions each measure all
16 cases with nine observations, retain the fastest seven and include a fresh-process
warmup per participant/case. Each host produces 1,296 timed observations and 144 warmups.
Every invocation validates complete generated lists and normal classification outside the
clock. All 2,880 observations, case means, session ratios and medians were independently
recomputed. All 4,345 transferred Linux files were verified by SHA-256; both hosts used
identical baseline, candidate, diagnostic, rule and driver bytes.

## Results

Negative is faster. Per-case values are medians of the three session ratios, with the
exact-image copy as a separate control; they are descriptive and are not significance tests.

| Image | Case            | macOS candidate | macOS copy | Linux candidate | Linux copy |
| :---- | :-------------- | --------------: | ---------: | --------------: | ---------: |
| lean  | `cold/snapshot` |         +4.718% |    +2.226% |         -1.735% |    -3.599% |
| lean  | `cold/catalog`  |         +2.015% |    +0.442% |         -0.016% |    +2.088% |
| lean  | `warm/snapshot` |         -0.146% |    -0.215% |         +0.181% |    +0.012% |
| lean  | `lists/8/0`     |         -0.971% |    +1.633% |         +1.694% |    +0.079% |
| lean  | `lists/8/1`     |         -0.202% |    +1.367% |         +1.892% |    +0.416% |
| lean  | `lists/8/4`     |         -2.278% |    +0.722% |         -1.717% |    -1.368% |
| lean  | `lists/8/16`    |         -1.004% |    +0.758% |         -0.958% |    +1.574% |
| lean  | `lists/8/64`    |         -1.796% |    -1.126% |         -0.829% |    -0.463% |
| lean  | `lists/128/0`   |         -0.803% |    +0.126% |         +1.683% |    -0.827% |
| lean  | `lists/128/1`   |         -0.609% |    +0.661% |         +1.168% |    +0.211% |
| lean  | `lists/128/4`   |         -2.863% |    -0.030% |         -0.960% |    +0.367% |
| lean  | `lists/128/16`  |         -2.105% |    -0.693% |         -0.467% |    -1.125% |
| lean  | `lists/128/64`  |         -0.448% |    +0.864% |         -0.131% |    -1.791% |
| full  | `cold/snapshot` |         -0.909% |    +0.333% |         -0.420% |    +0.003% |
| full  | `cold/catalog`  |         -0.483% |    +0.062% |         +0.296% |    +0.104% |
| full  | `warm/snapshot` |         +0.292% |    +0.297% |         +0.124% |    -0.063% |

The full linked cold snapshot changes -0.909% on macOS and -0.420% on Linux;
cold catalog changes -0.483% and +0.296%, respectively. Linux 8-language lists with
zero or one value cost +1.694%/+1.892%, and the 128-language empty case costs +1.683%
(copy -0.827%). The lean macOS cold snapshot costs +4.718% (copy +2.226%). Keep all
cases and controls visible. The isolated results do not establish a general CLI speedup;
no allocation-count, RSS or production-binary claim follows from this screen.

The decision remains to retain the current runtime, with the audit and negative result
available for future work. The earlier qualification remains separate and unchanged.

## Evidence

[The lossless capsule](language-list-reserve-requalification-20261108.json.gz) retains 8,680 individually hashed records
(542,618 compressed bytes; SHA-256 `7ecdc251f181d979cc1ca839f725dc25343027588bca740923d895e4b82205fc`). It includes all raw process output,
ordering, summaries, source/driver snapshots, build and linkage logs, image hashes and
Linux transport manifest. Executables remain private. Both host builds and all 2,880
observations completed successfully.
