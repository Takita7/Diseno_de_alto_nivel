#!/bin/bash
#
# benchmark.sh - Main benchmark harness runner
#
# Runs benchmark suites and collects performance results
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"
BENCHMARK_DIR="$SCRIPT_DIR/benchmark"
RESULTS_DIR="$PROJECT_ROOT/results/benchmarks"

echo "====================================="
echo "RISCV GPGPU Benchmark Harness"
echo "====================================="
echo "Project root: $PROJECT_ROOT"
echo "Results directory: $RESULTS_DIR"
echo ""

# Create results directory
mkdir -p "$RESULTS_DIR"

# Run benchmarks
echo "Running benchmarks..."
if [ -f "$BENCHMARK_DIR/run_benchmarks.sh" ]; then
    bash "$BENCHMARK_DIR/run_benchmarks.sh" | tee "$RESULTS_DIR/benchmark.log"
else
    echo "Benchmark harness not yet implemented"
fi

echo ""
echo "Benchmarks complete. Results saved to: $RESULTS_DIR"
