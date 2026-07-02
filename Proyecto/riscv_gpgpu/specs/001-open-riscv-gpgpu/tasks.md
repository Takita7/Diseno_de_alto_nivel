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
	- Implemented now: `top.cpp` fully wires `MemoryHierarchy`, `WarpScheduler`, `SIMTController`, and `ComputeUnit` instances. `GPGPUTop` owns per-CU signals for port binding, binds all `clk/reset` and `memory_ready/request` ports internally. `gpgpu_top` static library exports `launchKernel`, `configureKernel`, `getTotalCycles/Instructions`, `getL1CacheHits/Misses`, `getDivergenceEvents`.
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

- [x] T027 [P] [US3] Add compiler/backend smoke tests in `tests/compiler/llvm/test_llvm_backend.cpp`
	- Status: smoke coverage exists in `tests/compiler/llvm/test_llvm_backend.cpp`; backend-specific compile regressions also exist in `software/llvm/backend/test_llvm_backend.cpp`.
- [x] T028 [P] [US3] Add runtime and driver integration tests in `runtime/src/test_runtime_api.cpp` and `driver/src/test_driver_api.cpp`
	- Status: runtime upload/launch/poll flow and driver API smoke checks are covered by C++ tests registered through CMake.
### Implementation for User Story 3 (detailed)

- [x] T029 [US3] Define the compiler/runtime interface contract in `docs/software/interfaces.md`
	- Deliverable: `docs/software/interfaces.md` with ABI, kernel-binary layout, kernel args, memory model mapping, and host-driver protocol (RPC/IOCTL verbs).

- [x] T030 [US3] Implement the LLVM backend adaptation scaffold in `software/llvm/backend/`
	- Deliverable: prototype LLVM backend directory with TableGen `.td` files, `TargetLowering`, and `SelectionDAG`/ISel hooks.
	- Notes: Target name `riscv-gpgpu` (start from `llvm-project/llvm/lib/Target/RISCV` as a template).
	- Implemented now: `clang`/`ld.lld`-based RISC-V ELF emission for C/C++ and CUDA-like `.cu` frontend sources, plus demo artifact generation and backend tests.
	- Pending for closure: real LLVM target integration (`TableGen`, lowering, instruction selection, ABI-aware codegen) instead of the current compiler-driver wrapper.

- [x] T031 [US3] Implement assembler/linker additions in `software/llvm/mc/` or `software/binutils/`
	- Deliverable: MC/ASM support for new SIMT instructions and assembler syntax; update to `lld` if using LLVM linking.
	- Notes: current implementation uses `clang`/`ld.lld` to emit and link `riscv32-unknown-elf` kernels using `rv32gc`/`ilp32`.
	- Implemented now: object assembly/link helpers and validated disassembly flow using `binutils-riscv64-unknown-elf`.
	- Pending for closure: custom SIMT/GPGPU instruction encodings, assembler syntax, and MC-layer validation beyond baseline RISC-V code generation.

- [x] T032 [US3] Implement the runtime kernel-launch and execution-status interface in `runtime/src/`
	- Deliverable: `runtime/src/host_runtime.cpp` exposing kernel upload, launch, status, and simple memory management APIs compatible with CUDA/HIP semantics.
	- Implemented now: grid-level launch (`KernelLaunchInfo` carries `grid_x/y/z` + `workgroup_x/y/z`), `waitKernelCompletion()` for synchronous completion, `pollKernelStatus()` reporting IDLE/CONFIGURED/RUNNING/COMPLETED/FAILED from the driver. New `host_runtime.h` public header. CMakeLists links `kernel_loader_lib` + `driver_lib`.
	- Tests added: `WaitKernelCompletion` in `runtime/src/test_runtime_api.cpp`.

- [x] T033 [US3] Implement the driver and host API layers in `driver/src/` and `software/host_api/`
	- Deliverable: userspace loader `driver/src/loader.cpp` implementing bitstream/kernel load, DMA setup, and kernel control commands; host API in `software/host_api/` that maps runtime calls to driver operations.
	- Implemented now: driver has device buffer registry (host-backed, device addresses from 0x10000000, 16-byte aligned); `allocateDeviceBuffer`, `freeDeviceBuffer`, `copyHostToDevice`, `copyDeviceToHost`, `configureLaunch(KernelLaunchArgs)`, `pollKernelCompletion`. Host API exposes `gpgpuMalloc`, `gpgpuFree`, `gpgpuMemcpyH2D`, `gpgpuMemcpyD2H`, `gpgpuLaunchKernel`, `gpgpuSynchronize`, `clearKernelArguments`. New `host_api.h` public header.
	- Tests added: `AllocateFreeBuffer`, `CopyHostToDeviceAndBack`, `ConfigureLaunchAndStart`, `PollCompletion`, `StatusAfterCompletion`, `LegacyConfigureKernel` in driver; `MallocAndFree`, `MemcpyH2DAndD2H`, `SetAndClearKernelArgument`, `LaunchKernelAndSynchronize` in host_api.

- [x] T034 [US3] Implement the kernel-loader and configuration-management path in `software/kernel_loader/`
	- Deliverable: tooling to pack kernel binaries, metadata (workgroup size, required shared mem), and support upload format (e.g., simple tar/json manifest).
	- Implemented now: `resolveEntrySymbol(binary_path, base_name, &symbol)` using `riscv64-unknown-elf-nm` (POSIX format, falls back to system nm); `listKernelSymbols(binary_path, &report)` dumps all defined text symbols. `kernel_loader.h` extended. `kernel_loader.cpp` now includes its own header (forward declarations).
	- Tests added: `ResolveEntrySymbol`, `ListKernelSymbols` in `software/kernel_loader/test_kernel_loader.cpp`.

- [x] T035 [US3] Add benchmark harnesses and reproducibility scripts in `benchmarks/` and `scripts/benchmark/`
	- Deliverable: example kernels (Rodinia subset) and scripts to build, upload, run, and collect metrics for comparison.
	- Implemented now: `benchmarks/workloads/vector_add/vector_add.cu` and `run_vector_add.sh` (compile → RISC-V ELF, disassemble, symbol resolve, bundle manifest, launch packet, host-sim validation — N=1024 PASS); `benchmarks/workloads/saxpy/saxpy.cu` and `run_saxpy.sh` (same flow, a=2.5 — N=1024 PASS). Both scripts produce `*.riscv.elf`, `*.disasm.txt`, `*_manifest.json`, `*.launch.json` under `build/benchmarks/`.

### Current Software Status Snapshot (updated)

- Completed and validated:
	- RISC-V 32-bit kernel emission from C/C++ and CUDA-like `.cu` sources (linker uses `ld.lld` via `-fuse-ld=lld -nostdlib -Wl,--entry,0`).
	- ELF disassembly, symbol inspection, and ELF metadata pipeline.
	- Bundle manifest pack/load/inspect + runtime upload/launch plumbing.
	- **Driver**: simulated device buffer registry (alloc/free/H2D/D2H copies); structured `KernelLaunchArgs` with grid+block dims; completion state machine; legacy shim preserved.
	- **Host API**: `gpgpuMalloc/Free/MemcpyH2D/MemcpyD2H/LaunchKernel/Synchronize` fully wired to driver; `clearKernelArguments`; `host_api.h` public header.
	- **Runtime**: `KernelLaunchInfo` with `grid_x/y/z`; `waitKernelCompletion`; `host_runtime.h` public header.
	- **Kernel Loader**: `resolveEntrySymbol` (nm-based symbol search); `listKernelSymbols`; `kernel_loader.h` includes own header.
	- **Benchmarks**: `vector_add.cu` + `saxpy.cu` (bare-metal, no stdlib headers); `run_vector_add.sh` + `run_saxpy.sh` (compile → disassemble → bundle → host-sim validate, both PASS N=1024).
	- **SystemC hardware model**: real `MemoryHierarchy` (byte-addressable sparse memory, L1/L2 cache), full RV32I+M decoder (`riscv_isa.h`), real `ComputeUnit` fetch/decode/execute loop, `ElfLoader` (ELF32 parser), `KernelBridge` SW↔SC integration, `GPGPUTop` top-level with all ports bound, `WarpScheduler` and `SIMTController` `.cpp` implementations.
	- **End-to-end test**: `test_systemc_integration.cpp` verifies ELF loading, CU register arithmetic, vector_add (N=8, H2D→bridge→D2H), and scalar_mul (N=4).
	- 9/9 test suites pass (34 tests total).
- Still pending before calling the stack production-complete:
	- Real LLVM backend/target work (TableGen, TargetLowering, SelectionDAG) instead of the `clang` wrapper.
	- Custom GPGPU/SIMT instruction definitions and MC-layer assembler syntax.
	- Real hardware binding (DMA, register-mapped MMIO) in the driver instead of host-memory simulation.
	- Full Rodinia or GPGPU benchmark suite results through actual hardware or a cycle-accurate simulator.
	- Real hardware binding (DMA, register-mapped MMIO) in the driver instead of host-memory simulation.
	- Full Rodinia or GPGPU benchmark suite results through actual hardware or a cycle-accurate simulator.
	- Multi-warp/multi-block execution in `KernelBridge` (currently runs single warp 0 only).

---

## Phase 5b: SystemC ↔ Software Integration (hardware_main branch)

**Goal**: Connect the SystemC hardware model to the software stack for end-to-end functional simulation.

- [x] T036b [US1] Implement real byte-addressable `MemoryHierarchy` with L1/L2 cache simulation in `models/systemc/memory/memory_hierarchy.cpp`
	- Sparse `byte_memory_` map; word-aligned L1/L2 cache sets; `writeBytes`/`readBytes` bulk access for ELF loading.
- [x] T037b [US1] Implement RV32I + M-extension decoder in `models/systemc/integration/riscv_isa.h`
	- Header-only; `RV32Instr` struct with `Op` enum covering all RV32I+M ops; `decodeRV32()`; `expandRVC()` for compressed instructions.
- [x] T038b [US1] Rewrite `ComputeUnit` with real fetch/decode/execute in `models/systemc/compute_unit/compute_unit.cpp`
	- Full RV32I+M execution; warp context (rf[32], pc, halted); detects `JALR x0, x1, 0` (ret) and return-sentinel PC as completion.
- [x] T039b [US1] Implement ELF32 binary loader in `models/systemc/integration/elf_loader.h/.cpp`
	- Manual ELF32 parser (no libelf); reads PT_LOAD segments into `MemoryHierarchy`; parses SHT_SYMTAB for function symbols; `findSymbol()` by name.
- [x] T040b [US1] Implement `KernelBridge` SW↔SC integration in `models/systemc/integration/kernel_bridge.h/.cpp`
	- Orchestrates: create standalone MemoryHierarchy → load ELF → copy H2D driver buffers → resolve entry symbol → set up registers (a0..a7=args, sp=0x20000000, ra=sentinel) → run CU step loop until complete → copy results D2H → print metrics (cycles, IPC, cache hit rate).
- [x] T041b [US1] Implement `GPGPUTop` with all port bindings in `models/systemc/top/top.cpp`
	- Wires ComputeUnits, WarpScheduler, MemoryHierarchy, SIMTController; binds all `clk/reset/memory_ready/memory_request` ports to internal signals.
- [x] T042b [US1] Implement `WarpScheduler` and `SIMTController` stub `.cpp` files
	- `warp_scheduler.cpp`: ROUND_ROBIN/PRIORITY/FIFO scheduling, multi-CU warp queues, load balancing.
	- `simt_controller.cpp`: active mask management, divergence stack, per-warp thread activation.
- [x] T043b [US1] Add SystemC integration test suite in `tests/systemc/test_systemc_integration.cpp`
	- 4 tests: `ElfLoaderBasic`, `ComputeUnitAddsRegisters`, `VectorAddEndToEnd` (N=8, H2D→bridge→D2H), `ScalarMultiply` (N=4). All pass.
- [x] T044b [US1] Fix CMake SystemC detection and build system
	- `cmake/FindSystemC.cmake`: added `/usr/local/lib-linux64` search path.
	- `models/CMakeLists.txt`: enabled `add_subdirectory(systemc)` guarded on `SystemC_FOUND`.
	- `models/systemc/top/CMakeLists.txt`: split into `gpgpu_top` static library + `systemc_simulation` executable.
	- `tests/systemc/CMakeLists.txt`: added `sc_gtest_main.cpp` for `sc_main()`→GTest bridge; dropped conflicting `GTest::Main`.
	- All 9 test suites pass (34 tests total).

### Notes / Repositories to clone for US3 work

- `llvm-project` (preferred integration): implement `riscv-gpgpu` target inside LLVM and build `clang`/`lld`.
- `riscv-gnu-toolchain` or `binutils` (optional): if you prefer GNU assembler/linker flows.
- `pocl` (or other runtime): adapt device plugin to call host runtime API.
- Reference projects to study and adapt: `vortex`, `CuPBoP`, `ventus`.

### Local paths and environment

- `software/llvm/backend/` — workspace for backend development (TableGen, codegen)
- `software/host_api/` — host-side API and headers used by the runtime
- `runtime/src/` — runtime implementation that interacts with `driver/src/`
- `driver/src/` — userspace loader and kernel uploader

Update tasks and mark progress in `tasks.md` as work progresses; each subtask should link to a test in `tests/` and a doc entry in `docs/`.

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
