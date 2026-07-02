# Benchmarks

This directory contains benchmark configurations and harnesses for performance evaluation.

## Contents

### configurations/
Benchmark configuration templates:
- rodinia_config.yaml - Rodinia benchmark suite configuration
- custom_micro_config.yaml - Custom microbenchmark definitions
- design_space_config.yaml - Design-space exploration configurations

### workloads/
Benchmark kernel workload definitions:
- divergence_test/ - Benchmark for divergence behavior
- memory_access_test/ - Memory bandwidth and latency tests
- synchronization_test/ - Barrier and synchronization performance
- scalar_workload/ - Representative scalar workloads

### scripts/
Benchmark execution scripts:
- run_benchmarks.sh - Main benchmark runner
- analyze_results.py - Result analysis and plotting
- compare_configurations.py - Configuration comparison utility

### results/
Benchmark execution results:
- baseline/ - Baseline configuration results
- configurations/ - Alternative configuration results
- analysis/ - Analyzed results and reports

## Benchmark Categories

### Rodinia Suite
Standard GPU benchmark suite adapted for RISC-V GPGPU:
- BFS (Breadth-first search)
- Hotspot (Heat diffusion simulation)
- Needle (Sequence alignment)
- Gaussian Elimination
- LavaMD (Molecular dynamics)

### Custom Microbenchmarks
Targeted benchmarks for architecture evaluation:
- Thread divergence effectiveness
- Memory hierarchy utilization
- Synchronization overhead
- Warp scheduling efficiency

### Design-Space Exploration
Configurations to evaluate:
- Threads per warp (16, 32, 64)
- Warps per CU (4, 8, 16, 32)
- Shared memory sizes (16KB, 32KB, 48KB)
- Cache hierarchy variants

## Measurement Metrics

### Performance Metrics
- Throughput (kernel launches/sec)
- Latency (execution time per kernel)
- Speedup relative to baseline
- Efficiency under varying workloads

### Resource Metrics
- LUT utilization (FPGA)
- Register utilization
- Memory bandwidth utilization
- Power consumption (if available)

## Status

- Benchmark structure initialized
- Configuration templates created
- Benchmark harness framework established
- Detailed workloads to be implemented in Phase 5
