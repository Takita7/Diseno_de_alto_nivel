# Tasks: FPGA-Based Open RISC-V GPGPU

**Input**: Design documents from `/specs/001-open-riscv-gpgpu/`

**Prerequisites**: plan.md (required), spec.md (required), constitution.md (required), research.md (not present; use plan/spec as authoritative sources)

**Organization**: Tasks are grouped by implementation workstream to enable independent development, verification, and milestone progression.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependency on incomplete work)
- **[Story]**: Which story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

## Constitution-Aligned Execution Rules

- Each task MUST be traceable to a requirement, design decision, or milestone from the plan/spec.
- Tasks affecting interfaces, ISA behavior, memory structure, or toolchain behavior MUST include a verification step and compatibility note.
- Performance-sensitive tasks MUST include a measurement or validation strategy.
- Complex tasks MUST preserve modularity, documentation, and traceability expectations.

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create the repository structure and shared tooling required for the multi-stage implementation workflow.

- [x] T001 Create repository structure for architecture, SystemC, HLS, RTL, software, tests, and documentation in `docs/`, `models/`, `hls/`, `rtl/`, `software/`, `runtime/`, `driver/`, `tests/`, and `benchmarks/`
- [x] T002 Initialize the build and configuration framework with CMake and environment scripts in `CMakeLists.txt`, `cmake/`, and `scripts/`
- [x] T003 [P] Create the baseline documentation skeleton and traceability templates in `docs/architecture/`, `docs/verification/`, and `docs/reproducibility/`
- [x] T004 [P] Add the initial verification and reporting harness templates in `tests/`, `scripts/verify/`, and `scripts/benchmark/`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Establish the foundation for architecture definition, configuration management, and simulation infrastructure.

**⚠️ CRITICAL**: No story work can begin until this phase is complete.

- [x] T005 Define the baseline architecture interface contracts and parameter schema in `docs/architecture/interfaces.md` and `config/arch_config.yaml`
- [x] T006 Implement the shared configuration and parameter parsing infrastructure in `config/` and `software/common/`
- [x] T007 Create the SystemC project skeleton and common simulation utilities in `models/systemc/common/` and `models/systemc/README.md`
- [x] T008 Implement the traceability and evidence-reporting framework in `docs/traceability/` and `scripts/collect_evidence.py`
- [x] T009 Create the initial benchmark and measurement configuration templates in `benchmarks/` and `scripts/benchmark/`

**Checkpoint**: Foundation ready - implementation workstreams can now proceed in parallel.

---

## Phase 3: User Story 1 - Baseline Architecture and SystemC Model (Priority: P1) 🎯 MVP

**Goal**: Deliver a baseline, configurable, traceable architectural model for thread execution, scheduling, SIMT behavior, and memory access.

**Independent Test**: A researcher can compile and run the baseline SystemC model and observe correct behavior for kernel launch, scheduling, divergence, memory access, and synchronization on representative scenarios.

### Tests for User Story 1

- [x] T010 [P] [US1] Add unit tests for scheduler and thread-group dispatch in `tests/systemc/test_scheduler.cpp`
- [x] T011 [P] [US1] Add integration tests for kernel launch, divergence, and memory access in `tests/systemc/test_pipeline.cpp`

### Implementation for User Story 1

- [x] T012 [US1] Define the baseline ISA and execution semantics in `docs/architecture/isa.md`
- [x] T013 [US1] Implement the baseline compute unit model in `models/systemc/compute_unit.cpp`
- [x] T014 [US1] Implement the warp scheduler and dispatch model in `models/systemc/warp_scheduler.cpp`
- [x] T015 [US1] Implement the SIMT controller and divergence/reconvergence behavior in `models/systemc/simt_controller.cpp`
- [x] T016 [US1] Implement the memory hierarchy and shared-memory model in `models/systemc/memory_hierarchy.cpp`
- [x] T017 [US1] Integrate the architecture components into an executable top-level SystemC model in `models/systemc/top.cpp`
- [x] T018 [US1] Add configuration-driven scenario scripts and simulation entry points in `scripts/run_systemc_sim.sh` and `scripts/scenarios/`

**Checkpoint**: At this point, the baseline architecture model is functional and independently testable.

---

## Phase 4: User Story 2 - HLS, RTL, and FPGA Path (Priority: P2)

**Goal**: Translate the validated architecture into an HLS-ready implementation and prepare the path to RTL and FPGA deployment.

**Independent Test**: A researcher can synthesize the HLS design and generate RTL artifacts for a representative configuration with known resource and timing estimates.

### Tests for User Story 2

- [ ] T019 [P] [US2] Add HLS regression tests and resource-estimation checks in `tests/hls/test_hls_pipeline.cpp`
- [ ] T020 [P] [US2] Add RTL and FPGA flow smoke tests in `tests/fpga/test_flow.tcl`

### Implementation for User Story 2

- [ ] T021 [US2] Define HLS interface contracts and synthesis constraints in `docs/hls/interfaces.md` and `hls/constraints/`
- [ ] T022 [US2] Implement the HLS-ready compute pipeline in `hls/src/compute_pipeline.cpp`
- [ ] T023 [US2] Implement the memory and load/store pipeline in `hls/src/memory_pipeline.cpp`
- [ ] T024 [US2] Add synthesis configuration, pragmas, and target-specific directives in `hls/config/` and `hls/pragma/`
- [ ] T025 [US2] Create RTL generation and FPGA build scripts in `rtl/` and `fpga/scripts/`
- [ ] T026 [US2] Add FPGA deployment and validation scripts in `fpga/tests/` and `scripts/deploy_fpga.sh`

**Checkpoint**: At this point, the HLS/RTL/FPGA path is independently testable and ready for hardware validation.

---

## Phase 5: User Story 3 - Compiler, Runtime, and Benchmark Integration (Priority: P3)

**Goal**: Provide the software stack required for kernel compilation, runtime execution, driver interaction, and benchmark evaluation.

**Independent Test**: A researcher can compile a representative kernel, launch it through the runtime, and observe execution status and benchmark results from the end-to-end workflow.

### Tests for User Story 3

- [ ] T027 [P] [US3] Add compiler/backend smoke tests in `tests/compiler/test_llvm_backend.py`
- [ ] T028 [P] [US3] Add runtime and driver integration tests in `tests/runtime/test_runtime_api.py`

### Implementation for User Story 3

- [ ] T029 [US3] Define the compiler/runtime interface contract in `docs/software/interfaces.md`
- [ ] T030 [US3] Implement the LLVM backend adaptation scaffold in `software/llvm/backend/`
- [ ] T031 [US3] Implement the runtime kernel launch and execution-status interface in `runtime/src/`
- [ ] T032 [US3] Implement the driver and host API layers in `driver/src/` and `software/host_api/`
- [ ] T033 [US3] Implement the kernel-loader and configuration-management path in `software/kernel_loader/`
- [ ] T034 [US3] Add benchmark harnesses and reproducibility scripts in `benchmarks/` and `scripts/benchmark/`
- [ ] T035 [US3] Publish verification and benchmark report templates in `docs/verification/` and `docs/reproducibility/`

**Checkpoint**: At this point, the software stack, runtime, and benchmark flow are independently functional.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Improve completeness, reproducibility, and maintainability across all implementation workstreams.

- [ ] T036 [P] Refresh architecture and software documentation in `docs/`
- [ ] T037 Refactor shared code and configuration paths to reduce duplication across `models/`, `hls/`, `runtime/`, and `software/`
- [ ] T038 Run end-to-end benchmark comparison and capture results in `docs/verification/benchmark_results.md`
- [ ] T039 [P] Add release checklist and reproducibility package contents in `docs/reproducibility/` and `REPRODUCIBILITY.md`
- [ ] T040 Validate the full traceability chain from requirements to evidence for all major artifacts

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies - can start immediately.
- **Foundational (Phase 2)**: Depends on Setup completion - BLOCKS all story work.
- **User Story 1 (Phase 3)**: Depends on Foundational completion.
- **User Story 2 (Phase 4)**: Depends on User Story 1 completion and the shared foundation.
- **User Story 3 (Phase 5)**: Depends on User Story 1 and the shared foundation.
- **Polish (Phase 6)**: Depends on all desired implementation workstreams being complete.

### User Story Dependencies

- **US1**: Can start after Foundational; no dependency on US2 or US3.
- **US2**: Depends on US1 for architecture and interface stability.
- **US3**: Depends on US1 for execution semantics and on the shared foundation.

### Parallel Opportunities

- Setup tasks T003 and T004 can run in parallel.
- Foundational tasks T008 and T009 can run in parallel with T007 after shared setup.
- Tests for US1 and US2 can be developed in parallel once the foundational components are available.
- Documentation and reproducibility tasks can run in parallel with implementation work in later phases.

---

## Implementation Strategy

### MVP First (US1 Only)

1. Complete Phase 1: Setup.
2. Complete Phase 2: Foundational.
3. Complete Phase 3: US1 baseline model and tests.
4. **STOP and VALIDATE**: Run the baseline SystemC simulation and confirm expected functional behavior.
5. Extend to US2 and US3 only after the MVP is validated.

### Incremental Delivery

1. Establish architecture and configuration foundation.
2. Deliver the baseline SystemC model and validation harness.
3. Add HLS/RTL/FPGA path and validate synthesis readiness.
4. Add compiler/runtime integration and benchmark harness.
5. Complete reproducibility, documentation, and cross-cutting quality improvements.

### Parallel Team Strategy

With multiple contributors:

1. One contributor completes the shared foundation and architecture contracts.
2. A second contributor delivers the SystemC model and tests.
3. A third contributor develops the HLS/RTL/FPGA path.
4. A fourth contributor implements the software toolchain, runtime, and benchmarks.
