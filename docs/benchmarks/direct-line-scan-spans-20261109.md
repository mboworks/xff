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

The identical added serial diagnostic is built at benchmark-only baseline
`7e4ef7909d27a78fa782e0d098196dd5c4fde7b5`, stack
`f4290d429a1c58dc3b8e8941a8fdf2540954965f`, and heap
`74650ad5697b2f19e15dd268e6ee6722cdfcf0c5`. Their runtime parents are the three
original sources above. Both hosts use the same signed commits, verified after
transfer; headless Linux signing is not bypassed. Native confirmations use the
actual frozen `f429` shipping images, with the byte-equality proof described below.

## Behavioral verification

The affected suites cover line scanning, line matching, read-source contracts and
engine rg execution. Eleven added scanner cases exercise short reads, CRLF and
blank/final lines, empty and exact-boundary input, poisoned/uninitialized unused
tails, changing capabilities in either direction, long lines, binary-scan limits,
oversized counts, late errors after stop/binary detection and reentrant scanning.
Existing incapable-stream cases remain. Both variants pass the four focused
suites and two-translation-unit clang-tidy on both hosts, ASan on both hosts and
MSan on Linux. These development results retain the source IDs above.

Implementation head `f4290d429a1c58dc3b8e8941a8fdf2540954965f` adds the diagnostic
benchmark and documentation. It passes all 344 configured release test targets
on both hosts, including required Linux FUSE tests, four freshly executed affected
release suites, and eight fresh affected suites under ASan/TSan on both hosts and
MSan on Linux. Source hashes are unchanged throughout each qualification. Its
full/minimal shipping images are byte-identical to the original stack images on
both hosts; independently retained build/link proofs establish that fact.

The later `f0f2963124e853801c1fbd347b51fa159a68ca1f` changes only the release-site
page mapping. Its complete PR Test workflow `42634678865`, including coverage,
clang-tidy, all platforms, sanitizers and benchmark aggregation, succeeds. Final
report/data publication must receive its own current-head checks; these results
must not be relabeled as testing an unpublished later head.

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
reported cores. Drogon is a KVM guest exposing ten logical CPUs on a Ryzen 9
9950X host; guest affinity is not proof of host physical-core pinning. The Mac
is an 18-CPU Apple M5 Pro with 64 GiB memory, and records worker requests without
claiming CPU affinity. These are separate host populations, not direct OS rankings.
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

Select the caller-owned stack implementation. Corrected native confirmation shows
about 18% less single-CPU large streaming elapsed time on Linux and 5-6.4% less on
Mac. Higher worker requests have smaller gains or remain near parity, including
the adverse observations below. Lazy heap storage adds an allocation and does not
improve the complete kernel/engine populations consistently enough to select it.
Its source, correctness results and every measurement remain retained.

The original 180-case native matrices complete on both hosts, with 10,680 checked
invocations each. Corrected matching-invocation confirmations and fixed independent
small-image studies complete separately on both hosts. This is a scoped streaming
optimization, not a claim of a universal speedup or a zero-cost legacy fallback.
Final report/data publication requires its own current-head validation.

### Mac confirmation

The same predeclared selection rule used on Linux selects 121 cases on Mac, with
4,780 checked invocations. Every output and image identity passes. All original
and confirmation samples remain separate. The complete large streaming group's
broad/deep geometric means are:

| Shipping configuration | Requested workers | Stack / baseline | Independent baseline copy / baseline |
| :--------------------- | ----------------: | ---------------: | -----------------------------------: |
| Full                   |                 1 |           0.9506 |                               0.9960 |
| Full                   |                 3 |           0.9978 |                               1.0273 |
| Full                   |                10 |           1.0059 |                               1.0025 |
| Minimal                |                 1 |           0.9355 |                               0.9945 |
| Minimal                |                 3 |           0.9947 |                               1.0191 |
| Minimal                |                10 |           0.9957 |                               0.9993 |

The single-worker group improves by about 5-6.4%; higher worker requests are
closer to parity, with a 0.6% slower full/10-worker aggregate. Two individual
all-nine paired diagnostics have intervals wholly above one: minimal/broad/
100,000/3-worker rg counts (fastest-seven ratio 1.0196, copied baseline 1.0390)
and full/broad/100,000/10-worker rg lines (1.0163, copied baseline 1.0080).
Their all-nine geometric ratios are 1.0262 and 1.0315, with within-case bootstrap
intervals [1.0034, 1.0547] and [1.0059, 1.0705]. These are the same distinct
diagnostic estimator described below, not fastest-seven confidence bounds.
The observations remain adverse tradeoffs, without declaring either code or
host variation a proven cause. A favorable small-image study must not erase
these large-case observations.

The original 10,000-member archive cases are near parity: stack point ratios
range from 0.9821 to 1.0016. Replaying preceding headers remains the dominant
separate archive opportunity; this scanner change does not remove that work.

The separate 90-case small-image study applies exactly the Linux protocol below:
three independent copies per variant, equal-length paired paths, matching program
basenames, rotated image order, and nine measured rounds retaining seven per image.
All 5,400 invocations pass their independent output oracles. The group geometric
means and all individual observations are retained separately from confirmation.

| Small-case task                 | Cases | Stack / baseline geometric mean |
| :------------------------------ | ----: | ------------------------------: |
| Streaming rg lines              |    12 |                          0.9917 |
| Streaming rg matching filenames |    12 |                          0.9877 |
| Streaming rg counts             |    12 |                          0.9914 |
| Preloaded native lines          |    12 |                          0.9999 |
| Exact file listing              |    12 |                          0.9982 |
| Native name filter              |    12 |                          0.9971 |
| Metadata filter                 |    12 |                          1.0041 |
| TAR member lines                |     6 |                          0.9880 |

The worst aggregate cell is 1.0204 for minimal/broad/100/10-worker name filtering;
individual image-pair costs can be larger and are retained. These same-machine
copies characterize image variation, not independent hardware or population
confidence. The study does not replace the two slower large cases above.

### Linux confirmation

The matching-invocation confirmation preserves the executable basename and gives
each image one invocation per round. The selection rule was recorded before this
confirmation: all 100-file/native and 100-member/archive cases, every 100,000-file
`rg-lines` case, and any original case with a stack ratio above 1.05 or either
baseline control outside 0.95-1.05. This produces 109 cases and 4,300 checked
invocations. Original, first balanced, and corrected confirmation cohorts remain
separate, with every adverse observation preserved.

For the complete primary large streaming group, the table shows the geometric
mean of the broad/deep ratios. Each underlying time is the arithmetic mean of
its fastest seven of nine measured rounds, following one discarded warmup.
Ratios below one mean less elapsed time. These are per-machine observations,
not confidence bounds or forecasts for arbitrary storage and workloads.

| Shipping configuration | Logical CPU allowance | Stack / baseline | Independent baseline copy / baseline |
| :--------------------- | --------------------: | ---------------: | -----------------------------------: |
| Full                   |                     1 |           0.8208 |                               0.9969 |
| Full                   |                     3 |           0.8740 |                               1.0006 |
| Full                   |                    10 |           0.9339 |                               0.9939 |
| Minimal                |                     1 |           0.8199 |                               0.9993 |
| Minimal                |                     3 |           0.8733 |                               0.9909 |
| Minimal                |                    10 |           0.9390 |                               1.0018 |

Small unrelated-work cases in the confirmation include slower individual points:
full/deep/100/10-CPU metadata is 1.0928 and minimal/deep/100/3-CPU name filtering
is 1.0364. Their copied-baseline ratios are 1.1134 and 0.9510 respectively.
Separate all-nine paired log-ratio bootstrap diagnostics have intervals barely
above one for those two cases. Those intervals are not confidence intervals for
the fastest-seven estimator and do not correct for multiple comparisons. The
entire original full/deep/100,000/1-CPU native-line disturbance is retained too:
stack 1.302, heap 1.504 and the exact repeated baseline image 1.512. It does not
establish a runtime-code cause.

To examine ordinary small-case image variation, a separately recorded study uses
three independent ordinary copies per variant, matching basenames and path
lengths, alternating copy creation order, and rotating all six images once per
round. All 90 small cases complete, with 5,400 checked invocations. It averages
the three per-image fastest-seven means before computing a variant ratio; it is
not pooled with the original or confirmation cohorts.

| Small-case task                 | Cases | Stack / baseline geometric mean |
| :------------------------------ | ----: | ------------------------------: |
| Streaming rg lines              |    12 |                          0.9623 |
| Streaming rg matching filenames |    12 |                          0.9617 |
| Streaming rg counts             |    12 |                          0.9586 |
| Preloaded native lines          |    12 |                          0.9993 |
| Exact file listing              |    12 |                          1.0046 |
| Native name filter              |    12 |                          0.9984 |
| Metadata filter                 |    12 |                          0.9968 |
| TAR member lines                |     6 |                          0.9874 |

No aggregate cell in that study is more than 5% slower; the worst is 1.0253 for
minimal/deep/100/10-CPU exact listing. Individual paired-image ratios can exceed
that, and remain in the data. Copies on one host are not independent machines,
do not establish population confidence, and do not prove physical backing-page
isolation. The purpose is to retain and characterize the earlier small-case
variation rather than silently exclude it or divide by a favorable control.

### Serial scanner and in-memory engine

The scanner diagnostic completes 32 cases per image, four images and ten rounds
on each host: 1,280 checked benchmark rows per host. Forced legacy and direct
streams share the same owning-read implementation and byte fixture; their only
capability difference is whether `SupportsReadInto` returns true. Input lengths
are 0, 256, 4,096, 65,536 and 131,072 bytes, with 8,192/65,536-byte chunk limits;
the stop group omits empty and 4,096-byte inputs. The callback checks line/byte
counts before timing. No filesystem, source opening or multi-worker execution
is inside this serial kernel.

| Host  | Kernel group | Stack / baseline | Lazy heap / baseline | Baseline copy / baseline |
| :---- | :----------- | ---------------: | -------------------: | -----------------------: |
| Mac   | Legacy/all   |           1.0101 |               0.9920 |                   1.0333 |
| Mac   | Legacy/stop  |           0.9984 |               1.0197 |                   1.0324 |
| Mac   | Direct/all   |           0.8105 |               1.1628 |                   1.0294 |
| Mac   | Direct/stop  |           0.6575 |               0.8244 |                   1.0337 |
| Linux | Legacy/all   |           1.0581 |               1.0613 |                   1.0081 |
| Linux | Legacy/stop  |           1.0473 |               1.0656 |                   0.9983 |
| Linux | Direct/all   |           0.8318 |               1.0321 |                   1.0055 |
| Linux | Direct/stop  |           0.7315 |               0.8953 |                   1.0025 |

These are geometric means of per-case elapsed ratios, with no input-size or
production-frequency weighting. In particular, tiny and empty direct inputs
benefit more than long inputs: Linux direct/all at 128 KiB has ratios 1.0143
(64 KiB chunks) and 0.9891 (8 KiB chunks). This is not a universal per-read gain.
Both new policies incur measurable Linux legacy costs, detailed below.

The unchanged engine diagnostic completes 48 cases per image and ten rounds,
or 1,920 rows per host. Its streaming `rg-lines` geometric mean is 0.9895 on Mac
and 0.9892 on Linux for stack storage; lazy heap is 0.9992 and 1.0540. The large
output group is 0.8610/0.9898 for stack and 0.8472/1.0188 for heap (Mac/Linux).
These are in-memory engine observations and retain their original 1/4-worker
contract; they must not be described as shipping process startup or native CLI
elapsed time.

To run the committed serial diagnostic with the shipping configuration:

```sh
bazel run //xff/content:line_scan_benchmark --config=clang_release --config=xff_full -- \
  --benchmark_min_time=0.05s --benchmark_out=scanner.json --benchmark_out_format=json
```

The retained drivers freeze each configuration immediately after its configured
build, verify image/source identities, shut down Bazel before timing, interleave
images, preserve warmups, and check every native result against the fixture
recipe. Reproducing the qualified comparison requires the recorded baseline,
stack and heap source revisions and the same contracts, rather than comparing
two uncontrolled invocations of that command.

### Shipping image sizes

These are actual configured ThinLTO build outputs before release stripping or
compression, not tagged release assets. The implementation head produces the
same bytes as the original stack runtime images. Full/minimal configurations
are built and queried separately; no stale `bazel-bin` alias supplies the sizes.

| Host  | Configuration | Baseline bytes | Stack bytes | Change bytes |
| :---- | :------------ | -------------: | ----------: | -----------: |
| Mac   | Full          |      8,322,616 |   8,322,040 |         -576 |
| Mac   | Minimal       |      4,242,080 |   4,241,904 |         -176 |
| Linux | Full          |     18,575,776 |  18,578,064 |        2,288 |
| Linux | Minimal       |     12,831,704 |  12,834,000 |        2,296 |

### Lossless evidence

The [Mac dataset](direct-line-scan-spans-20261109-macos.json.gz) and
[Linux dataset](direct-line-scan-spans-20261109-linux.json.gz) contain all
separate native populations, every raw benchmark JSON, measured and excluded
rounds, contracts, exact drivers, source/build identities and qualification
receipts. It retains all slower observations and the invocation-name limitation
of the earlier controls. There is no post-hoc control division or cohort pooling.
JSON documents are stored as parsed values with original file hashes and lengths;
the capsules retain the byte-exact original files as well.
Linux compressed SHA-256 is
`407dec1f4bc2e8d7d0a97384b978e0a7924a171d2d73b079dbe900a579483330`
(1,885,769 bytes).

Mac compressed SHA-256 is
`c9190c76258f53fbe21c88ad22b07b7231946bcc101fc0b3bfb1a582fc85a8c6`
(2,135,444 bytes). The original, confirmation and small-image campaigns complete
with 180/121/90 cases and 10,680/4,780/5,400 checked invocations respectively.

The durable Linux capsule is
`/home/marcus/Documents/direct-line-scan-spans-20261109-linux-evidence.tar.gz`
on drogon: 59,565,583 bytes, SHA-256
`d76fc9a95beed63cb9f3d05707234dbe7d5e1d2a5a64068e3da46034c96a679a`.
All 394 manifest entries are verified after copying to durable storage. It also
retains actual executables, complete logs, signatures, failed experiments and
link actions. Byte-identical archived contents may share a tar hard-link entry;
the distinct original measurement images and their identities are recorded in
their contracts. Bazel caches, toolchain downloads, Python bytecode, generated
full release sites and fixture mounts are excluded. These exclusions do not
remove timing samples or their independent validation recipes.

The corresponding Mac capsule is
`/home/marcus/Documents/direct-line-scan-spans-20261109-macos-evidence.tar.gz`
on drogon: 31,917,717 bytes, SHA-256
`5725b6217259db7c1ef2b5e22741fd2717416ea17cde26fa026d44df16572f6d`.
All 409 manifest entries pass verification again after the durable transfer.

## Additional observations retained for separate work

The legacy scanner kernel uses an incapable stream whose capability method returns
false. On Linux, the stack implementation's 256-byte scan costs about 8 ns more
than baseline (+25%); its 128 KiB scan costs about 0.14-0.18 us more (+1.9-2.4%).
Do not describe fallback performance as free or infer a native CLI regression
from this isolated kernel. Mac legacy measurements are near parity. Both raw
populations and all copied-image controls must remain in the final data.

The call-local 64 KiB buffer also exists when the stream uses owning reads; that
path retains its owning chunk as well. Logical buffer capacity is not a measured
RSS value or a whole-process bound. The candidate makes no general source-memory
admission claim.

The TAR content cases expose a separate opportunity. In
`extra_modules/archive/archive_reader.cc`, `MemberSource::Open` creates a session
and walks headers to find each requested member. The requested name is normalized
again in each comparison. Preparing that invariant once per open is a separate
O19g3 candidate, with first/missing-member controls. Reusing observed payload
locations for validated uncompressed regular TAR members is a distinct O19b
proposal; compressed, sparse and other format/decoder semantics must retain their
existing path. These ideas are not implemented or counted as gains by this PR.

### Invocation-name control audit

`InvocationConfigForProgram` treats an executable basename as an implicit config
selector, mapping `xff_full` to `xff` and preserving other names verbatim. The
original native and first confirmation copy controls used different basenames.
Their output oracles pass, but their startup/config-selection path is not an
identical invocation. Do not attribute their timing differences solely to image
backing. Candidate and baseline invocations already preserve the same basename.
Retain those earlier cohorts, and use a separately recorded confirmation whose
independent copies preserve `xff`/`xff_full`. The serial/engine kernel timings
exclude executable initialization and do not dispatch XFF by their benchmark
binary's name.
