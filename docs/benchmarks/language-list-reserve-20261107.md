# Exact language JSON list reservation, 2026-11-07

Retain the current runtime. The one-line prototype reserves `found->size()` for the
`StringList` result before appending borrowed JSON strings. Cold linked-language
loads improve modestly, but the complete population also contains slower controls
and substantial variation between identical images. This screen does not establish
a useful practical command-level gain. Keep the evidence and do not add the runtime
or the research executable to production.

## Scope and source

The baseline is merged main `77aa9ef9bc9475adcbdd37cf3004143a96f03060`.
The candidate changes only `xff/matching/language/language.cc` by adding
`result.reserve(found->size());`. Parsing, validation, claims, precedence,
normalization, registration, immutable snapshot ownership and fallback behavior
remain unchanged. This is not a generated-table implementation or an allocation
counting measurement. No library, wrapper, cache, flag or worker policy is added.

A source-only transfer to an isolated Linux workspace verifies all 1,023 source
hashes. Parent and candidate C++ bytes match across hosts. Each host uses separate
immutable parent, candidate and byte-identical parent-copy images. Linux images
link only the system C/math libraries and loader; Mac images link only system
libc++/System. Rebuilding the candidate cannot change a library loaded by its parent.
Hermetic optimized diagnostics use the recorded `clang_release` configuration;
these executable sizes and timings are not published CLI size or ThinLTO claims.

## Measurement contract

Two harness versions remain separate. The initial Google Benchmark driver completes
all 16 cases on Linux. Mac observations reveal about one second of per-process
bookkeeping outside the measured API call; that would cost about 24 minutes for the
population. The private Mac run is intentionally stopped, preserving every completed
and partial artifact. The direct-clock replacement repeats the complete population
on both hosts; no initial sample is overwritten, relabeled or used as a later sample.

The final driver starts a fresh process for every observation, prepares fixtures
before the clock, then measures the first `ActiveSnapshot` and optionally
`ActiveCatalog`. A warm control initializes the snapshot before the clock and
measures 1,000 subsequent acquisitions. All synthetic list values in all five fields
are verified independently outside timing, along with the ordinary C++ filename
classification. A failure returns nonzero; no observation fails those checks.

The lean image covers cold snapshot/catalog, warm snapshot, and 8/128-language
fixtures with 0/1/4/16/64 values per list. The full image adds the actual linked
Linguist database for cold snapshot/catalog and warm controls. Each of three sessions
randomizes case order and rotates participant order, with nine measured rounds,
fastest-seven means, and one fresh-process warmup per participant/case/session.
Each host has 1,296 timed observations and 144 warmups. Linux pins every measurement
to one allowed CPU; no macOS pinning is claimed. Both hosts avoid concurrent local
builds/hooks during timings. Absolute API timing excludes process startup, fixture
construction, validation and printing.

## Complete final population

Negative elapsed change means faster. Each cell is the median of its three session
ratios, using each session's fastest-seven mean. The copied-parent values remain
independent controls; they are not subtracted to manufacture a corrected result.

| Image | Case            | Mac candidate | Mac copy | Linux candidate | Linux copy |
| :---- | :-------------- | ------------: | -------: | --------------: | ---------: |
| lean  | `cold/snapshot` |       +0.877% |  +2.255% |         -1.346% |    -0.188% |
| lean  | `cold/catalog`  |       +4.992% |  +4.882% |         +0.228% |    -1.382% |
| lean  | `warm/snapshot` |       +0.217% |  +0.727% |         +0.000% |    +0.052% |
| lean  | `lists/8/0`     |       +2.081% |  +0.175% |         -1.417% |    -0.738% |
| lean  | `lists/8/1`     |       -0.255% |  -1.237% |         -2.357% |    -1.273% |
| lean  | `lists/8/4`     |       -2.972% |  -0.187% |         -3.702% |    -4.227% |
| lean  | `lists/8/16`    |       -0.806% |  +2.040% |         +0.741% |    +0.171% |
| lean  | `lists/8/64`    |       -1.890% |  -0.638% |         +0.094% |    +0.412% |
| lean  | `lists/128/0`   |       +0.161% |  +0.351% |         +1.792% |    +0.248% |
| lean  | `lists/128/1`   |       -0.623% |  -0.093% |         +0.349% |    +0.197% |
| lean  | `lists/128/4`   |       -1.976% |  +0.095% |         -0.757% |    -0.345% |
| lean  | `lists/128/16`  |       -1.781% |  +0.684% |         -1.078% |    +0.499% |
| lean  | `lists/128/64`  |       -0.134% |  -0.317% |         -0.039% |    +0.117% |
| full  | `cold/snapshot` |       -1.121% |  +0.600% |         -0.627% |    +0.073% |
| full  | `cold/catalog`  |       -0.474% |  +0.558% |         -0.556% |    -0.036% |
| full  | `warm/snapshot` |       -0.504% |  +1.084% |         -0.111% |    -0.202% |

The linked cold snapshot/catalog medians are -1.121%/-0.474% on Mac and
-0.627%/-0.556% on Linux. Every final session for those four cases improves, which
is retained rather than dismissed. However, the earlier complete Linux driver gives
cold linked changes near its copied-parent drift, the final warm unchanged controls
also move, and the overall isolated signal remains small. No end-to-end CLI or
filesystem result establishes its practical value.

Keep the costs too: the Mac empty eight-language list case is +2.081% overall and
positive in all three sessions (+2.081%, +3.129%, +1.225%). Linux's empty
128-language case is +1.792%, with sessions +1.296%, +6.654%, +1.792%.
The unaffected lean Mac catalog median is +4.992%, versus its copied parent +4.882%.
Do not assign an allocator/cache/layout explanation or an empty-list threshold to
these observations. A removed growth operation alone does not prove a command gain.

## Verification and limits

Six focused release targets pass on each host: language, catalog, default cache,
language-database integration, shared language API, and the linked language extension.
The runtime source tested there is exactly the one-line candidate. Every v2 raw
output, iteration count, session mean, copy ratio, candidate ratio and final median
is independently recomputed from the retained per-process records. Linux transport
verifies 8,682 evidence hashes before analysis.

This is an exploratory rejection screen, not a final runtime qualification. No full
release suite, sanitizer, local clang-tidy, public-head CI, allocation count, peak RSS,
startup or whole-command speedup is claimed. No additional final-runtime cycle is
justified for this unselected implementation. Existing published PR heads remain
unchanged; the decision can accompany the candidate-inventory audit.

## Evidence

[Lossless source, configuration, logs and observations](language-list-reserve-20261107.json.gz)
contain both harness versions and all raw populations. Executable identities are
retained; binaries and repeated full source archives are private. The public baseline
commit plus the source hash manifest and exact experimental sources reconstruct the
trial. The capsule identity is recorded alongside the handoff.
