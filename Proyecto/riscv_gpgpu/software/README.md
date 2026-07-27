# RISC-V GPGPU – Software Stack

This directory contains the complete user-space software stack that sits between
an application and the GPGPU hardware (or its SystemC simulation).  
It mirrors what a production CUDA/HIP driver stack does, but targeting the
custom RISC-V GPGPU defined in `models/systemc/` and, eventually, the FPGA
fabric (`fpga/`).

---

## Layer diagram

```
Application (CUDA-style .cu / C++)
         │
         ▼
  software/host_api/       gpgpuMalloc, gpgpuFree, gpgpuMemcpyH2D/D2H,
                           gpgpuLaunchKernel, gpgpuSynchronize
         │
         ▼
  runtime/src/             KernelLaunchInfo, waitKernelCompletion,
                           pollKernelStatus, bundle manifest resolution
         │
         ▼
  driver/src/              device buffer registry, H2D/D2H copies,
                           configureLaunch, pollKernelCompletion
         │           ┌─────────────────────────────────────────┐
         ▼           │  (simulation path — BUILD_SYSTEMC_INTEGRATION=ON)
  [FPGA hardware]    │  models/systemc/integration/KernelBridge
  AXI DMA transfers  │  ELF load → writeBytes → step() loop → readBytes
                     └─────────────────────────────────────────┘
```

---

## Sub-directories

### `common/`
Shared configuration parsing used by all layers.

| File | Purpose |
|------|---------|
| `config.h` | `GpgpuConfig` struct — arch parameters loaded from `config/arch_config.yaml` |

### `host_api/`
Highest-level user-facing API. Intentionally close to CUDA semantics so existing
GPU code can be ported with minimal changes.

| Symbol | Description |
|--------|-------------|
| `gpgpuMalloc(ptr, size)` | Allocate device buffer, return device address |
| `gpgpuFree(ptr)` | Release device buffer |
| `gpgpuMemcpyH2D(dev, host, size)` | Host → device transfer |
| `gpgpuMemcpyD2H(host, dev, size)` | Device → host transfer |
| `gpgpuLaunchKernel(name, grid, block, args)` | Submit kernel for execution |
| `gpgpuSynchronize()` | Block until all submitted kernels complete |
| `clearKernelArguments()` | Reset argument list between launches |

Tests: `host_api/test_host_api.cpp`

### `kernel_loader/`
Tooling to pack and inspect kernel binaries and their metadata manifests.

| Symbol | Description |
|--------|-------------|
| `resolveEntrySymbol(elf_path, base_name, &sym)` | Find kernel entry via `nm` |
| `listKernelSymbols(elf_path, &report)` | Dump all defined text symbols |

Manifests are JSON files (`*.manifest.json`) that carry workgroup geometry,
required shared memory, and the ELF path.  
Tests: `kernel_loader/test_kernel_loader.cpp`

### `llvm/`
LLVM backend scaffold for the custom RISC-V GPGPU target.

| Sub-dir | Status | Description |
|---------|--------|-------------|
| `backend/` | Scaffold (T030) | `llvm_backend.h/.cpp` — compiles C/CUDA-style sources to RV32IM ELF by invoking `clang -target riscv32-unknown-elf`. Real LLVM target integration (TableGen, TargetLowering, SelectionDAG) is pending. |
| `mc/` | Scaffold (T031) | `riscv_gpgpu_mc.h/.cpp` — assembler/disassembler helpers using `binutils-riscv64-unknown-elf`. Custom SIMT instruction encodings are pending. |

Current kernel compilation (what tests use today):
```bash
clang -target riscv32-unknown-elf -march=rv32im -mabi=ilp32 \
      -O1 -fno-exceptions -fomit-frame-pointer \
      -fuse-ld=lld -nostdlib -Wl,--entry,0 \
      -o kernel.elf kernel.c
```

---

## Build

```bash
# From project root
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build -j

# Run all software-layer tests
ctest --test-dir build -R "host_api|driver|runtime|kernel_loader|compiler"
```

---

## Kernel development quick-start

### 1. Write a bare-metal kernel in C

```c
// kernel.c — no stdlib, no headers
void vector_add(const int* a, const int* b, int* c, int n) {
    for (int i = 0; i < n; ++i) c[i] = a[i] + b[i];
}
```

### 2. Compile to RISC-V ELF

```bash
clang -target riscv32-unknown-elf -march=rv32im -mabi=ilp32 \
      -O1 -fno-exceptions -fomit-frame-pointer \
      -fuse-ld=lld -nostdlib -Wl,--entry,0 \
      -o kernel.elf kernel.c
```

### 3. Run via host API (simulation)

```cpp
#include "host_api/host_api.h"

uint64_t a_ptr, b_ptr, c_ptr;
gpgpuMalloc(a_ptr, N * sizeof(int));
gpgpuMalloc(b_ptr, N * sizeof(int));
gpgpuMalloc(c_ptr, N * sizeof(int));

gpgpuMemcpyH2D(a_ptr, host_a, N * sizeof(int));
gpgpuMemcpyH2D(b_ptr, host_b, N * sizeof(int));

gpgpuLaunchKernel("vector_add", {1,1,1}, {1,1,1},
                  {a_ptr, b_ptr, c_ptr, (uint64_t)N});
gpgpuSynchronize();

gpgpuMemcpyD2H(host_c, c_ptr, N * sizeof(int));
```

The call chain is: `host_api` → `runtime` → `driver` → `KernelBridge`
(simulation) or FPGA AXI driver (hardware).

---

## Traceability to tasks

| Task | Component | Status |
|------|-----------|--------|
| T029 | Compiler/runtime interface contract (`docs/software/interfaces.md`) | ✅ |
| T030 | LLVM backend scaffold (`software/llvm/backend/`) | Partial |
| T031 | Assembler/MC additions (`software/llvm/mc/`) | Partial |
| T032 | Runtime kernel-launch interface (`runtime/src/`) | ✅ |
| T033 | Driver and host API (`driver/src/`, `software/host_api/`) | ✅ |
| T034 | Kernel loader (`software/kernel_loader/`) | ✅ |
| T049b | CMake `compile_kernel` target | Pending |
| T051 | ARM↔FPGA userspace driver (`driver/src/fpga_driver.cpp`) | Pending |
