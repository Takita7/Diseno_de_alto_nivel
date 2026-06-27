# System Specification: FPGA-Based Open RISC-V GPGPU

**Status**: Draft  
**Created**: 2026-06-27  
**Document Owner**: Project Architecture Team  
**Source**: RISC-V GPGPU Constitution v1.0.0

## 1. System Overview and Purpose

The system shall provide an open-source, FPGA-targetable, research-oriented general-purpose GPU architecture based on the RISC-V instruction set and a SIMT execution model. The system shall support kernel execution, thread management, memory access, synchronization, host interaction, and reproducible evaluation across architectural configurations.

The specification shall define the required behavior of the platform without prescribing a specific implementation approach. It shall serve as the authoritative basis for architecture definition, SystemC modeling, HLS development, RTL refinement, LLVM integration, verification planning, and documentation.

## 2. Scope

The system scope shall include:
- a programmable GPU-style execution fabric for FPGA deployment;
- a SIMT-oriented execution model with thread, warp, and control-flow semantics;
- host-side communication for kernel submission, configuration, and status reporting;
- a software stack including compiler, runtime, driver, and host API support;
- configurable architectural parameters for design-space exploration;
- verification and benchmarking procedures for functional and performance evaluation.

The scope shall exclude proprietary-only toolchains, non-reproducible deployment flows, and implementations that are not traceable to a documented requirement.

## 3. Goals

The system shall:
- enable open and reproducible research on RISC-V-based GPU architecture;
- target FPGA implementation as the primary execution path;
- support a CUDA/HIP-inspired programming model adapted to the project architecture;
- preserve modularity, parameterizability, and extensibility for future experimentation;
- provide a clear hardware/software co-design path from specification to validation;
- support traceable development from requirements to verification evidence.

## 4. Non-Goals

The system shall not:
- mandate a single fixed microarchitecture as the only valid design;
- require proprietary IP or closed-source toolchains for baseline operation;
- guarantee performance parity with commercial GPU products;
- define a complete production-grade software ecosystem beyond the minimum research and validation requirements;
- treat FPGA implementation as a substitute for architectural specification.

## 5. Functional Requirements

The following functional requirements shall govern the system.

| ID | Requirement | Rationale | Priority | Verification Method | Affected Subsystems |
|----|-------------|-----------|----------|---------------------|---------------------|
| REQ-001 | The system shall support execution of user-defined kernels from a host environment through a well-defined submission interface. | Kernel execution is the primary functional capability of the platform. | Mandatory | Simulation, FPGA Test | Host API, Runtime, Driver, Kernel Loader |
| REQ-002 | The system shall expose a configuration interface that allows the selection of architectural parameters such as compute unit count, warp size, register file capacity, and shared memory size. | Configurability is required for design-space exploration and research evaluation. | Mandatory | Inspection, Simulation | Configuration Interface, Runtime, Hardware Control |
| REQ-003 | The system shall organize execution into logical threads and groups that can be mapped to hardware execution resources in a consistent manner. | Thread organization is essential for portability of the programming model and predictable execution. | Mandatory | Simulation | Thread Model, Compute Units, Warp Scheduler |
| REQ-004 | The system shall support warp-level scheduling behavior that selects executable groups of threads for dispatch according to resource availability and execution state. | Scheduling defines how work is assigned to execution resources. | Mandatory | Simulation, FPGA Test | Warp Scheduler, SIMT Controller |
| REQ-005 | The system shall implement SIMT execution semantics in which threads execute in lockstep where applicable, while preserving explicit handling of divergence and reconvergence. | SIMT behavior is a core architectural property of the system. | Mandatory | Simulation, FPGA Test | SIMT Controller, Compute Units |
| REQ-006 | The system shall provide deterministic handling of control-flow divergence and reconvergence so that execution semantics remain understandable and verifiable. | Divergence handling affects correctness and predictability. | Mandatory | Simulation | SIMT Controller, Instruction Fetch/Decode |
| REQ-007 | The system shall support memory access operations for registers, local memory, shared memory, and global memory in a manner consistent with the defined memory model. | Memory behavior is central to correctness and performance. | Mandatory | Simulation, FPGA Test | Load/Store Unit, Memory Controller, Shared Memory |
| REQ-008 | The system shall support synchronization primitives that coordinate execution between threads and thread groups as required by the programming model. | Synchronization is necessary for correctness in parallel workloads. | Mandatory | Simulation, FPGA Test | Synchronization Logic, Memory Controller |
| REQ-009 | The system shall provide a runtime interface that enables kernel launch, execution status observation, and error reporting. | Runtime interaction is required for practical use and debugging. | Mandatory | Inspection, Simulation, FPGA Test | Runtime, Driver, Host API |
| REQ-010 | The system shall expose host-to-device communication semantics for data movement, configuration, and execution control. | Host communication is necessary for deployment and integration. | Mandatory | Inspection, Simulation, FPGA Test | Host API, Driver, FPGA Interface |
| REQ-011 | The system shall support compiler-visible ISA semantics that are sufficient for code generation and execution of the supported programming model. | Compiler integration depends on a stable and documented ISA contract. | Mandatory | Inspection, Simulation | ISA, Compiler Backend, Instruction Fetch/Decode |
| REQ-012 | The system shall provide a traceable record of requirements, design decisions, implementation artifacts, and verification evidence for each major feature. | Traceability is required by the constitution and supports research reproducibility. | Mandatory | Inspection | Documentation, Verification Infrastructure |
| REQ-013 | The system shall support diagnostic and status reporting for execution failures, resource contention, and configuration errors. | Diagnostics improve maintainability and debugging. | Recommended | Simulation, FPGA Test | Runtime, Driver, Verification Infrastructure |
| REQ-014 | The system shall permit architectural experimentation through parameter changes without requiring invasive redesign of unrelated subsystems. | Extensibility is a core project objective. | Recommended | Inspection, Simulation | Configuration Interface, Compute Units, Memory Hierarchy |

## 6. Non-Functional Requirements

| ID | Requirement | Rationale | Priority | Verification Method | Affected Subsystems |
|----|-------------|-----------|----------|---------------------|---------------------|
| NFR-001 | The system shall support performance evaluation under representative workloads with measurable throughput, latency, and resource utilization metrics. | Performance objectives must be verified rather than assumed. | Mandatory | Benchmark | Compute Units, Memory Hierarchy, Verification Infrastructure |
| NFR-002 | The system shall scale to increasing numbers of compute units and memory resources without requiring a redesign of the core execution semantics. | Scalability is a key objective for design-space exploration. | Recommended | Simulation, Benchmark | Compute Units, Scheduler, Memory Controller |
| NFR-003 | The system shall be configurable across a range of architectural parameters while preserving a consistent external behavior. | Configurability supports research and experimentation. | Mandatory | Inspection, Simulation | Configuration Interface, Hardware Architecture |
| NFR-004 | The system shall preserve modular boundaries between compute, control, memory, and software interface elements. | Modularity is required for maintainability and reuse. | Mandatory | Inspection | Module Design, Interfaces |
| NFR-005 | The system shall be maintainable through clear documentation, explicit interfaces, and traceable design decisions. | Maintainability supports long-term project survival. | Mandatory | Inspection | Documentation, Repository Structure |
| NFR-006 | The system shall be portable across supported toolchains and host environments where the required dependencies are available. | Portability improves collaboration and reproducibility. | Recommended | Inspection, Simulation | Toolchain, Runtime, Driver |
| NFR-007 | The system shall support reproducible builds, simulations, tests, and benchmark runs. | Reproducibility is central to the research mission. | Mandatory | Inspection, Benchmark | Toolchain, Verification Infrastructure |

## 7. Hardware Requirements

The hardware portion of the system shall include:
- compute units capable of executing the supported execution model;
- a warp scheduler for dispatching executable work;
- a SIMT controller for divergence and reconvergence handling;
- a register file supporting the required thread state and lane execution model;
- a load/store unit for memory access operations;
- a memory controller for access coordination and interface behavior;
- shared memory resources for intra-group communication and reuse;
- instruction fetch and decode capabilities for the supported ISA semantics.

These hardware capabilities shall be described in a way that supports both simulation and synthesis-oriented reasoning.

## 8. Software Requirements

The software portion of the system shall include:
- an LLVM/Clang-based compilation path with a custom backend where required;
- a runtime component for kernel launch, execution control, and status reporting;
- a driver component for low-level communication and device management;
- a host API for submission, configuration, and diagnostics;
- a kernel loader for program preparation and deployment.

The software stack shall remain implementation-independent in this specification while preserving the required functional behavior.

## 9. Hardware/Software Interface Requirements

### 9.1 ISA Requirements
The ISA shall support the required execution semantics for thread-level, warp-level, and SIMT-oriented processing. The ISA contract shall define the visible behavior of instructions affecting control flow, synchronization, memory access, and configuration.

### 9.2 Thread Model
The system shall define the relationship between logical threads, groups, and hardware execution resources. The model shall make thread coordination, scheduling, and divergence behavior explicit.

### 9.3 Memory Model
The memory model shall define the behavior of register, local, shared, and global memory accesses, including visibility and ordering expectations where relevant.

### 9.4 Synchronization Model
The synchronization model shall define the semantics of primitives used to coordinate thread execution and shared-memory access.

### 9.5 Configuration Interface
The system shall expose a mechanism for configuring architectural parameters and execution-related options in a way that is consistent across software and hardware views.

## 10. External Interfaces

The system shall define the following external interfaces:
- Host to Runtime: submission of kernels, configuration, and status queries;
- Runtime to Driver: execution control, data movement coordination, and diagnostics;
- Driver to FPGA: configuration and execution transport where applicable;
- Compiler to ISA: code generation and validation of supported semantics.

Each interface shall be documented with its responsibilities, expectations, and error behavior.

## 11. Configurable Parameters

The system shall support configuration of the following parameters where applicable:
- number of compute units;
- warp size;
- number of warps per compute unit or execution group;
- register file size;
- shared memory size;
- cache parameters where the architecture includes cache behavior;
- other execution and memory-related parameters required for design-space exploration.

Configuration changes shall not invalidate the core functional contract of the system.

## 12. Assumptions

The specification shall assume:
- the project will continue to pursue FPGA as the primary implementation target;
- SystemC will remain the architectural golden model for exploration and reasoning;
- HLS will remain a supported path to synthesizable RTL where feasible;
- the software toolchain will continue to evolve alongside the hardware architecture;
- contributors will maintain traceability between specifications, implementations, and verification evidence.

## 13. Constraints

The system shall operate under the following constraints:
- design decisions shall remain consistent with the governing constitution;
- changes to interfaces or ISA semantics shall be explicitly specified before implementation;
- performance claims shall be backed by reproducible evidence;
- implementation choices shall preserve modularity and maintainability;
- research-oriented development shall favor transparency and extensibility over opaque optimization.

## 14. Risks

The following risks shall be monitored:
- interface drift between hardware and software components;
- excessive complexity that reduces maintainability;
- incomplete verification coverage for new architectural features;
- unbounded parameterization that weakens consistency;
- performance improvements that are not reproducible under comparable conditions.

## 15. Acceptance Criteria

The system shall be considered acceptable when:
- all mandatory requirements are satisfied by the relevant artifacts and evidence;
- verification results demonstrate correctness for the specified scenarios;
- the architecture supports at least one representative FPGA deployment path;
- documentation and traceability artifacts accompany the implementation;
- the design remains configurable and extensible for future research work.

## 16. Verification Requirements

### 16.1 Unit-Level Verification
The system shall include unit-level verification for individual modules and subsystems covering functionality, interface behavior, and expected edge cases.

### 16.2 Integration Verification
The system shall include integration verification for the interaction of compute, scheduling, memory, software, and control subsystems.

### 16.3 FPGA Validation
The system shall include validation of the architecture using FPGA deployment where applicable, including functional correctness and representative execution scenarios.

### 16.4 Benchmark Validation
The system shall include benchmark validation for performance, resource utilization, and scalability under representative workloads.

## 17. Requirement Traceability

Each functional and non-functional requirement shall be traceable to its originating stakeholder need, the relevant subsystem, and the verification evidence produced during development.

| Requirement ID | Traceability Focus | Primary Evidence Expected |
|----------------|--------------------|---------------------------|
| REQ-001 | Kernel execution path | Simulation and FPGA test results |
| REQ-002 | Configuration interface and architectural parameterization | Inspection and simulation evidence |
| REQ-003 | Thread organization and execution mapping | Simulation evidence |
| REQ-004 | Warp scheduling behavior | Simulation and FPGA test results |
| REQ-005 | SIMT semantics and execution behavior | Simulation and FPGA test results |
| REQ-006 | Divergence and reconvergence behavior | Simulation evidence |
| REQ-007 | Memory semantics and access behavior | Simulation and FPGA test results |
| REQ-008 | Synchronization semantics | Simulation and FPGA test results |
| REQ-009 | Runtime interaction and status reporting | Inspection, simulation, and FPGA test evidence |
| REQ-010 | Host communication and device integration | Inspection and hardware/software validation |
| REQ-011 | ISA and compiler compatibility | Inspection and simulation evidence |
| REQ-012 | Traceability and documentation completeness | Inspection evidence |
| REQ-013 | Diagnostics and error reporting | Simulation and FPGA test evidence |
| REQ-014 | Extensibility through configuration | Inspection and simulation evidence |
| NFR-001 | Performance measurement and benchmarking | Benchmark results |
| NFR-002 | Scalability | Simulation and benchmark evidence |
| NFR-003 | Configurability | Inspection and simulation evidence |
| NFR-004 | Modularity | Inspection evidence |
| NFR-005 | Maintainability | Inspection evidence |
| NFR-006 | Portability | Inspection and simulation evidence |
| NFR-007 | Reproducibility | Inspection and benchmark evidence |
