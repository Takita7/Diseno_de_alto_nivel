# Architecture Interfaces

This document defines the interface contracts between major architecture components.

## 1. Compute Unit Interface

### Overview
The Compute Unit (CU) executes RISC-V instructions and manages warp scheduling and resource allocation.

### Input Signals
- `config`: Configuration parameters (thread count, shared memory size, etc.)
- `kernel_launch`: Kernel launch request with grid and block dimensions
- `warp_dispatch`: Warp dispatch instruction from scheduler

### Output Signals
- `warp_complete`: Warp completion signal with status
- `memory_request`: Memory access requests to memory hierarchy
- `synchronization_event`: Barrier and synchronization events

## 2. Warp Scheduler Interface

### Overview
The Warp Scheduler selects warps for execution and manages their lifecycle.

### Input Signals
- `compute_unit_ready`: Compute unit availability signal
- `kernel_queue`: Incoming kernel launch requests
- `warp_status`: Warp completion and status reports

### Output Signals
- `warp_dispatch`: Selected warp for execution
- `scheduler_status`: Scheduler state and utilization metrics

## 3. Memory Hierarchy Interface

### Overview
The Memory Hierarchy manages shared and global memory access patterns.

### Input Signals
- `memory_request`: Load/store requests from compute units
- `cache_control`: Cache coherence and invalidation signals

### Output Signals
- `memory_response`: Data responses to load requests
- `memory_status`: Cache hit/miss information

## 4. SIMT Controller Interface

### Overview
The SIMT Controller manages divergence, reconvergence, and branch behavior.

### Input Signals
- `branch_instruction`: Branch and control flow instructions
- `active_mask`: Current warp active mask

### Output Signals
- `pc_update`: Program counter updates
- `divergence_info`: Divergence tracking information

## Status
- Created as template placeholder - to be detailed during architecture definition phase (T005)
