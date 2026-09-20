# Coverage policy

[`coverage_policy.json`](../coverage_policy.json) is the single source of truth for coverage scope,
ratings, and CI enforcement. The short report, retained report page, generated LCOV colours and
legend, and the coverage job all consume it. A presentation-only threshold is not permitted.

## Ratings and enforcement

Every metric has two boundaries:

| Rating | Range                                  |
| ------ | -------------------------------------- |
| low    | Below `minimum`                        |
| medium | At least `minimum`, but below `target` |
| high   | At least `target`                      |

The `enforce` value is either `medium` or `high`. CI fails when a measurement is below its enforced
rating. Thus medium enforcement accepts medium and high; high enforcement accepts only high. The
overview reports `GOOD` when every metric is high, `OK` when enforcement passes but at least one
metric remains medium, and `BAD: L/F/B` when the named metrics do not meet their enforcement rating.
Percentage cells retain their low, medium, or high colour, so a medium cell explains either an `OK`
category or a failure when that metric enforces high. Only `BAD` fails CI.

Start a metric at medium enforcement while improving it toward its target. After it reliably reaches
high, changing `enforce` to `high` prevents a regression into medium. Do not collapse `minimum` and
`target` merely to strengthen enforcement; the two boundaries continue to describe the useful
three-band rating.

## Category overrides

The top-level policy applies to `overall` and is inherited by every overview category. A category
may override any subset of `minimum`, `target`, and `enforce`, independently per metric. This allows
both stricter enforcement for mature modules and lower initial limits for new or unusually
constrained modules. An override that lowers either boundary or relaxes enforcement must include a
non-empty `reason`; policy validation rejects an unexplained weakening.

Category policies affect the overview rating and the CI result. The detailed LCOV source report can
represent only one policy, so its colours and policy matrix deliberately use the global boundaries
and enforcement. Every overview rate cell includes a compact low/medium/high policy strip; its
outlined band is the enforced rating. Category exceptions therefore remain visible in the table as
well as its downloadable JSON data.

The `patch` object uses the same `minimum`, `target`, and `enforce` vocabulary for changed coverable
lines and branches. Functions are not attributed to changed lines and are therefore not patch-gated.

## Published data

Each retained report links its detailed LCOV source view, full `coverage-summary.json`, and workflow
`coverage-meta.json`. The coverage index also links the summary JSON directly. The summary records
measurements and the fully resolved minimum, target, and enforcement values for every overview row,
so consumers do not need to reimplement inheritance.

## Retained report ordering

The coverage index shows one row per PR or release, with no standalone `main` rows.
PRs and releases share one newest-first list:
PRs use GitHub's actual `merged_at` timestamp, and annotated tags use their tagger timestamp.
Lightweight tags contain no creation timestamp, so their tagged commit's committer timestamp
is the explicit fallback. Neither PR numbers, version numbers, CI completion time, nor a
commit's position on main controls this order. A nested PR uses its own merge time.

Open PRs and reports without a reference timestamp follow, newest CI run creation first.
PRs closed without merging are excluded from the overview; their direct report URLs and
immutable run-history snapshots remain available. Reopening a PR restores its overview row.
Every Test or Release completion and manual publication refreshes GitHub PR state and regenerates
the indexes. Closing or reopening a PR does not trigger a separate publication: row visibility and
merge ordering may remain stale until the next publication. Reopened PRs normally get a new CI run,
and merges trigger main CI. A PR closed without merging may remain visible longer; its measurements
are retained. Dispatch the publisher without `source_run_id` for an immediate metadata-only refresh
that does not download or replace coverage reports. No `pull_request_target` policy exception is needed.
Report ingestion, metadata refresh, and release-site publishing share the serialized
`coverage-pages` concurrency queue. Each run reads the retained branch and current PR state only
after acquiring that queue; they cannot overwrite each other's updates concurrently.
Only trusted main-branch code executes for PR lifecycle events.

Each publication refreshes these timestamps for all retained reports. The `reference_time`
metadata controls presentation only; workflow creation, run ID, and attempt still determine
which report may replace an older report for the same target. Stored `history` ancestry is
provenance, not a sorting key.

## Individual run retention and aggregation

Each published report is retained under `coverage/runs/RUN_ID/ATTEMPT/`, including its LCOV
source pages, summary JSON, and original source-run metadata. The coverage overview links a
pre-merge and post-merge results index, with at most one result per phase per PR and one per release.
Repeated runs and attempts are not listed. The per-PR/main/release URLs continue to show their latest report; replacing
one does not replace its archived snapshot. Existing identified reports are archived before
replacement. Incoming reports are also archived when a newer run already owns the latest URL. Legacy reports without a real run ID
remain available at their existing URL but are not assigned an invented run identity.

An aggregation PR does not combine or relabel the coverage measurements of its constituent PRs.
Each retains its original tested commit, workflow run, and detailed report. Each constituent
PR is ordered by its own actual merge time, even when merged into an aggregation branch.
The recorded main-integration ancestry remains available as provenance, including verified
membership in squashed aggregations, but does not affect the overview's timestamp order.

The overview initially labels a PR result **pre-merge**. A published main report replaces that
row as **post-merge** only when its tested commit exactly equals the PR's merge commit.
A later main commit does not stand in for that result. If the merge build fails or never publishes,
the pre-merge result stays visible. PRs without a pre-merge report can still acquire a post-merge row.
The separate results index shows both phases when available; retries select the latest report
within each phase. A late pre-merge publication never replaces an available post-merge result.
The publisher stores the PR-to-merge-commit mapping in `coverage/pull-requests.json` and resolves
both current reports and retained snapshots, so out-of-order main publications are attributed
correctly. Existing snapshots are not rewritten or deleted. Aggregated constituent PRs keep
pre-merge results unless a main run actually tested their own exact merge commit; the aggregate's
post-merge result belongs to the aggregation PR alone.

Coverage publication requires the source workflow's `coverage` job to succeed, not every job
in that workflow. An unrelated sanitizer or lint failure does not suppress a valid coverage
report. A failed, cancelled, or skipped coverage job leaves the previous report intact while
PR metadata still refreshes. In the two-phase view, post-merge precedes pre-merge within each PR;
PR groups retain their merge-time ordering even when a pre-merge retry finishes later.

To recover an unpublished report without rerunning tests, dispatch `Publish coverage` with
`source_run_id` set to the original completed Test or Release workflow run ID. For example:

```sh
gh workflow run coverage_pages.yml -f source_run_id=35460479026
```

The source run's coverage job must have succeeded and its report artifact must still be available.
The publisher uses the source run's original commit, timestamps, run ID, and attempt; it does not
assign the recovery dispatch's identity to the measurements. Older reports are archived without
replacing newer target reports. Recovery shares the normal publication queue. Omitting the input
continues to refresh metadata only. Expired artifacts cannot be recovered by this command.
