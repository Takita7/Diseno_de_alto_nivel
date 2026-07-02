# Traceability Matrix

Detailed traceability mapping requirements to implementation and verification artifacts.

## System Requirements

| Req ID | Category | Requirement | Component | Implementation File | Test File | Status |
|--------|----------|-------------|-----------|-------------------|----------|--------|
| REQ-EXE-001 | Execution | Support up to 32 threads per warp | Compute Unit | models/systemc/compute_unit.cpp | tests/systemc/test_compute_unit.cpp | Planned |
| REQ-EXE-002 | Execution | Support configurable warps per CU | Warp Scheduler | models/systemc/warp_scheduler.cpp | tests/systemc/test_scheduler.cpp | Planned |
| REQ-SIMT-001 | SIMT | Implement SIMT divergence handling | SIMT Controller | models/systemc/simt_controller.cpp | tests/systemc/test_simt.cpp | Planned |
| REQ-SIMT-002 | SIMT | Support immediate/deferred reconvergence | SIMT Controller | models/systemc/simt_controller.cpp | tests/systemc/test_simt.cpp | Planned |
| REQ-MEM-001 | Memory | Implement shared memory per CU | Memory Hierarchy | models/systemc/memory_hierarchy.cpp | tests/systemc/test_memory.cpp | Planned |
| REQ-MEM-002 | Memory | Support L1/L2 cache hierarchy | Memory Hierarchy | models/systemc/memory_hierarchy.cpp | tests/systemc/test_memory.cpp | Planned |
| REQ-SYNC-001 | Synchronization | Implement barrier synchronization | Synchronization | models/systemc/synchronization.cpp | tests/systemc/test_sync.cpp | Planned |
| REQ-SYNC-002 | Synchronization | Support atomic operations | Synchronization | models/systemc/synchronization.cpp | tests/systemc/test_sync.cpp | Planned |

## Design Decisions

| Decision ID | Area | Decision | Rationale | Impact |
|-------------|------|----------|-----------|--------|
| DEC-001 | Architecture | Use configurable parameter schema | Enable design-space exploration | All components |
| DEC-002 | Scheduler | Implement round-robin as default policy | Balance simple implementation with effectiveness | Warp Scheduler |
| DEC-003 | SIMT | Use stack-based reconvergence | Support arbitrary control flow | SIMT Controller |
| DEC-004 | Memory | Model realistic cache latencies | Enable performance prediction | Memory Hierarchy |

## Verification Plan

| Test ID | Component | Test Type | Expected Result | Acceptance Criteria |
|---------|-----------|-----------|-----------------|-------------------|
| TEST-001 | Scheduler | Unit | Warps dispatch in correct order | Dispatch order matches policy |
| TEST-002 | SIMT | Unit | Divergence paths tracked | Active mask correct after divergence |
| TEST-003 | Memory | Unit | Cache hits/misses recorded | Latency matches model |
| TEST-004 | Integration | Integration | Kernel runs to completion | All threads reach barrier |

## Artifact Locations

- **Architecture Specification**: `docs/architecture/`
- **Implementation**: `models/systemc/`, `hls/src/`, `rtl/`
- **Tests**: `tests/`
- **Documentation**: `docs/`
- **Configuration**: `config/`
- **Benchmarks**: `benchmarks/`

## Verification Evidence

Evidence is collected in the following directories:

- **Unit Test Results**: `results/unit_tests/`
- **Integration Test Results**: `results/integration_tests/`
- **FPGA Test Results**: `results/fpga/`
- **Benchmark Results**: `results/benchmarks/`
- **Documentation**: `docs/verification/`

## Status

- Traceability matrix initialized
- To be updated as implementation progresses
