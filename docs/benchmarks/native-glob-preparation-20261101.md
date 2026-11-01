# Native glob preparation (2026-11-01)

## Decision

Retain the existing owned pattern string instead of copying it through a view. Prepare literal, prefix, suffix, contains and one-interior-star ASCII patterns once in the existing operand pool; evaluate them with byte slices when no case folding applies. Preserve libc `fnmatch` for folded case, multibyte/invalid bytes, escapes, character classes, question marks and other complex patterns. There is no new pool, lock, flag, wrapper hierarchy or user-visible grammar.

The reference is #1041 head `cb441b7d8dee9893f4b76ebef0a5613d46679f59`, whose runtime incorporates #1040 plus owned collection text sharing. All three release variants share that source tree except `evaluate.cc`: baseline, owned-pattern-only, and owned-pattern plus prepared simple patterns. This isolates the cheap reference change from the larger prepared strategy.

## Correctness and costs

The immutable AST owns the pattern. A prepared operand borrows it and survives plan moves; its worker and AST lifetime contract is unchanged. Inputs remain length-delimited views. Match the existing first-NUL C-string boundary explicitly, and never assume a view ends in a NUL. Prefix/suffix checks require enough input bytes to avoid overlapping literals. Every unsupported case returns to the platform implementation. Native case policy, safety, metadata observations and short-circuit behavior are unchanged.

The prepared record is a string view, a star offset and a kind in the existing operand variant. It adds preparation/pool storage for name/path predicates that formerly needed no prepared operand. It does not add mutable per-worker glob storage. Native binary growth is recorded below; preparation costs are reported separately from repeated matching.

`glob_evaluate_test` differentially checks all six name/path/wholename variants, explicit folding, native folding, simple/complex/escaped/malformed patterns, nonterminated input slices, NUL boundaries and plan moves with two independent workers. `glob_unicode_test` separately checks accented multibyte strings and deliberately malformed bytes against native locale behavior; ordinary fixtures remain ASCII. Existing evaluation, execution and full-engine tests remain applicable.

## Measurement contract

Local M5 Pro: warm APFS, one/three worker allowances, no affinity. Linux drogon: Ryzen 9950X KVM guest, tmpfs fixtures, child affinity limited to one/three CPUs. Both use release LLVM 23 and ThinLTO. Builds finish before timing. Preserve the host distinction rather than comparing absolute times across machines.

Each native case has three sessions, a discarded warmup per variant/session, nine interleaved measurements and the fastest seven retained. Exact filename output is checked independently after every run. Counts are 10/1,000/10,000/100,000. Cases include a simple suffix, a literal selecting one file, a complex bracket pattern retaining libc, and a no-name-predicate control. Linux and macOS default series have 32 cases/session. Two macOS supplemental series have 24 cases/session: `--case=sensitive` alone, then `--case=sensitive --exact`. Across all series this is 10,080 successful fresh invocations including warmups.

The first supplemental series is an explicit control, not byte-exact matching: documented filesystem-native folding still applies without `--exact`. In XFF mode the runner also requests full metadata and keeps matching serial while filesystem-native probing is active. The exact series changes those existing policies for all three binaries; compare variants within each series, never interpret cross-series differences as the glob optimization alone.

The existing in-memory expression benchmark separately measures preparation and prepared execution for name/type/constant-true predicates, chains of 1/16/64 predicates and batches of 10/1,000 entries. Nine rounds retain seven, with three shuffled variant sessions. Host I/O is absent; correctness is checked before kernel timing. Percent changes below are median within-session ratios; negative is faster.

## Native 100,000-file results

| Series        | Case    | Workers | Baseline ms | Owned-only change | Prepared ms | Prepared change |
| ------------- | ------- | ------: | ----------: | ----------------: | ----------: | --------------: |
| linux-default | suffix  |       1 |     103.257 |            -0.00% |     101.944 |          -1.26% |
| linux-default | suffix  |       3 |      37.732 |            -0.42% |      37.194 |          -1.77% |
| linux-default | literal |       1 |      97.751 |            +0.37% |      97.144 |          -0.36% |
| linux-default | literal |       3 |      35.642 |            -0.46% |      35.278 |          -1.02% |
| linux-default | control |       1 |     113.272 |            +0.20% |     114.197 |          +0.82% |
| linux-default | control |       3 |      90.439 |            +1.10% |      91.881 |          +1.58% |
| macos-default | suffix  |       1 |     248.826 |            -0.35% |     248.913 |          -0.05% |
| macos-default | suffix  |       3 |     108.380 |            +0.86% |     110.371 |          +1.20% |
| macos-default | literal |       1 |     230.520 |            +0.37% |     229.872 |          -0.35% |
| macos-default | literal |       3 |     108.045 |            +0.14% |     110.079 |          +2.21% |
| macos-default | control |       1 |     233.033 |            -0.35% |     233.150 |          -0.81% |
| macos-default | control |       3 |     192.807 |            +0.03% |     190.701 |          -1.09% |
| macos-exact   | suffix  |       1 |     250.326 |            -0.43% |     235.959 |          -5.07% |
| macos-exact   | suffix  |       3 |     210.744 |            -0.02% |     196.678 |          -6.84% |
| macos-exact   | literal |       1 |      71.383 |            -0.25% |      68.573 |          -3.94% |
| macos-exact   | literal |       3 |      49.299 |            -0.42% |      49.275 |          +0.10% |
| macos-exact   | control |       1 |     232.772 |            -0.38% |     232.404 |          -0.15% |
| macos-exact   | control |       3 |     195.425 |            +0.18% |     194.890 |          -1.06% |

The default macOS series mostly uses the native fallback and is not a claimed case-sensitive speedup. No-name-predicate controls also change slightly; retain these code-layout/system-noise controls instead of attributing them to matching. The exact series is useful in find-compatible or explicitly exact XFF workflows.

## Kernel, preparation and binary size

| Host          | Case                | Baseline ns | Owned-only change | Prepared ns | Prepared change |
| ------------- | ------------------- | ----------: | ----------------: | ----------: | --------------: |
| linux-default | kernel/name/1/1000  |    23444.46 |            -9.09% |    13604.80 |         -41.96% |
| linux-default | kernel/name/16/1000 |   504453.38 |            -4.02% |   351124.72 |         -30.26% |
| linux-default | prepare/name/1      |      179.58 |            -2.90% |      205.01 |         +12.89% |
| linux-default | prepare/name/64     |     9109.75 |            -0.74% |     9711.85 |          +6.90% |
| macos-default | kernel/name/1/1000  |    77201.27 |            +0.30% |    19737.90 |         -74.43% |
| macos-default | kernel/name/16/1000 |  1358585.49 |            -2.25% |   412661.24 |         -69.63% |
| macos-default | prepare/name/1      |      396.49 |            -1.77% |      437.81 |          +5.91% |
| macos-default | prepare/name/64     |    16162.58 |            +1.95% |    17122.16 |          +5.56% |

| Host          | Baseline binary bytes | Owned-only bytes | Prepared bytes |
| ------------- | --------------------: | ---------------: | -------------: |
| linux-default |              12293344 |         12291624 |       12307056 |
| macos-default |               4065104 |          4065136 |        4069136 |

These executable sizes include the build output debug sections and are not final compressed release sizes. A fast repeated kernel does not imply a similarly large whole-command gain: traversal, metadata, formatting and process startup remain real costs.

## Counterexamples and remaining work

- `linux-default`: worst paired median +3.66% at (10, 3, 'literal'); best -1.77% at (100000, 3, 'suffix'). All raw samples, including slower cases and controls, are retained.
- `macos-default`: worst paired median +2.21% at (100000, 3, 'literal'); best -1.86% at (1000, 3, 'control'). All raw samples, including slower cases and controls, are retained.
- `macos-case-sensitive-flag`: worst paired median +2.39% at (10, 3, 'control'); best -1.68% at (1000, 1, 'control'). All raw samples, including slower cases and controls, are retained.
- `macos-exact`: worst paired median +1.25% at (10, 1, 'control'); best -6.84% at (100000, 3, 'suffix'). All raw samples, including slower cases and controls, are retained.

Preparation is slightly slower and reserves operand capacity once; it must not be described as free. The simple ASCII path deliberately scans for multibyte input to keep locale-dependent behavior native. Case folding and complex patterns still pay for input materialization. No claim is made for eliminating all glob allocations or speeding every small invocation.

The metadata/worker follow-up (O11/O20) should examine the existing filesystem-native case probe: `RunFindCore` includes it in `full_metadata` and excludes it from parallel matching. Propagating validated directory/filesystem case policy may remove regular-file metadata demand without changing matching semantics. Root files, mounted volumes, symlinks and virtual filesystems need explicit tests; this is a new decision experiment within the retained metadata/worker work, not a shortcut changing defaults.

## Validation

Release differential glob/Unicode, evaluator, expression-execution and full-engine targets passed on both hosts. The complete Linux release/extension suite passed 304/304 with its existing FUSE-required configuration. The complete macOS release/extension suite also passed 304/304. Scoped clang-tidy passed the runtime and Unicode translation units. Its two test-only findings (const helper and trace concatenation) were corrected; that translation unit then passed focused release tests on both hosts and its scoped clang-tidy recheck. Normal hooks and published-head CI remain required before merge.

## Raw evidence

[Lossless JSON evidence](native-glob-preparation-20261101.json.gz): 483,174 bytes; SHA-256 `304977003bf522eff9e5c52922a46c647b828a97097da03e0929d9b59cd6b4a0`. Raw native/kernel samples, source variants, build manifests, scripts and verification logs are retained.
