# Deferred ZIP sizes: compatibility investigation, 2026-11-07

Do not recover every unknown archive size by decoding its payload during listing. The
current streaming ZIP reader reports zero for nonempty members whose local headers
leave sizes to data descriptors. Decoding and counting data blocks recovers sizes for
stored and deflated examples, but introduces failures in previously successful listings.
The size defect remains open; this investigation changes no production code.

## Reproduction and controls

The private diagnostic uses exact main `77aa9ef9bc9475adcbdd37cf3004143a96f03060`, the
pinned libarchive 3.8.1 and `--config=clang_release` with Clang 23.1.4. Six investigative
GoogleTest cases pass on both macOS and Linux. These cases describe current behavior,
including defects; they are retained as evidence rather than added to the production
suite as acceptable behavior.

Each method has a complete one-member ZIP with a known local size and another with a
signed data descriptor. All contain `data.txt`, six bytes `hello\n`, a matching CRC and
central directory. Fixtures are built in memory without host-file writes, external
archivers, or additional dependencies. GoogleTest XML retains their exact bytes.

| Compression | Method | Known-header listing size | Deferred listing size | Deferred streaming content read | Decoding to count blocks |
| :---------- | -----: | ------------------------: | --------------------: | :------------------------------ | :----------------------- |
| Stored      |      0 |                         6 |                     0 | Six correct bytes               | Six bytes                |
| Deflate     |      8 |                         6 |                     0 | Six correct bytes               | Six bytes                |
| Bzip2       |     12 |                         6 |                     0 | Data loss: truncated body       | Fatal decoding error     |
| Deflate64   |      9 |                         6 |                     0 | Unsupported decoder             | Decoder failure          |

The known-header listing succeeds for all four methods without requiring their content
readers. Explicit `archive_read_data_skip` succeeds for every deferred fixture, but
`archive_entry_size_is_set` remains false and `archive_entry_size` remains zero afterward.
Reading the size after skipping therefore does not fix this defect.

Python's independent `zipfile` reader fully decodes and checks CRCs for the exact six
stored/deflate/bzip2 fixtures exported by each host. Both hosts produce identical fixture
identities. For the two Deflate64 fixtures, Python verifies the ZIP directory, lengths and
CRC metadata, and zlib verifies the common final stored-block payload. No independent
Deflate64 decoder was run: this narrowly constructed fixture uses no extended length,
distance or Huffman features. It does not qualify general Deflate64 support.

The initial Mac diagnostic assumed deferred bzip2 could be decoded like deflate and
failed that expectation. Its complete failure log is retained. The corrected diagnostic
separates that discovered limitation, without changing the fixture or production reader.

## Why the shortcut fails

The [ZIP specification](https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT)
defines data descriptors for streamed entries. A zero local size in that case is not an
assertion that the file is empty. The central directory contains the fixture's correct
size, but the current source reader does not select libarchive's seekable ZIP path.

[The pinned libarchive ZIP implementation](https://github.com/libarchive/libarchive/blob/v3.8.1/libarchive/archive_read_support_format_zip.c)
uses different paths for skipping and reading. Its streamable skip routine can consume a
known compressed extent or find a signed descriptor without a decoder. Deflate's unknown
extent uses a decoder while skipping. The public entry's size is not updated by these
paths. This explains why successful header traversal does not establish payload readability.

The diagnostic also corrupts only the CRC in stored/deflated fixtures. Listing still
succeeds while reading the payload reports data loss. Replacing header traversal with
unconditional decoding would add content-validation failures to metadata-only operations.
This is a behavior change even if stricter validation might be desirable as a separate option.

Deferred bzip2 exposes another existing gap. `ReadMember` over the complete memory bytes
successfully returns `hello\n`, whereas `MemberReadSource` over the same memory storage
reports a truncated bzip2 body. Libarchive's streamable bzip2 reader bounds its input by
`entry_bytes_remaining`, which is zero for this fixture; its seekable reader obtains the
size from the central directory. The complete memory API versus streaming source API
must remain separate controls. Calling them both memory reads would hide the difference.

Encrypted and signatureless descriptor cases, arbitrary short reads, ZIP64, malformed
sizes, sparse offsets and nested replay limits are not qualified by these six cases.
No universal compatibility or performance claim follows from them.

## Next implementation boundary

1. Preserve the distinction between unknown and empty member sizes. Do not export an
   unknown size as a trustworthy zero into VFS metadata, summaries or size predicates.
   Specify which observations require an exact size before changing representation.
2. Identify cheap, identity-stable random access explicitly. `ReadSource::OpenAt` and
   `Size` may replay sequential inputs within a shared budget; their mere availability
   does not justify seeking or rebuilding an index on every member read. Reuse the
   prior O19b2/O19b3 first-member and all-visible-members controls.
3. Acquire central-directory sizes only when their source identity, local-member
   association and budget are proven. Preserve duplicate order, orphan/local-only
   entries, normalization and source lifetimes. A directory record must not silently
   substitute a different member for the one exposed by the streaming walk.
4. Define the sequential fallback and unavailable-size error explicitly. Require exact
   size when requested, while preserving metadata-only traversal of entries without a
   usable decoder. Do not make an unbounded payload decode the implicit fallback.
5. Independently address deferred bzip2 extraction, with both memory and streaming
   controls. Keep listing, content validation and exact-size acquisition as distinct
   requirements; do not hide the bug with a zero-size special case.

Before runtime adoption, add desired-behavior regression tests and qualify known/deferred,
empty/nonempty, signed/signatureless, ZIP64, unsupported/encrypted and corrupt members;
local/central disagreements and duplicates; short reads and descriptor boundaries;
nested/decoded sources, failure propagation, and shared memory/replay budgets. Measure
first/last/repeated member reads, metadata-only listing and size-demanding workloads on
both hosts. There are no timing, allocation, RSS, full-suite, sanitizer or public-head CI
results in this investigation. Existing seeking/index regressions remain relevant.

## Retained evidence

[The lossless capsule](deferred-zip-size-investigation-20261107.json.gz) contains 19 hashed
records: the diagnostic and temporary Bazel rule, relevant exact source/configuration,
initial failure, final host test/build/XML records and independent fixture validators.
Every record was decoded and hash-checked after packaging. The capsule is 103,735 bytes;
SHA-256: `b0bd93614cf62b23828be6700ef86cfa45133b70974df1ca0d6c35997ffb8cf2`.

Reconstruct the base checkout, apply the retained private build-rule patch and diagnostic
source, and run `bazel test @xff_archive//:deferred_zip_investigation_test --config=clang_release`.
Run the retained `validate-fixtures.py` against that test's XML for the independent check.
The diagnostic is intentionally not a production acceptance test: several expectations
record bugs that the future implementation must change.
