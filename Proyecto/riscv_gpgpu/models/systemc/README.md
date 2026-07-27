# RISC-V GPGPU – SystemC Functional Model

SystemC/TLM functional model of a Ventus-inspired RISC-V GPGPU, developed
as part of a hardware/software co-design methodology targeting FPGA platforms.

The model provides **functional validation and architectural exploration**
before High-Level Synthesis (HLS). It is not cycle-accurate — instruction
counts and cache statistics are exact, but timing measurements are not
meaningful at this abstraction level.

> **ISA note**: the model operates at two levels of abstraction.
> The *virtual ISA mode* uses custom abstract opcodes (`VADD`, `VSUB`, …)
> that represent SIMT-parallel operations — these are **not** RISC-V V
> (RVV) instructions. The *binary execution mode* runs real `rv32im` ELF
> binaries compiled with `-march=rv32im` (no V extension). RVV support
> is a future work item.

Two complementary execution modes coexist:

| Mode | Entry point | Use case |
|------|-------------|----------|
| **Virtual ISA** | `GPGPUTop::launchKernel(grid_x, grid_y, vector<Instruction>)` | Architecture-level exploration with abstract opcodes |
| **Binary execution** | `ComputeUnit::setEntryPoint` + `step()` + `KernelBridge` | Full-stack validation: Clang → RV32I ELF → fetch/decode/execute |

---

## Directory structure

```
systemc/
├── src/
│   ├── common/
│   │   ├── types.h              shared data structures (WarpContext, Instruction, Opcode…)
│   │   ├── kernel_programs.h    ready-to-launch kernel library  ← start here
│   │   ├── logging.h            LOG_INFO / LOG_DEBUG / LOG_ERROR macros
│   │   └── platform.h           printSimulationBanner, printPhaseHeader
│   ├── top/
│   │   ├── top.h / top.cpp      GPGPUTop: single-GPU top-level module
│   │   └── simulation_main.cpp  standalone executable entry point
│   ├── system_top/
│   │   └── system_top.h/.cpp    SystemTop: multi-GPU wrapper (N × GPGPUTop)
│   ├── scheduler/
│   │   └── warp_scheduler.h/.cpp  round-robin / FIFO / priority warp dispatch
│   ├── simt_controller/
│   │   └── simt_controller.h/.cpp IPDOM stack, divergence tracking, barriers
│   ├── memory/
│   │   └── memory_hierarchy.h/.cpp L1/L2/global/shared memory (write-through)
│   │       writeBytes / readBytes — bulk DMA-equivalent access for ELF loading
│   └── compute_unit/
│       └── compute_unit.h/.cpp
│           Virtual ISA mode:  executeWarp(WarpContext&)
│           Binary mode:       setEntryPoint / setInitialRegisters / step()
├── integration/                  SW↔SC bridge (requires BUILD_SYSTEMC_INTEGRATION=ON)
│   ├── kernel_bridge.h/.cpp      orchestrates ELF load → H2D → CU step loop → D2H
│   ├── elf_loader.h/.cpp         manual ELF32 parser, no libelf dependency
│   └── riscv_isa.h               header-only RV32I+M decoder (decodeRV32 / expandRVC)
└── test/
    ├── Makefile
    ├── regression_test.cpp        full correctness suite (Phases 0–13, all green)
    ├── benchmark_test.cpp         stress benchmarks + design space exploration
    └── top_test_v7.cpp            reference snapshot (multi-GPU, 9 phases)
```

---

## Build and run

### Standalone (model only — no CMake)

```bash
cd test/

make all        # build both binaries
make regression # build + run full regression suite (Phases 0–13)
make benchmark  # build + run stress benchmark + design space tables
make clean      # remove build artefacts
make info       # show SystemC path, source count
```

**Requirement:** SystemC 3.x at `/usr/local` (override with `SYSTEMC_HOME=/your/path`).

### CMake (full project — includes integration tests)

```bash
cmake -S . -B build -DBUILD_SYSTEMC_MODELS=ON -DBUILD_TESTS=ON
cmake --build build -j

# Enable the SW↔SC integration layer (requires clang + lld for RV32I kernels):
cmake -S . -B build -DBUILD_SYSTEMC_MODELS=ON -DBUILD_TESTS=ON -DBUILD_SYSTEMC_INTEGRATION=ON
cmake --build build -j && ctest --test-dir build -R systemc
```

---

## Execution modes

### Virtual ISA (architecture exploration)

Kernels are expressed as `vector<Instruction>` using the abstract `Opcode` enum.
Good for exploring scheduling, divergence, and memory access patterns without a
compiler in the loop.

```cpp
GPGPUTop top("gpgpu", config);
top.launchKernel(4, 1, kernels::vectorAdd());  // 4 warps, 1 warp/grid row
sc_core::sc_start(sc_core::sc_time(200, SC_NS));
```

### Binary execution (end-to-end validation)

Runs a real RV32I ELF compiled with Clang. The `step()` loop fetches machine
words from `MemoryHierarchy`, decodes them with `riscv_isa.h`, and executes.
This mirrors what the FPGA ARM driver will do:

| Software (simulation) | FPGA (hardware) |
|-----------------------|-----------------|
| `ElfLoader::load()` → `mem.writeBytes()` | ARM DMA → FPGA instruction memory |
| `cu.setEntryPoint(pc)` | Write `PC_INIT` register |
| `cu.step()` loop | GPGPU fetch/decode/execute pipeline |
| `mem.readBytes()` → D2H copy | ARM DMA ← FPGA data memory |

```cpp
// Compile kernel: clang -target riscv32-unknown-elf -march=rv32im ...
ElfLoader elf;  elf.load("kernel.elf", mem);
ComputeUnit cu("cu0", cfg);
cu.setMemoryHierarchy(&mem);
cu.setEntryPoint(elf.getEntryPoint());
cu.setInitialRegisters(regs);   // a0..a7 = kernel args
cu.setReturnSentinel(0x1);
cu.launchKernel(0, 1, 1);
while (!cu.isComplete()) cu.step();
uint32_t result = cu.getRegister(0, 10);  // read a0
```

For multi-block workloads use `KernelBridge` (see `integration/kernel_bridge.h`).

---

## Register convention

### Virtual ISA (`buildWarpContext`)

| Register | Value | Meaning |
|----------|-------|---------|
| `r0[t]` | `0` | Zero register (always) |
| `r1[t]` | `global_tid` | Unique thread ID across all warps and GPUs |
| `r2[t]` | `0x10000 + global_tid × 4` | Unique 4-byte-aligned memory address |
| `r3[t]` | `local_warp_id` | Warp index within this kernel launch |
| `r4–r31` | `0` | Free for the kernel |

### Binary execution (RISC-V ABI)

| Register | ABI name | Role |
|----------|----------|------|
| `x0` | `zero` | Always 0 |
| `x1` | `ra` | Set to return sentinel (signals kernel completion) |
| `x2` | `sp` | Stack pointer (set to `0x20000000`) |
| `x10–x17` | `a0–a7` | Kernel arguments (pointer args carry device addresses) |
| `x14` | `a4` | Block ID (injected per-worker by `KernelBridge`) |
| `x15` | `a5` | Thread ID within block (injected per-worker) |

---

## Available opcodes (`types.h` → `enum class Opcode`)

### Scalar integer ALU
| Opcode | Operation |
|--------|-----------|
| `ADD`, `SUB`, `AND`, `OR`, `XOR`, `SLT` | Standard R-type |
| `ADDI`, `LUI` | Immediate forms |

### Scalar FP (registers store IEEE 754 bits)
| Opcode | Operation |
|--------|-----------|
| `FADD` | `rd = float(rs1) + float(rs2)` |
| `FMUL` | `rd = float(rs1) × float(rs2)` |

### Memory
| Opcode | Operation |
|--------|-----------|
| `LW` | `rd = mem[rs1 + imm]` (4-byte word) |
| `SW` | `mem[rs1 + imm] = rs2` (write-through) |

### SIMT control
| Opcode | Semantics |
|--------|-----------|
| `VBRANCH` | Threads where `rs1[t] == 0` stay active; others masked until matching `VJOIN` |
| `VJOIN` | Restores masked threads from IPDOM stack |
| `BARRIER` | `imm` = barrier ID — suspends warp until all warps in kernel arrive |

### SIMT-parallel operations (virtual ISA only — not RVV)
These opcodes exist only in the abstract virtual ISA mode.
They represent operations that execute on all active thread lanes simultaneously.
They have **no encoding in the RISC-V V extension** — compiled `rv32im`
binaries cannot emit them.

| Opcode | Operation |
|--------|-----------|
| `VADD`, `VSUB`, `VMUL`, `VFMADD` | Integer SIMT-parallel |
| `VFADD`, `VFSUB`, `VFMUL`, `VFFMADD` | FP SIMT-parallel |

### Control
| Opcode | Operation |
|--------|-----------|
| `HALT` | End of warp program |

---

## Kernel library (`kernel_programs.h`)

All functions return `std::vector<Instruction>` and are tagged `[TOP]` or
`[DIRECT]`:

| Tag | Usage | Register setup |
|-----|-------|----------------|
| `[TOP]` | `top.launchKernel(grid_x, grid_y, kernel())` | Done by `buildWarpContext` |
| `[DIRECT]` | `cu.executeWarp(ctx)` after manually setting `ctx.regs` | Caller's responsibility |

| Function | Tag | Description |
|----------|-----|-------------|
| `intSaxpy(alpha, y)` | TOP | Integer SAXPY: `r6 = alpha*(tid+1)+y` |
| `fpUniformSaxpy(alpha, x, y)` | TOP | Float SAXPY, same value all threads |
| `memoryRoundTrip()` | TOP | SW→LW(miss)→LW(hit) per thread |
| `divergentOddEven()` | TOP | Even threads active, odd masked |
| `barrierRoundTrip(id)` | TOP | Store→BARRIER→Load, cross-warp sync |
| `parallelReduction()` | TOP | Pairwise sum across 2 warps via BARRIER |
| `fpSaxpy()` | DIRECT | Float SAXPY: caller sets r3=alpha, r4=x[t], r5=y |
| `fpFmadd()` | DIRECT | Float FMA: caller sets r3, r4, r5 |
| `fpGemm()` | DIRECT | 2×2 GEMM tile, K=4 via VFFMADD |
| `conv2d3x3()` | DIRECT | 3×3 convolution via VFMADD×9 |

---

## Multi-GPU usage

```cpp
SystemTop::Config cfg;
cfg.num_gpus   = 5;          // any number
cfg.gpu_config = gpu_config; // same GPGPUTop::Config for all GPUs
SystemTop sys("sys", cfg);

// Warps are distributed evenly: total_warps / num_gpus per GPU
// Remainder warps go to the first GPUs (e.g. 3 warps / 2 GPUs → GPU0:2, GPU1:1)
sys.launchKernel(20, 1, intSaxpy());

// Per-GPU statistics
sys.getGPU(0).getTotalInstructions();
sys.getGPU(0).getL1CacheHits();
sys.getGPU(0).getDivergenceEvents();

// Aggregated
sys.getTotalInstructions();
sys.getL1CacheMisses();
sys.getDivergenceEvents();
```

---

## Known limitations

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

