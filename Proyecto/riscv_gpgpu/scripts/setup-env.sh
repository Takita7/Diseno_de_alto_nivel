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
if [ -n "$SYSTEMC_HOME" ]; then
    # Already set (e.g. exported from the shell profile) - trust it rather
    # than clobbering it with an empty string below if pkg-config doesn't
    # happen to know about this install.
    echo "Using existing SYSTEMC_HOME: $SYSTEMC_HOME"
elif command -v pkg-config &> /dev/null && pkg-config --exists systemc 2>/dev/null; then
    # pkg-config returns the lib directory, go up to find SystemC_HOME
    export SYSTEMC_HOME=$(dirname "$(pkg-config --variable=libdir systemc)")
    echo "Detected SystemC: $SYSTEMC_HOME"
fi

if [ -z "$SYSTEMC_HOME" ]; then
    # Try common installation paths, including a dedicated systemc/
    # subdirectory (e.g. /usr/local/systemc/include/systemc.h), not just
    # the prefix directly.
    for _dir in /usr /usr/local /usr/local/systemc /opt/systemc; do
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

if command -v riscv64-unknown-elf-objdump &> /dev/null; then
    export RISCV_OBJDUMP=$(command -v riscv64-unknown-elf-objdump)
    echo "Detected RISC-V objdump: $RISCV_OBJDUMP"
fi

# Detect and source the Xilinx Vitis/Vivado/Vitis HLS toolchain (needed for
# T025/T026 HLS->RTL synthesis). Vitis's settings64.sh transitively sources
# Vivado's and Vitis HLS's own settings64.sh, so sourcing just that one file
# is enough to get vivado/vitis/vitis-run/platforminfo on PATH.
#
# NOTE: as of the Vitis 2026.1 unified installer, the standalone `vitis_hls`
# binary no longer exists. C-synthesis is invoked through the unified CLI
# instead: `vitis-run --mode hls --tcl <script.tcl>` (hls is --mode's
# default, so `vitis-run --tcl <script.tcl>` also works). Older installs
# (e.g. 2023.1, standalone Vitis_HLS) still ship a real `vitis_hls` binary,
# so both are checked below.
if ! command -v vitis_hls &> /dev/null && ! command -v vitis-run &> /dev/null; then
    _pre_xilinx_path="$PATH"
    if [ -n "$XILINX_VITIS_SETTINGS" ] && [ -f "$XILINX_VITIS_SETTINGS" ]; then
        # shellcheck disable=SC1090
        . "$XILINX_VITIS_SETTINGS"
    else
        for _dir in /tools/Xilinx /opt/Xilinx; do
            # maxdepth 3 (not 2) so this also finds version-numbered install
            # roots, e.g. /tools/Xilinx/2026.1/Vitis/settings64.sh, not just
            # the older /tools/Xilinx/Vitis/<ver>/settings64.sh layout.
            _vitis_settings=$(find "$_dir" -maxdepth 3 -path "*/Vitis/settings64.sh" 2>/dev/null | sort -V | tail -1)
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
    echo "Detected Vitis HLS (standalone vitis_hls): $XILINX_HLS"
elif command -v vitis-run &> /dev/null; then
    echo "Detected Vitis HLS (unified vitis-run --mode hls): $XILINX_HLS"
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

# ── Optional: Rodinia benchmark suite ───────────────────────────────────────
RODINIA_DIR="$PROJECT_ROOT/external/rodinia"
if [ ! -d "$RODINIA_DIR" ]; then
    echo ""
    echo "Optional: Rodinia benchmark suite not found at external/rodinia"
    echo "  To enable upstream Rodinia kernels, clone it once:"
    echo "    git clone --depth 1 https://github.com/yuhc/gpu-rodinia.git $RODINIA_DIR"
    echo "  Then configure with:"
    echo "    cmake .. -DRODINIA_ROOT=$RODINIA_DIR"
else
    echo "Rodinia checkout detected at $RODINIA_DIR"
    echo "  Configure with: cmake .. -DRODINIA_ROOT=$RODINIA_DIR"
fi

echo ""
echo "Environment setup complete"
echo ""
echo "Next steps:"
echo "  cd $PROJECT_ROOT/build"
echo "  cmake ..  # add -DRODINIA_ROOT=$RODINIA_DIR if Rodinia is present"
echo "  make"
echo ""
