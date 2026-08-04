# Reproducibility Package

This file defines the reproducibility package contents required to replay core
results for this repository.

## Required Host Baseline

- Linux environment
- CMake 3.24+
- C++17 toolchain
- SystemC installation (for model/integration runs)
- Python 3

## Minimal Reproduction Steps

```bash
cmake -S . -B build-all -DBUILD_TESTS=ON -DBUILD_SYSTEMC_MODELS=ON
cmake --build build-all -j$(nproc)
ctest --test-dir build-all --output-on-failure
```

## Benchmark Reproduction

```bash
bash scripts/benchmark/run_rodinia_real_matrix.sh
python3 scripts/benchmark/analyze_results.py --matrix-dir results/benchmarks/rodinia_real_matrix
```

Expected benchmark evidence:

- results/benchmarks/rodinia_real_matrix/summary.tsv
- results/benchmarks/rodinia_real_matrix/summary.json
- docs/verification/benchmark_results.md

## Kria Deployment Reproduction (hardware)

```bash
bash scripts/deploy_kria.sh --bitstream <bit.bin> --kernel <kernel.elf> --test <test_binary> --host <user@ip>
```

Expected hardware evidence:

- docs/verification/kria_results.md

## Package Checklist

- Source tree at a commit hash
- Build configuration command lines
- Test command lines and outputs
- Benchmark command lines and outputs
- Verification summaries under docs/verification
- Traceability matrix under docs/traceability
