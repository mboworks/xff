# Legacy archive read buffer initialization, 2026-11-07

Remove only the value initialization of the 64 KiB stack array in `ReadPositionedEntry`.
The library initializes the bytes it returns, and the caller appends exactly that count.
Legacy memory and file-backed TAR reads improve on both hosts. There is a tradeoff:
macOS single-file gzip around 64 KiB becomes about 2-3.3% slower. This is a scoped API
optimization, not a claim that ordinary CLI archive searches or every extraction gets faster.

## Scope and correctness

The function serves `ReadMember`, `ReadMemberOfFile` and `ReadCompressedSingleFile`.
The ordinary `MemberReadSource` streaming path is unchanged and remains a control.
No source capability, read budget, error handling, archive format, compression setting,
allocation strategy, buffer size, output limit, filesystem routing or worker policy changes.
There is no additional wrapper, branch or threshold.

A positive library read appends only the written prefix. EOF appends nothing; a negative
read propagates the existing error. Two committed tests cover byte-exact binary payloads
at 0, 1, 1,024, 65,535, 65,536, 65,537 and 131,073 bytes, exact and too-small limits,
and large/empty/short sequential calls that must not expose an earlier stack payload.
The new tests pass against both parent and candidate on both hosts.

Each host passes five release targets: the new buffer test, archive reader, source buffer,
filesystem and format-fixture tests. Each also passes the three focused reader/source/buffer
ASan targets. Linux additionally passes those three under the repository's MSan configuration.
These are development checks on the frozen source, not the eventual integration PR's CI.
No memory-footprint saving is claimed: the stack array still occupies 64 KiB.

## Measurement contract

The frozen base is main `77aa9ef9bc9475adcbdd37cf3004143a96f03060`. Parent and candidate
use the same `clang_release` configuration and Clang 23.1.4, changing only the array's
initializer. Benchmark sources, runtime sources, image hashes and system-library linkage
are retained. Diagnostic executables do not load a shared mutable xff/libarchive library.
The identical parent image is copied and measured again as an independent drift control.

The initial 16-case memory/streaming screen has three repetitions, retaining two, per
participant on each host. It remains separate. Full qualification uses four modes and
eight payload sizes: 0, 1, 64, 1,024, 65,535, 65,536, 65,537 and 1,048,576 bytes. File-backed
TAR and single-file gzip fixtures are generated outside timing, transferred unchanged,
and hash-identical across hosts. File reads use the native host filesystem with warm-up;
they are not presented as RAM-filesystem measurements.

Each timed API result is compared against complete independently generated content.
Timing includes that consumer comparison and result destruction. Streaming creates its
ordinary source/cursor machinery; it is not a bare decoder kernel. Three independently
ordered sessions each collect nine repetitions per case/participant and average the
fastest seven. Cases are shuffled with recorded seeds and participant order rotates.
There are no concurrent same-host builds during timing. Linux pins the whole process to
one allowed CPU; macOS is unpinned. All warm-up and timed extractions run the same oracle.

The legacy API rejects a valid empty gzip before reaching the buffer. The first full Mac
session correctly stopped on that baseline precondition. Its initial diagnostic source
and raw errors remain. Collection then resumed only missing measurements, retaining
completed successful samples. Empty gzip is an explicit availability control on both
hosts, with nine expected errors per participant/session and no timing ratio. This is an
existing separate defect, not a zero-time improvement or silently dropped case.

Thus full qualification retains 2,511 valid timing observations and 81 expected-error
observations per host, plus all initial-screen observations. The first diagnostic build's
const optimization-barrier issue and its corrected source are also recorded.

## Results and costs

For each case/session, change is the candidate's fastest-seven mean divided by the
parent's, minus one. Take the median of the three session changes per case, then the
median across that mode's cases. Positive is slower. This is an equal-case descriptive
summary, not elapsed-time weighting or a statistical significance test. Parent-repeat
controls use the same calculation and are not subtracted to manufacture a corrected win.

| Mode                    | Mac candidate | Mac parent repeat | Linux candidate | Linux parent repeat |
| :---------------------- | ------------: | ----------------: | --------------: | ------------------: |
| Legacy memory TAR       |        -8.73% |            -0.09% |          -3.89% |              -0.01% |
| Native file-backed TAR  |        -1.98% |            +0.71% |          -1.89% |              +0.22% |
| Native single-file gzip |        -2.37% |            +0.04% |          -0.19% |              -0.18% |
| Unchanged stream path   |        -0.79% |            -0.29% |          +0.14% |              +0.00% |

Small legacy memory TAR cases improve about 12-16% on Mac and 5-6% on Linux; 1 MiB
improves less than 1%. Keep the weak Linux gzip aggregate alongside the gains: it does
not establish a meaningful general gzip speedup.

The Mac gzip costs are real counterexamples to a universal claim:

| Payload bytes | Candidate median | Parent repeat median |
| ------------: | ---------------: | -------------------: |
|        65,535 |           +2.11% |               -3.05% |
|        65,536 |           +3.33% |               +0.32% |
|        65,537 |           +3.08% |               +1.72% |

The exact-64-KiB candidate costs +3.33%, +2.57% and +4.48% across sessions, with parent
repeat -0.39%, +3.00% and +0.32%. Its parent time is about 65 microseconds. A 1 KiB gzip
case has a +9.74% individual session despite a favorable three-session median. Do not
hide those observations in the aggregate or repeat them until favorable.

Mac's unchanged 1 MiB stream control is particularly unstable: candidate sessions are
+1.71%, -13.77% and +8.07%; parent-repeat sessions are +51.31%, -32.10% and +4.29%.
Its median is +1.71%, versus parent repeat +4.29%. The source path is unchanged, but
that does not prove all measured costs are noise. No cache, allocator, page-fault or
instruction-layout explanation is established by this experiment.

Select the simple change for the reproducible TAR API gains, retaining the gzip tradeoff
for review. Do not add a payload-size or metadata-based workaround without an independent
mechanism and qualification. Do not remove other buffers' initialization by analogy:
O07 already retains a one-time clear after different Linux regressions. Main benchmark
and ordinary-command speedups remain unproven for this legacy helper.

## Retained evidence

[The lossless capsule](archive-read-buffer-clear-20261107.json.gz) contains 2,114 hashed
records, including every raw sample, summary, output/error record, both source/image
families, plans, fixed seeds, exact native fixtures, benchmark driver, validator source,
relevant configuration and both-host focused/sanitizer logs. Diagnostic image hashes are
retained; executable copies remain private. Each record was decoded and hash-checked.
The 1,106,635-byte capsule SHA-256 is
`c86d0b03d23f71cb334ee813b6d90eb98524dfe3c6082906cf6bd3f2c1b79af7`.

Reconstruct the base, apply the retained experimental rule, restore the relevant parent
or candidate runtime file, and build the retained benchmark with `--config=clang_release`.
The initial and full drivers have their own explicit directories and sampling policies.
For a full session run `qualify.py --directory=IMAGES --platform=macos|linux --session=1`
(and separately 2 and 3), keeping its canonical fixtures and named images. A resume uses
existing raw files rather than overwriting successful measurements. No research benchmark
source or target is added to production by this change. The public PR's current-base
hooks, coverage, sanitizer, GCC and CI clang-tidy checks remain required.
