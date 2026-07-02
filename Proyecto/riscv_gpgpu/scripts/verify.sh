#!/bin/bash
#
# verify.sh - Main verification harness runner
#
# Runs all verification suites and collects results
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"
VERIFY_DIR="$SCRIPT_DIR/verify"
RESULTS_DIR="$PROJECT_ROOT/results/verification"

echo "====================================="
echo "RISCV GPGPU Verification Harness"
echo "====================================="
echo "Project root: $PROJECT_ROOT"
echo "Results directory: $RESULTS_DIR"
echo ""

# Create results directory
mkdir -p "$RESULTS_DIR"

# Run unit tests
echo "Running unit tests..."
if [ -f "$VERIFY_DIR/run_unit_tests.sh" ]; then
    bash "$VERIFY_DIR/run_unit_tests.sh" | tee "$RESULTS_DIR/unit_tests.log"
else
    echo "Unit test harness not yet implemented"
fi

# Run integration tests
echo ""
echo "Running integration tests..."
if [ -f "$VERIFY_DIR/run_integration_tests.sh" ]; then
    bash "$VERIFY_DIR/run_integration_tests.sh" | tee "$RESULTS_DIR/integration_tests.log"
else
    echo "Integration test harness not yet implemented"
fi

# Collect evidence
echo ""
echo "Collecting evidence..."
if [ -f "$SCRIPT_DIR/collect_evidence.py" ]; then
    python3 "$SCRIPT_DIR/collect_evidence.py" --output "$RESULTS_DIR/evidence.json"
else
    echo "Evidence collection not yet implemented"
fi

echo ""
echo "Verification complete. Results saved to: $RESULTS_DIR"
