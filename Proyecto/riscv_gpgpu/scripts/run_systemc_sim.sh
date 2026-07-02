#!/bin/bash
#
# run_systemc_sim.sh - Run SystemC simulation with configuration
#
# Usage: ./run_systemc_sim.sh [--config config.yaml] [--scenario scenario_name]
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$(dirname "$(dirname "$SCRIPT_DIR")")")"
BUILD_DIR="$PROJECT_ROOT/build"
BIN_DIR="$BUILD_DIR/bin"
SIM_DIR="$SCRIPT_DIR/scenarios"

# Default configuration
CONFIG="${PROJECT_ROOT}/config/arch_config.yaml"
SCENARIO="baseline"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --config)
            CONFIG="$2"
            shift 2
            ;;
        --scenario)
            SCENARIO="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

echo "====================================="
echo "RISCV GPGPU SystemC Simulation"
echo "====================================="
echo "Configuration: $CONFIG"
echo "Scenario: $SCENARIO"
echo ""

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "Build directory not found. Building..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake .. -DBUILD_SYSTEMC_MODELS=ON
    make
fi

# Check if executable exists
if [ ! -f "$BIN_DIR/systemc_simulation" ]; then
    echo "Building SystemC simulation..."
    cd "$BUILD_DIR"
    make systemc_simulation
fi

# Create results directory
mkdir -p "$PROJECT_ROOT/results/simulation"

# Run simulation
echo "Running simulation..."
cd "$PROJECT_ROOT/results/simulation"

# Source the scenario if it exists
if [ -f "$SIM_DIR/${SCENARIO}.sh" ]; then
    source "$SIM_DIR/${SCENARIO}.sh"
fi

# Run the simulation executable
"$BIN_DIR/systemc_simulation"

echo ""
echo "Simulation complete. Results saved to: $PROJECT_ROOT/results/simulation"
