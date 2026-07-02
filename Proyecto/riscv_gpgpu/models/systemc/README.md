# SystemC Models

This directory contains the SystemC implementation of the RISCV GPGPU architecture.

## Overview

The SystemC models provide a golden reference implementation of the architecture specification. The models capture:

- Thread execution and warp scheduling
- SIMT divergence and reconvergence
- Memory hierarchy and access patterns
- Synchronization and barrier semantics
- Performance characteristics

## Directory Structure

### common/
Shared utilities and common components:
- `platform.h` - SystemC platform utilities
- `types.h` - Common type definitions
- `logging.h` - Logging and tracing utilities
- `memory_model.h` - Memory hierarchy model

### compute_unit/
Compute unit implementation:
- `compute_unit.cpp` - Main compute unit model
- `compute_unit.h` - Compute unit interfaces

### scheduler/
Warp scheduling implementation:
- `warp_scheduler.cpp` - Warp scheduler model
- `warp_scheduler.h` - Scheduler interfaces

### simt_controller/
SIMT and divergence handling:
- `simt_controller.cpp` - SIMT controller model
- `simt_controller.h` - SIMT controller interfaces

### memory/
Memory hierarchy:
- `memory_hierarchy.cpp` - Memory hierarchy model
- `memory_hierarchy.h` - Memory interfaces

### top/
Top-level integration:
- `top.cpp` - Top-level SystemC module
- `main.cpp` - Simulation entry point

## Building

### Prerequisites

- SystemC 2.3.x or later installed
- C++ compiler with C++17 support
- CMake 3.24 or later

### Build Instructions

```bash
cd /path/to/riscv_gpgpu
mkdir -p build && cd build
cmake .. -DBUILD_SYSTEMC_MODELS=ON
make
```

### Running Simulations

```bash
cd build
./bin/systemc_simulation
```

## Configuration

Configuration is loaded from `config/arch_config.yaml`. Key parameters:

- `execution.threads_per_warp`: SIMT width
- `execution.max_warps_per_cu`: Max warps per compute unit
- `memory.shared_memory_size`: Shared memory size
- `scheduler.policy`: Scheduling policy

## Testing

Unit and integration tests are located in `tests/systemc/`.

## Design Decisions

### Scheduler Implementation

The scheduler uses a configurable policy (round-robin, priority, FIFO) to dispatch warps for execution. The implementation captures:

- Warp state machine (READY, RUNNING, STALLED, COMPLETED)
- Resource availability (ALU, memory, synchronization)
- Stall conditions (data dependencies, memory latency)

### SIMT Divergence Model

Divergence handling follows a reconvergence strategy:

- Track active masks for each thread in a warp
- Use stack-based reconvergence with immediate or deferred modes
- Support nested conditionals and loops

### Memory Model

The memory hierarchy models:

- Per-warp shared memory with coherency
- Per-CU L1 cache (configurable size and associativity)
- Shared L2 cache
- Global memory with realistic latencies

## Performance Characterization

The models capture:

- Instruction-level execution latencies
- Memory access latencies (cache hits/misses)
- Stall cycles due to synchronization
- Warp scheduling overhead

Results are collected in simulation logs and can be analyzed for performance prediction and bottleneck identification.

## Status

- Initial skeleton created
- Common utilities defined
- Component structure prepared
- Individual components to be implemented in Phase 3 (US1)
