# SystemC Functional Model

This directory owns the functional model of the RISC-V GPGPU and the
SystemC-side integration used by software tests.

Scope of this README:
- model components and execution modes
- how to build/run SystemC tests

Out of scope:
- software API contracts (`software/`)
- benchmark workflows (`benchmarks/`)
- FPGA deployment runbook (`scripts/deploy_kria.sh`, `fpga/`)

## Execution Modes

| Mode | Main use |
|---|---|
| Virtual ISA | Architecture exploration with abstract SIMT instructions |
| Binary execution | End-to-end flow with RV32 ELF binaries |

Virtual ISA instructions are project-specific abstractions and are not RVV.

## Directory Scope

| Path | Scope |
|---|---|
| `models/systemc/src/` | Core modules: compute unit, scheduler, memory, SIMT controller, top/system_top |
| `models/systemc/integration/` | Bridge from software launches to SystemC execution (`kernel_bridge`, ELF loader) |
| `models/systemc/test/` | Standalone model regression and benchmark test programs |

## Build and Test

Project CMake flow:

```bash
cmake -S . -B build-all -DBUILD_SYSTEMC_MODELS=ON -DBUILD_TESTS=ON
cmake --build build-all -j$(nproc)
ctest --test-dir build-all -R systemc
```

Standalone model tests:

```bash
cd models/systemc/test
make regression
```

## Integration Notes

- `BUILD_SYSTEMC_INTEGRATION=ON` enables software-to-model integration tests.
- `integration/kernel_bridge.cpp` is the handoff point used by host/runtime flow.
- Workload metrics and divergence counters exposed by the bridge are consumed by
    benchmark analysis scripts.

| Limitation | Notes |
|-----------|-------|
| **No cycle counts** | `getTotalCycles()` always returns 0 for the virtual ISA path. Binary mode (`step()`) counts cycles as instruction fetches, not wall-clock. Cycle-accurate timing comes from HLS synthesis. |
| **No ITOF** | No integer-to-float conversion in the virtual ISA. FP kernels launched via `top.launchKernel()` use uniform float immediates. Per-thread float data requires pre-loading from memory or using [DIRECT] kernels. |
| **Binary mode: single-thread per CU** | `KernelBridge` spawns one `ComputeUnit` per thread (one thread per block in current implementation). SIMT lane masking and divergence events are not generated in binary mode — each thread executes independently. This is T047b/T048b work. |
| **TLM socket deferred** | `MemoryHierarchy` has no TLM target socket yet. Access is via direct method calls (`loadWord`/`storeWord`/`writeBytes`/`readBytes`). TLM binding is planned for Phase 4. |
| **Single-CU memory in virtual ISA mode** | All CUs in a `GPGPUTop` share the same `MemoryHierarchy` instance. Setting `num_compute_units > 1` distributes warps correctly but all memory traffic goes through one L1/L2. |
| **No RVV** | The RISC-V V extension is not implemented. The virtual ISA's `VADD`/`VFMADD`/etc. are abstract SIMT opcodes, not encoded RVV instructions. |

---

## Benchmark quick reference

Run `make benchmark` from `test/`. Key metrics (5 GPUs, 32 threads/warp, 1 CU/GPU):

| Benchmark | Warps | Instrs | L1 Hit% | Div |
|-----------|-------|--------|---------|-----|
| B1 Int SAXPY | 20 | 120 | 0% | 0 |
| B2 FP SAXPY | 20 | 120 | 0% | 0 |
| B3 Memory round-trip | 10 | 40 | 50% | 0 |
| B4 Divergent odd/even | 20 | 120 | 0% | 20 |
| B5 Barrier sync | 10 | 40 | 0% | 0 |
| B6 Parallel reduction | 10 | 150 | 0% | 0 |
| B7 GEMM (2×2, K=4) | 1 | 5 | — | 0 |
| B8 Conv2D (3×3) | 1 | 10 | — | 0 |

---

## Authors

ITCR School of Electronics Engineering — Master's programme MP6160  
RISC-V GPGPU co-design project, 2025

