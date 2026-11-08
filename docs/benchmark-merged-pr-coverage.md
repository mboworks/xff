# Missing merged PR benchmark inventory

Snapshot: November 8, 2026, using the published benchmark catalog, retained run tree,
GitHub merged PR metadata, and main benchmark workflow metadata captured during the audit.
Entries can become stale as publishers and workflows complete. This inventories PRs merged
on or after September 19, 2026; it is not an audit of earlier history.

A PR is present when its merge commit appears in the merged CI viewer catalog. A catalog
entry may still be hard to find by PR number: existing post-merge entries use `main / <sha>`
as the version label, with the PR only in the links. Local data is excluded from this count.

The snapshot has 252 merged PRs: 157 represented and 95 absent.
The table records observed workflow conclusions, not inferred causes. A canceled run may
have been superseded or canceled for another reason. A failed run requires log investigation.
Older successful reports can exist without the viewer data required by the current catalog.

| PR                                                 | Merged (UTC) | Merge commit   | Observed reason or status                                                                                                                                 |
| -------------------------------------------------- | ------------ | -------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [#1145](https://github.com/mboworks/xff/pull/1145) | 2026-11-08   | `57a3cfb50b02` | Workflow pending ([run](https://github.com/mboworks/xff/actions/runs/42490418192))                                                                        |
| [#1146](https://github.com/mboworks/xff/pull/1146) | 2026-11-08   | `31dc55b400f3` | Workflow in_progress ([run](https://github.com/mboworks/xff/actions/runs/42486013719))                                                                    |
| [#1136](https://github.com/mboworks/xff/pull/1136) | 2026-11-07   | `1f25ba6d4ae0` | Measurement workflow canceled; cause not established ([run](https://github.com/mboworks/xff/actions/runs/42398990564))                                    |
| [#1132](https://github.com/mboworks/xff/pull/1132) | 2026-11-07   | `5d413a35cbec` | Measurement workflow canceled; cause not established ([run](https://github.com/mboworks/xff/actions/runs/42378502027))                                    |
| [#1130](https://github.com/mboworks/xff/pull/1130) | 2026-11-07   | `ae8c5af3eb11` | Measurement workflow canceled; cause not established ([run](https://github.com/mboworks/xff/actions/runs/42367122125))                                    |
| [#1128](https://github.com/mboworks/xff/pull/1128) | 2026-11-07   | `1278c8943a6a` | Measurement workflow canceled; cause not established ([run](https://github.com/mboworks/xff/actions/runs/42354657340))                                    |
| [#1121](https://github.com/mboworks/xff/pull/1121) | 2026-11-06   | `d9bf84b04784` | Measurement workflow canceled; cause not established ([run](https://github.com/mboworks/xff/actions/runs/42265512655))                                    |
| [#1118](https://github.com/mboworks/xff/pull/1118) | 2026-11-06   | `c7bb70cc6f3e` | Measurement workflow canceled; cause not established ([run](https://github.com/mboworks/xff/actions/runs/42234848791))                                    |
| [#1115](https://github.com/mboworks/xff/pull/1115) | 2026-11-06   | `ab909839da7d` | Measurement workflow canceled; cause not established ([run](https://github.com/mboworks/xff/actions/runs/42218900004))                                    |
| [#1113](https://github.com/mboworks/xff/pull/1113) | 2026-11-06   | `5729026a8482` | Measurement workflow canceled; cause not established ([run](https://github.com/mboworks/xff/actions/runs/42200480427))                                    |
| [#1111](https://github.com/mboworks/xff/pull/1111) | 2026-11-06   | `5cade08b6701` | Measurement workflow canceled; cause not established ([run](https://github.com/mboworks/xff/actions/runs/42184472882))                                    |
| [#1106](https://github.com/mboworks/xff/pull/1106) | 2026-11-06   | `e7e42d01e067` | Measurement workflow canceled; cause not established ([run](https://github.com/mboworks/xff/actions/runs/42146475737))                                    |
| [#1099](https://github.com/mboworks/xff/pull/1099) | 2026-11-05   | `76185cb37fa2` | Measurement workflow failed; exact cause not audited ([run](https://github.com/mboworks/xff/actions/runs/41999670147))                                    |
| [#1098](https://github.com/mboworks/xff/pull/1098) | 2026-11-05   | `7c8cd64e02a8` | Measurement workflow failed; exact cause not audited ([run](https://github.com/mboworks/xff/actions/runs/41986760505))                                    |
| [#1097](https://github.com/mboworks/xff/pull/1097) | 2026-11-05   | `910e2a7bbae5` | Measurement workflow canceled; cause not established ([run](https://github.com/mboworks/xff/actions/runs/41977457900))                                    |
| [#1095](https://github.com/mboworks/xff/pull/1095) | 2026-11-05   | `6792b5030de7` | Measurement workflow canceled; cause not established ([run](https://github.com/mboworks/xff/actions/runs/41962227281))                                    |
| [#1092](https://github.com/mboworks/xff/pull/1092) | 2026-11-04   | `a89459ed819a` | Measurement workflow canceled; cause not established ([run](https://github.com/mboworks/xff/actions/runs/41920310179))                                    |
| [#1088](https://github.com/mboworks/xff/pull/1088) | 2026-11-04   | `9bd70fc9b460` | Measurement workflow failed; exact cause not audited ([run](https://github.com/mboworks/xff/actions/runs/41851695947))                                    |
| [#1084](https://github.com/mboworks/xff/pull/1084) | 2026-11-04   | `f06cea15f5b1` | Measurement workflow canceled; cause not established ([run](https://github.com/mboworks/xff/actions/runs/41798644066))                                    |
| [#1082](https://github.com/mboworks/xff/pull/1082) | 2026-11-04   | `6e6b25ffc869` | Measurement workflow failed; exact cause not audited ([run](https://github.com/mboworks/xff/actions/runs/41783344161))                                    |
| [#1080](https://github.com/mboworks/xff/pull/1080) | 2026-11-04   | `669f68a3e263` | Measurement workflow failed; exact cause not audited ([run](https://github.com/mboworks/xff/actions/runs/41771864734))                                    |
| [#1078](https://github.com/mboworks/xff/pull/1078) | 2026-11-03   | `76054b3a9c60` | Measurement workflow failed; exact cause not audited ([run](https://github.com/mboworks/xff/actions/runs/41711975639))                                    |
| [#1073](https://github.com/mboworks/xff/pull/1073) | 2026-11-03   | `6db2de9e49f1` | Measurement workflow canceled; cause not established ([run](https://github.com/mboworks/xff/actions/runs/41628320126))                                    |
| [#1067](https://github.com/mboworks/xff/pull/1067) | 2026-11-03   | `fb726c946063` | Measurement workflow canceled; cause not established ([run](https://github.com/mboworks/xff/actions/runs/41573418793))                                    |
| [#1059](https://github.com/mboworks/xff/pull/1059) | 2026-11-03   | `22702c34a0bc` | Measurement workflow canceled; cause not established ([run](https://github.com/mboworks/xff/actions/runs/41559497818))                                    |
| [#1057](https://github.com/mboworks/xff/pull/1057) | 2026-11-02   | `6bebe940ba36` | Measurement workflow canceled; cause not established ([run](https://github.com/mboworks/xff/actions/runs/41524744291))                                    |
| [#1053](https://github.com/mboworks/xff/pull/1053) | 2026-11-02   | `dc4e70237eff` | Measurement workflow canceled; cause not established ([run](https://github.com/mboworks/xff/actions/runs/41451327523))                                    |
| [#1050](https://github.com/mboworks/xff/pull/1050) | 2026-11-02   | `2d615ea1e447` | Measurement workflow failed; exact cause not audited ([run](https://github.com/mboworks/xff/actions/runs/41425575717))                                    |
| [#1044](https://github.com/mboworks/xff/pull/1044) | 2026-11-02   | `d546fcb3d864` | Measurement workflow canceled; cause not established ([run](https://github.com/mboworks/xff/actions/runs/41378115229))                                    |
| [#1042](https://github.com/mboworks/xff/pull/1042) | 2026-11-02   | `2b21790502fd` | Measurement workflow canceled; cause not established ([run](https://github.com/mboworks/xff/actions/runs/41367091220))                                    |
| [#1027](https://github.com/mboworks/xff/pull/1027) | 2026-10-29   | `d406f4132d06` | Measurement workflow canceled; cause not established ([run](https://github.com/mboworks/xff/actions/runs/40816218296))                                    |
| [#1011](https://github.com/mboworks/xff/pull/1011) | 2026-10-23   | `801087f3e9a8` | Measurement workflow failed; exact cause not audited ([run](https://github.com/mboworks/xff/actions/runs/39886813900))                                    |
| [#1007](https://github.com/mboworks/xff/pull/1007) | 2026-10-14   | `33a01e13d7cd` | Measurement workflow canceled; cause not established ([run](https://github.com/mboworks/xff/actions/runs/38653097649))                                    |
| [#1006](https://github.com/mboworks/xff/pull/1006) | 2026-10-14   | `20095dd53c31` | Measurement workflow failed; exact cause not audited ([run](https://github.com/mboworks/xff/actions/runs/38644322359))                                    |
| [#994](https://github.com/mboworks/xff/pull/994)   | 2026-10-10   | `88d4facd15eb` | Measurement workflow failed; exact cause not audited ([run](https://github.com/mboworks/xff/actions/runs/38049489783))                                    |
| [#993](https://github.com/mboworks/xff/pull/993)   | 2026-10-10   | `dac3747d63ca` | Measurement workflow canceled; cause not established ([run](https://github.com/mboworks/xff/actions/runs/38045263735))                                    |
| [#986](https://github.com/mboworks/xff/pull/986)   | 2026-10-07   | `61ab1ce41300` | Successful run absent from site; artifacts unavailable ([run](https://github.com/mboworks/xff/actions/runs/37689818933))                                  |
| [#976](https://github.com/mboworks/xff/pull/976)   | 2026-10-06   | `73b2cfcd7101` | Measurement workflow canceled; cause not established ([run](https://github.com/mboworks/xff/actions/runs/37393233041))                                    |
| [#979](https://github.com/mboworks/xff/pull/979)   | 2026-10-05   | `378d3540691e` | Measurement workflow canceled; cause not established ([run](https://github.com/mboworks/xff/actions/runs/37388497650))                                    |
| [#973](https://github.com/mboworks/xff/pull/973)   | 2026-10-05   | `dfd5447644f0` | Measurement workflow canceled; cause not established ([run](https://github.com/mboworks/xff/actions/runs/37388437775))                                    |
| [#954](https://github.com/mboworks/xff/pull/954)   | 2026-10-03   | `2817247f59cb` | Measurement workflow failed; exact cause not audited ([run](https://github.com/mboworks/xff/actions/runs/37138060568))                                    |
| [#950](https://github.com/mboworks/xff/pull/950)   | 2026-10-03   | `c262c0a71360` | Measurement workflow failed; exact cause not audited ([run](https://github.com/mboworks/xff/actions/runs/37114542638))                                    |
| [#940](https://github.com/mboworks/xff/pull/940)   | 2026-10-01   | `d2cd1e882428` | Measurement workflow failed; exact cause not audited ([run](https://github.com/mboworks/xff/actions/runs/36926663737))                                    |
| [#939](https://github.com/mboworks/xff/pull/939)   | 2026-10-01   | `cc01d2edbab6` | No main benchmark workflow found                                                                                                                          |
| [#935](https://github.com/mboworks/xff/pull/935)   | 2026-10-01   | `481c32aaf2bd` | Measurement workflow failed; exact cause not audited ([run](https://github.com/mboworks/xff/actions/runs/36831896372))                                    |
| [#937](https://github.com/mboworks/xff/pull/937)   | 2026-10-01   | `c8a5fe9e22c3` | Measurement workflow failed; exact cause not audited ([run](https://github.com/mboworks/xff/actions/runs/36826183983))                                    |
| [#936](https://github.com/mboworks/xff/pull/936)   | 2026-10-01   | `be954012e048` | No main benchmark workflow found                                                                                                                          |
| [#929](https://github.com/mboworks/xff/pull/929)   | 2026-09-27   | `471284c4d8a5` | No main benchmark workflow found                                                                                                                          |
| [#928](https://github.com/mboworks/xff/pull/928)   | 2026-09-27   | `f0dfc3edefc7` | No main benchmark workflow found                                                                                                                          |
| [#922](https://github.com/mboworks/xff/pull/922)   | 2026-09-27   | `5db1240146be` | No main benchmark workflow found                                                                                                                          |
| [#921](https://github.com/mboworks/xff/pull/921)   | 2026-09-27   | `df460ae32bc2` | No main benchmark workflow found                                                                                                                          |
| [#920](https://github.com/mboworks/xff/pull/920)   | 2026-09-27   | `5a327394fbbb` | No main benchmark workflow found                                                                                                                          |
| [#919](https://github.com/mboworks/xff/pull/919)   | 2026-09-27   | `7c8164de5b06` | No main benchmark workflow found                                                                                                                          |
| [#918](https://github.com/mboworks/xff/pull/918)   | 2026-09-27   | `97c5e68ac92d` | No main benchmark workflow found                                                                                                                          |
| [#917](https://github.com/mboworks/xff/pull/917)   | 2026-09-27   | `4ef35c0305de` | No main benchmark workflow found                                                                                                                          |
| [#916](https://github.com/mboworks/xff/pull/916)   | 2026-09-27   | `ce5ee3c40579` | No main benchmark workflow found                                                                                                                          |
| [#914](https://github.com/mboworks/xff/pull/914)   | 2026-09-24   | `9bbee34da11e` | Successful run absent from site; artifacts unavailable ([run](https://github.com/mboworks/xff/actions/runs/36068976975))                                  |
| [#902](https://github.com/mboworks/xff/pull/902)   | 2026-09-22   | `be2f400392dc` | Retained historical report lacks a viewer catalog entry; format/eligibility not audited ([run](https://github.com/mboworks/xff/actions/runs/35677724823)) |
| [#904](https://github.com/mboworks/xff/pull/904)   | 2026-09-21   | `80d75576c32b` | Retained historical report lacks a viewer catalog entry; format/eligibility not audited ([run](https://github.com/mboworks/xff/actions/runs/35667805574)) |
| [#905](https://github.com/mboworks/xff/pull/905)   | 2026-09-21   | `77c00ec8cbea` | Retained historical report lacks a viewer catalog entry; format/eligibility not audited ([run](https://github.com/mboworks/xff/actions/runs/35666152368)) |
| [#903](https://github.com/mboworks/xff/pull/903)   | 2026-09-21   | `9b04999acb79` | Retained historical report lacks a viewer catalog entry; format/eligibility not audited ([run](https://github.com/mboworks/xff/actions/runs/35664420733)) |
| [#906](https://github.com/mboworks/xff/pull/906)   | 2026-09-21   | `79278f51e63c` | Retained historical report lacks a viewer catalog entry; format/eligibility not audited ([run](https://github.com/mboworks/xff/actions/runs/35662475460)) |
| [#901](https://github.com/mboworks/xff/pull/901)   | 2026-09-21   | `f76015ff96f8` | Retained historical report lacks a viewer catalog entry; format/eligibility not audited ([run](https://github.com/mboworks/xff/actions/runs/35653591054)) |
| [#899](https://github.com/mboworks/xff/pull/899)   | 2026-09-21   | `98d31e1616a7` | Retained historical report lacks a viewer catalog entry; format/eligibility not audited ([run](https://github.com/mboworks/xff/actions/runs/35564630217)) |
| [#898](https://github.com/mboworks/xff/pull/898)   | 2026-09-20   | `c0c0b6d39e26` | Retained historical report lacks a viewer catalog entry; format/eligibility not audited ([run](https://github.com/mboworks/xff/actions/runs/35538423416)) |
| [#897](https://github.com/mboworks/xff/pull/897)   | 2026-09-20   | `6e882aea6d2c` | Retained historical report lacks a viewer catalog entry; format/eligibility not audited ([run](https://github.com/mboworks/xff/actions/runs/35536193393)) |
| [#896](https://github.com/mboworks/xff/pull/896)   | 2026-09-20   | `ced00ac473e6` | Retained historical report lacks a viewer catalog entry; format/eligibility not audited ([run](https://github.com/mboworks/xff/actions/runs/35529346711)) |
| [#895](https://github.com/mboworks/xff/pull/895)   | 2026-09-20   | `a185ae12d70d` | Retained historical report lacks a viewer catalog entry; format/eligibility not audited ([run](https://github.com/mboworks/xff/actions/runs/35521619198)) |
| [#894](https://github.com/mboworks/xff/pull/894)   | 2026-09-20   | `42febc0e8d0a` | Retained historical report lacks a viewer catalog entry; format/eligibility not audited ([run](https://github.com/mboworks/xff/actions/runs/35510379808)) |
| [#893](https://github.com/mboworks/xff/pull/893)   | 2026-09-20   | `29c75be40529` | Retained historical report lacks a viewer catalog entry; format/eligibility not audited ([run](https://github.com/mboworks/xff/actions/runs/35503333615)) |
| [#892](https://github.com/mboworks/xff/pull/892)   | 2026-09-20   | `c9b56c2f009e` | Retained historical report lacks a viewer catalog entry; format/eligibility not audited ([run](https://github.com/mboworks/xff/actions/runs/35502743136)) |
| [#891](https://github.com/mboworks/xff/pull/891)   | 2026-09-20   | `a39758eff8ac` | Retained historical report lacks a viewer catalog entry; format/eligibility not audited ([run](https://github.com/mboworks/xff/actions/runs/35499899635)) |
| [#890](https://github.com/mboworks/xff/pull/890)   | 2026-09-19   | `1f568ceecd28` | Retained historical report lacks a viewer catalog entry; format/eligibility not audited ([run](https://github.com/mboworks/xff/actions/runs/35474269925)) |
| [#888](https://github.com/mboworks/xff/pull/888)   | 2026-09-19   | `b7d28411a9cc` | Retained historical report lacks a viewer catalog entry; format/eligibility not audited ([run](https://github.com/mboworks/xff/actions/runs/35473674929)) |
| [#889](https://github.com/mboworks/xff/pull/889)   | 2026-09-19   | `1945c9ec032f` | No main benchmark workflow found                                                                                                                          |
| [#887](https://github.com/mboworks/xff/pull/887)   | 2026-09-19   | `0bcb461dbb08` | Retained historical report lacks a viewer catalog entry; format/eligibility not audited ([run](https://github.com/mboworks/xff/actions/runs/35470455554)) |
| [#886](https://github.com/mboworks/xff/pull/886)   | 2026-09-19   | `2bd76d6e682f` | No main benchmark workflow found                                                                                                                          |
| [#885](https://github.com/mboworks/xff/pull/885)   | 2026-09-19   | `2448257a8d4d` | No main benchmark workflow found                                                                                                                          |
| [#884](https://github.com/mboworks/xff/pull/884)   | 2026-09-19   | `4fc594246067` | No main benchmark workflow found                                                                                                                          |
| [#883](https://github.com/mboworks/xff/pull/883)   | 2026-09-19   | `b1df434503a3` | No main benchmark workflow found                                                                                                                          |
| [#882](https://github.com/mboworks/xff/pull/882)   | 2026-09-19   | `9282e3d1da7c` | No main benchmark workflow found                                                                                                                          |
| [#881](https://github.com/mboworks/xff/pull/881)   | 2026-09-19   | `990045bf2d3b` | No main benchmark workflow found                                                                                                                          |
| [#880](https://github.com/mboworks/xff/pull/880)   | 2026-09-19   | `c8b35dfa8249` | No main benchmark workflow found                                                                                                                          |
| [#879](https://github.com/mboworks/xff/pull/879)   | 2026-09-19   | `7f48d0d888cb` | No main benchmark workflow found                                                                                                                          |
| [#878](https://github.com/mboworks/xff/pull/878)   | 2026-09-19   | `889c652ec8cf` | No main benchmark workflow found                                                                                                                          |
| [#877](https://github.com/mboworks/xff/pull/877)   | 2026-09-19   | `99b78c8f1bad` | No main benchmark workflow found                                                                                                                          |
| [#876](https://github.com/mboworks/xff/pull/876)   | 2026-09-19   | `e1cc1f72605e` | No main benchmark workflow found                                                                                                                          |
| [#868](https://github.com/mboworks/xff/pull/868)   | 2026-09-19   | `c092e8245ff8` | No main benchmark workflow found                                                                                                                          |
| [#875](https://github.com/mboworks/xff/pull/875)   | 2026-09-19   | `9197abc259ba` | No main benchmark workflow found                                                                                                                          |
| [#874](https://github.com/mboworks/xff/pull/874)   | 2026-09-19   | `a61f320479c9` | No main benchmark workflow found                                                                                                                          |
| [#873](https://github.com/mboworks/xff/pull/873)   | 2026-09-19   | `13ba8e9474be` | No main benchmark workflow found                                                                                                                          |
| [#872](https://github.com/mboworks/xff/pull/872)   | 2026-09-19   | `ecc9b1f7868a` | No main benchmark workflow found                                                                                                                          |
| [#871](https://github.com/mboworks/xff/pull/871)   | 2026-09-19   | `05140bb49bf5` | No main benchmark workflow found                                                                                                                          |
| [#870](https://github.com/mboworks/xff/pull/870)   | 2026-09-19   | `c7aa7ebea9b4` | No main benchmark workflow found                                                                                                                          |
| [#869](https://github.com/mboworks/xff/pull/869)   | 2026-09-19   | `41e18f2748ff` | No main benchmark workflow found                                                                                                                          |

## What the audit shows

- PR #1143 is represented on Linux and macOS; it is labeled by main and commit, not by PR number.
- The publisher reconciles available successful artifacts, but cannot recreate a run that
  never produced a final report or whose artifact expired.
- Main benchmark runs previously used a single pending concurrency slot; #1138 changed
  the workflow to a 100-entry queue. That does not by itself retry failed or canceled runs,
  and an exhausted queue can still lose requests. Individual cancellations were not audited.
- At the time of the audit the backfill planner selected commits already present in retained
  history, and every job had to succeed before any result became visible. Neither behavior
  provides a durable missing-PR queue.
- Merged PR #1147 provides explicit revisions for backfill, but that remains manual and does not
  establish automatic completeness.

## Proposed prevention

1. Maintain a durable inventory keyed by merged PR number, merge commit, and platform.
   Discover expected entries from trusted merged PR metadata, independently of workflow
   events and retained reports. Store merge and tree identity, discovery time, state,
   attempt identities, errors and retry time. Reconcile this inventory periodically.
2. For each platform, select a compatible retained measurement whose measured commit or
   verified tree matches the merged PR. Accept PR measurements only after proving the
   measured tree matches the merge result; otherwise schedule a new measurement. Keep
   actual measurement and source identities and show backfill provenance in the viewer.
3. Expose a PR-labeled entry in the viewer for every merge. Show pending, running, failed
   and retrying states instead of silently omitting unmeasured PRs. Do not plot a point
   without a completed, validated measurement. A PR may be visible while coverage is
   incomplete on one or both platforms.
4. Reconcile completed artifacts independently per PR and platform. Retry missing,
   failed, canceled, expired and queue-rejected work with bounded concurrency and
   backoff. Make retries idempotent and retain successful platforms even if another fails.
   No workflow trigger or finite GitHub queue may be the only record of required work.
5. Limit remeasurement cost by reusing a trusted, compatible existing measurement when
   it proves the same merge tree and benchmark contract; otherwise run one targeted
   job for each missing platform. Do not equate different trees or measurement contracts.
6. Publish catalog and status from the durable inventory under the shared site lock,
   coalesce redundant publishing requests, and report backlog age, failures, artifact
   expiry and stalled progress. After a successful publisher, confirm every eligible
   retained result is indexed; retry publication independently of measurement.

## Recovery command

After PR #1147 has deployed the targeted revision input, run:

```sh
bazel run //tools:benchmark_recovery -- --repository mboworks/xff
```

The command compares merged PRs with the current CI viewer catalog and reports missing
platforms, active main measurements and retained artifacts awaiting publication. The
default starting date is September 19, 2026; use `--since=YYYY-MM-DD` to select another
inclusive date, or `--pr=986` to inspect one PR. It reads GitHub through the authenticated
`gh` CLI. An available artifact still needs validation and successful publication.

Add `--dispatch-next` to dispatch up to four of the oldest PRs requiring new measurements.
Use `--batch-size=1` through `--batch-size=8` to change the bound. It starts one targeted
backfill campaign for both platforms and refuses if another backfill is active or queued.
Run the inventory again after that campaign and its publisher finish;
repeat until the desired range is covered. This is an operator-driven recovery step,
not the persistent automatic scheduler described above. A concurrent dispatch after
the active-campaign check can still race; avoid starting multiple operators at once.

The publisher can retain each complete validated platform from a failed campaign, so a
failed platform does not discard another platform's results. Recovery selects the union
of missing platforms in a batch; `--pr=NUMBER` or `--batch-size=1` allows a platform-specific
retry without repeating a retained platform. The command does not cancel a pending publisher or
automatically retry a failed run.

## Decisions and limits

- Decide the supported historical starting point and platforms before scheduling a
  complete backfill. The table includes older PRs whose original comparison contract or
  reference tools may differ from the current contract.
- A successful backfill is a new measurement with its own host and method, not a
  reconstruction of the original run. Preserve that distinction and do not mix incompatible
  series silently.
- The implementation still needs a persistent scheduler;
  merely increasing the concurrency queue, adding retries to one workflow, or adding
  PR labels cannot guarantee eventual coverage.

## Continuation after the current campaigns

The main performance objective is to close the gap to equivalent plain `find` and `rg`
commands. Coverage recovery supports that work by restoring a trustworthy history.

First let the [#914/#986 campaign](https://github.com/mboworks/xff/actions/runs/42498912253)
and the queued [#1143 campaign](https://github.com/mboworks/xff/actions/runs/42499244750)
finish, and merge PR #1148. Then:

1. Inspect campaign results and publication, including failures and partial platforms.
   Confirm each recovered PR is selectable by PR number in the viewer; workflow success
   or a retained report directory alone does not establish that.
2. Recompute this inventory from the live catalog. Recover the remaining missing PRs in
   bounded campaigns, preserving existing valid measurements and documenting failures
   that need build or measurement fixes. Do not dispatch another campaign while one is
   active or queued.
3. Resume concrete optimization of basic discovery and content search, comparing equivalent
   output and semantics with `find` and `rg` on Linux and macOS. Use production binaries,
   verify their identities, and keep paired measurements with reference tools. The local
   investigation requests the full 1/3/10 worker grid; the existing CI backfill contract
   remains 1/3. Keep those contracts distinguishable.
4. Develop a few representative combined workloads alongside the simple comparisons.
   Select them for real use and matching semantics, rather than for a favorable result.
   Establish correctness, repeatability, and sensitivity before adding them to the viewer.
5. Freeze and identify the additional workload contract before scheduling its historical
   backfill. Preserve the existing simple comparisons and make compatibility explicit;
   a new task must not silently appear as if it had been measured in earlier reports.

## Historical Linux linker compatibility

The November 8 campaign reached compilation but failed before any Linux measurement:

- [PR #986 Linux job](https://github.com/mboworks/xff/actions/runs/42498912253/job/128787591927):
  historical `ld.lld` could not load `libicui18n.so.70`.
- [PR #914 Linux job](https://github.com/mboworks/xff/actions/runs/42498912253/job/128787591953):
  historical `ld.lld` could not load `libxml2.so.2`.

Both jobs used `ubuntu-latest`, which resolved to Ubuntu 26.04. New historical Linux
campaigns select `ubuntu-22.04` and explicitly install `libicu70` and `libxml2`. This
preserves each revision's compiler and LTO configuration. The selected runner remains
in the campaign plan, and validation still accepts older plans using `ubuntu-latest`.
The ordinary main benchmark workflow keeps its existing runner selection.

The backfill dispatch also accepts `platforms=linux` or `platforms=macos`, so failed Linux
jobs can be remeasured in a new campaign without repeating completed macOS work. Earlier
plans without a platform selection still require both platforms, and the new selection
is part of the immutable campaign identity. For this campaign, retry the two original
revisions with `platforms=linux` after the fixed workflow is deployed on main.

This pin needs a replacement before [GitHub retires Ubuntu 22.04 on April 17, 2027](https://github.com/actions/runner-images/issues/14254).
A replacement must provide the historical build dependencies, preserve build provenance,
and record the measurement environment. Replacing historical compilers would change
what the backfill measures.

## Historical command timeouts

The macOS #986 job completed successfully. The macOS #914 job failed at the 100,000-file
broad content-search warm-up when an invocation exceeded the fixed 60-second limit.
New CI backfill plans use a 300-second command limit, recorded in the campaign, batch,
and measurement contract. Earlier plans retain the implicit 60-second limit. Other
comparison callers keep their current default. Timeout failures still terminate the
whole worker process group and report the failed command; no timed-out sample is kept.

After deployment, retry Linux #914/#986 together and macOS #914 separately. Preserve
the completed macOS #986 report from the original campaign. The longer command limit
has not yet been exercised on that historical macOS workload; the overall job deadline
still bounds total measurement time.
