# Apple package resource measurements

These measurements exercise complete XAR/PBZX/CPIO traversal through the release `xff_full` binary,
with fixed 1 MiB XZ chunks and one original repeated-byte file. They test whether working memory
scales with the decoded payload rather than predict performance for arbitrary application packages.

Measured on macOS ARM64, Clang release/ThinLTO, 2026-09-20. Each row contains three fresh-process
runs, without cache flushing. The first 16 MiB run took 0.423 seconds (cold-start effects are included);
medians below include every run. `tools/measure_resources.py` reports OS peak child RSS and elapsed
wall time. Fixture generation runs separately and is not included in memory or runtime measurements.

| Decoded file (MiB) | XIP container (bytes) | Median elapsed (s) | Peak RSS range (MiB) |
| -----------------: | --------------------: | -----------------: | -------------------: |
|                 16 |                 5,299 |              0.042 |          13.44-17.00 |
|                 64 |                19,698 |              0.129 |          14.03-16.23 |
|                256 |                77,300 |              0.479 |          19.36-25.22 |

All nine commands succeeded and listed both the `Content` member and its nested `large.bin`.
The 256 MiB case stayed below 26 MiB RSS. This is evidence for these highly compressible fixtures,
not a whole-process memory guarantee or a worst-case decoder-memory measurement. Chunk-limit,
excessive-dictionary, concurrent-budget, and replay-exhaustion behavior is tested separately.

## Reproduce

```sh
bazel build --config=clang_release --//xff:xff_all=true //xff/cli:xff_full
python3 extra_modules/apple/generate_fixtures.py --large-payload-mib 16 --output /tmp/xff-apple-large
python3 extra_modules/apple/generate_fixtures.py --large-payload-mib 64 --output /tmp/xff-apple-large
python3 extra_modules/apple/generate_fixtures.py --large-payload-mib 256 --output /tmp/xff-apple-large
python3 tools/measure_resources.py -- bazel-bin/xff/cli/xff_full --no-config --no-pager \
  --archive=all --archive-depth=3 /tmp/xff-apple-large/large-256.xip -type f -print
```

Run the measurement command three times for each generated file. The generator is deliberately
separate from the measured command; its in-memory fixture construction is not the streaming reader.
Use an isolated configuration environment: `--no-config` cannot bypass required globals.

Native producer coverage is complementary: committed unsigned `pkgbuild` and `productbuild`
fixtures contain only repository-owned text, and their traversal is tested on Linux and macOS.
See [fixture provenance and reader limits](../extra_modules/apple/README.md).
