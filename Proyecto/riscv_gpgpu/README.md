# RISC-V GPGPU — Build & Run

## Overview

FPGA-oriented, SystemC-based open RISC-V GPGPU research platform.  
Target hardware: AMD Kria KV260/KR260 (ARM Cortex-A53 PS + Xilinx PL).

```
Application (.cu / .c)
      │  clang -target riscv32-unknown-elf
      ▼
  RISC-V ELF kernel
      │  software/ host_api → runtime → driver
      ▼
  KernelBridge  ←→  models/systemc/   (functional simulation, x86 host)
      │              (SystemC + RV32I fetch/decode/execute)
      │
      ▼  (future — T050-T052)
  FPGA fabric via AXI DMA   (Kria PL — hls/ → rtl/ → fpga/)
```

Block READMEs: [SystemC model](models/systemc/README.md) · [Software stack](software/README.md) · [HLS](hls/README.md)

---

## Prerequisites

- Linux (Ubuntu 22.04+ recommended)
- CMake 3.24+ and a C++17 compiler (GCC/Clang)
- SystemC 3.x (`/usr/local` — override with `SYSTEMC_HOME`)
- Google Test (`libgtest-dev`, `libgmock-dev`)
- LLVM/Clang cross tools: `clang`, `lld`, `binutils-riscv64-unknown-elf`
- Python 3.10+ (for scripts)

---

## Quick start

```bash
source scripts/setup-env.sh       # optional: sets SYSTEMC_HOME, LLVM_HOME

# Model only (no integration layer)
cmake -S . -B build -DBUILD_SYSTEMC_MODELS=ON -DBUILD_TESTS=ON
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure

# Full stack (SystemC + SW integration tests, requires clang cross toolchain)
cmake -S . -B build -DBUILD_SYSTEMC_MODELS=ON -DBUILD_TESTS=ON \
      -DBUILD_SYSTEMC_INTEGRATION=ON
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

---

## Test status (branch `gpgpu/codesign_dmedina`, 2026-07-26)

| Suite | Tests | Status |
|-------|-------|--------|
| `systemc_pipeline_tests` | 3 | ✅ |
| `systemc_scheduler_tests` | — | ✅ |
| `systemc_simt_controller_tests` | — | ✅ |
| `systemc_compute_unit_tests` | 2 | ✅ |
| `systemc_integration_tests` | 4/5 | 🔶 (1 divergence counter pending T047b) |
| `compiler_tests` | — | ✅ |
| `runtime_tests` | — | ✅ |
| `driver_tests` | — | ✅ |
| `host_api_tests` | — | ✅ |
| `kernel_loader_tests` | — | ✅ |
| SystemC standalone (`make regression`) | 13/13 phases | ✅ |

---

## Scripts

| Script | Purpose |
|--------|---------|
| `scripts/run_systemc_sim.sh` | Build + run `systemc_simulation` executable |
| `scripts/verify.sh` | Full verification harness |
| `scripts/benchmark.sh` | Benchmark harness (results → `results/benchmarks/`) |
| `scripts/demo_cuda_kernel_bundle.sh` | Compile CUDA-style kernel → ELF bundle artifacts |
| `scripts/setup-env.sh` | Set `SYSTEMC_HOME`, `LLVM_HOME` in current shell |
| `scripts/setup-software-dev.sh` | Install system packages + Python venv (needs sudo) |

---

## Repository layout

```
├── models/systemc/   SystemC functional model (src/ + integration/ + test/)
├── software/         Host API, runtime, driver, kernel loader, LLVM backend scaffold
├── hls/              HLS implementation (Vitis HLS — pending T022-T024)
├── rtl/              RTL output from HLS (pending T025)
├── fpga/             FPGA build scripts and deployment (pending T026, T050-T052)
├── runtime/          Kernel-launch and status-poll runtime
├── driver/           Userspace driver (simulation backend today, AXI FPGA driver pending)
├── tests/            CMake-managed test suites
├── benchmarks/       Benchmark workloads (vector_add, saxpy, Rodinia subset)
├── docs/             Architecture docs, traceability, reproducibility guides
├── config/           arch_config.yaml — shared architecture parameters
└── specs/            Feature spec, plan, and tasks (tasks.md is the source of truth)
```

---

## Development environment (Python / LLVM)

```bash
# Local virtualenv (preferred)
python3 -m venv .venv && source .venv/bin/activate
pip install pyyaml jinja2 lit numpy

# System-wide setup (requires sudo)
sudo ./scripts/setup-software-dev.sh
source /opt/riscv-gpgpu-venv/bin/activate

# Build LLVM with RISC-V target (for real backend work — T030)
mkdir -p /opt/riscv-src && cd /opt/riscv-src
git clone https://github.com/llvm/llvm-project.git
mkdir llvm-project/build && cd llvm-project/build
cmake -G Ninja ../llvm -DLLVM_ENABLE_PROJECTS="clang;lld" \
      -DCMAKE_BUILD_TYPE=Release -DLLVM_TARGETS_TO_BUILD=RISCV
ninja -j$(nproc)
```

---

## Contributing

Follow contribution guidelines and keep changes traceable to `docs/traceability/`.  
Task definitions and status live in `specs/001-open-riscv-gpgpu/tasks.md`.
