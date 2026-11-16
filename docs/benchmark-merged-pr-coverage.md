# Missing merged PR benchmark inventory

Snapshot: November 9, 2026, 09:37 UTC. Scope: PRs merged directly into main on or after
September 19, 2026, by that snapshot. Later merges, including #1155, are outside this dated
inventory. Refresh before selecting another backfill campaign.

A platform is covered only when the exact merge commit appears in the published merged-CI
catalog. Local and extended-contract datasets do not fill gaps in this contract. A validated
report awaiting publication is recorded separately from a selectable catalog entry.

## Current coverage at the snapshot

- 240 directly merged PRs: 171 published on both platforms; 69 missing from both viewer datasets.
- PR #1154 has validated Linux and macOS reports awaiting publication. It needs publication,
  not another measurement run.
- The other 68 missing PRs lack validated retained reports at the snapshot. Four of them,
  #881-884, already have an active bounded backfill campaign; do not dispatch them again.
- PRs #877-880 are now published and browser-verified on both platforms. All eight selections
  loaded their exact merge commit and chart data at 09:35 UTC, without page errors.
- PR #1151 is also published on both platforms. PRs #868/#876 were browser-verified at 03:58 UTC;
  #914/#986/#1143 were verified at 01:14 UTC.

The earlier 05:10 snapshot counted 239 direct merges, 166 covered and 73 missing. Since then,
#1154 merged while #1151 and #877-880 were published, producing 240/171/69. The November 8
total of 252 included PRs targeting other development branches; those are outside this scope.

## Missing PRs at the snapshot

Every row lacks both published catalog entries. Historical workflow conclusions are observations,
not established root causes: a successful workflow does not prove that usable reports remain,
and a cancellation needs its own logs to establish why. Available reports and active backfills
are identified separately.

| PR                                                 | Merged (UTC) | Merge commit   | Observed state                                                                                                                                                    |
| -------------------------------------------------- | ------------ | -------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [#1154](https://github.com/mboworks/xff/pull/1154) | 2026-11-09   | `ad3be8a12e05` | Both validated reports retained; awaiting publication ([main run](https://github.com/mboworks/xff/actions/runs/42583205104))                                      |
| [#1136](https://github.com/mboworks/xff/pull/1136) | 2026-11-07   | `1f25ba6d4ae0` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/42398990564))                                     |
| [#1132](https://github.com/mboworks/xff/pull/1132) | 2026-11-07   | `5d413a35cbec` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/42378502027))                                     |
| [#1130](https://github.com/mboworks/xff/pull/1130) | 2026-11-07   | `ae8c5af3eb11` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/42367122125))                                     |
| [#1128](https://github.com/mboworks/xff/pull/1128) | 2026-11-07   | `1278c8943a6a` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/42354657340))                                     |
| [#1121](https://github.com/mboworks/xff/pull/1121) | 2026-11-06   | `d9bf84b04784` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/42265512655))                                     |
| [#1118](https://github.com/mboworks/xff/pull/1118) | 2026-11-06   | `c7bb70cc6f3e` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/42234848791))                                     |
| [#1115](https://github.com/mboworks/xff/pull/1115) | 2026-11-06   | `ab909839da7d` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/42218900004))                                     |
| [#1113](https://github.com/mboworks/xff/pull/1113) | 2026-11-06   | `5729026a8482` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/42200480427))                                     |
| [#1111](https://github.com/mboworks/xff/pull/1111) | 2026-11-06   | `5cade08b6701` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/42184472882))                                     |
| [#1106](https://github.com/mboworks/xff/pull/1106) | 2026-11-06   | `e7e42d01e067` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/42146475737))                                     |
| [#1099](https://github.com/mboworks/xff/pull/1099) | 2026-11-05   | `76185cb37fa2` | Main run failure; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41999670147))                                       |
| [#1098](https://github.com/mboworks/xff/pull/1098) | 2026-11-05   | `7c8cd64e02a8` | Main run failure; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41986760505))                                       |
| [#1097](https://github.com/mboworks/xff/pull/1097) | 2026-11-05   | `910e2a7bbae5` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41977457900))                                     |
| [#1095](https://github.com/mboworks/xff/pull/1095) | 2026-11-05   | `6792b5030de7` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41962227281))                                     |
| [#1092](https://github.com/mboworks/xff/pull/1092) | 2026-11-04   | `a89459ed819a` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41920310179))                                     |
| [#1088](https://github.com/mboworks/xff/pull/1088) | 2026-11-04   | `9bd70fc9b460` | Main run failure; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41851695947))                                       |
| [#1084](https://github.com/mboworks/xff/pull/1084) | 2026-11-04   | `f06cea15f5b1` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41798644066))                                     |
| [#1082](https://github.com/mboworks/xff/pull/1082) | 2026-11-04   | `6e6b25ffc869` | Main run failure; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41783344161))                                       |
| [#1080](https://github.com/mboworks/xff/pull/1080) | 2026-11-04   | `669f68a3e263` | Main run failure; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41771864734))                                       |
| [#1078](https://github.com/mboworks/xff/pull/1078) | 2026-11-03   | `76054b3a9c60` | Main run failure; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41711975639))                                       |
| [#1073](https://github.com/mboworks/xff/pull/1073) | 2026-11-03   | `6db2de9e49f1` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41628320126))                                     |
| [#1067](https://github.com/mboworks/xff/pull/1067) | 2026-11-03   | `fb726c946063` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41573418793))                                     |
| [#1059](https://github.com/mboworks/xff/pull/1059) | 2026-11-03   | `22702c34a0bc` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41559497818))                                     |
| [#1057](https://github.com/mboworks/xff/pull/1057) | 2026-11-02   | `6bebe940ba36` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41524744291))                                     |
| [#1053](https://github.com/mboworks/xff/pull/1053) | 2026-11-02   | `dc4e70237eff` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41451327523))                                     |
| [#1050](https://github.com/mboworks/xff/pull/1050) | 2026-11-02   | `2d615ea1e447` | Main run failure; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41425575717))                                       |
| [#1044](https://github.com/mboworks/xff/pull/1044) | 2026-11-02   | `d546fcb3d864` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41378115229))                                     |
| [#1042](https://github.com/mboworks/xff/pull/1042) | 2026-11-02   | `2b21790502fd` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/41367091220))                                     |
| [#1027](https://github.com/mboworks/xff/pull/1027) | 2026-10-29   | `d406f4132d06` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/40816218296))                                     |
| [#1011](https://github.com/mboworks/xff/pull/1011) | 2026-10-23   | `801087f3e9a8` | Main run failure; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/39886813900))                                       |
| [#1007](https://github.com/mboworks/xff/pull/1007) | 2026-10-14   | `33a01e13d7cd` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/38653097649))                                     |
| [#1006](https://github.com/mboworks/xff/pull/1006) | 2026-10-14   | `20095dd53c31` | Main run failure; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/38644322359))                                       |
| [#994](https://github.com/mboworks/xff/pull/994)   | 2026-10-10   | `88d4facd15eb` | Main run failure; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/38049489783))                                       |
| [#993](https://github.com/mboworks/xff/pull/993)   | 2026-10-10   | `dac3747d63ca` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/38045263735))                                     |
| [#979](https://github.com/mboworks/xff/pull/979)   | 2026-10-05   | `378d3540691e` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/37388497650))                                     |
| [#976](https://github.com/mboworks/xff/pull/976)   | 2026-10-06   | `73b2cfcd7101` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/37393233041))                                     |
| [#973](https://github.com/mboworks/xff/pull/973)   | 2026-10-05   | `dfd5447644f0` | Main run cancelled; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/37388437775))                                     |
| [#954](https://github.com/mboworks/xff/pull/954)   | 2026-10-03   | `2817247f59cb` | Main run failure; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/37138060568))                                       |
| [#950](https://github.com/mboworks/xff/pull/950)   | 2026-10-03   | `c262c0a71360` | Main run failure; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/37114542638))                                       |
| [#939](https://github.com/mboworks/xff/pull/939)   | 2026-10-01   | `cc01d2edbab6` | No main-push benchmark run found; no validated retained report discovered                                                                                         |
| [#936](https://github.com/mboworks/xff/pull/936)   | 2026-10-01   | `be954012e048` | No main-push benchmark run found; no validated retained report discovered                                                                                         |
| [#935](https://github.com/mboworks/xff/pull/935)   | 2026-10-01   | `481c32aaf2bd` | Main run failure; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/36831896372))                                       |
| [#928](https://github.com/mboworks/xff/pull/928)   | 2026-09-27   | `f0dfc3edefc7` | No main-push benchmark run found; no validated retained report discovered                                                                                         |
| [#916](https://github.com/mboworks/xff/pull/916)   | 2026-09-27   | `ce5ee3c40579` | No main-push benchmark run found; no validated retained report discovered                                                                                         |
| [#906](https://github.com/mboworks/xff/pull/906)   | 2026-09-21   | `79278f51e63c` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35662475460))                                       |
| [#905](https://github.com/mboworks/xff/pull/905)   | 2026-09-21   | `77c00ec8cbea` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35666152368))                                       |
| [#904](https://github.com/mboworks/xff/pull/904)   | 2026-09-21   | `80d75576c32b` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35667805574))                                       |
| [#903](https://github.com/mboworks/xff/pull/903)   | 2026-09-21   | `9b04999acb79` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35664420733))                                       |
| [#902](https://github.com/mboworks/xff/pull/902)   | 2026-09-22   | `be2f400392dc` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35677724823))                                       |
| [#901](https://github.com/mboworks/xff/pull/901)   | 2026-09-21   | `f76015ff96f8` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35653591054))                                       |
| [#899](https://github.com/mboworks/xff/pull/899)   | 2026-09-21   | `98d31e1616a7` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35564630217))                                       |
| [#898](https://github.com/mboworks/xff/pull/898)   | 2026-09-20   | `c0c0b6d39e26` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35538423416))                                       |
| [#897](https://github.com/mboworks/xff/pull/897)   | 2026-09-20   | `6e882aea6d2c` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35536193393))                                       |
| [#896](https://github.com/mboworks/xff/pull/896)   | 2026-09-20   | `ced00ac473e6` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35529346711))                                       |
| [#895](https://github.com/mboworks/xff/pull/895)   | 2026-09-20   | `a185ae12d70d` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35521619198))                                       |
| [#894](https://github.com/mboworks/xff/pull/894)   | 2026-09-20   | `42febc0e8d0a` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35510379808))                                       |
| [#893](https://github.com/mboworks/xff/pull/893)   | 2026-09-20   | `29c75be40529` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35503333615))                                       |
| [#892](https://github.com/mboworks/xff/pull/892)   | 2026-09-20   | `c9b56c2f009e` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35502743136))                                       |
| [#891](https://github.com/mboworks/xff/pull/891)   | 2026-09-20   | `a39758eff8ac` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35499899635))                                       |
| [#890](https://github.com/mboworks/xff/pull/890)   | 2026-09-19   | `1f568ceecd28` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35474269925))                                       |
| [#888](https://github.com/mboworks/xff/pull/888)   | 2026-09-19   | `b7d28411a9cc` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35473674929))                                       |
| [#887](https://github.com/mboworks/xff/pull/887)   | 2026-09-19   | `0bcb461dbb08` | Main run success; no validated retained report discovered ([run](https://github.com/mboworks/xff/actions/runs/35470455554))                                       |
| [#886](https://github.com/mboworks/xff/pull/886)   | 2026-09-19   | `2bd76d6e682f` | No main-push benchmark run found; no validated retained report discovered                                                                                         |
| [#885](https://github.com/mboworks/xff/pull/885)   | 2026-09-19   | `2448257a8d4d` | No main-push benchmark run found; no validated retained report discovered                                                                                         |
| [#884](https://github.com/mboworks/xff/pull/884)   | 2026-09-19   | `4fc594246067` | No main-push benchmark run found; no validated retained report discovered; already in [active backfill](https://github.com/mboworks/xff/actions/runs/42589939294) |
| [#883](https://github.com/mboworks/xff/pull/883)   | 2026-09-19   | `b1df434503a3` | No main-push benchmark run found; no validated retained report discovered; already in [active backfill](https://github.com/mboworks/xff/actions/runs/42589939294) |
| [#882](https://github.com/mboworks/xff/pull/882)   | 2026-09-19   | `9282e3d1da7c` | No main-push benchmark run found; no validated retained report discovered; already in [active backfill](https://github.com/mboworks/xff/actions/runs/42589939294) |
| [#881](https://github.com/mboworks/xff/pull/881)   | 2026-09-19   | `990045bf2d3b` | No main-push benchmark run found; no validated retained report discovered; already in [active backfill](https://github.com/mboworks/xff/actions/runs/42589939294) |

## Completed backfill and browser publication

[Campaign 42566817872](https://github.com/mboworks/xff/actions/runs/42566817872) measured
PRs #877-880 on Linux and macOS. All eight attempt-1 jobs failed on HTTP 504 dependency
downloads before compiling or measuring. The exact failure logs and download preflights remain.
After downloads recovered, one failed-only rerun was requested. Attempt 2 completed successfully
on every configuration. No successful configuration needs another measurement run.

The first browser check at 08:47 UTC found none of these eight selections. The subsequent
09:35 UTC check loaded all eight exact commits and landscape payloads, without page errors.
Both observations remain. Successful measurement was verified separately from publication;
the publication delay did not require repeating the measurements.

## Active bounded campaign

After a fresh check confirmed that no backfill was active, exactly PRs #881-884 were dispatched
for both platforms at 08:59 UTC in
[campaign 42589939294](https://github.com/mboworks/xff/actions/runs/42589939294).
The 09:37 inventory still observes this original campaign as active. PRs #877-880 were excluded
because their successful artifacts were already available. The dispatch receipt retains the
selected commits, inventory hash, live campaign preflight, exact command, timestamp and result.
Observe the original campaign to completion; preserve successes and retry failed configurations
only after investigating terminal failures. Its dispatch is not proof of published viewer entries.

## Reproduction and retained evidence

Refresh the read-only planner with:

    bazel run //tools:benchmark_recovery -- --repository mboworks/xff

The planner distinguishes missing platforms, active measurements and validated reports awaiting
publication. Check active backfill workflows separately. PR #1153 contains the planner fix that
subtracts available platforms before grouping requests, and propagates API observation errors;
it remains unmerged at this snapshot. Every dispatch must preserve available configurations.

The complete 09:37 inventory, request index and raw API responses are under
`/home/marcus/Documents/xff-coverage-refresh-post-backfill-20261109/post-browser-877-880/`
on drogon. The audit rejects non-GET API requests and identifies source revision
`d7dee8aca41fea4c9d287800946a546377e5a85e`. The 94-response index records content hashes.
Document generation independently checked all three main-run response pages against that index.
The complete missing rows and artifact classifications come from the retained inventory.

Earlier snapshots remain separate under the same root's `snapshot/` and
`post-campaign-42566817872/` directories. Failed attempt-1 logs and the rerun receipt remain
under `campaign-42566817872/`. The next dispatch is recorded in
`dispatch-881-884-receipt.json`. Browser evidence for #877-880 is retained as
`xff-recovered-877-880-browser-20261109.{json,png,log}` for 08:47 and
`xff-recovered-877-880-after-publisher-20261109.{json,png,log}` for 09:35.

## Preservation and remaining work

- Preserve all successful reports; retry only missing or failed configurations after observing
  the original run's terminal state and investigating its failure. Do not cancel active work
  merely because observation timed out.
- The coalescing publisher and six-hour scheduled reconciliation can publish retained reports.
  They do not automatically measure every missing merge. A durable retry queue, pending/failed
  viewer entries and automatic completeness enforcement remain proposed work.
- Historical entries conservatively show Not verified when workflow-driver provenance differs
  from the measured source. Do not retrofit unsupported binary verification.
- The 130-revision extended-production-v1 history is a separate contract and does not fill these
  original merged-CI gaps. Its continuous integration and live publication remain separate work.

### Earlier historical Linux compatibility and timeouts

The original #914/#986 Linux campaign failed on missing historical linker libraries. Selecting
Ubuntu 22.04 then allowed compilation but produced binaries that required a newer glibc
(`GLIBC_ABI_DT_RELR`). The deployed follow-up uses Ubuntu 24.04 and obtains `libicu70` from signed
Jammy package indexes without changing the runner's configured sources. Runtime receipts record
package and loaded-library hashes, OS, glibc and preparer identity. Historical compiler/source/LTO
configurations remain fixed. See
[the retained runtime smoke proof](benchmarks/historical-runtime-smoke-20261108.json).
The successful [Linux retry](https://github.com/mboworks/xff/actions/runs/42534282665) has now been
published; the obsolete Ubuntu 22.04 campaign requires no further retry.

The original macOS #914 warmup exceeded a 60-second command limit. New backfill plans record a
300-second command limit; [its macOS retry](https://github.com/mboworks/xff/actions/runs/42519073200)
succeeded. Preserve that report and the original successful macOS #986 report. Timeouts terminate
the worker process group, and no timed-out sample is retained. These historical failures are
resolved for those two PRs; the #877-880 attempt-1 HTTP 504 failures occurred before that runtime was exercised.

## Browser verification

The published viewer loaded #914, #986 and #1143 in both CI datasets at 01:14 UTC, and #868 and
#876 on both at 03:58 UTC. Those checks retained exact merge commits, actual loaded landscape
payloads, conservative binary states, screenshots and absence of page errors.

The 09:35 check for #877-880 retained all eight successful selections. Each loaded its own
merge commit and landscape in both `linux / x86_64` and `macos / arm64`. Binary state remained
Not verified; publication does not manufacture provenance. The catalog had 570 entries, and
the browser reported neither missing selections nor page errors. Future recovered entries
require the same actual selection and payload checks.
