# Benchmark Results

This report captures end-to-end benchmark evidence for Phase 6 (T038) using
artifacts produced by the repository benchmark flow.

## Scope

- Workload: Rodinia BFS stress benchmark (real benchmark wrapper)
- Matrix source: results/benchmarks/rodinia_real_matrix
- Analysis source: results/benchmarks/rodinia_real_matrix/summary.json
- Date baseline: 2026-08-04

## Run Command

```bash
bash scripts/benchmark/run_rodinia_real_matrix.sh
python3 scripts/benchmark/analyze_results.py --matrix-dir results/benchmarks/rodinia_real_matrix
```

## Case Matrix (PASS set)

| Case | CUs | Threads | Status | elapsed_ms | cycles | effective_cycles |
|---|---:|---:|---|---:|---:|---:|
| cus1_threads128 | 1 | 128 | PASS | 14 | 3707 | 3707 |
| cus2_threads128 | 2 | 128 | PASS | 443 | 3707 | 1854 |
| cus3_threads128 | 3 | 128 | PASS | 300 | 3707 | 1236 |
| cus5_threads128 | 5 | 128 | PASS | 243 | 3707 | 742 |
| cus10_threads128 | 10 | 128 | PASS | 232 | 3707 | 371 |
| cus20_threads128 | 20 | 128 | PASS | 220 | 3707 | 186 |

## Aggregates

From summary.json:

- case_count: 6
- pass_count: 6
- fail_count: 0
- elapsed_ms: min 14, max 443, mean 242, median 237.5
- cycles: min/max/mean/median 3707
- worker_cycles_mean: 28.9609
- effective_cycles: min 186, max 3707, mean 1349.33, median 989.0
- parallelism_factor: 1.60476

## Interpretation

- Total worker work (cycles) stays constant across CU counts, as expected for
  a fixed workload under the current scheduler model.
- effective_cycles decreases with active CU count, which is the useful
  parallel-scaling indicator for this simulation path.
- Functional correctness for this matrix is represented by PASS status in all
  six selected cases.

## Evidence Files

- results/benchmarks/rodinia_real_matrix/summary.tsv
- results/benchmarks/rodinia_real_matrix/summary.json
- results/benchmarks/rodinia_real_matrix/cus*_threads128.log
