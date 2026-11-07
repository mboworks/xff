# Archive B-tree: reconcile the preserved historical decision

Keep the private archive node index as `std::map`. The earlier O19k trial replaced only that
container with `absl::btree_map`, using its default transparent string comparator and a direct
Bazel dependency. Its construction and some flat/chain workloads improved, but most wide/deep
directory queries slowed down on both hosts. Those costs prevent adoption of that implementation.

The TODO and analysis still described O19k as unmeasured after this experiment had finished.
This report corrects that tracking gap and publishes the retained observations. It runs no new
benchmark, changes no runtime and does not present old observations as current-main qualification.

## Source and lifetime audit

The preserved candidate and clean parent worktrees identify base
`45f4c5e65bfb7de5d0facf8bcbdca1d28320ae4a`. Their patch touches only the archive header and its
direct dependency. `Index` is the only mutation phase: inserted node references are consumed before
ancestor insertion, and an existing ancestor's count is updated immediately before breaking.
Parent keys own their text. Immutable read iterators stay within their calls; metadata, listings,
symlink text and deferred member sources return owned values or own their retained source/key.
No escaping node reference was found. Relocation of large nodes and short string keys is legal
under that contract, but its performance is workload-dependent.

The default `btree_map<std::string, Node>` comparator matters: pinned Abseil supplies its transparent
three-way string comparison adapter. This was not a trial accidentally forcing `std::less<>`.
There were no borrowed-key changes, ancestor hints, interning, new workers or decoder policies.

## Preserved protocol

There are three sessions on each host. Session orders are parent/candidate/parent,
candidate/parent/candidate, then parent/candidate/parent. Each invocation has nine raw repetitions;
the fastest seven `real_time` values are averaged per case. When a role appears twice in a
session, its two retained means are combined with a geometric mean. Divide candidate by parent
within each session, then take the median of those three ratios per case. Negative elapsed-time
change means faster.

The 28 construction cases include parsing, index construction and destruction. The 88 directory
cases separately measure ordinary listings and complete metadata listings across flat, wide,
deep and chain shapes, including 10 through 100,000 members where the fixture permits. This is
single-threaded public archive API work, not native CLI throughput or multiworker scaling.
The Linux runner pins CPU 4, in its available ten-CPU set; Mac reports eighteen available logical
CPUs and has no affinity enforcement. Driver paths and JSON context retain the exact invocations.

Each host's corrected `-v2` population retains all eighteen unique JSON runs and eighteen logs:
nine construction and nine directory invocations. Every case has nine raw observations and no
benchmark error. Initial populations overwrote one of the repeated roles' output files; preserve
them as exploratory data only. Do not combine them with the corrected population or call their
remaining files complete controls. The corrected driver fixes that recording defect; it is not
an attempt to select only favorable observations.

## Recomputed historical results

Each cohort applies its statistic to the per-case median ratios described above. Faster-case
counts include every case. Geometric means alone conceal the slower majority of wide/deep work.

| Workload              | Cases | Mac median | Mac geometric mean | Mac faster | Linux median | Linux geometric mean | Linux faster |
| --------------------- | ----: | ---------: | -----------------: | ---------: | -----------: | -------------------: | -----------: |
| Construction          |    28 |     -7.06% |             -6.09% |      25/28 |       -0.66% |               -0.96% |        18/28 |
| All directory queries |    88 |     +1.92% |             -6.58% |      39/88 |       +2.60% |               -2.73% |        41/88 |
| Flat                  |    10 |     -8.49% |             -6.27% |       8/10 |       -5.10% |               -4.83% |         7/10 |
| Wide                  |    30 |     +3.71% |             -2.30% |      10/30 |       +6.20% |               +1.79% |        10/30 |
| Deep                  |    30 |     +2.35% |             -5.70% |      10/30 |       +7.87% |               +3.78% |        10/30 |
| Chain                 |    18 |     -3.13% |            -14.80% |      11/18 |       -8.29% |              -18.06% |        14/18 |

Examples include Linux deep/one-directory/1,000 members at about +18.8% and wide/one-directory/
1,000 at +14.8%. Mac deep/ten-directories/100,000 is about +8.6%. Strong chain/root cells improve
about 35-43%; they do not cancel those costs. The all-cell CSV and three session ratios remain
in the capsule, so a reader can inspect individual signs rather than infer them from a surface
plot or an aggregate geometric mean.

## Provenance and verification limits

The original decision note reports optimized archive filesystem tests passing on both hosts,
and benchmark image growth of about 39 KiB. That
note is retained verbatim. It did not freeze a complete contemporaneous compiler-action/source/
image manifest. Later worktree/image identities collected during this reconciliation are labeled
**later captures**; they cannot prove the exact bytes used in the earlier timed invocations.
Raw JSON contexts do identify their executable paths, hosts, dates and release benchmark library.
No full product binary size, full target suite, sanitizer matrix, RSS or current-head CI claim is
made for this historical trial.

The current base `d8cf61cccd4b0efed5058bf6fa45228e47162b48` differs from the old parent: archive
ownership/index-return handling, directory scanning and release/tool linking configuration have
changed. The preserved diffs make that explicit. Do not transfer the old percentages to current
production or add them to later gains. They are sufficient reason to keep the old policy
unselected, not proof that every possible new B-tree/index design is slower.

Before discovering the completed records, a new private worktree was created and a mixed
short/long-key duplicate-inode regression was drafted. Only formatting and source transfer ran;
no candidate compile, test or timing started. That draft stays private and unqualified. Reconcile
existing evidence before spending another measurement cycle. A future directory-specific child
index or range representation is a distinct proposal, with its own lifetime, retained-byte and
construction/query controls, rather than another unchanged global-container swap.

## Download and reproduce the analysis

[Lossless historical capsule](archive-btree-historical-decision-20261107.json.gz): 155 individually hashed records,
43,505,320 decoded bytes; 2,205,832 compressed bytes.
SHA-256: `e609682737ebd09c27b5760e3dd811e55f7e54fbc9c1b852c7e21f8dcf983df0`.

Restore each JSON `records` entry according to `encoding`, then verify `bytes` and `sha256`.
It retains both initial and corrected populations, every raw log, the original decision note,
runner/analysis programs, source patch and later source/configuration receipts. Executable images
stay private; later identities are expressly not a replacement for missing original receipts.

`analyze_o19k_bracketed.py` reproduces the original analysis after adjusting its two input paths.
`prepare-historical-report.py` additionally checks all corrected case/repetition counts, preserves
session ratios and emits `all-case-comparisons.csv`. Neither command rebuilds or times a binary.
Keep the exact original runtime policy rejected; require a substantive new design and fresh
both-host qualification before considering adoption.
