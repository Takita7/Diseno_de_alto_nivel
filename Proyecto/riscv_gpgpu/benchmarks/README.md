# Benchmarks

This directory contains benchmark executables, benchmark-specific configs, and
workload assets.

Scope of this README:
- what is inside `benchmarks/`
- how benchmark binaries are built and run
- where outputs are written

Out of scope:
- architecture internals (`models/systemc/`)
- host API/runtime implementation (`software/`, `runtime/`, `driver/`)

## Directory Scope

| Path | Scope |
|---|---|
| `benchmarks/CMakeLists.txt` | Benchmark build targets |
| `benchmarks/configurations/` | YAML benchmark configurations |
| `benchmarks/workloads/` | Workload assets (Rodinia, vector_add, saxpy) |
| `benchmarks/ptx_kernels_benchmark.cpp` | PTX kernel benchmark binary |
| `benchmarks/rodinia_benchmark.cpp` | Rodinia benchmark entrypoint |
| `benchmarks/rodinia_real_benchmark.cpp` | Real Rodinia wrapper benchmark |

## Build

From repository root:

```bash
cmake -S . -B build-all -DBUILD_BENCHMARKS=ON
cmake --build build-all --target ptx_kernels_benchmark rodinia_benchmark rodinia_real_benchmark -j$(nproc)
```

## Execute

Typical run entrypoints are in `scripts/benchmark/`:

```bash
# Matrix runner for Rodinia real benchmark
bash scripts/benchmark/run_rodinia_real_matrix.sh

# Analyze matrix logs into JSON summary
python3 scripts/benchmark/analyze_results.py --matrix-dir results/benchmarks/rodinia_real_matrix
```

## Outputs

- Matrix runs: `results/benchmarks/rodinia_real_matrix/`
- Per-case logs: `results/benchmarks/rodinia_real_matrix/logs/`
- Aggregates: `summary.tsv` and `summary.json`

## Notes

- Optional local Rodinia checkout is supported through CMake variables
  (`RODINIA_ROOT`, `RODINIA_KERNELS`, `RODINIA_<KERNEL>_SOURCE`).
- Current real benchmark path includes upstream BFS (`kernel.cu` + `kernel2.cu`).
