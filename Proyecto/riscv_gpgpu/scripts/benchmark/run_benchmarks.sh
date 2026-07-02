#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CONFIG_FILE="$ROOT_DIR/benchmarks/configurations/default_config.yaml"
RESULT_DIR="$ROOT_DIR/results/benchmarks"

mkdir -p "$RESULT_DIR"

if [ ! -f "$CONFIG_FILE" ]; then
  echo "Benchmark config not found: $CONFIG_FILE" >&2
  exit 1
fi

echo "Running RISCV GPGPU benchmark harness"
echo "Configuration: $CONFIG_FILE"
echo "Results: $RESULT_DIR"

python3 "$ROOT_DIR/scripts/benchmark/analyze_results.py" --config "$CONFIG_FILE" --output "$RESULT_DIR/summary.json"

echo "Benchmark harness completed. Results written to $RESULT_DIR/summary.json"
