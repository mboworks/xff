# Missing merged PR benchmark inventory

Snapshot: November 9, 2026, 01:02 UTC. Scope: PRs merged directly into `main` on or after
September 19, 2026. This is a dated inventory; publishing and active measurements may change it.

A platform is covered only when the exact merge commit is selectable in the published merged-CI
catalog. Local and extended-contract datasets are excluded. A retained artifact awaiting publication
is recorded separately from a catalog entry.

## Current coverage

- 237 directly merged PRs: 162 published on both Linux and macOS; 75 absent on both.
- Of the 75, PR #1150 has validated reports for both platforms awaiting publication.
- The remaining 74 require recovery according to the read-only planner. Two already have an active
  targeted campaign: #868 and #876 have successful Linux jobs and macOS jobs still running in
  [run 42538554174](https://github.com/mboworks/xff/actions/runs/42538554174). Do not repeat those jobs.
- PRs #914 and #986 are now published on both platforms. Their Linux recovery completed successfully
  in [run 42534282665](https://github.com/mboworks/xff/actions/runs/42534282665). PR #1143 is also present
  on both platforms. Live browser selection verification is recorded below.

The November 8 total of 252 included PRs merged into other development branches. This refresh
counts only direct merges into `main`; merges into development branches are excluded from this audit.
The present metadata contains 256 merges across all base branches, of which 19 target other branches.

## Missing PRs

Every row currently lacks both Linux and macOS catalog entries. Workflow conclusions are observed
states, not established root causes. A canceled run may have been canceled manually or superseded;
a failed run requires its logs to establish the cause. No validated report was discovered for a
row unless the table explicitly identifies one.

| PR                                                 | Merged (UTC) | Merge commit   | Observed state                                                                                                                |
| -------------------------------------------------- | ------------ | -------------- | ----------------------------------------------------------------------------------------------------------------------------- |
| [#1150](https://github.com/mboworks/xff/pull/1150) | 2026-11-08   | `52950f937ad3` | Validated Linux/macOS reports awaiting publication                                                                            |
| [#1136](https://github.com/mboworks/xff/pull/1136) | 2026-11-07   | `1f25ba6d4ae0` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/42398990564)) |
| [#1132](https://github.com/mboworks/xff/pull/1132) | 2026-11-07   | `5d413a35cbec` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/42378502027)) |
| [#1130](https://github.com/mboworks/xff/pull/1130) | 2026-11-07   | `ae8c5af3eb11` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/42367122125)) |
| [#1128](https://github.com/mboworks/xff/pull/1128) | 2026-11-07   | `1278c8943a6a` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/42354657340)) |
| [#1121](https://github.com/mboworks/xff/pull/1121) | 2026-11-06   | `d9bf84b04784` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/42265512655)) |
| [#1118](https://github.com/mboworks/xff/pull/1118) | 2026-11-06   | `c7bb70cc6f3e` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/42234848791)) |
| [#1115](https://github.com/mboworks/xff/pull/1115) | 2026-11-06   | `ab909839da7d` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/42218900004)) |
| [#1113](https://github.com/mboworks/xff/pull/1113) | 2026-11-06   | `5729026a8482` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/42200480427)) |
| [#1111](https://github.com/mboworks/xff/pull/1111) | 2026-11-06   | `5cade08b6701` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/42184472882)) |
| [#1106](https://github.com/mboworks/xff/pull/1106) | 2026-11-06   | `e7e42d01e067` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/42146475737)) |
| [#1099](https://github.com/mboworks/xff/pull/1099) | 2026-11-05   | `76185cb37fa2` | Main run failure; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41999670147))   |
| [#1098](https://github.com/mboworks/xff/pull/1098) | 2026-11-05   | `7c8cd64e02a8` | Main run failure; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41986760505))   |
| [#1097](https://github.com/mboworks/xff/pull/1097) | 2026-11-05   | `910e2a7bbae5` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41977457900)) |
| [#1095](https://github.com/mboworks/xff/pull/1095) | 2026-11-05   | `6792b5030de7` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41962227281)) |
| [#1092](https://github.com/mboworks/xff/pull/1092) | 2026-11-04   | `a89459ed819a` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41920310179)) |
| [#1088](https://github.com/mboworks/xff/pull/1088) | 2026-11-04   | `9bd70fc9b460` | Main run failure; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41851695947))   |
| [#1084](https://github.com/mboworks/xff/pull/1084) | 2026-11-04   | `f06cea15f5b1` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41798644066)) |
| [#1082](https://github.com/mboworks/xff/pull/1082) | 2026-11-04   | `6e6b25ffc869` | Main run failure; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41783344161))   |
| [#1080](https://github.com/mboworks/xff/pull/1080) | 2026-11-04   | `669f68a3e263` | Main run failure; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41771864734))   |
| [#1078](https://github.com/mboworks/xff/pull/1078) | 2026-11-03   | `76054b3a9c60` | Main run failure; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41711975639))   |
| [#1073](https://github.com/mboworks/xff/pull/1073) | 2026-11-03   | `6db2de9e49f1` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41628320126)) |
| [#1067](https://github.com/mboworks/xff/pull/1067) | 2026-11-03   | `fb726c946063` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41573418793)) |
| [#1059](https://github.com/mboworks/xff/pull/1059) | 2026-11-03   | `22702c34a0bc` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41559497818)) |
| [#1057](https://github.com/mboworks/xff/pull/1057) | 2026-11-02   | `6bebe940ba36` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41524744291)) |
| [#1053](https://github.com/mboworks/xff/pull/1053) | 2026-11-02   | `dc4e70237eff` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41451327523)) |
| [#1050](https://github.com/mboworks/xff/pull/1050) | 2026-11-02   | `2d615ea1e447` | Main run failure; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41425575717))   |
| [#1044](https://github.com/mboworks/xff/pull/1044) | 2026-11-02   | `d546fcb3d864` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41378115229)) |
| [#1042](https://github.com/mboworks/xff/pull/1042) | 2026-11-02   | `2b21790502fd` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41367091220)) |
| [#1027](https://github.com/mboworks/xff/pull/1027) | 2026-10-29   | `d406f4132d06` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/40816218296)) |
| [#1011](https://github.com/mboworks/xff/pull/1011) | 2026-10-23   | `801087f3e9a8` | Main run failure; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/39886813900))   |
| [#1007](https://github.com/mboworks/xff/pull/1007) | 2026-10-14   | `33a01e13d7cd` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/38653097649)) |
| [#1006](https://github.com/mboworks/xff/pull/1006) | 2026-10-14   | `20095dd53c31` | Main run failure; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/38644322359))   |
| [#994](https://github.com/mboworks/xff/pull/994)   | 2026-10-10   | `88d4facd15eb` | Main run failure; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/38049489783))   |
| [#993](https://github.com/mboworks/xff/pull/993)   | 2026-10-10   | `dac3747d63ca` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/38045263735)) |
| [#976](https://github.com/mboworks/xff/pull/976)   | 2026-10-06   | `73b2cfcd7101` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/37393233041)) |
| [#979](https://github.com/mboworks/xff/pull/979)   | 2026-10-05   | `378d3540691e` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/37388497650)) |
| [#973](https://github.com/mboworks/xff/pull/973)   | 2026-10-05   | `dfd5447644f0` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/37388437775)) |
| [#954](https://github.com/mboworks/xff/pull/954)   | 2026-10-03   | `2817247f59cb` | Main run failure; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/37138060568))   |
| [#950](https://github.com/mboworks/xff/pull/950)   | 2026-10-03   | `c262c0a71360` | Main run failure; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/37114542638))   |
| [#939](https://github.com/mboworks/xff/pull/939)   | 2026-10-01   | `cc01d2edbab6` | No main-push benchmark run found; no retained report discovered                                                               |
| [#935](https://github.com/mboworks/xff/pull/935)   | 2026-10-01   | `481c32aaf2bd` | Main run failure; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/36831896372))   |
| [#936](https://github.com/mboworks/xff/pull/936)   | 2026-10-01   | `be954012e048` | No main-push benchmark run found; no retained report discovered                                                               |
| [#928](https://github.com/mboworks/xff/pull/928)   | 2026-09-27   | `f0dfc3edefc7` | No main-push benchmark run found; no retained report discovered                                                               |
| [#916](https://github.com/mboworks/xff/pull/916)   | 2026-09-27   | `ce5ee3c40579` | No main-push benchmark run found; no retained report discovered                                                               |
| [#902](https://github.com/mboworks/xff/pull/902)   | 2026-09-22   | `be2f400392dc` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35677724823))   |
| [#904](https://github.com/mboworks/xff/pull/904)   | 2026-09-21   | `80d75576c32b` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35667805574))   |
| [#905](https://github.com/mboworks/xff/pull/905)   | 2026-09-21   | `77c00ec8cbea` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35666152368))   |
| [#903](https://github.com/mboworks/xff/pull/903)   | 2026-09-21   | `9b04999acb79` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35664420733))   |
| [#906](https://github.com/mboworks/xff/pull/906)   | 2026-09-21   | `79278f51e63c` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35662475460))   |
| [#901](https://github.com/mboworks/xff/pull/901)   | 2026-09-21   | `f76015ff96f8` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35653591054))   |
| [#899](https://github.com/mboworks/xff/pull/899)   | 2026-09-21   | `98d31e1616a7` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35564630217))   |
| [#898](https://github.com/mboworks/xff/pull/898)   | 2026-09-20   | `c0c0b6d39e26` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35538423416))   |
| [#897](https://github.com/mboworks/xff/pull/897)   | 2026-09-20   | `6e882aea6d2c` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35536193393))   |
| [#896](https://github.com/mboworks/xff/pull/896)   | 2026-09-20   | `ced00ac473e6` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35529346711))   |
| [#895](https://github.com/mboworks/xff/pull/895)   | 2026-09-20   | `a185ae12d70d` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35521619198))   |
| [#894](https://github.com/mboworks/xff/pull/894)   | 2026-09-20   | `42febc0e8d0a` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35510379808))   |
| [#893](https://github.com/mboworks/xff/pull/893)   | 2026-09-20   | `29c75be40529` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35503333615))   |
| [#892](https://github.com/mboworks/xff/pull/892)   | 2026-09-20   | `c9b56c2f009e` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35502743136))   |
| [#891](https://github.com/mboworks/xff/pull/891)   | 2026-09-20   | `a39758eff8ac` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35499899635))   |
| [#890](https://github.com/mboworks/xff/pull/890)   | 2026-09-19   | `1f568ceecd28` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35474269925))   |
| [#888](https://github.com/mboworks/xff/pull/888)   | 2026-09-19   | `b7d28411a9cc` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35473674929))   |
| [#887](https://github.com/mboworks/xff/pull/887)   | 2026-09-19   | `0bcb461dbb08` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35470455554))   |
| [#886](https://github.com/mboworks/xff/pull/886)   | 2026-09-19   | `2bd76d6e682f` | No main-push benchmark run found; no retained report discovered                                                               |
| [#885](https://github.com/mboworks/xff/pull/885)   | 2026-09-19   | `2448257a8d4d` | No main-push benchmark run found; no retained report discovered                                                               |
| [#884](https://github.com/mboworks/xff/pull/884)   | 2026-09-19   | `4fc594246067` | No main-push benchmark run found; no retained report discovered                                                               |
| [#883](https://github.com/mboworks/xff/pull/883)   | 2026-09-19   | `b1df434503a3` | No main-push benchmark run found; no retained report discovered                                                               |
| [#882](https://github.com/mboworks/xff/pull/882)   | 2026-09-19   | `9282e3d1da7c` | No main-push benchmark run found; no retained report discovered                                                               |
| [#881](https://github.com/mboworks/xff/pull/881)   | 2026-09-19   | `990045bf2d3b` | No main-push benchmark run found; no retained report discovered                                                               |
| [#880](https://github.com/mboworks/xff/pull/880)   | 2026-09-19   | `c8b35dfa8249` | No main-push benchmark run found; no retained report discovered                                                               |
| [#879](https://github.com/mboworks/xff/pull/879)   | 2026-09-19   | `7f48d0d888cb` | No main-push benchmark run found; no retained report discovered                                                               |
| [#878](https://github.com/mboworks/xff/pull/878)   | 2026-09-19   | `889c652ec8cf` | No main-push benchmark run found; no retained report discovered                                                               |
| [#877](https://github.com/mboworks/xff/pull/877)   | 2026-09-19   | `99b78c8f1bad` | No main-push benchmark run found; no retained report discovered                                                               |
| [#876](https://github.com/mboworks/xff/pull/876)   | 2026-09-19   | `e1cc1f72605e` | Linux backfill succeeded; macOS measuring ([campaign](https://github.com/mboworks/xff/actions/runs/42538554174))              |
| [#868](https://github.com/mboworks/xff/pull/868)   | 2026-09-19   | `c092e8245ff8` | Linux backfill succeeded; macOS measuring ([campaign](https://github.com/mboworks/xff/actions/runs/42538554174))              |

## Reproduction and retained evidence

Refresh the inventory without starting measurements:

```sh
bazel run //tools:benchmark_recovery -- --repository mboworks/xff
```

The read-only planner reports missing platforms, live main measurements and validated retained
reports awaiting publication. Active backfill campaigns require an additional workflow-status
check; their incomplete plans are not yet included in the planner's completed-campaign inventory.

This snapshot retains the complete responses locally as:

- `/private/tmp/xff-missing-merged-prs-20261109-refresh.json` and its diagnostic log;
- `/private/tmp/xff-coverage-pulls-20261109.json`: all 1,139 closed PRs across 12 API pages;
- `/private/tmp/xff-coverage-runs-20261109.json`: all 225 main benchmark runs across three pages;
- `/private/tmp/xff-published-catalog-live-20261109.json`: 590 published entries, catalog Git blob
  `854b5bf4cf46515b5d0ca70dd0fa6f83de384233`.

These are observation snapshots, not pending instructions to dispatch a new recovery campaign.

## Preservation and remaining work

- Preserve successful backfills and let the active #868/#876 jobs finish. Reconciliation publishes
  valid configurations independently; a failed sibling must not force successful work to rerun.
- Retry only a genuinely missing or failed configuration. PR #1147 added targeted revision inputs;
  #1148 added the reusable inventory/recovery command. #1149/#1150 fixed historical Linux runtime
  preparation. The additional planner fixes in #1153 avoid repeating available platforms across
  different missing-platform sets and propagate API observation failures.
- The publisher coalesces pending requests and reconciles available artifacts under the shared site
  lock, with a six-hour scheduled reconciliation. This republishes existing evidence; it does not
  automatically measure every missing merge.
- A durable per-PR/platform retry queue, pending/failed viewer entries and automatic completeness
  enforcement remain proposed work. A finite GitHub concurrency queue alone cannot prove coverage.
- Historical backfill entries currently report `Not verified` with a workflow-provenance mismatch.
  Their workflow driver commit differs from the measured historical commit. Validating that relation
  requires the retained campaign/build provenance; do not bypass the check or label them verified.

## Browser verification

Chromium selected PRs #914, #986 and #1143 in both CI datasets on the
[published viewer](https://mboworks.github.io/xff/benchmarks/) at 01:14 UTC on November 9.
All six selections loaded their actual chart payloads and displayed the expected merge commit,
with no page errors. Their binary state remains `Not verified`, matching the conservative
provenance limitation above. The retained check is
`/private/tmp/xff-recovered-browser-20261109.json` and its companion screenshot.
