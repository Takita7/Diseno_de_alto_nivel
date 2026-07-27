# RISC-V GPGPU – HLS Implementation

This directory will contain the High-Level Synthesis (HLS) implementation of the
GPGPU compute pipeline, targeting the Kria KV260/KR260 FPGA platform via Vitis HLS.

The HLS blocks are derived directly from the `models/systemc/src/` functional
model. The design principle is: **what the SystemC model validates, the HLS
synthesizes**.

---

## Relationship to the SystemC model

The SystemC model and HLS blocks share the same algorithmic logic. The key
differences are HLS-specific constraints:

| Concern | SystemC model | HLS block |
|---------|--------------|-----------|
| Memory | `std::map<Address, uint32_t>` (dynamic) | Fixed-size BRAM arrays |
| Containers | `std::vector`, `std::unique_ptr` | Static arrays, no heap |
| Interfaces | Direct method calls | `#pragma HLS INTERFACE` AXI4/AXI4-Lite |
| Timing | Not cycle-accurate | Pipeline II=1 target |
| Entry points | `sc_main` / GTest | `void top_func(...)` ap_ctrl_hs |

---

## Directory structure (target)

```
hls/
├── src/
│   ├── compute_pipeline.cpp    fetch / decode / execute pipeline (from ComputeUnit::step)
│   ├── memory_pipeline.cpp     L1/L2 cache + global memory BRAM controller
│   ├── warp_scheduler.cpp      round-robin dispatch (from WarpScheduler)
│   └── simt_controller.cpp     IPDOM divergence stack (from SIMTController)
├── config/
│   └── hls_config.tcl          Vitis HLS solution settings (clock, part, optimizations)
├── pragma/
│   └── interfaces.h            HLS pragma annotations for AXI port binding
└── README.md                   ← you are here
```

---

## Implementation plan

### Phase 1 — Compute pipeline (T022)

Port `ComputeUnit::step()` + `executeRV32()` from `src/compute_unit/compute_unit.cpp`.

Key pragmas:
```cpp
// hls/src/compute_pipeline.cpp
void compute_pipeline(
    uint32_t* instr_mem,    // AXI4 master — instruction fetch
    uint32_t* data_mem,     // AXI4 master — load/store
    uint32_t* reg_file,     // internal BRAM (32 × num_warps)
    volatile uint32_t* ctrl // AXI4-Lite — PC_INIT, CTRL, STATUS
) {
#pragma HLS INTERFACE m_axi port=instr_mem bundle=imem
#pragma HLS INTERFACE m_axi port=data_mem  bundle=dmem
#pragma HLS INTERFACE s_axilite port=ctrl
#pragma HLS PIPELINE II=1
    // ... fetch → decode → execute ...
}
```

### Phase 2 — Memory hierarchy (T023)

Port `MemoryHierarchy::loadWord/storeWord` with:
- L1 cache as `#pragma HLS ARRAY_PARTITION` BRAM
- Write-through policy retained
- `writeBytes`/`readBytes` mapped to AXI4 burst transfers

### Phase 3 — Warp scheduler (T022)

Port `WarpScheduler::selectWarp()` — small FSM, synthesizes cleanly.

### Phase 4 — SIMT controller (T047b)

Port `SIMTController::handleBranch/handleJoin` after reconvergence PC
tracking is completed in the SystemC model.

---

## Synthesis targets

| Parameter | Value |
|-----------|-------|
| Tool | Vitis HLS 2023.x |
| Part | xck26-sfvc784-2LV-c (Kria K26 SOM) |
| Clock | 200 MHz (5 ns period) |
| Top function | `gpgpu_top` |

Synthesis scripts will be added to `fpga/scripts/` as part of T025.

---

## Traceability to tasks

| Task | Description | Status |
|------|-------------|--------|
| T021 | HLS interface contracts (`docs/hls/interfaces.md`) | Pending |
| T022 | Compute + warp scheduler HLS implementation | Pending |
| T023 | Memory/load-store pipeline HLS implementation | Pending |
| T024 | Synthesis configuration, pragmas, directives | Pending |
| T025 | RTL generation and FPGA build scripts | Pending |

---

## Pre-conditions before starting HLS work

These SystemC model features should be solid before porting to HLS:

- [x] `ComputeUnit::step()` executes full RV32I+M instruction set
- [x] `MemoryHierarchy::writeBytes/readBytes` bulk access validated
- [x] `WarpScheduler` round-robin dispatch verified by regression tests
- [ ] `SIMTController` reconvergence PC tracking complete (T047b)
- [ ] `KernelBridge` refactored to use `GPGPUTop` (T048b)
