# Apple package payload reader

The removable `xff_apple` extension adds PBZX raw/XZ payload decoding and contextual discovery of
extensionless package payloads. It is included in the existing full binary on Linux and macOS;
there is no separate Apple binary. XAR and its Expat XML reader belong to `xff_archive`.

Use `--archive=all --archive-depth=3` to inspect an application inside a package. For example:

```sh
xff --archive=all --archive-depth=3 Application.pkg -name '*.plist' -print
xff --archive=all --archive-depth=3 Application.xip -type f --summary=ext
```

An ordinary host `.app` is a directory and already uses normal traversal. ZIP application bundles,
IPA and the outer IPSW ZIP use the archive reader. XAR-based PKG and XIP expose their metadata and
supported CPIO payloads. PBZX decoding does not add a visible namespace layer: paths retain the
package and payload names, for example `Application.pkg!Payload!Applications/Example.app/Contents`.
The archive depth counts those visible container layers.

Installer scripts are readable bytes and are never automatically executed. Container checksums do
not verify signatures or notarization. XAR member rewriting is refused even if the file is renamed.
Existing extraction and execution actions retain their normal safety checks.

This implementation supports length-framed PBZX with raw or XZ chunks. Each stored or decoded chunk
is limited to 64 MiB; the XZ decoder has a 128 MiB memory limit. Independent sequential cursors own
their parent sources, so nested payloads need not be materialized as one complete byte string.
Reopening a member rescans the parent stream; there is no random-access index or disk spill cache.
These limits are per decoder, not a process-wide memory budget. Legacy byte-only readers retain a
256 MiB fallback materialization limit. Requested unsupported PBZX framing is an error.

DMG, UDIF, HFS+, APFS and Apple Archive payloads are not implemented here. IPSW recognition therefore
does not imply that embedded filesystem images are traversable. Filesystem readers are a separate
future extension. All new dependencies use permissive licenses; GPL and LGPL are excluded.

## Fixtures

`python3 extra_modules/apple/generate_fixtures.py` generates the tiny fixtures from original strings
using Python's standard library. They contain no proprietary application data, signing keys or
installer code. The generator independently writes XAR and CPIO headers, SHA-1 checksums, PBZX
length framing, and raw/XZ chunks. ZIP fixtures carry the same small `.app` tree. XAR member lists
were independently checked with macOS `xar`. These are unsigned structural samples, not production
Apple-signed distributions. Source and fixtures use the repository Apache-2.0 license.
