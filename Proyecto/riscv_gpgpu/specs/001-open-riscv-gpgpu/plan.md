# Implementation Plan: FPGA-Based Open RISC-V GPGPU

**Status**: Draft  
**Created**: 2026-06-27  
**Source Documents**: Constitution v1.0.0, System Specification v1.0.0  
**Document Purpose**: Provide an implementation strategy for the full research and development lifecycle of the proposed FPGA-based open RISC-V GPGPU platform.

## 1. Development Strategy

The implementation strategy shall follow a specification-driven, research-oriented workflow that proceeds from architecture definition to FPGA validation through a sequence of intermediate artifacts. The project shall treat the architecture as an evolving research platform rather than a single fixed implementation. The primary development methodology shall be hardware/software co-design, with explicit interfaces between architectural modeling, simulation, HLS, RTL generation, compiler support, runtime development, and verification.

The work shall be organized around modular components and parameterized configuration points to enable design-space exploration. A strict distinction shall be maintained between internally developed components and externally reused or referenced open-source components. All implementation work shall remain traceable to the system specification and the governing constitution.

## 2. Hardware/Software Co-Design Workflow

The hardware/software co-design workflow shall consist of the following activities:

1. Define the architecture and system interfaces from the specification.
2. Capture the core execution model in SystemC as the architectural golden model.
3. Develop or adapt compiler and runtime abstractions for the target execution semantics.
4. Generate synthesizable implementation candidates through HLS and refine them into RTL.
5. Validate functional correctness with simulation and FPGA experimentation.
6. Measure performance and resource usage for comparison across configurations.

Each stage shall be reviewed for correctness, compatibility, and traceability before the next stage is initiated. Decisions that affect both hardware and software shall be made jointly by the relevant architecture and toolchain stakeholders.

## 3. Incremental Development Milestones

The project shall proceed through the following milestones:

- Milestone 1: Architecture definition and interface specification.
- Milestone 2: SystemC model of the baseline execution pipeline.
- Milestone 3: Functional validation of the baseline model.
- Milestone 4: HLS-based implementation of the core compute pipeline.
- Milestone 5: RTL refinement and synthesis readiness.
- Milestone 6: FPGA deployment and functional validation.
- Milestone 7: LLVM backend and runtime integration.
- Milestone 8: Verification, benchmark evaluation, and reproducible reporting.

Each milestone shall produce a documented artifact, a verification report, and a set of acceptance criteria before progression is approved.

## 4. Expected Deliverables per Phase

### Phase A: Architecture Definition
- architecture specification and interface contract documents;
- initial component decomposition and parameter list;
- traceability matrix linking requirements to architecture elements.

### Phase B: SystemC Modeling
- executable SystemC model of the baseline architecture;
- model documentation for execution semantics, memory behavior, and scheduling;
- initial functional testbench and scenarios.

### Phase C: Functional Validation
- unit and integration test suites for the model;
- simulation results for thread execution, divergence, memory access, and synchronization;
- recorded deviations and fixes.

### Phase D: HLS Implementation
- HLS-ready implementation of the core pipeline;
- resource and timing estimates;
- design-space exploration configuration files.

### Phase E: RTL and FPGA Implementation
- RTL refinement artifacts;
- synthesis and implementation reports;
- FPGA deployment bitstream and validation logs.

### Phase F: Compiler and Runtime Integration
- LLVM backend adaptation documents and generated artifacts;
- runtime and driver scaffolding;
- host API and kernel submission flow.

### Phase G: Verification and Benchmark Evaluation
- verification reports for unit, integration, FPGA, and benchmark testing;
- benchmark results for baseline and alternative configurations;
- reproducibility package and documentation.

## 5. Development Environment

### Operating System
- Ubuntu Linux 22.04 LTS or later shall be the primary development environment.
- The implementation shall be validated on the selected host operating system and documented for reproducibility.

### Compiler Toolchain
- LLVM/Clang shall be used as the primary compiler framework for software and backend integration.
- GCC or Clang host-side tools may be used where appropriate for auxiliary build tasks.

### Simulation Tools
- SystemC shall be used for architectural modeling and functional simulation.
- Verilator may be used for additional RTL-level simulation support where applicable.

### Modeling Tools
- SystemC shall be the primary modeling language for functional and structural architecture modeling.
- Additional scripting and data-processing tools such as Python and shell tooling shall be used for automated experiments and result collection.

### High-Level Synthesis Tools
- Vitis HLS shall be used as the primary HLS implementation flow.
- The project shall preserve the HLS source in a form that can be regenerated and compared across configurations.

### FPGA Implementation Tools
- Xilinx Vivado shall be used for RTL synthesis, implementation, and FPGA deployment.
- The target FPGA board and device family shall be selected based on availability, resource requirements, and research relevance.

### Verification Tools
- simulation test benches;
- waveform viewers and log-based analysis tools;
- benchmark execution scripts;
- automated comparison scripts for expected results.

### Version Control
- Git shall be used for source and artifact version control.
- Branching policies shall separate feature work, experimental configurations, and release or milestone snapshots.

### Build System
- CMake shall be used for build automation where appropriate.
- A consistent build and run flow shall be documented for each major component.

## 6. Fixed Tool Versions

The following versions are proposed as the initial baseline for development. Exact versions shall be frozen in the reproducibility package and updated only through documented change control.

| Tool / Component | Version | Purpose | Reason for Version Selection | External Dependencies |
|---|---:|---|---|---|
| Ubuntu Linux | 22.04 LTS | Host development environment | Stable long-term support environment with broad toolchain support | GCC, make, Python, git |
| LLVM/Clang | 17.x or 18.x | Compiler and backend development | Mature, widely supported, suitable for custom backend work | Clang frontend, LLVM infrastructure |
| SystemC | 2.3.x | Architectural golden model | Stable and broadly used for system-level modeling | C++ compiler, build system |
| Vitis HLS | 2023.x or 2024.x | HLS implementation flow | Aligns with current FPGA toolchain support and synthesis workflows | Xilinx toolchain, Tcl scripts |
| Vivado | 2023.x or 2024.x | RTL synthesis and FPGA implementation | Compatible with the selected FPGA flow and supported board targets | Xilinx device support packages |
| Verilator | 5.x | RTL simulation and test integration | Lightweight and reproducible simulation alternative | C++ compiler, SystemC where needed |
| CMake | 3.24+ | Build orchestration | Widely available and integrates with LLVM, SystemC, and FPGA flows | compiler toolchain |
| Python | 3.10+ | Automation, analysis, and reporting | Good balance of portability and ecosystem support | packaging tools, numpy, matplotlib |
| Git | 2.40+ | Version control | Standard and reliable collaboration workflow | repository hosting |
| Rodinia Benchmark Suite | current stable release | Benchmark workload collection | Widely used for GPU and accelerator evaluation research | CUDA/HIP-compatible runtime where applicable |
| CUDA/HIP APIs | relevant compatible version | Programming model reference and host-side compatibility expectations | Provides a familiar abstraction for the project’s programming model | runtime libraries, compiler support |

## 7. External Dependencies and Reused Components

The project shall reuse or reference the following external projects and libraries where appropriate:

- LLVM/Clang for compiler infrastructure and code generation.
- SystemC for architectural modeling and simulation.
- Vitis HLS and Vivado for synthesis and FPGA implementation.
- Verilator for simulation support where appropriate.
- CUDA/HIP APIs as a conceptual and compatibility reference for the programming model.
- Rodinia Benchmark Suite for representative kernel workloads.
- Additional libraries such as Python scientific packages, JSON/YAML tooling, and build automation utilities as required.

All reused components shall be documented with their version, purpose, and compatibility status within the project reproducibility package.

## 8. State-of-the-Art Baselines

The implementation shall study and compare against the following open-source or research-oriented projects where applicable.

| Baseline | Repository | Research Paper | Components to Study or Reuse | Limitations Relative to This Project |
|---|---|---|---|---|
| Vortex | https://github.com/ruhr-uni-bochum/vortex | Relevant Vortex publications | SIMT execution model, compiler integration concepts, open-source GPU architecture ideas | May target different implementation goals and not be FPGA-first |
| Ventus | https://github.com/ventus-project/ventus | Relevant Ventus publications | RISC-V GPU microarchitecture concepts, programmable execution model | May emphasize different ISA or software assumptions |
| CuPBoP | https://github.com/CMU-SAFARI/CuPBoP | Relevant CuPBoP publications | CUDA-like programming concepts and accelerator integration ideas | Not necessarily aligned with FPGA-centric or open-source research workflow |
| SoftCUDA | https://github.com/softcuda/softcuda | Relevant SoftCUDA publications | Software-defined GPU concepts and kernel execution abstractions | Different execution target and maturity level |
| Additional RISC-V GPU projects | Repository-specific | Repository-specific | Execution model, ISA ideas, compiler/runtime integration patterns | May not provide the full FPGA co-design workflow |

These baselines shall be used as references for architectural inspiration and evaluation, not as direct replacements for the project’s own specification-driven design.

## 9. Development Phases

### 9.1 Architecture Definition
The architecture definition phase shall establish the baseline execution model, system interfaces, parameterization strategy, and component boundaries. The resulting artifacts shall define the expected behavior of the hardware and software subsystems in a way that is independent of the eventual implementation technology.

### 9.2 SystemC Modeling
The SystemC modeling phase shall produce an executable architectural model that captures thread execution, warp scheduling, memory access, synchronization, and configuration behavior. The SystemC model shall serve as the golden model for functional behavior and as the basis for early experimentation.

### 9.3 Functional Validation
Functional validation shall verify the architectural behavior of the SystemC model against the specification. The validation phase shall include tests for correct kernel launch behavior, warp execution, divergence handling, memory access, and synchronization semantics.

### 9.4 HLS Implementation
The HLS phase shall translate the architectural concepts into HLS-ready source that can be synthesized into hardware. The phase shall also collect resource and timing estimates for evaluation and design-space exploration.

### 9.5 RTL Generation
The RTL generation phase shall refine the HLS implementation into synthesizable RTL and produce the artifacts required for FPGA implementation. The phase shall preserve traceability to the original architecture and verification requirements.

### 9.6 FPGA Implementation
The FPGA implementation phase shall synthesize, implement, and deploy the design on the selected FPGA platform. It shall produce an implementation report including timing, area, and functional validation evidence.

### 9.7 LLVM Backend Adaptation
The LLVM backend adaptation phase shall extend or adapt the compiler toolchain so that the architectural semantics are visible to the software stack. The phase shall ensure that the programming model and execution model remain consistent with the architecture.

### 9.8 Runtime Development
The runtime development phase shall provide the host-visible execution environment, kernel submission path, and runtime status reporting. It shall also establish an execution abstraction suitable for integration with the driver and FPGA path.

### 9.9 Integration
The integration phase shall combine the hardware, software, and runtime components into a coherent end-to-end flow. This phase shall validate the interfaces between compiler, runtime, driver, and hardware components.

### 9.10 Verification
The verification phase shall execute the unit, integration, FPGA, and benchmark validation plan. Verification shall be documented as a set of evidence artifacts rather than informal observations.

### 9.11 Benchmark Evaluation
The benchmark evaluation phase shall run representative workloads and compare performance and resource utilization across baseline and alternative configurations. Results shall be documented with the measurement methodology and the exact configuration used.

## 10. Verification Plan

Verification shall be performed at each phase using the following criteria:

- Architecture definition: verified by inspection against the system specification and constitution.
- SystemC modeling: verified by functional simulation and trace comparison.
- Functional validation: verified by unit and integration test results.
- HLS implementation: verified by synthesis reports, resource estimates, and simulation.
- RTL generation: verified by RTL simulation and timing analysis.
- FPGA implementation: verified by functional tests on hardware and resource utilization reports.
- LLVM backend adaptation: verified by compilation of representative kernels and execution trace analysis.
- Runtime development: verified by end-to-end kernel submission and status reporting.
- Integration: verified by end-to-end execution across hardware/software interfaces.
- Benchmark evaluation: verified by reproducible benchmark execution and documented performance results.

Progression to the next phase shall be permitted only when the previous phase meets its defined acceptance criteria and produces the required evidence package.

## 11. Experimental Infrastructure

### FPGA Board
- A mid-range or research-oriented FPGA development board with sufficient resources for a baseline GPGPU-style implementation shall be selected.
- The selected board shall support the required synthesis flow and provide enough capacity for the initial implementation target.

### Host Computer
- A Linux workstation with sufficient CPU, memory, and storage resources shall be used for modeling, compilation, synthesis, and benchmarking.
- The host shall be documented in the reproducibility package.

### Operating System
- Ubuntu Linux 22.04 LTS shall be the reference operating system for the project.

### Benchmark Suite
- The Rodinia benchmark suite shall be used as the initial reference workload set where applicable.
- Additional custom microbenchmarks may be used for targeted evaluation of divergence, synchronization, and memory behavior.

### Measurement Tools
- simulation logs;
- hardware execution logs;
- timing and resource utilization reports;
- benchmark execution scripts and result parsers.

### Performance Metrics
- throughput;
- latency;
- speedup over baseline configurations;
- efficiency under varying workload sizes;
- resource utilization per configuration.

### Resource Utilization Metrics
- LUT usage;
- FF usage;
- BRAM usage;
- DSP usage;
- clock frequency and timing slack.

## 12. Risks and Mitigation

The implementation shall anticipate the following major risks:

- Interface drift between hardware and software components.
  - Mitigation: maintain explicit interface contracts, versioned documentation, and regular cross-domain review.
- Excessive complexity in the microarchitecture.
  - Mitigation: start from a minimal baseline, preserve modularity, and use parameterization rather than invasive changes.
- Verification gaps for new architectural features.
  - Mitigation: require verification artifacts before phase progression and maintain unit and integration test coverage.
- Toolchain incompatibility or version drift.
  - Mitigation: freeze tool versions, document dependencies, and maintain a reproducibility package.
- FPGA resource limitations.
  - Mitigation: prioritize a scalable baseline architecture and use design-space exploration to manage resource trade-offs.
- Reproducibility failure.
  - Mitigation: preserve scripts, configuration files, build instructions, and versioned dependency metadata.

## 13. Reproducibility

The project shall maintain a reproducibility package containing:
- the exact tool versions used;
- the source repositories and commit references for internally developed artifacts;
- a list of external dependencies and their versions;
- build scripts and configuration files;
- benchmark scripts and input data references;
- verification reports and result summaries;
- documentation describing the expected environment and execution steps.

Any researcher reproducing the work shall be able to reconstruct the implementation path from architecture definition to FPGA evaluation without relying on undocumented local changes.
