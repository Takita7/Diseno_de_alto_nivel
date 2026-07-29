#!/bin/bash
#
# setup-env.sh - Initialize development environment for RISCV GPGPU
#
# Usage: source setup-env.sh
#        or: . setup-env.sh
#

set -e

# ${BASH_SOURCE[0]} is bash-only; fall back to zsh's %N so this also works
# when sourced from an interactive zsh shell (this project's default shell).
if [ -n "${BASH_SOURCE:-}" ]; then
    _self="${BASH_SOURCE[0]}"
elif [ -n "${ZSH_VERSION:-}" ]; then
    _self=${(%):-%N}
else
    _self="$0"
fi
SCRIPT_DIR="$(cd "$(dirname "$_self")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
unset _self

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
    for _dir in /usr /usr/local /opt/systemc; do
        if [ -f "$_dir/include/systemc.h" ] || [ -f "$_dir/include/systemc/systemc.h" ]; then
            export SYSTEMC_HOME="$_dir"
            echo "Found SystemC at: $SYSTEMC_HOME"
            break
        fi
    done
    unset _dir
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

# Detect and source the Xilinx Vitis/Vivado/Vitis HLS toolchain (needed for
# T025/T026 HLS->RTL synthesis). Vitis's settings64.sh transitively sources
# Vivado's and Vitis HLS's own settings64.sh, so sourcing just that one file
# is enough to get vivado/vitis/vitis_hls/platforminfo on PATH.
if ! command -v vitis_hls &> /dev/null; then
    _pre_xilinx_path="$PATH"
    if [ -n "$XILINX_VITIS_SETTINGS" ] && [ -f "$XILINX_VITIS_SETTINGS" ]; then
        # shellcheck disable=SC1090
        . "$XILINX_VITIS_SETTINGS"
    else
        for _dir in /tools/Xilinx /opt/Xilinx; do
            _vitis_settings=$(find "$_dir/Vitis" -maxdepth 2 -iname "settings64.sh" 2>/dev/null | sort -V | tail -1)
            if [ -n "$_vitis_settings" ]; then
                # shellcheck disable=SC1090
                . "$_vitis_settings"
                break
            fi
        done
        unset _dir _vitis_settings
    fi
    # Vitis bundles its own (old, and on this machine broken - missing
    # libidn.so.11) cmake under tps/lnx64/, which its settings64.sh puts
    # ahead of the system one on PATH. Re-prepend the pre-Xilinx PATH so
    # system tools (cmake, make, python, ...) win over any same-named
    # Xilinx-bundled copy; Xilinx-only tools (vivado/vitis_hls/etc., which
    # don't exist in the pre-Xilinx PATH) are still found since they're
    # still present later in the combined PATH.
    export PATH="$_pre_xilinx_path:$PATH"
    unset _pre_xilinx_path
fi

if command -v vitis_hls &> /dev/null; then
    echo "Detected Vitis HLS: $XILINX_HLS"
else
    echo "WARNING: Vitis HLS not found. Set XILINX_VITIS_SETTINGS to the Vitis settings64.sh path, or install Vitis (see docs/hls/interfaces.md)"
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
