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

- [x] T019 [P] [US2] Add HLS regression tests and resource-estimation checks — landed as 4 separate GTest binaries under `tests/hls/` (`test_hls_data_structures.cpp`, `test_compute_pipeline.cpp`, `test_memory_pipeline.cpp`, `test_pipeline_integration.cpp`) rather than the single `test_hls_pipeline.cpp` this line originally named; superseded that placeholder once T022/T023 landed real parity assertions against the golden model (csim-level, via real Vitis HLS headers + plain g++, not the `vitis_hls` tool itself). No resource-estimation checks here (that's real-tool territory, not csim) — see T020.
- [x] T020 [P] [US2] Add RTL and FPGA flow smoke tests in `tests/fpga/test_flow.tcl` — drives real `vitis_hls -f` batch csynth (not csim) for `compute_pipeline`/`memory_pipeline` against every board with installed device support, skipping (not failing) boards without it. First real run caught a genuine bug T019-era csim never could: `#pragma HLS ARRAY_PARTITION`/`BIND_STORAGE` on class-member arrays (`cache_bank.h`, `memory_pipeline.h`) were placed at class scope instead of inside a constructor — legal C++, silently ignored by plain g++, but a hard Vitis HLS csynth error (`HLS 207-5507`) since pragmas are function-scope-only. Fixed by moving them into `SetAssocCache`'s (new) and `MemorySubsystem`'s constructors. Both kernels now synthesize cleanly on KV260 (U55C still skipped - device support not installed yet in this environment).

### Implementation for User Story 2

- [x] T021 [US2] Define HLS interface contracts and synthesis constraints in `docs/hls/interfaces.md` and `hls/constraints/` — v3, corrected to restore `m_axi` to external memory (DDR on KV260 via PS HP/HPC, HBM pseudo-channel on U55C) with on-chip BRAM scoped to shared/L1/L2 caches only, built as N-way set-associative banks (N parallel direct-mapped arrays). Barriers: host-orchestrated for the first milestone (mirrors `top.cpp`'s `simulationProcess`), not an on-chip barrier unit. See doc §6 for remaining open decisions (`MAX_PROGRAM_LEN`, exact `WAYS`, `m_axi` port binding) before T022/T023 start.
- [x] T022 [US2] Implement the HLS-ready compute pipeline in `hls/src/compute_unit/compute_pipeline.cpp` — direct port of `ComputeUnit::executeWarp()` onto `hls/src/common/hls_types.h` and `hls/src/simt_controller/divergence_stack.h`. Register-file parity verified against 5 `kernel_programs.h` kernels (`tests/hls/test_compute_pipeline.cpp`), incl. barrier stall/resume across two invocations and a threaded memory req/resp stand-in for T023. Added `cu_id` param to the `docs/hls/interfaces.md` SS2.2 signature (needed for response routing, was already implied by `hls_types.h`'s `mem_req_t`/`mem_resp_t`). **Briefly downgraded to [~] then reverted to [x]: see T022b.** A real fork was found between this target and `hls/README.md`'s "port `ComputeUnit::step()`" note; reconsidered and confirmed `executeWarp()` stands — `hls_types.h`'s `Opcode` enum matches `types.h`'s real enum value-for-value, checked directly, and the SystemC model's own documented functionality (not strict RISC-V/RVV compliance) is the standard this port complies with (`docs/hls/interfaces.md` §11.3).
- [x] T022b [US2] **CLOSED, not pursued** — was: realign `compute_pipeline`'s decode stage to `ComputeUnit::step()`/`executeRV32()` (real RV32I) + real RVV decode. Reconsidered: `hls/README.md`'s "port `ComputeUnit::step()`" instruction describes a different target (real RV32I binary execution), not a required correction to the Virtual-ISA path this port has always used and still uses. Full reconsideration, with the original finding kept intact for the later unification audit: `docs/hls/interfaces.md` §11.3. Real RV32I/RVV compliance remains a possible future direction, not a blocker — §12 keeps the 9-step plan on file for that if it's ever picked up.
- [x] T022c [US2] Implement the on-chip warp scheduler/dispatch FSM in `hls/src/scheduler/` (`cu_dispatch_unit.h`, `barrier_arbiter.h`, `mem_arbiter.h`, `gpgpu_top.h`) — ports `WarpScheduler`'s round-robin dispatch and `GPGPUTop::simulationProcess()`'s global barrier resolution fully on-chip, per `hls/README.md`'s own Phase 3 note ("port `WarpScheduler::selectWarp()` — small FSM, synthesizes cleanly"). Global barrier scope kept bit-faithful to the golden model within a new hardware capacity limit (hazard-mitigated, not a silent hang — `docs/hls/interfaces.md` §10.6). Capstone-verified: a two-warp barrier kernel runs to completion driven entirely by the autonomous scheduler (no test-side dispatch/barrier orchestration), matching the golden model exactly. Formal `tests/hls/` GTest coverage (`test_cu_dispatch_unit.cpp`, `test_barrier_arbiter.cpp`, `test_mem_arbiter.cpp`, `test_gpgpu_top.cpp`) still local/uncommitted, not yet on any remote branch.
- [x] T023 [US2] Implement the memory and load/store pipeline in `hls/src/memory/memory_pipeline.cpp` — direct port of `MemoryHierarchy::loadWord()`/`storeWord()` (write-through, no-write-allocate, L1→L2→global chain) onto `hls/src/memory/cache_bank.h`'s `L1Cache`/`L2Cache`, wired to a real `m_axi` pointer for the global tier. `MemorySubsystem` class kept directly testable (shared-mem bypass, L2-hit-refills-L1, m_axi transaction counts — `tests/hls/test_memory_pipeline.cpp`); free-running (`while(true)`) top-level kernel per the persistent-hardware model (`docs/hls/interfaces.md` SS3.3). T022+T023 also verified wired together for real, no mocks on either side (`tests/hls/test_pipeline_integration.cpp`).
- [x] T024 [US2] Add synthesis configuration, pragmas, and target-specific directives in `hls/config/` and `hls/pragma/` — per-board macro config in `hls/config/{kv260,u55c}.h` (ADDR_BITS, m_axi burst/outstanding, consumed by `hls_config.h`/`memory_pipeline.cpp`). Pragmas themselves (`BIND_STORAGE`, `ARRAY_PARTITION`, `PIPELINE`) added inline in `cache_bank.h`/`memory_pipeline.h`/`compute_pipeline.cpp` next to the code they apply to, not under `hls/pragma/` as the path above suggests — standard Vitis HLS practice for class-member pragmas, and the directory stayed a `.keep` placeholder. Found and fixed 2 correctness-for-synthesis gaps: `regs`/cache `WAYS` dimension needed `ARRAY_PARTITION complete` for the `UNROLL` pragmas already present to be synthesizable at all (not an optimization), and `cache_bank.h`'s line fill/read loops were `UNROLL`-ed across `WORDS_PER_LINE=32` (would force expensive full partition) instead of `PIPELINE`d like the sibling m_axi burst loop already was — now consistent. All `tests/hls/*` re-verified passing after (pragmas don't affect csim, this was the regression check). **Caveat**: no `vitis_hls` in this environment — nothing here is validated against real C-synthesis resource/timing reports (see `docs/hls/interfaces.md` §8).
- [ ] T025 [US2] Create RTL generation and FPGA build scripts in `rtl/` and `fpga/scripts/` — no longer blocked (T022b closed, not pursued).
- [ ] T026 [US2] Add FPGA deployment and validation scripts in `fpga/tests/` and `scripts/deploy_fpga.sh`

**Golden-model reconciliation (post-T024)**: `origin/init_gpgpu` advanced past the commit this port was built from (`5a80f01` → `9c4dfea` "GPGPU READY" — real `SIMTController::handleBranch()` bug fix, genuine multi-CU fan-out in `top.cpp`, new `parallelReduction`/`fpGemm`/`conv2d3x3` kernels, new `r3=local_warp_id` register convention). Merged in (clean fast-forward, no file overlap with local uncommitted work) and reconciled — see `docs/hls/interfaces.md` §9 for the full discrepancy list and kernel test-coverage matrix. Net changes: `divergence_stack.h`'s `handleBranch()` fixed to match the 3-case golden logic (was replicating the pre-fix bug); 3 new tests added (`test_compute_pipeline.cpp`'s `FpGemm2x2TileK4`/`Conv2d3x3`, `test_pipeline_integration.cpp`'s `ParallelReductionAcrossTwoWarpsWithBarrier` — the last one is the first HLS-side test exercising a 2-warp barrier, and empirically verified to fail against the pre-fix `divergence_stack.h`, confirming it's a real regression test); §2.4's barrier rationale corrected (design unchanged, premise was stale). `barrierRoundTrip` now ported too (`test_pipeline_integration.cpp::BarrierRoundTripPreservesMemoryAcrossStall`, single-warp — golden usage is a 10-warp multi-GPU launch in `benchmark_test.cpp`, corrected from an earlier mis-citation of `regression_test.cpp`). `fpDivergentSaxpy` now ported too (`test_compute_pipeline.cpp::FpDivergentSaxpy`) — found and documented a real doc bug in the kernel's own comment while porting it: the claimed "even/odd" masking is wrong for its actual `r0 & (r0+1)` branch condition, which produces a sparse `{0,1,3,7,15,31}` fall-through set for sequential thread indices, not alternating even/odd (see `docs/hls/interfaces.md` §9.2). No golden execution of this kernel exists anywhere (grep-verified) to cross-check the fix against, unlike every other kernel in the coverage matrix. **Every kernel in `kernel_programs.h` now has at least one HLS-side test** — the coverage matrix is complete.

**Directory refactor (post-coverage-matrix)**: `hls/src/` was flat (`compute_pipeline.*`/`memory_pipeline.*` at the root, `divergence_stack.h`/`cache_bank.h` parked under `common/` despite being component-specific) — it had drifted from `models/systemc/src/`'s per-component layout, hurting legibility. Reorganized to mirror it: `hls/src/compute_unit/compute_pipeline.{h,cpp}`, `hls/src/simt_controller/divergence_stack.h`, `hls/src/memory/{memory_pipeline.{h,cpp},cache_bank.h}`, `hls/src/common/{hls_config,hls_types}.h` unchanged (genuinely shared across components, same role as `common/types.h` in the golden model). No `scheduler/`/`top/`/`system_top/` directories — those components were never ported (see §2.4; they were always meant to become host software, which doesn't exist yet). Pure file move + include-path fix, no logic changes — verified via a full rebuild/rerun of all 25 HLS tests against the real Vitis HLS headers, identical results.

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
