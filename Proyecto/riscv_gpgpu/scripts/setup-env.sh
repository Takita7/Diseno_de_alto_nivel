#!/bin/bash
#
# setup-env.sh - Initialize development environment for RISCV GPGPU
#
# Usage: source setup-env.sh
#        or: . setup-env.sh
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "Setting up development environment for RISCV GPGPU"
echo "Project root: $PROJECT_ROOT"

# Detect and set SystemC environment
if command -v pkg-config &> /dev/null; then
    SYSTEMC_HOME=$(pkg-config --variable=libdir systemc 2>/dev/null || echo "")
    if [ -n "$SYSTEMC_HOME" ]; then
        # pkg-config returns the lib directory, go up to find SystemC_HOME
        export SYSTEMC_HOME=$(dirname "$SYSTEMC_HOME")
        echo "Detected SystemC: $SYSTEMC_HOME"
    fi
fi

if [ -z "$SYSTEMC_HOME" ]; then
    # Try common installation paths
    for path in /usr /usr/local /opt/systemc; do
        if [ -f "$path/include/systemc.h" ] || [ -f "$path/include/systemc/systemc.h" ]; then
            export SYSTEMC_HOME="$path"
            echo "Found SystemC at: $SYSTEMC_HOME"
            break
        fi
    done
fi

if [ -z "$SYSTEMC_HOME" ]; then
    echo "WARNING: SystemC not found. Set SYSTEMC_HOME manually or install SystemC"
fi

# Detect and set LLVM environment
if command -v llvm-config &> /dev/null; then
    export LLVM_DIR=$(llvm-config --cmakedir)
    export LLVM_HOME=$(llvm-config --prefix)
    echo "Detected LLVM: $LLVM_HOME"
fi

# Create build directory
if [ ! -d "$PROJECT_ROOT/build" ]; then
    mkdir -p "$PROJECT_ROOT/build"
    echo "Created build directory"
fi

# Update PATH to include scripts
export PATH="$PROJECT_ROOT/scripts:$PATH"

# Load tool-specific environment if available
if [ -f "$PROJECT_ROOT/scripts/setup-tools.sh" ]; then
    # shellcheck disable=SC1091
    . "$PROJECT_ROOT/scripts/setup-tools.sh"
fi

echo "Environment setup complete"
echo ""
echo "Next steps:"
echo "  cd $PROJECT_ROOT/build"
echo "  cmake .."
echo "  make"
echo ""
