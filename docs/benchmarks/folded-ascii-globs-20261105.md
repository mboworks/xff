<!-- SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Prepared folded ASCII globs

Reuse prepared simple-glob shapes for case-folded ASCII name and path matching, while retaining
libc for complex patterns, multibyte inputs and unsupported flags. In confirmation, broad 100k-file
native-name cases improve about 5-8% on macOS; explicit `-iname` improves about 17-18%. Long-name
Mac cases improve about 28-51%. Linux folded literal/prefix/suffix cases improve modestly, commonly
about 1-3%; native-case controls remain mostly unchanged. These are native pipeline results, not
universal per-predicate gains. Keep the smaller costs and parent-copy drift visible below.

## Implementation and boundaries

The existing prepared operand retains the glob shape and positions in the expression-owned string.
The selected change adds no larger operand, allocation, shared owner, lock, timer, cached global
locale, flag or dependency. Compile-time exact/folded helpers implement ASCII literal, prefix,
suffix, contains and prefix/suffix matching. ASCII equality first compares unchanged bytes, then
uses byte folding in a single-byte locale or wide folding in a multibyte locale, matching native
libc behavior for validated ASCII. Read the active thread locale on demand; a prepared plan must
not retain a process-global locale assumption across `uselocale` changes.

The first NUL retains native termination semantics. Non-ASCII input or patterns, escaped patterns,
character classes, question marks and unsupported flags retain native `fnmatch`. The selected Linux
policy also keeps folded contains patterns in libc without an extra ASCII scan: the generic search
lost 4-6% on two Linux contains workloads despite helping macOS. Both platforms retain the prepared
literal/prefix/suffix/prefix-suffix path. Exact matching keeps the existing byte operations.

Only prepared `-name`/`-iname`, `-path`/`-ipath` and wholename aliases change execution. The scalar
reference evaluator and separate fnmatch regex backend remain native. Registry semantics, filesystem
case decisions, worker eligibility, traversal, content reads and safety enforcement are unchanged.
This does not implement a general terminated-input API or alter public string-view ownership.

## Correctness and locale coverage

The committed ASCII matrix compares scalar, prepared, iterative and moved prepared plans against
native `fnmatch`, across the name/path/wholename variants, both fold decisions, simple/complex
patterns, missing operands, embedded NULs and non-terminated views. The dedicated Unicode file
covers accented Latin, dotted/dotless I, long s, Kelvin sign and invalid bytes; ordinary functional
fixtures stay ASCII. A second matrix changes the active thread locale after preparing a plan and
compares native/scalar/prepared/iterative results. POSIX locale handles are narrowly annotated ABI
handles with scope cleanup restoring the preceding thread locale before freeing the selected one.

Available locale cases were `C`, `POSIX`, `C.UTF-8`, `en_US.UTF-8`, `tr_TR.UTF-8` and
`tr_TR.ISO8859-9` on this Mac; Linux supplied `C`, `POSIX`, `C.UTF-8`, `C.utf8` and `en_US.UTF-8`.
Unavailable locales are skipped individually and each test requires at least one available locale.
The tests record the actual available names rather than claiming Turkish coverage on Linux.

## Measurement method and provenance

The parent is the scoped native stat-path runtime from #1096; #1097 adds only tests and decision
records. Exact parent and candidate source snapshots, build logs, binary sizes/SHA-256, raw samples,
launcher identity and scripts are retained. The fixtures reuse the reproducible broad/deep/long-name
trees from the [eager native paths report](eager-native-paths-20261104.md). Long components contain
180 characters. The first screen covers 38 cases: broad/deep 10/1k/100k native-name and explicit
`-iname` with one/three workers, exact-case controls, long names, all-match and complex patterns.
A second screen covers six simple-glob shapes at 100k files with one/three workers. Selected-policy
confirmation retains 22 cases and compares parent, identical parent copy, first policy and selected
policy. No first-policy observations are discarded.

Each matrix has three sessions, participant order rotates for every repetition, and each case has
one discarded warmup and nine measured runs, retaining the fastest seven. Each invocation checks
exit status and complete stdout against the parent outside the native timing interval. There are
3,420 + 1,080 + 2,640 = 7,140 timed invocations per host, including warmups. Linux pins one/three
allowed logical CPUs; Mac CPU pinning is not claimed. No competing same-host agent build/test/lint
ran during timing. The native launcher measures process execution; these are cached native
filesystem pipelines rather than isolated in-memory matching kernels. Preparation, traversal,
metadata, workers and output remain part of the measured command.

Tables show medians of within-session candidate/parent ratios, not a ratio of the displayed median
milliseconds. Below one is faster. Parent-copy drift remains visible and is not silently subtracted.
The archived exact invocation and output hashes distinguish case/worker/backend effects.

## First-policy shape screen

This screen uses generic folded contains search on both hosts.

| Host  | Case                              | Parent ms | Candidate ms | Candidate / parent | Parent-copy / parent |
| :---- | :-------------------------------- | --------: | -----------: | -----------------: | -------------------: |
| macOS | shape/literal/100000/1            |    70.834 |       67.655 |             0.9594 |               1.0070 |
| macOS | shape/prefix/100000/1             |    77.068 |       75.342 |             0.9785 |               0.9998 |
| macOS | shape/contains/100000/1           |    77.703 |       75.404 |             0.9716 |               1.0095 |
| macOS | shape/contains-miss/100000/1      |    81.229 |       73.333 |             0.9028 |               1.0034 |
| macOS | shape/prefix-suffix/100000/1      |    83.280 |       69.172 |             0.8417 |               0.9932 |
| macOS | shape/contains-extension/100000/1 |    83.727 |       73.874 |             0.8848 |               0.9992 |
| macOS | shape/literal/100000/3            |    71.625 |       68.618 |             0.9557 |               0.9971 |
| macOS | shape/prefix/100000/3             |    77.348 |       75.722 |             0.9790 |               1.0015 |
| macOS | shape/contains/100000/3           |    78.209 |       75.670 |             0.9689 |               1.0012 |
| macOS | shape/contains-miss/100000/3      |    81.440 |       74.386 |             0.9093 |               1.0032 |
| macOS | shape/prefix-suffix/100000/3      |    82.043 |       68.529 |             0.8397 |               1.0004 |
| macOS | shape/contains-extension/100000/3 |    83.337 |       73.638 |             0.8836 |               0.9953 |
| Linux | shape/literal/100000/1            |    39.806 |       39.317 |             0.9877 |               1.0015 |
| Linux | shape/prefix/100000/1             |    42.806 |       42.161 |             0.9849 |               0.9967 |
| Linux | shape/contains/100000/1           |    43.597 |       42.171 |             0.9673 |               1.0035 |
| Linux | shape/contains-miss/100000/1      |    38.823 |       40.836 |             1.0576 |               0.9965 |
| Linux | shape/prefix-suffix/100000/1      |    39.534 |       38.605 |             0.9751 |               0.9989 |
| Linux | shape/contains-extension/100000/1 |    39.434 |       41.076 |             1.0416 |               1.0011 |
| Linux | shape/literal/100000/3            |    39.128 |       38.854 |             0.9888 |               0.9942 |
| Linux | shape/prefix/100000/3             |    43.312 |       42.600 |             0.9898 |               0.9968 |
| Linux | shape/contains/100000/3           |    44.040 |       42.449 |             0.9667 |               1.0007 |
| Linux | shape/contains-miss/100000/3      |    39.911 |       42.065 |             1.0606 |               0.9977 |
| Linux | shape/prefix-suffix/100000/3      |    40.302 |       39.339 |             0.9728 |               0.9985 |
| Linux | shape/contains-extension/100000/3 |    40.265 |       41.783 |             1.0377 |               0.9974 |

Linux contains-miss costs 5.76%/6.06% with one/three workers, while copies gain 0.35%/0.23%.
Contains-extension costs 4.16%/3.77% with nearly neutral copies. Those are material repeated costs;
retain libc for folded contains on Linux. Mac contains-miss gains about 9-10%, and the corresponding
extension case gains about 12%, so its prepared folded search remains selected.

## Selected-policy confirmation

| Host  | Case                                | Parent ms | Candidate ms | Candidate / parent | Parent-copy / parent |
| :---- | :---------------------------------- | --------: | -----------: | -----------------: | -------------------: |
| macOS | native-name/broad/100000/1/name-txt |   262.331 |      248.459 |             0.9478 |               1.0094 |
| macOS | iname/broad/100000/1/name-txt       |    83.739 |       69.412 |             0.8308 |               0.9966 |
| macOS | native-name/broad/100000/3/name-txt |   174.126 |      159.976 |             0.9187 |               1.0007 |
| macOS | iname/broad/100000/3/name-txt       |    84.272 |       69.063 |             0.8222 |               0.9981 |
| macOS | exact/broad/10/1/name-txt           |     4.485 |        4.579 |             1.0041 |               1.0093 |
| macOS | exact/broad/100000/3/name-txt       |    69.562 |       69.655 |             1.0058 |               1.0001 |
| macOS | long/100000/1/native-name           |   546.746 |      392.994 |             0.7168 |               1.0011 |
| macOS | long/100000/1/iname                 |   302.993 |      148.772 |             0.4881 |               0.9951 |
| macOS | long/100000/3/native-name           |   413.909 |      257.561 |             0.6226 |               0.9993 |
| macOS | long/100000/3/iname                 |   304.967 |      149.306 |             0.4889 |               1.0014 |
| macOS | shape/literal/100000/1              |    71.595 |       69.133 |             0.9620 |               0.9998 |
| macOS | shape/prefix/100000/1               |    77.750 |       76.068 |             0.9744 |               1.0068 |
| macOS | shape/contains/100000/1             |    78.660 |       76.628 |             0.9678 |               1.0011 |
| macOS | shape/contains-miss/100000/1        |    82.144 |       74.048 |             0.9030 |               0.9971 |
| macOS | shape/prefix-suffix/100000/1        |    82.061 |       69.737 |             0.8528 |               0.9992 |
| macOS | shape/contains-extension/100000/1   |    83.426 |       74.362 |             0.8842 |               0.9927 |
| macOS | shape/literal/100000/3              |    71.520 |       68.520 |             0.9621 |               1.0004 |
| macOS | shape/prefix/100000/3               |    77.852 |       76.851 |             0.9821 |               1.0048 |
| macOS | shape/contains/100000/3             |    78.900 |       76.246 |             0.9672 |               0.9918 |
| macOS | shape/contains-miss/100000/3        |    81.742 |       74.424 |             0.9105 |               1.0005 |
| macOS | shape/prefix-suffix/100000/3        |    82.473 |       69.802 |             0.8454 |               1.0096 |
| macOS | shape/contains-extension/100000/3   |    83.532 |       74.303 |             0.8896 |               1.0009 |
| Linux | native-name/broad/100000/1/name-txt |    38.817 |       38.727 |             1.0027 |               1.0006 |
| Linux | iname/broad/100000/1/name-txt       |    39.834 |       39.146 |             0.9792 |               0.9952 |
| Linux | native-name/broad/100000/3/name-txt |    39.192 |       39.214 |             1.0023 |               0.9985 |
| Linux | iname/broad/100000/3/name-txt       |    40.047 |       39.384 |             0.9807 |               0.9989 |
| Linux | exact/broad/10/1/name-txt           |     0.825 |        0.820 |             1.0021 |               0.9940 |
| Linux | exact/broad/100000/3/name-txt       |    39.540 |       39.427 |             0.9988 |               0.9968 |
| Linux | long/100000/1/native-name           |    63.211 |       62.888 |             1.0004 |               1.0011 |
| Linux | long/100000/1/iname                 |    64.473 |       62.765 |             0.9744 |               1.0007 |
| Linux | long/100000/3/native-name           |    62.793 |       62.854 |             0.9998 |               0.9994 |
| Linux | long/100000/3/iname                 |    64.796 |       62.950 |             0.9719 |               0.9990 |
| Linux | shape/literal/100000/1              |    38.747 |       38.299 |             0.9965 |               0.9971 |
| Linux | shape/prefix/100000/1               |    42.557 |       42.135 |             0.9901 |               1.0002 |
| Linux | shape/contains/100000/1             |    43.556 |       43.086 |             0.9920 |               0.9993 |
| Linux | shape/contains-miss/100000/1        |    38.962 |       39.376 |             0.9944 |               0.9965 |
| Linux | shape/prefix-suffix/100000/1        |    40.051 |       39.233 |             0.9787 |               1.0048 |
| Linux | shape/contains-extension/100000/1   |    40.033 |       39.740 |             0.9974 |               0.9990 |
| Linux | shape/literal/100000/3              |    39.494 |       38.979 |             0.9870 |               0.9959 |
| Linux | shape/prefix/100000/3               |    42.802 |       42.438 |             0.9885 |               1.0009 |
| Linux | shape/contains/100000/3             |    43.667 |       43.203 |             0.9899 |               0.9988 |
| Linux | shape/contains-miss/100000/3        |    39.159 |       39.686 |             1.0130 |               0.9964 |
| Linux | shape/prefix-suffix/100000/3        |    40.522 |       39.597 |             0.9804 |               0.9999 |
| Linux | shape/contains-extension/100000/3   |    40.429 |       40.357 |             1.0003 |               0.9972 |

Mac exact 100k/three-worker control costs 0.58% while its copy costs 0.01%; the 10-file control
costs 0.41% while its copy costs 0.93%. Linux contains-miss/three workers costs 1.30% while its
copy gains 0.36%, despite selecting the libc path. Do not hide these costs or call all cells faster.
The first native screen also retains Mac 10-file exact control cost of about 2.87% with 2.42%
copy drift. Small startup/control variation limits claims about sub-percent differences.

Linux native-case paths generally remain case-sensitive through its existing uniform-case
capability, so folded matching improves explicit `-iname` rather than those native controls. On
macOS, insensitive native matching reaches the optimized folded path, explaining its larger
native-name gains. Requested worker counts do not imply this change introduces new parallelism.

## Qualification and binary footprint

The selected source passes all 316 release targets on both hosts, including all nine built-in
extensions and required Linux FUSE integration. Eight focused evaluator/expression/run/parallel/case
targets pass ASan on both hosts and MSan/TSan on Linux. Actual scoped clang-tidy ran on the changed
C++ translation units; selected runtime changes receive a subsequent actual evaluate.cc check.
Normal commit/push hooks remain required for publication; complete current-head CI remains the
merge gate. Earlier partial preflight/build attempts are retained as diagnostics, not successful
qualification of the completed candidate.

Eight focused Linux coverage targets cover 51/53 added executable evaluate.cc lines (96.23%) and
46/50 branches (92.00%) using a conservative raw diff calculation before canonical exclusions.
The uncovered lines are the exhaustive-switch unreachable/end lines; raw observations and LCOV
are retained. This is focused patch evidence, not a whole-program or extension coverage claim.
The complete CI coverage gate still applies.

| Host  | Parent bytes | Selected bytes | Change |
| :---- | -----------: | -------------: | -----: |
| macOS |    8,250,504 |      8,250,616 |   +112 |
| Linux |   18,401,048 |     18,404,960 | +3,912 |

The final selected release executables are byte-identical to the frozen binaries used in selected
confirmation on both hosts. The strict ELF/Mach-O comparison scripts and original signature
checks are retained; no executable was edited to manufacture equality. Sources remained unchanged
through selected timing, qualification and runtime proof.

## Decision and retained work

Select the prepared folded path with the measured per-platform contains decision. No public
threshold or compatibility alias is added. Bulk metadata remains the larger host syscall candidate;
worker startup policies remain separately unselected. Terminated-input interfaces and more general
compiled glob matching need independent semantics, coverage and measurement rather than enlarging
this change.

The accompanying lossless `folded-ascii-globs-20261105.json.gz` retains both hosts' source stages,
all three matrices, first-policy/selected binary identities, all original observations, reproduction
scripts, locale/build/test/lint logs, focused LCOV and final runtime proofs. Frozen executables,
large native trees and stdout golden files remain local; drivers, fixture references and full stdout
hashes describe reproduction. No public binary or fixture dump is needed for this evidence record.
