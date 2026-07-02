#!/usr/bin/env bash
set -euo pipefail

# setup-software-dev.sh
# Install software-development dependencies for User Story 3 (compiler/runtime)
# This script installs host packages (requires sudo) and creates a Python virtualenv
# Usage: sudo ./scripts/setup-software-dev.sh

if [ "$EUID" -ne 0 ]; then
  echo "This script should be run as root (sudo)." >&2
  echo "It will install system packages and create /opt/riscv-gpgpu-venv for contributors." >&2
  exit 1
fi

echo "Updating package lists..."
apt-get update

echo "Installing build tools and libraries (non-FPGA/HLS)..."
apt-get install -y build-essential cmake ninja-build git curl python3-venv python3-pip \
  pkg-config libssl-dev zlib1g-dev libncurses5-dev libelf-dev libudev-dev libfdt-dev \
  libxml2-dev autoconf automake libtool bison flex python3-dev libgtest-dev libgmock-dev \
  clang lld llvm-dev llvm-tools libhwloc-dev libnuma-dev ocl-icd-opencl-dev opencl-c-headers opencl-clhpp-headers

echo "Creating global virtualenv for project at /opt/riscv-gpgpu-venv"
VENV_DIR=/opt/riscv-gpgpu-venv
python3 -m venv "$VENV_DIR"
"$VENV_DIR/bin/pip" install --upgrade pip
"$VENV_DIR/bin/pip" install pyyaml jinja2 lit numpy

echo "Optional: clone recommended repositories under /opt/riscv-src"
echo "  git clone https://github.com/llvm/llvm-project.git /opt/riscv-src/llvm-project"
echo "  git clone https://github.com/riscv/riscv-gnu-toolchain.git /opt/riscv-src/riscv-gnu-toolchain"
echo "  git clone https://github.com/pocl/pocl.git /opt/riscv-src/pocl"

echo "Setup complete. Activate the venv with: source /opt/riscv-gpgpu-venv/bin/activate"
echo "Add to your shell rc: export PATH=/opt/riscv-gpgpu-venv/bin:\$PATH"

exit 0
