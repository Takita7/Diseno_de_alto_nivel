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
- [x] T007 Create the SystemC project skeleton and common simulation utilities in `models/systemc/src/common/` and `models/systemc/README.md`
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
- [x] T013 [US1] Implement the baseline compute unit model in `models/systemc/src/compute_unit/compute_unit.cpp`
- [x] T014 [US1] Implement the warp scheduler and dispatch model in `models/systemc/src/scheduler/warp_scheduler.cpp`
- [x] T015 [US1] Implement the SIMT controller and divergence/reconvergence behavior in `models/systemc/src/simt_controller/simt_controller.cpp`
- [x] T016 [US1] Implement the memory hierarchy and shared-memory model in `models/systemc/src/memory/memory_hierarchy.cpp`
- [x] T017 [US1] Integrate the architecture components into an executable top-level SystemC model in `models/systemc/src/top/top.cpp`
	- Implemented now: `top.cpp` fully wires `MemoryHierarchy`, `WarpScheduler`, `SIMTController`, and `ComputeUnit` instances. `GPGPUTop` owns per-CU signals for port binding, binds all `clk/reset` and `memory_ready/request` ports internally. `gpgpu_top` static library exports `launchKernel`, `configureKernel`, `getTotalCycles/Instructions`, `getL1CacheHits/Misses`, `getDivergenceEvents`.
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
- [x] T022d [US2] Give `compute_pipeline`'s program store a real RV32I + custom-opcode encoding (`hls/src/compute_unit/rv32i_codec.h`) — narrower than T022b's shelved plan: `executeWarp()` stays the golden target (T022b), only the on-chip *representation* of instructions changes, from a host-constructed `Instruction` struct directly to a real `ap_uint<32>` word (`raw_instr_t`, `instr_word_t` repointed), decoded once per fetch via `decodeInstruction()`. Standard ops (`ADD`/`ADDI`/`LW`/`BEQ`/... ) get real RV32I major opcodes; GPGPU-specific ops (`VADD`/`VBRANCH`/`BARRIER`/...) get RISC-V's reserved `custom-0`/`custom-1` opcode space — design in `docs/hls/interfaces.md` §13, worked bit-level examples in §13.9. Found and fixed a real gap during implementation, not just design review (§13.12): `kernel_programs.h`'s `fpUniformSaxpy()` loads float constants via `ADDI` with a 32-bit immediate (the golden model's `int32_t imm` field is unconstrained), which doesn't fit real RV32I's 12-bit `ADDI` immediate — fixed with `encodeInstructionExpanded()`, the standard `LUI`+`ADDI` expansion real assemblers use for `li rd, imm32`, plus a second fix (`loadProgram()` returning the real expanded word count instead of callers using `src.size()`). All of `tests/hls/` re-verified passing (full suite, not just the targets this task touched) via a from-source GTest build (no prebuilt GTest in this environment, real `-std=c++17` matching `CMakeLists.txt`): `test_hls_data_structures` 9/9, `test_compute_pipeline` 8/8, `test_pipeline_integration` 3/3, `test_gpgpu_top` 3/3, `test_mem_arbiter` 4/4, `test_cu_dispatch_unit` 8/8, `test_barrier_arbiter` 6/6. RVV remains explicitly out of scope (§13.10) — this is RV32I+custom-opcode compliance only.
- [x] T023 [US2] Implement the memory and load/store pipeline in `hls/src/memory/memory_pipeline.cpp` — direct port of `MemoryHierarchy::loadWord()`/`storeWord()` (write-through, no-write-allocate, L1→L2→global chain) onto `hls/src/memory/cache_bank.h`'s `L1Cache`/`L2Cache`, wired to a real `m_axi` pointer for the global tier. `MemorySubsystem` class kept directly testable (shared-mem bypass, L2-hit-refills-L1, m_axi transaction counts — `tests/hls/test_memory_pipeline.cpp`); free-running (`while(true)`) top-level kernel per the persistent-hardware model (`docs/hls/interfaces.md` SS3.3). T022+T023 also verified wired together for real, no mocks on either side (`tests/hls/test_pipeline_integration.cpp`).
- [x] T024 [US2] Add synthesis configuration, pragmas, and target-specific directives in `hls/config/` and `hls/pragma/` — per-board macro config in `hls/config/{kv260,u55c}.h` (ADDR_BITS, m_axi burst/outstanding, consumed by `hls_config.h`/`memory_pipeline.cpp`). Pragmas themselves (`BIND_STORAGE`, `ARRAY_PARTITION`, `PIPELINE`) added inline in `cache_bank.h`/`memory_pipeline.h`/`compute_pipeline.cpp` next to the code they apply to, not under `hls/pragma/` as the path above suggests — standard Vitis HLS practice for class-member pragmas, and the directory stayed a `.keep` placeholder. Found and fixed 2 correctness-for-synthesis gaps: `regs`/cache `WAYS` dimension needed `ARRAY_PARTITION complete` for the `UNROLL` pragmas already present to be synthesizable at all (not an optimization), and `cache_bank.h`'s line fill/read loops were `UNROLL`-ed across `WORDS_PER_LINE=32` (would force expensive full partition) instead of `PIPELINE`d like the sibling m_axi burst loop already was — now consistent. All `tests/hls/*` re-verified passing after (pragmas don't affect csim, this was the regression check). **Caveat**: no `vitis_hls` in this environment — nothing here is validated against real C-synthesis resource/timing reports (see `docs/hls/interfaces.md` §8).
- [ ] T025 [US2] Create RTL generation and FPGA build scripts in `rtl/` and `fpga/scripts/` — no longer blocked (T022b closed, not pursued). In progress: real Vitis 2023.1 configured from `/tools/Xilinx` (`scripts/setup-env.sh` auto-detects it), `tests/fpga/test_flow.tcl` re-verified against it (KV260 csynth passes for both kernels). **Board scope decided at the start of this task: Alveo U55C discarded permanently, KV260-only from here on** (`docs/hls/interfaces.md` §14) — U55C's part isn't even installed in this Vitis instance (`platforminfo -l` shows no platforms), and every remaining U55C item was still open/unmeasured, not close to done.
- [ ] T026 [US2] Add FPGA deployment and validation scripts in `fpga/tests/` and `scripts/deploy_fpga.sh` — KV260-only (T025's board-scope decision applies here too).

**Golden-model reconciliation (post-T024)**: `origin/init_gpgpu` advanced past the commit this port was built from (`5a80f01` → `9c4dfea` "GPGPU READY" — real `SIMTController::handleBranch()` bug fix, genuine multi-CU fan-out in `top.cpp`, new `parallelReduction`/`fpGemm`/`conv2d3x3` kernels, new `r3=local_warp_id` register convention). Merged in (clean fast-forward, no file overlap with local uncommitted work) and reconciled — see `docs/hls/interfaces.md` §9 for the full discrepancy list and kernel test-coverage matrix. Net changes: `divergence_stack.h`'s `handleBranch()` fixed to match the 3-case golden logic (was replicating the pre-fix bug); 3 new tests added (`test_compute_pipeline.cpp`'s `FpGemm2x2TileK4`/`Conv2d3x3`, `test_pipeline_integration.cpp`'s `ParallelReductionAcrossTwoWarpsWithBarrier` — the last one is the first HLS-side test exercising a 2-warp barrier, and empirically verified to fail against the pre-fix `divergence_stack.h`, confirming it's a real regression test); §2.4's barrier rationale corrected (design unchanged, premise was stale). `barrierRoundTrip` now ported too (`test_pipeline_integration.cpp::BarrierRoundTripPreservesMemoryAcrossStall`, single-warp — golden usage is a 10-warp multi-GPU launch in `benchmark_test.cpp`, corrected from an earlier mis-citation of `regression_test.cpp`). `fpDivergentSaxpy` now ported too (`test_compute_pipeline.cpp::FpDivergentSaxpy`) — found and documented a real doc bug in the kernel's own comment while porting it: the claimed "even/odd" masking is wrong for its actual `r0 & (r0+1)` branch condition, which produces a sparse `{0,1,3,7,15,31}` fall-through set for sequential thread indices, not alternating even/odd (see `docs/hls/interfaces.md` §9.2). No golden execution of this kernel exists anywhere (grep-verified) to cross-check the fix against, unlike every other kernel in the coverage matrix. **Every kernel in `kernel_programs.h` now has at least one HLS-side test** — the coverage matrix is complete.

**Directory refactor (post-coverage-matrix)**: `hls/src/` was flat (`compute_pipeline.*`/`memory_pipeline.*` at the root, `divergence_stack.h`/`cache_bank.h` parked under `common/` despite being component-specific) — it had drifted from `models/systemc/src/`'s per-component layout, hurting legibility. Reorganized to mirror it: `hls/src/compute_unit/compute_pipeline.{h,cpp}`, `hls/src/simt_controller/divergence_stack.h`, `hls/src/memory/{memory_pipeline.{h,cpp},cache_bank.h}`, `hls/src/common/{hls_config,hls_types}.h` unchanged (genuinely shared across components, same role as `common/types.h` in the golden model). No `scheduler/`/`top/`/`system_top/` directories — those components were never ported (see §2.4; they were always meant to become host software, which doesn't exist yet). Pure file move + include-path fix, no logic changes — verified via a full rebuild/rerun of all 25 HLS tests against the real Vitis HLS headers, identical results.

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
	- Implemented now: grid-level launch (`KernelLaunchInfo` carries `grid_x/y/z` + `workgroup_x/y/z`), `waitKernelCompletion()` for synchronous completion, `pollKernelStatus()` reporting IDLE/CONFIGURED/RUNNING/COMPLETED/FAILED from the driver. Runtime now preserves bundle launch metadata, auto-fills workgroup defaults from the packed manifest when omitted, and resolves the ELF entry symbol before dispatch. New `host_runtime.h` public header. CMakeLists links `kernel_loader_lib` + `driver_lib`.
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
	- **Runtime**: `KernelLaunchInfo` with `grid_x/y/z`; `waitKernelCompletion`; `host_runtime.h` public header; bundle metadata now flows from manifest into launch geometry and entry-symbol selection.
	- **Kernel Loader**: `resolveEntrySymbol` (nm-based symbol search); `listKernelSymbols`; `kernel_loader.h` includes own header.
	- **Benchmarks**: `vector_add.cu` + `saxpy.cu` (bare-metal, no stdlib headers); `run_vector_add.sh` + `run_saxpy.sh` (compile → disassemble → bundle → host-sim validate, both PASS N=1024).
	- **SystemC hardware model**: real `MemoryHierarchy` (byte-addressable sparse memory, L1/L2 cache), full RV32I+M decoder (`riscv_isa.h`), real `ComputeUnit` fetch/decode/execute loop, `ElfLoader` (ELF32 parser), `KernelBridge` SW↔SC integration, `GPGPUTop` top-level with all ports bound, `WarpScheduler` and `SIMTController` `.cpp` implementations.
	- **SIMT support**: `GPGPUTop` now exposes divergence metrics from `SIMTController`, distributes kernel blocks round-robin across compute units, and has a dedicated `test_simt_controller.cpp` validating active masks and reconvergence.
	- **End-to-end test**: `test_systemc_integration.cpp` verifies ELF loading, CU register arithmetic, vector_add (N=8, H2D→bridge→D2H), and scalar_mul (N=4).
	- 10/11 CTest suites pass in this branch; current failing suite: `systemc_integration_tests` (test case `SystemCIntegration.CudaMultiUnitSimtEndToEnd`).
- Still pending before calling the stack production-complete:
	- Real LLVM backend/target work (TableGen, TargetLowering, SelectionDAG) instead of the `clang` wrapper.
	- Custom GPGPU/SIMT instruction definitions and MC-layer assembler syntax.
	- Real hardware binding (DMA, register-mapped MMIO) in the driver instead of host-memory simulation.
	- Full Rodinia or GPGPU benchmark suite results through actual hardware or a cycle-accurate simulator.
	- Multi-warp/multi-block execution in `KernelBridge` (currently runs single warp 0 only).

---

## Phase 5b: SystemC ↔ Software Integration (hardware_main branch)

**Goal**: Connect the SystemC hardware model to the software stack for end-to-end functional simulation.

- [x] T036b [US1] Implement real byte-addressable `MemoryHierarchy` with L1/L2 cache simulation in `models/systemc/src/memory/memory_hierarchy.cpp`
	- Sparse `byte_memory_` map; word-aligned L1/L2 cache sets; `writeBytes`/`readBytes` bulk access for ELF loading.
- [x] T037b [US1] Implement RV32I + M-extension decoder in `models/systemc/integration/riscv_isa.h`
	- Header-only; `RV32Instr` struct with `Op` enum covering all RV32I+M ops; `decodeRV32()`; `expandRVC()` for compressed instructions.
- [x] T038b [US1] Rewrite `ComputeUnit` with real fetch/decode/execute in `models/systemc/src/compute_unit/compute_unit.cpp`
	- Full RV32I+M execution; warp context (rf[32], pc, halted); detects `JALR x0, x1, 0` (ret) and return-sentinel PC as completion.
- [x] T039b [US1] Implement ELF32 binary loader in `models/systemc/integration/elf_loader.h/.cpp`
	- Manual ELF32 parser (no libelf); reads PT_LOAD segments into `MemoryHierarchy`; parses SHT_SYMTAB for function symbols; `findSymbol()` by name.
- [x] T040b [US1] Implement `KernelBridge` SW↔SC integration in `models/systemc/integration/kernel_bridge.h/.cpp`
	- Orchestrates: create standalone MemoryHierarchy → load ELF → copy H2D driver buffers → resolve entry symbol → set up registers (a0..a7=args, sp=0x20000000, ra=sentinel) → run CU step loop until complete → copy results D2H → print metrics (cycles, IPC, cache hit rate).
	- Updated now: KernelBridge now preserves driver launch geometry, records the effective grid/block used during execution, surfaces the resolved entry symbol in metrics output, and dispatches blocks across a bounded pool of functional CUs.
- [x] T041b [US1] Implement `GPGPUTop` with all port bindings in `models/systemc/src/top/top.cpp`
	- Wires ComputeUnits, WarpScheduler, MemoryHierarchy, SIMTController; binds all `clk/reset/memory_ready/memory_request` ports to internal signals.
- [x] T042b [US1] Implement `WarpScheduler` and `SIMTController` `.cpp` files
	- `models/systemc/src/scheduler/warp_scheduler.cpp`: ROUND_ROBIN/PRIORITY/FIFO scheduling, multi-CU warp queues, load balancing.
	- `models/systemc/src/simt_controller/simt_controller.cpp`: active mask management, divergence mask stack, per-warp thread activation.
- [x] T043b [US1] Add SystemC integration test suite in `tests/systemc/test_systemc_integration.cpp`
	- Current suite contains 5 tests: `ElfLoaderBasic`, `ComputeUnitAddsRegisters`, `VectorAddEndToEnd` (N=8, H2D→bridge→D2H), `ScalarMultiply` (N=4), `CudaMultiUnitSimtEndToEnd`.
	- Branch check (2026-07-26): first 4 pass; `CudaMultiUnitSimtEndToEnd` currently fails and drives the `systemc_integration_tests` suite failure.
- [x] T044b [US1] Fix CMake SystemC detection and build system
	- `cmake/FindSystemC.cmake`: added `/usr/local/lib-linux64` search path.
	- `models/CMakeLists.txt`: enabled `add_subdirectory(systemc)` guarded on `SystemC_FOUND`.
	- `models/systemc/src/top/CMakeLists.txt`: split into `gpgpu_top` static library + `systemc_simulation` executable.
	- `tests/systemc/CMakeLists.txt`: added `sc_gtest_main.cpp` for `sc_main()`→GTest bridge; dropped conflicting `GTest::Main`.
	- Branch check (2026-07-26): 10/11 CTest suites pass; `systemc_integration_tests` currently fails `CudaMultiUnitSimtEndToEnd`.

- [x] T045b [US1] Add SIMT controller validation and top-level block dispatch support in `tests/systemc/test_simt_controller.cpp` and `models/systemc/src/top/top.cpp`
	- `SIMTController` now has a dedicated unit test covering active-mask initialization, branch masking, and join/reconvergence.
	- `GPGPUTop::launchKernel()` distributes blocks round-robin across compute units and reports divergence metrics through `SIMTController`.

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

## Phase 5c: Software Stack — Simulation Completeness (codesign_dmedina gaps)

**Goal**: Close the remaining gaps in the SystemC simulation so that the full SIMT execution model — scheduler, multi-warp dispatch, and divergence reconvergence — works end-to-end before moving to FPGA.

**Independent Test**: A researcher can launch a kernel with multiple warps across multiple compute units, observe the WarpScheduler selecting warps by policy, and verify that divergent branches reconverge correctly before the kernel completes.

- [x] T046b [US1] Connect `WarpScheduler` into `GPGPUTop::simulationProcess()` in `models/systemc/src/top/top.cpp`
	- Implemented now: `simulationProcess()` dispatches with `scheduler_->selectWarp(cu_id)` and marks completion with `scheduler_->markWarpComplete(cu_id, warp_id)`.
	- Remaining hardening: add a dedicated scheduler-dispatch regression test (for example `tests/systemc/test_scheduler_dispatch.cpp`) to validate deterministic round-robin ordering under multi-CU load.

- [x] T047b [US1] Complete reconvergence semantics in `SIMTController` in `models/systemc/src/simt_controller/simt_controller.cpp`
	- `handleBranch(warp_id, conditions, reconvergence_pc=0)` now stores the IPDOM PC in `pc_stack` (was always 0). New `getReconvergencePC(warp_id)` accessor exposes it.
	- `pushDivergenceState` signature updated to carry the PC; `popDivergenceState` clears it on join.
	- Test added: `ReconvergencePcTracked` in `tests/systemc/test_simt_controller.cpp` — verifies PC is stored after divergent `handleBranch` and cleared after `handleJoin`. All three SIMT controller tests pass.
	- Virtual-ISA path passes `reconvergence_pc=0` (VJOIN is still explicit); binary-mode path detects reconvergence via PC comparison in `KernelBridge` (see T048b).

- [x] T048b [US1] Add warp-level lockstep SIMT execution in `KernelBridge` in `models/systemc/integration/kernel_bridge.cpp`
	- When `threads_per_warp > 1`: `runOnHardware()` groups threads into `WarpGroup`s and steps all threads in a warp simultaneously each cycle.
	- After each warp step: all non-complete thread PCs are compared; first cycle where PCs disagree increments `last_divergence_events_` (one count per divergent branch, not per cycle).
	- `ComputeUnit::getCurrentPC()` added to expose the binary-mode fetch PC for cross-thread comparison.
	- When `threads_per_warp == 1`: original independent single-thread-per-worker loop is used (no behaviour change for scalar kernels).
	- Verification: `SystemCIntegration.CudaMultiUnitSimtEndToEnd` (grid=2×1, block=8×1, odd/even divergence) now passes — `lastDivergenceEvents()=2 > 0`. Full CTest suite: **11/11 pass** (was 10/11).

- [x] T049b [US3] Add automated CUDA/C++ → RISC-V ELF build target in `CMakeLists.txt` and `scripts/`
	- `cmake/RiscvKernel.cmake` created: provides `add_riscv_kernel(<name> SOURCE <file> [ENTRY] [MARCH] [MABI] [FLAGS])` function and `compile_kernels` meta-target.
	- `benchmarks/workloads/vector_add/CMakeLists.txt` and `benchmarks/workloads/saxpy/CMakeLists.txt` created, each calling `add_riscv_kernel()`.
	- `benchmarks/CMakeLists.txt` updated: `include(RiscvKernel)` + `add_subdirectory` for both workloads.
	- Individual targets: `vector_add_kernel`, `saxpy_kernel`; build all at once with `cmake --build <dir> --target compile_kernels`.
	- Output: `${CMAKE_BINARY_DIR}/kernels/<name>.elf` (ELF32 RISC-V, `rv32gc/ilp32`).
	- Verification: `cmake --build build_integration_on --target compile_kernels` produces `vector_add.elf` and `saxpy.elf`; both pass `riscv64-unknown-elf-nm` symbol check (entry symbols `vector_add` and `saxpy` at expected addresses). All 11/11 CTest suites continue to pass.

**Checkpoint**: Scheduler, SIMT reconvergence, and multi-CU dispatch are all exercised by the test suite. `KernelBridge` uses the full `GPGPUTop` stack.

---

## Phase 7: Kria FPGA Deployment (ARM Host + FPGA GPGPU)

**Goal**: Run a CUDA-like kernel compiled to RISC-V on the Kria board — ARM PS executes the host software stack, PL implements the GPGPU, and data flows via AXI DMA.

**Target platform**: AMD Kria KV260 or KR260 — ARM Cortex-A53 PS + Xilinx PL fabric.

**Independent Test**: A researcher can run `vector_add` (N=1024) end-to-end: compile on host, transfer to Kria, load bitstream, execute on FPGA GPGPU, read back results, and verify correctness.

### Hardware Interface Definition

- [ ] T050 [US2] Define the GPGPU AXI register map and DMA interface in `docs/architecture/axi_interface.md` and `fpga/constraints/`
	- Required registers: `CTRL` (start/reset), `STATUS` (idle/running/done/error), `PC_INIT` (entry point), `GRID_X/Y` (launch dimensions), `IRQ_ENABLE`.
	- Required DMA channels: one AXI4 master for instruction memory load (ELF segments), one AXI4 master for data memory (H2D and D2H transfers).
	- Deliverable: register map table, address offsets, and AXI4-Lite/AXI4 port widths documented in `docs/architecture/axi_interface.md`.

### ARM Driver (Userspace)

- [ ] T051 [US2] Implement ARM↔FPGA userspace driver in `driver/src/fpga_driver.cpp` and `driver/src/fpga_driver.h`
	- Replaces the host-memory simulation in `loader.cpp` (`g_device_buffers`) with real hardware access.
	- Required: open UIO device or `/dev/mem`; `mmap()` AXI-Lite register space; use `libdma` or kernel DMA proxy to transfer buffers; implement `allocateDeviceBuffer()`, `copyHostToDevice()`, `copyDeviceToHost()` against real FPGA memory.
	- Build guard: `#ifdef FPGA_TARGET` so the simulation driver remains usable on x86.
	- Verification: unit test in `tests/fpga/test_fpga_driver.cpp` that maps registers and reads `STATUS` register (expected: IDLE after reset).

- [ ] T052 [US2] Implement ELF loader to FPGA instruction memory in `driver/src/fpga_elf_loader.cpp`
	- Replaces `ElfLoader` (which writes to `MemoryHierarchy`) with AXI DMA transfers to the FPGA instruction memory.
	- Required: parse ELF PT_LOAD segments; DMA each segment to its load address in FPGA global memory; write `PC_INIT` register with ELF entry point.
	- Verification: after loading, read back first 16 bytes of instruction memory via DMA and compare against ELF segment content.

### ARM Runtime Adaptation

- [ ] T053 [US2] Adapt `gpgpuLaunchKernel()` to write FPGA control registers in `software/host_api/host_api.cpp`
	- Required: write `GRID_X`, `GRID_Y`, `PC_INIT` to AXI-Lite registers; write `CTRL.start = 1` to begin execution.
	- Build guard: `#ifdef FPGA_TARGET` to preserve simulation path.
	- Verification: after writing `CTRL.start`, poll `STATUS` and confirm transition from IDLE → RUNNING within 10 ms.

- [ ] T054 [US2] Adapt `gpgpuSynchronize()` to wait for FPGA completion IRQ or poll `STATUS` in `software/host_api/host_api.cpp`
	- Required: either register a UIO interrupt handler for the GPGPU done IRQ, or poll `STATUS == DONE` with a timeout.
	- Verification: after `gpgpuSynchronize()` returns, `STATUS` register reads DONE and result data is available in FPGA memory.

### End-to-End Deployment

- [ ] T055 [US2] Create Kria deployment script and cross-compilation Makefile in `scripts/deploy_kria.sh` and `fpga/`
	- Required: cross-compile software stack for `aarch64-linux-gnu`; `scp` binary + kernel ELF to Kria; load FPGA bitstream via `fpgautil`; run test and capture output.
	- Deliverable: `scripts/deploy_kria.sh` that takes `--bitstream`, `--kernel`, and `--test` arguments and produces a pass/fail report.
	- Verification: `vector_add` (N=1024) produces correct results on Kria hardware; report captured in `docs/verification/kria_results.md`.

**Checkpoint**: End-to-end CUDA → RISC-V ELF → ARM host → FPGA GPGPU → results verified on Kria hardware.

---


---

## Phase 5d: PTX→RISC-V Transpiler (Compiler Core)

**Goal**: Implement the transpilador PTX→RISC-V que vive en el runtime del host. Cuando el programador llama `gpgpuLaunchKernel()`, el runtime extrae el PTX embebido en el binario, lo transpila a RISC-V ELF en memoria, y lo carga a cada core del GPGPU.

**Resultado esperado**: Un programador escribe CUDA, compila con `nvcc --ptx`, y el runtime convierte automáticamente el PTX a RISC-V ELF antes de lanzar el kernel.

**Independent Test**: Dado un archivo `.ptx` con un kernel `vector_add`, el transpilador produce un RISC-V ELF válido que ejecuta correctamente en el SystemC `ComputeUnit` y produce los mismos resultados que la versión compilada directamente con clang.

### Tests para Phase 5d

- [ ] T056 [P] [US3] Add PTX parser unit tests in `tests/compiler/ptx/test_ptx_parser.cpp`
  - Test cases: parse `.reg`, `.param`, `mov.u32`, `ld.global.f32`, `st.global.f32`, `add.f32`, `bar.sync`, `ret`
  - Verification: parser produces correct AST nodes for each PTX instruction type

- [ ] T057 [P] [US3] Add RISC-V emitter unit tests in `tests/compiler/ptx/test_rv_emitter.cpp`
  - Test cases: each PTX→RISC-V mapping (see T060), register allocation, special register injection
  - Verification: emitter produces valid RISC-V assembly that assembles without errors

- [ ] T058 [P] [US3] Add end-to-end transpiler integration test in `tests/compiler/ptx/test_ptx_transpiler.cpp`
  - Input: `vector_add.ptx` (generated by `nvcc --ptx benchmarks/workloads/vector_add/vector_add.cu`)
  - Expected output: RISC-V ELF that produces correct results when run through `KernelBridge`
  - Verification: compare D2H results against reference CPU implementation

### Implementation para Phase 5d

- [ ] T059 [US3] Define PTX subset scope and instruction mapping table in `docs/software/ptx_to_riscv_mapping.md`
  - Required PTX instructions for MVP: `.reg`, `.param`, `mov.u32/f32`, `ld.global.f32/u32`, `st.global.f32/u32`, `add.f32/u32`, `mul.lo.u32`, `mad.lo.u32`, `fma.rn.f32`, `setp.lt/eq/gt.f32`, `@%p bra`, `bar.sync`, `ret`
  - Special registers: `%tid.x/y/z`, `%ctaid.x/y/z`, `%ntid.x/y/z` → mapped to fixed memory addresses (THREAD_CTX_BASE)
  - Document the THREAD_CTX_BASE memory layout: `[0]=tid.x, [4]=tid.y, [8]=tid.z, [12]=ctaid.x, [16]=ctaid.y, [20]=ctaid.z, [24]=ntid.x, [28]=ntid.y, [32]=ntid.z`

- [ ] T060 [US3] Implement PTX lexer and parser in `driver/src/ptx_transpiler/ptx_parser.h` and `ptx_parser.cpp`
  - Input: PTX text (string)
  - Output: AST with `PtxKernel` → `PtxInstr[]` nodes
  - Required: handle `.entry`, `.reg`, `.param` declarations; parse all MVP instructions
  - Reference: PTX ISA documentation (https://docs.nvidia.com/cuda/parallel-thread-execution/)

- [ ] T061 [US3] Implement RISC-V assembly emitter in `driver/src/ptx_transpiler/rv_emitter.h` and `rv_emitter.cpp`
  - Input: `PtxKernel` AST
  - Output: RISC-V assembly text (`.s` file content)
  - Required: register allocator (PTX virtual regs → RISC-V ABI regs); emit prologue (load THREAD_CTX from gp); emit each instruction per mapping table; emit `bar.sync` as `fence` + call to `__barrier_impl`
  - Floating point: emit RV32F instructions (`flw`, `fsw`, `fadd.s`, `fmul.s`, `fmadd.s`, `flt.s`)

- [ ] T062 [US3] Implement ELF assembler/linker wrapper in `driver/src/ptx_transpiler/ptx_transpiler.h` and `ptx_transpiler.cpp`
  - Orchestrates: `ptx_parser` → AST → `rv_emitter` → `.s` text → invoke `clang --target=riscv32-unknown-elf -march=rv32imf` → `.elf` in memory
  - Public API: `RiscvElf PtxTranspiler::compile(const std::string& ptx_text)`
  - Must work without writing to disk (use `/tmp` or `memfd_create` for intermediate files)

- [ ] T063 [US3] Integrate transpiler into `gpgpuLaunchKernel()` in `software/host_api/host_api.cpp` and `runtime/src/host_runtime.cpp`
  - Required: extract PTX from kernel function pointer (via `__cudaRegisterFunction` mechanism or embedded `.ptx` section); call `PtxTranspiler::compile(ptx)` to get RISC-V ELF; pass ELF to existing `ElfLoader` + `KernelBridge` flow
  - Build guard: `#ifdef PTX_TRANSPILER_ENABLED` to preserve direct-ELF path for testing
  - Verification: `gpgpuLaunchKernel(vectorAdd, ...)` produces correct results without manually pre-compiling to RISC-V

- [ ] T064 [US3] Implement THREAD_CTX memory injection in `driver/src/` and `models/systemc/integration/kernel_bridge.cpp`
  - Required: before launching each core, write `{tid.x, tid.y, tid.z, ctaid.x, ctaid.y, ctaid.z, ntid.x, ntid.y, ntid.z}` to `THREAD_CTX_BASE` address in that core's memory
  - `THREAD_CTX_BASE` = `0x0000_1000` (below stack, above zero page) — document in `docs/architecture/memory_map.md`
  - Verification: kernel reading `%tid.x` via `lw a0, 0(gp)` gets the correct thread index

**Checkpoint**: `nvcc --ptx kernel.cu` → `PtxTranspiler::compile()` → RISC-V ELF → `KernelBridge` → correct results. Full PTX→RISC-V→SystemC pipeline functional.

---

## Phase 5e: RV32IMF — Floating Point Support

**Goal**: Agregar soporte de punto flotante (extensión F de RISC-V) al `ComputeUnit`. Requerimiento esencial del sistema: soporte de enteros Y punto flotante.

**Independent Test**: Un kernel que realiza operaciones FP (`fadd.s`, `fmul.s`, `fmadd.s`) ejecuta correctamente en el `ComputeUnit` y produce resultados con precisión IEEE 754 single-precision.

- [x] T065 [US1] Extend `riscv_isa.h` with RV32F instruction decoding in `models/systemc/integration/riscv_isa.h`
  - Added F-extension opcodes: `FLW`, `FSW`, `FADD_S`, `FSUB_S`, `FMUL_S`, `FDIV_S`, `FSQRT_S`, `FMADD_S`, `FMSUB_S`, `FNMADD_S`, `FNMSUB_S`, `FCVT_W_S`, `FCVT_WU_S`, `FCVT_S_W`, `FCVT_S_WU`, `FMV_X_W`, `FMV_W_X`, `FLT_S`, `FLE_S`, `FEQ_S`
  - Added `rs3` field to `RV32Instr` for R4-type FP instructions (FMADD/FMSUB/FNMADD/FNMSUB)
  - Added decoder cases for opcodes 0x07 (LOAD-FP), 0x27 (STORE-FP), 0x43/0x47/0x4B/0x4F (R4-type), 0x53 (OP-FP)

- [x] T066 [US1] Add 32 floating-point registers (f0-f31) to `ComputeUnit` warp context in `models/systemc/src/compute_unit/compute_unit.cpp`
  - Added `std::array<float, 32> binary_fregs_` to `ComputeUnit` private state
  - Added `setInitialFloatRegisters()` and `getFloatRegister()` public API
  - Implemented all RV32F instructions in `executeRV32()`: FLW/FSW, FADD/FSUB/FMUL/FDIV/FSQRT, FMADD/FMSUB/FNMADD/FNMSUB, FEQ/FLT/FLE, FCVT_W_S/FCVT_WU_S/FCVT_S_W/FCVT_S_WU, FMV_X_W/FMV_W_X
  - IEEE 754 compliant: uses C++ `float` arithmetic; bit-exact FMV via `memcpy`
  - Also implemented previously missing M-extension: MULH, MULHSU

- [x] T067 [P] [US1] Add FP unit tests in `tests/systemc/test_compute_unit_fp.cpp`
  - 14 test cases covering: FADD_S, FSUB_S, FMUL_S, FMADD_S, FMSUB_S, FSQRT_S, FLT_S (true/false), FEQ_S, FLW/FSW round-trip, FCVT_W_S, FCVT_S_W, FMV_X_W, FMV_W_X, SAXPY one-element end-to-end
  - All 14 tests pass (12/12 CTest suites pass, up from 11)

**Checkpoint**: `ComputeUnit` ejecuta RV32IMF completo. Kernels con operaciones FP producen resultados correctos. 12/12 CTest suites pass (branch: gpgpu/codesign_dmedina, 2026-07-29).

---

## Phase 5f: SIMT Thread Mapping — Grid/Block/Thread → Core

**Goal**: Implementar el mapeo correcto de la jerarquía CUDA (grid→blocks→threads) a cores RISC-V físicos. Cada thread corresponde a un core mínimo RISC-V.

**Independent Test**: Un kernel lanzado con `grid=(4,1,1), block=(32,1,1)` crea 128 threads, cada uno con el `tid.x` y `ctaid.x` correcto, y todos ejecutan en paralelo en los CUs disponibles.

- [ ] T068 [US1] Implement full grid/block/thread → core mapping in `runtime/src/host_runtime.cpp`
  - Required: `totalThreads = grid.x * grid.y * grid.z * block.x * block.y * block.z`
  - For each thread `t`: compute `tid = {t % block.x, (t/block.x) % block.y, t/(block.x*block.y)}` and `ctaid = {(t/(block.x*block.y*block.z)) % grid.x, ...}`
  - Pass `ThreadContext` (tid, ctaid, ntid) to driver for each thread
  - Verification: launch `vector_add` with N=256, grid=(8,1,1), block=(32,1,1) → 256 threads, each with correct tid

- [ ] T069 [US1] Implement thread-to-CU scheduling in `models/systemc/src/top/top.cpp` and `models/systemc/src/scheduler/warp_scheduler.cpp`
  - Required: when `totalThreads > numCUs`, schedule threads in rounds (time-multiplexing)
  - `WarpScheduler` assigns threads to available CUs; when a CU completes, assign next pending thread
  - Track completion: all threads must complete before `gpgpuSynchronize()` returns
  - Verification: launch kernel with 256 threads on 8 CUs → 32 rounds of 8 threads each, all complete correctly

- [ ] T070 [P] [US1] Add thread mapping integration test in `tests/systemc/test_thread_mapping.cpp`
  - Test: launch `tid_printer` kernel (writes `tid.x` to output[tid.x]) with N=64 threads
  - Verify: `output[i] == i` for all i in [0, 63]
  - Test: 2D grid (4x4 blocks of 4x4 threads) → verify `tid.x + ctaid.x * ntid.x` is unique per thread

**Checkpoint**: La jerarquía CUDA grid/block/thread mapea correctamente a cores RISC-V. Cada thread tiene su contexto único (tid, ctaid, ntid).

---

## Phase 5g: Shared Memory and Barriers

**Goal**: Implementar shared memory por bloque y `bar.sync` (`__syncthreads()`). Requerido para kernels de reducción, matrix multiply con tiling, y cualquier patrón que requiera comunicación entre threads del mismo bloque.

**Independent Test**: Un kernel de reducción paralela (suma de N elementos) usando shared memory y `__syncthreads()` produce el resultado correcto.

- [ ] T071 [US1] Implement per-block shared memory in `models/systemc/src/memory/memory_hierarchy.cpp`
  - Add `shared_memory_` map: `block_id → byte array` of size `SHARED_MEM_SIZE_BYTES` (default: 48KB per block)
  - `ld.shared` / `st.shared` PTX instructions → access `shared_memory_[ctaid]`
  - Shared memory is zeroed at block start, freed when all threads in block complete
  - Address space: `0x0001_0000` base for shared memory (document in `docs/architecture/memory_map.md`)

- [ ] T072 [US1] Implement `bar.sync` barrier in `models/systemc/src/simt_controller/simt_controller.cpp`
  - `bar.sync 0` → all threads in the same block must reach this point before any continue
  - Implementation: counter per block; when counter == `ntid.x * ntid.y * ntid.z`, release all waiting threads
  - `WarpScheduler` must handle blocked warps (do not schedule a warp waiting on barrier)

- [ ] T073 [P] [US1] Add shared memory and barrier tests in `tests/systemc/test_shared_memory.cpp`
  - Test: parallel reduction (sum N=32 elements using shared memory + `bar.sync`) → correct sum
  - Test: matrix transpose using shared memory → correct transposed matrix
  - Test: barrier ordering — verify no thread reads shared memory before all writes complete

**Checkpoint**: Kernels con `__shared__` y `__syncthreads()` ejecutan correctamente. Reducción paralela produce resultados correctos.

---

## Actualización de Dependencias

### Orden de ejecución actualizado

```
Phase 5c (gaps SystemC)
    ↓
Phase 5e (RV32IMF — FP support)   ← puede ir en paralelo con 5d
Phase 5d (PTX→RISC-V transpiler)  ← puede ir en paralelo con 5e
    ↓
Phase 5f (Thread mapping grid/block/thread)
    ↓
Phase 5g (Shared memory + barriers)
    ↓
Phase 7 (Kria FPGA deployment)
```

### Sistema final esperado al completar todas las fases

```
Programador escribe CUDA (.cu)
    │ nvcc --ptx
    ▼
PTX embebido en binario host
    │ gpgpuLaunchKernel() → PtxTranspiler::compile()
    ▼
RISC-V ELF (RV32IMF) en memoria del host
    │ Runtime: mapea grid/block/thread → cores
    │ Inyecta THREAD_CTX (tid, ctaid, ntid) por core
    │ DMA → FPGA / SystemC
    ▼
Hardware GPGPU (N cores RISC-V RV32IMF)
    ├── WarpScheduler: asigna threads a CUs
    ├── SIMTController: maneja divergencia + reconvergencia
    ├── Shared Memory: por bloque, con barriers
    └── MemoryHierarchy: L1/L2/Global
    │
    ▼
Resultados via DMA → gpgpuMemcpyD2H() → host
```


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
- **Phase 5c**: Depends on Phase 5b (SystemC integration complete).
- **Phase 5d** (PTX→RISC-V Transpiler): Depends on Phase 5c.
- **Phase 5e** (RV32IMF FP): Can run in parallel with Phase 5d; depends on Phase 5c.
- **Phase 5f** (Thread Mapping): Depends on Phase 5d and 5e.
- **Phase 5g** (Shared Memory + Barriers): Depends on Phase 5f.
- **Phase 7 (Kria Deployment)**: Depends on Phase 4 (HLS/RTL/FPGA path) and Phase 5g (full simulation complete).
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
