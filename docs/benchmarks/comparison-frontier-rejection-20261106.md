# Completed-directory comparison frontier prototype rejection

This prototype compared paired directory entries as both sides completed a directory. It was
correct on focused frontier and end-to-end comparison tests on macOS and Linux, but the timing
cost was too large to accept. The implementation and API were removed after these measurements.

## Method

Both revisions used the same in-memory synthetic `FileSystem`, same workload generator and
`--config=clang_release` build. The baseline is `71e3d68b`; the candidate adds the shared-map
frontier prototype. Each pair has equal left/right trees containing identical 8-byte files.
Broad trees distribute files across 100 immediate child directories. Deep trees use a 4-ary
directory hierarchy with one file per leaf. Runs cover 10, 100, 1,000 and 10,000 files with
1 or 3 comparison workers. Seven rounds were run, alternating baseline-first and candidate-first
order. Cells report the median real time; percent change is candidate versus baseline, so positive
values are slower. There is no real disk I/O in this workload. The macOS runner could not set thread
affinity; measurements use real wall time, not CPU-frequency estimates.

The focused `comparison_frontier_test` and `run_test` targets passed with `--config=clang_release`
on both hosts. The candidate is not a release binary and was never merged.

## macOS: Apple M5 Pro, 18 logical CPUs

| Tree  | Files | Workers | Baseline (us) | Frontier (us) |  Change |
| ----- | ----: | ------: | ------------: | ------------: | ------: |
| broad |    10 |       1 |          57.1 |          56.0 |   -2.0% |
| broad |    10 |       3 |         129.4 |         137.8 |   +6.5% |
| broad |   100 |       1 |         121.3 |         217.9 |  +79.6% |
| broad |   100 |       3 |         322.7 |         356.9 |  +10.6% |
| broad |  1000 |       1 |         362.0 |         886.1 | +144.8% |
| broad |  1000 |       3 |         513.1 |        1124.8 | +119.2% |
| broad | 10000 |       1 |        3006.5 |        4775.8 |  +58.8% |
| broad | 10000 |       3 |        3025.2 |        4715.8 |  +55.9% |
| deep  |    10 |       1 |          54.9 |          59.3 |   +7.9% |
| deep  |    10 |       3 |         167.9 |         175.5 |   +4.5% |
| deep  |   100 |       1 |         135.6 |         339.0 | +150.1% |
| deep  |   100 |       3 |         732.8 |         786.1 |   +7.3% |
| deep  |  1000 |       1 |        1209.6 |        3621.4 | +199.4% |
| deep  |  1000 |       3 |        6714.6 |        7359.5 |   +9.6% |
| deep  | 10000 |       1 |       13456.1 |       39099.3 | +190.6% |
| deep  | 10000 |       3 |       66966.5 |       76277.6 |  +13.9% |

## Linux: drogon, AMD Ryzen 9 9950X, 10 available CPUs

| Tree  | Files | Workers | Baseline (us) | Frontier (us) |  Change |
| ----- | ----: | ------: | ------------: | ------------: | ------: |
| broad |    10 |       1 |          98.6 |          90.7 |   -8.0% |
| broad |    10 |       3 |         278.7 |         273.1 |   -2.0% |
| broad |   100 |       1 |         136.6 |         278.6 | +103.9% |
| broad |   100 |       3 |         487.6 |         527.8 |   +8.2% |
| broad |  1000 |       1 |         319.1 |        1140.5 | +257.4% |
| broad |  1000 |       3 |         626.8 |        1452.5 | +131.7% |
| broad | 10000 |       1 |        2682.6 |        5806.9 | +116.5% |
| broad | 10000 |       3 |        3383.8 |        5979.6 |  +76.7% |
| deep  |    10 |       1 |          95.8 |          97.5 |   +1.8% |
| deep  |    10 |       3 |         417.7 |         391.5 |   -6.3% |
| deep  |   100 |       1 |         149.8 |         374.7 | +150.0% |
| deep  |   100 |       3 |        1370.3 |        1431.0 |   +4.4% |
| deep  |  1000 |       1 |         859.9 |        4359.7 | +407.0% |
| deep  |  1000 |       3 |       10958.7 |       11827.1 |   +7.9% |
| deep  | 10000 |       1 |        9481.3 |       51658.9 | +444.9% |
| deep  | 10000 |       3 |      103228.3 |      122871.1 |  +19.0% |

## Peak resident memory at 10,000 deep files, one worker

| Host         |  Baseline |  Frontier | Change |
| ------------ | --------: | --------: | -----: |
| macOS M5 Pro |  44.79 MB |  28.34 MB | -36.7% |
| Linux drogon | 22.53 MiB | 25.89 MiB | +14.9% |

## Decision

Reject this implementation. Its central `std::map` and per-entry lock/path ownership add large
costs: most 1-worker cases regress by 80-445%, and large 3-worker cases regress by 14-132%.
The only substantial memory improvement is the single deep macOS case; Linux uses more memory
for the same case. This does not justify the time cost. The retained source fixture is named
`comparison/broad` and `comparison/deep` so a future implementation can compare these shapes.
A retry needs a different coordination and ownership design that avoids shared per-entry work,
then must pass the same cross-platform timing and memory checks.

Raw round timings, medians, and workload metadata are in
[`comparison-frontier-rejection-20261106.json`](comparison-frontier-rejection-20261106.json).
The runs used `//xff/engine:compare_benchmark`, `--benchmark_filter="frontier/"` and
`--benchmark_min_time=0.03s`; each invocation wrote a separate JSON file. The maintained workload
is now named `comparison/`, and the raw JSON names were normalized accordingly.
