# Direct span reads for streaming line scans

## Scope and implementation

The candidate changes `content::ScanLines` to use `ReadStream::ReadInto` when the
stream currently supports it. A single caller-owned, uninitialized 64 KiB buffer
lives for the scan. Only the returned prefix is observed. This removes the owning
`Read` result's allocation, initialization and copy from capable streams without
changing the public VFS or output interface.

Capabilities are checked on each read. An incapable stream retains the existing
owning `Read` path, whose string remains alive through line processing. A failed
capable read is propagated; it is not retried through another interface after
input might have been consumed. Oversized returned counts fail before creating a
view. Binary detection, final-line handling and draining after visitor stop retain
their original semantics. Buffer ownership is local: no locks, shared scratch,
thread-local storage or pool is introduced.

A separate implementation lazily allocates the same uninitialized capacity with
`make_unique_for_overwrite`. It remains a distinct measured source. Measurements
must decide between the stack and heap forms; the existence of a zeroing hotspot
is not an elapsed-speedup forecast. O07's rejected uninitialized cursor and shared
fallback-loop implementations remain retained in their existing reports.

## Source identities

- Baseline: `cc30ffd2f85cd32fb902750af5023b6596889faf`, actual merged main after #1156.
- Stack: `4cd48ae6d7a2f83a824b85ec7f87c8baedb29eba`, containing that baseline.
- Lazy overwrite heap: `01689b4d83cac401edba8258a75dbee8eb475d09`, based on the stack source.

Every source is a clean actual Git revision on both hosts. Frozen images retain
source/tree identities, configuration hashes, the configured executable query,
binary SHA-256 and actual ThinLTO link actions. Later report commits must not
relabel these source IDs. All stack/heap outcomes, controls and adverse cells
remain in the raw evidence.

## Behavioral verification

The affected suites cover line scanning, line matching, read-source contracts and
engine rg execution. Eleven added scanner cases exercise short reads, CRLF and
blank/final lines, empty and exact-boundary input, poisoned/uninitialized unused
tails, changing capabilities in either direction, long lines, binary-scan limits,
oversized counts, late errors after stop/binary detection and reentrant scanning.
Existing incapable-stream cases remain. Both variants pass the four focused
suites and two-translation-unit clang-tidy on both hosts, ASan on both hosts and
MSan on Linux. These are development results at the source IDs above; current
pushed-head full release suites and CI remain required before merging.

## Measurement contract

The unchanged in-memory engine benchmark covers traversal, preloaded native line
output, streaming rg line output, reduced rg file/count output and large output.
Its existing worker counts are 1 and 4; these are not renamed to the native
1/3/10 CPU-budget cells. Each participant has one discarded warmup and nine
measured rounds, retaining the fastest seven per case. A copied baseline image
is an independent ordinary file with the same bytes, not proof of physical
backing-page isolation. Mac uses ordinary Bazel ThinLTO; Linux uses shipping
ThinLTO because ordinary split-indexing LTO plus fat objects fails to emit an
indexing output. The failed build and its actual actions are retained. No
sanitizer or memory-engine result is described as shipping CLI elapsed time.

Native qualification uses unsanitized full and minimal shipping ThinLTO CLIs,
broad/deep RAM-filesystem trees, 100 and 100,000 files and budgets 1/3/10. Linux
pins each process to the recorded allowed logical CPUs, preferring distinct
physical cores. Mac records worker requests without claiming CPU affinity.
Separate full-CLI tar-member cases cover 100 and 10,000 members. Archive filters
exclude the raw container, so the oracle checks actual member lines.

Each native case interleaves baseline, stack, heap, repeated baseline and a
separate byte-identical baseline image, plus GNU find or rg where applicable.
The native fork/exec/wait interval excludes Python output validation. Every
invocation has an independent filename/line/count/metadata oracle generated
from the fixture recipe; member names and line numbers are not inferred from
the baseline output. All raw rounds, including warmups and excluded rounds,
are retained. No post-hoc division by copied-baseline controls is applied.

Reduced rg file/count output still uses `ScanLines` for unloaded content; those
are other consumers of the changed scanner, not untouched negative controls.
Preloaded native `-M` output uses `VisitLines`. Listing, name and metadata paths
provide unrelated-work controls. General source/decoder/derived-cache admission,
prepared expression work and the broader O18/JC scope remain open.

## Decision and complete results

Qualification is in progress. Complete native matrices, adverse small cases,
image sizes, final source validation and durable archive receipts must be added
before this candidate is declared ready. Early large-case wins do not establish
a universal improvement or justify discarding ordinary-work regressions.
