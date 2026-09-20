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
Host and memory sources support direct offsets. Sequential sources can reopen and replay for
bounded range reads; no disk spill cache is created. XAR uses these operations when its member order
requires backward seeks, including packages emitted by `productbuild`.

One shared `ReadBudget` follows each source tree through archive members and PBZX layers. Its default
512 MiB memory allowance covers reserved PBZX decoded chunks, stored chunk/decoder workspace,
archive callback buffers, and retained `ReadBlock` ranges. Reservations are thread-safe and released
when their owners are destroyed. Independent cursors therefore cannot each claim a separate full
allowance. API callers can supply a smaller budget or share one across roots. This is not a whole-process
RSS cap: source input strings, archive indexes, allocator overhead, and libarchive's internal allocations
are outside this accounting. XZ still enforces its own 128 MiB decoder limit.

`ReadSourceRange` returns at most 64 MiB and retains its memory reservation with the returned block.
Native offsets do not consume replay work. Sequential offset/size operations share a cumulative
256 MiB replay allowance; failed operations do not reset it. Size discovery reads in 64 KiB blocks
and can inspect one final block before reporting exhaustion. Reopening nested members can still
rescan their parents; there is no persistent random-access index or automatic range-cache eviction.
Budget exhaustion is an explicit error. These APIs prepare for future disk readers without adding
filesystem-image support or implicit temporary files.

Legacy byte-only readers retain a 256 MiB fallback materialization limit. Requested unsupported
PBZX framing is an error.

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

`xz.pbzx` contains one independently compressed chunk. `memory-limit.pbzx` changes its XZ
block header to request a 256 MiB dictionary and recomputes the header CRC, testing rejection
by the 128 MiB decoder budget without constructing a large payload.

`native-component.pkg` and `native-product.pkg` are unsigned packages produced by macOS
`pkgbuild` and `productbuild` using only our `info.txt` and `Info.plist`. Recreate them with
`bash extra_modules/apple/generate_native_fixtures.sh` on macOS. Packaging timestamps and BOM
metadata can change their bytes; CI consumes the committed fixtures on both Linux and macOS.
The product fixture exercises a real producer's out-of-order XAR heap layout. This validates native
packaging tools, not proprietary application contents, signing, or every shipping package variant.

The PBZX fuzz target is isolated from host writes and execution. Its corpus includes raw, mixed,
XZ, and excessive-dictionary fixtures. See [resource measurements](../../docs/apple-package-resources.md)
for large-payload reproduction commands and measured results.
