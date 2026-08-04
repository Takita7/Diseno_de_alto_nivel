# Traceability Matrix

Validated requirements-to-evidence chain for major artifacts.

## Requirements to Implementation and Evidence

| Req ID | Requirement | Major implementation artifacts | Verification artifacts | Status |
|---|---|---|---|---|
| REQ-EXE-001 | RV32 kernel execution through SystemC integration | `models/systemc/src/compute_unit/`, `models/systemc/integration/kernel_bridge.cpp`, `models/systemc/integration/elf_loader.cpp` | `tests/systemc/test_systemc_integration.cpp`, CTest systemc suites | Implemented |
| REQ-SIMT-001 | SIMT divergence and reconvergence behavior | `models/systemc/src/simt_controller/`, `models/systemc/integration/kernel_bridge.cpp` | `tests/systemc/test_simt_controller.cpp`, integration divergence checks | Implemented |
| REQ-SCHED-001 | Multi-CU warp scheduling behavior | `models/systemc/src/scheduler/warp_scheduler.cpp`, `models/systemc/src/top/top.cpp` | `tests/systemc/test_scheduler.cpp` | Implemented |
| REQ-MEM-001 | Shared/global memory behavior and barrier synchronization | `models/systemc/src/memory/memory_hierarchy.cpp`, `models/systemc/src/compute_unit/compute_unit.cpp` | `tests/systemc/test_shared_memory.cpp` | Implemented |
| REQ-COMP-001 | PTX to RISC-V compilation path | `driver/src/ptx_transpiler/` | `tests/compiler/ptx/test_ptx_parser.cpp`, `tests/compiler/ptx/test_rv_emitter.cpp`, `tests/compiler/ptx/test_ptx_transpiler.cpp` | Implemented |
| REQ-BENCH-001 | End-to-end benchmark execution and metric capture | `benchmarks/rodinia_real_benchmark.cpp`, `scripts/benchmark/run_rodinia_real_matrix.sh`, `scripts/benchmark/analyze_results.py` | `results/benchmarks/rodinia_real_matrix/summary.tsv`, `results/benchmarks/rodinia_real_matrix/summary.json`, `docs/verification/benchmark_results.md` | Implemented |
| REQ-FPGA-001 | ARM host to FPGA control plane for Kria deployment | `driver/src/fpga_driver.cpp`, `driver/src/fpga_elf_loader.cpp`, `driver/src/fpga_regs.h`, `software/host_api/host_api.cpp`, `scripts/deploy_kria.sh` | `tests/fpga/test_fpga_driver.cpp`, `docs/architecture/axi_interface.md`, `docs/verification/kria_results.md` | Code complete, hardware evidence pending |

## Evidence Chain Status

| Chain stage | Artifact location | Validation state |
|---|---|---|
| Requirement and task intent | `specs/001-open-riscv-gpgpu/tasks.md` | Updated through Phase 7 + Phase 6 documentation tasks |
| Architecture and interface contracts | `docs/architecture/` | Updated (includes AXI contract) |
| Implementation | `models/`, `driver/`, `runtime/`, `software/`, `benchmarks/` | Present and buildable |
| Automated verification | `tests/`, CTest suites | Passing in local run baseline |
| Benchmark evidence | `results/benchmarks/`, `docs/verification/benchmark_results.md` | Captured |
| FPGA hardware evidence | `docs/verification/kria_results.md` | Pending physical board run |

## Gaps

- Hardware execution evidence on Kria board is pending and tracked explicitly in
	`docs/verification/kria_results.md`.
