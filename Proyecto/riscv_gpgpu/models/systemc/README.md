# RISC-V GPGPU – SystemC Functional Model

SystemC/TLM functional model of a Ventus-inspired RISC-V GPGPU with RVV
extensions, developed as part of a hardware/software co-design methodology
targeting FPGA platforms (see companion paper).

The model provides **functional validation and architectural exploration**
before High-Level Synthesis (HLS). It is not cycle-accurate — instruction
counts and cache statistics are exact, but timing measurements are not
meaningful at this abstraction level.

---

## Directory structure

```
systemc/
├── src/
│   ├── common/
│   │   ├── types.h              shared data structures (WarpContext, Instruction, Opcode…)
│   │   ├── kernel_programs.h    ready-to-launch kernel library  ← start here
│   │   ├── logging.h            LOG_INFO / LOG_DEBUG / LOG_ERROR macros
│   │   └── platform.h           printSimulationBanner, printPhaseHeader, GPGPU_CLOCK_PERIOD_NS
│   ├── top/
│   │   ├── top.h / top.cpp      GPGPUTop: single-GPU top-level module
│   ├── system_top/
│   │   ├── system_top.h/.cpp    SystemTop: multi-GPU wrapper (N × GPGPUTop)
│   ├── scheduler/
│   │   ├── warp_scheduler.h/.cpp  round-robin warp dispatch across CUs
│   ├── simt_controller/
│   │   ├── simt_controller.h/.cpp IPDOM stack, divergence tracking, barriers
│   ├── memory/
│   │   ├── memory_hierarchy.h/.cpp L1/L2/global/shared memory (write-through)
│   └── compute_unit/
│       ├── compute_unit.h/.cpp  ALU + RVV unit + memory ops + SIMT execution
├── test/
│   ├── Makefile
│   ├── regression_test.cpp      full correctness suite (Phases 0–13)
│   └── benchmark_test.cpp       stress benchmarks + design space exploration
└── README.md                    ← you are here
```

---

## Build and run

```bash
cd test/

make all        # build both binaries
make regression # build + run full regression suite (all phases pass → green)
make benchmark  # build + run stress benchmark + design space tables
make clean      # remove build artefacts
make info       # show SystemC path, source count
```

**Requirement:** SystemC 2.3.4 at `/usr/local` (override with
`make SYSTEMC_HOME=/your/path`).

---

## Running a new kernel in 30 seconds

### 1. Add a function to `src/common/kernel_programs.h`

```cpp
// [TOP] Multiply every element by 2
inline std::vector<Instruction> scaleByTwo() {
    return {
        makeInstr(Opcode::ADDI, 3, 0, 0, 2),   // r3 = 2
        makeInstr(Opcode::VMUL, 4, 1, 3, 0),   // r4 = r1 * 2  (r1 = global_tid)
        makeInstr(Opcode::HALT)
    };
}
```

### 2. Launch it from `benchmark_test.cpp` or `regression_test.cpp`

```cpp
top.launchKernel(4, 1, kernels::scaleByTwo());  // 4 warps
sc_core::sc_start(sc_core::sc_time(200, sc_core::SC_NS));
```

### 3. Run

```bash
make benchmark
```

---

## Register convention (`buildWarpContext`)

Every warp context is initialised by `GPGPUTop::buildWarpContext` before
`executeWarp` is called. The following registers are **pre-set** — do not
overwrite them unless you know what you are doing:

| Register | Value | Meaning |
|----------|-------|---------|
| `r0[t]` | `0` | Zero register (always) |
| `r1[t]` | `global_tid` | Unique thread ID across all warps and GPUs |
| `r2[t]` | `0x10000 + global_tid × 4` | Unique 4-byte-aligned memory address per thread |
| `r3[t]` | `local_warp_id` | 0 for the first warp of a kernel, 1 for the second, … |
| `r4–r31` | `0` | Free for the kernel to use |

`global_tid = (warp_id_offset + warp_id) × threads_per_warp + t`

For [DIRECT] kernels (run via `cu.executeWarp(ctx)` without going through
`launchKernel`), the caller sets ALL registers manually before the call.

---

## Available opcodes (`types.h` → `enum class Opcode`)

### Scalar integer ALU
| Opcode | Encoding | Operation |
|--------|----------|-----------|
| `ADD`  | 0x00 | `rd = rs1 + rs2` |
| `SUB`  | 0x01 | `rd = rs1 - rs2` |
| `AND`  | 0x02 | `rd = rs1 & rs2` |
| `OR`   | 0x03 | `rd = rs1 \| rs2` |
| `XOR`  | 0x04 | `rd = rs1 ^ rs2` |
| `SLT`  | 0x05 | `rd = (rs1 < rs2) ? 1 : 0` (signed) |
| `ADDI` | 0x10 | `rd = rs1 + imm` |
| `LUI`  | 0x11 | `rd = imm << 12` |

### Scalar FP (registers store IEEE 754 bits — use `floatAsReg` / `regAsFloat`)
| Opcode | Encoding | Operation |
|--------|----------|-----------|
| `FADD` | 0x06 | `rd = float(rs1) + float(rs2)` |
| `FMUL` | 0x08 | `rd = float(rs1) × float(rs2)` |

### Memory
| Opcode | Encoding | Operation |
|--------|----------|-----------|
| `LW`   | 0x20 | `rd = mem[rs1 + imm]` (4-byte word) |
| `SW`   | 0x21 | `mem[rs1 + imm] = rs2` (write-through) |

### SIMT control — VBRANCH / VJOIN (Option A semantics)
| Opcode | Encoding | Semantics |
|--------|----------|-----------|
| `VBRANCH` | 0x32 | Threads where `rs1[t] == 0` **fall through** (stay active); threads where `rs1[t] != 0` are **masked** until the matching VJOIN. No divergence event is generated when all threads agree. |
| `VJOIN`   | 0x33 | Restores masked threads from the IPDOM stack. |

### Integer vector (RVV-style — operates on all active thread lanes)
| Opcode | Encoding | Operation |
|--------|----------|-----------|
| `VADD`   | 0x40 | `rd[t] = rs1[t] + rs2[t]` |
| `VSUB`   | 0x41 | `rd[t] = rs1[t] - rs2[t]` |
| `VMUL`   | 0x42 | `rd[t] = rs1[t] × rs2[t]` |
| `VFMADD` | 0x43 | `rd[t] = rs1[t] × rs2[t] + rd[t]` (integer FMA) |

### FP vector
| Opcode | Encoding | Operation |
|--------|----------|-----------|
| `VFADD`   | 0x48 | `rd[t] = float(rs1[t]) + float(rs2[t])` |
| `VFSUB`   | 0x49 | `rd[t] = float(rs1[t]) - float(rs2[t])` |
| `VFMUL`   | 0x4A | `rd[t] = float(rs1[t]) × float(rs2[t])` |
| `VFFMADD` | 0x4B | `rd[t] = float(rs1[t]) × float(rs2[t]) + float(rd[t])` |

### Synchronisation
| Opcode | Encoding | Semantics |
|--------|----------|-----------|
| `BARRIER` | 0x70 | `imm` = barrier ID. Suspends the warp until **all warps** in the kernel have reached the same barrier ID, then all resume together. Multiple distinct barrier IDs are supported per kernel. |

### Control
| Opcode | Encoding | Operation |
|--------|----------|-----------|
| `HALT` | 0xFF | End of warp program. |

### FP helper functions (defined in `types.h`)
```cpp
floatAsReg(float f)    → uint32_t   // store a float value in a register
regAsFloat(uint32_t b) → float      // read a float value from a register
```

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
| **No cycle counts** | `getTotalCycles()` always returns 0. Cycle-accurate timing comes from the HLS synthesis step. |
| **No ITOF** | No integer-to-float conversion instruction. FP kernels launched via `top.launchKernel()` use uniform float immediates (same value for all threads). Per-thread float data requires pre-loading from memory or using [DIRECT] kernels. |
| **No ISA decoder** | Programs are hand-coded as `std::vector<Instruction>` using `makeInstr()`. A binary RISC-V decoder is outside the scope of the SystemC model. |
| **TLM socket deferred** | `MemoryHierarchy` has no TLM target socket yet. The compute unit accesses memory via direct method calls (`loadWord`/`storeWord`). |
| **Single-CU per GPGPUTop in practice** | The model supports N CUs (`num_compute_units`), but in `simulationProcess` all CUs share the same memory hierarchy instance. Setting `num_compute_units > 1` distributes warps correctly (verified in DSE-A) but all CU memory traffic goes through one L1/L2. |

---

## Benchmark quick reference

Run `make benchmark` from `test/`. Key metrics from the last run (5 GPUs,
32 threads/warp, 1 CU/GPU):

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
| **Scalability** | 1→5 GPU | 24→120 | — | — |

Design space exploration (DSE) results:
- **Multi-CU (1/2/4):** load distributes as 120/60/30 instr/CU
- **Lane width (32/16 tpw):** 24 vs 48 instructions for 128 threads
- **IPDOM overhead:** 50% lane efficiency loss under full divergence

---

## Authors

ITCR School of Electronics Engineering — Master's programme MP6160  
RISC-V GPGPU co-design project, 2025
