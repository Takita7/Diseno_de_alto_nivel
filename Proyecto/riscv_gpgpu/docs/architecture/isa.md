# Baseline ISA and Execution Semantics

## Overview

This document defines the RISC-V ISA subset and execution semantics for the RISCV GPGPU architecture.

## RISC-V Base ISA

The baseline implementation supports:

### RV32I - Base 32-bit Integer ISA
- 32-bit registers (x0-x31)
- Integer arithmetic and logical operations
- Load/store operations
- Control flow (branches, jumps)
- Basic synchronization primitives

### Key Instruction Categories

#### Arithmetic and Logical
- ADD, SUB, AND, OR, XOR, SLL, SRL, SRA
- ADDI, ANDI, ORI, XORI, SLLI, SRLI, SRAI

#### Load and Store
- LW, LH, LB (load word, halfword, byte)
- SW, SH, SB (store word, halfword, byte)
- All memory operations are data-race free when serialized correctly

#### Control Flow
- BEQ, BNE, BLT, BGE, BLTU, BGEU (branches)
- JAL, JALR (jumps and links)
- Program counter (PC) is 32-bit

#### Synchronization (Extensions)
- **FENCE**: Memory ordering primitive
- **FENCE.I**: Instruction fence
- **LR/SC** (Load-Reserved/Store-Conditional): Future extension
- **AMOSWAP, AMOADD** (Atomic operations): Future extension

## SIMT Execution Model

### Warp Definition
- A warp is a group of threads executing in SIMT lockstep
- Default warp size: 32 threads
- Threads are numbered 0 to 31 within a warp

### Thread State
Each thread maintains:
- Program counter (PC)
- Register file (32 x 32-bit registers)
- Active/inactive status (determined by control flow)
- Memory access permissions

### Execution States

**Thread States:**
```
┌─────────┐   Launch    ┌────────┐
│  IDLE   │──────────→  │ ACTIVE │
└─────────┘             └────────┘
                            ↑ │
                            │ ↓ (depends)
                            │ ┌────────┐
                            └─│ WAITING│
                              └────────┘
                                  ↓
                              ┌─────────┐
                              │ COMPLETE│
                              └─────────┘
```

### Warp Execution Model

Warps proceed through the following phases:

1. **Allocation**: Warp is assigned to a compute unit
2. **Scheduling**: Warp is selected for execution
3. **Execution**: Instructions execute in order (in-order pipeline)
4. **Synchronization**: Warp waits for synchronization events
5. **Completion**: All threads in warp reach barrier or exit

## Control Flow and Divergence

### Divergence Behavior

When a conditional branch is encountered:
- If all threads in warp take the same path: no divergence
- If threads take different paths: SIMT divergence occurs

### Reconvergence Model

**Immediate Reconvergence:**
- Reconstruct program flow stack when branch encountered
- Execute all branches separately
- Threads inactive on unselected branch
- Reconverge at join point (if-endif, loop-exit, etc.)

**Reconvergence Point Detection:**
- Post-dominators in control flow graph
- Can be explicit (barriers) or implicit (natural joins)
- Compiler responsibility to ensure correctness

### Active Mask

Each thread maintains an active mask:
- 1-bit per thread in warp
- Active=1: thread participates in current instruction
- Active=0: thread is inactive (diverged path)

Example divergence scenario:
```
Thread 0-15: take IF branch
Thread 16-31: take ELSE branch

Active mask for IF block: 0xFFFF (threads 0-15)
Active mask for ELSE block: 0xFFFF0000 (threads 16-31)

At reconvergence: Active mask = 0xFFFFFFFF (all active again)
```

## Memory Semantics

### Address Space
- **Shared Memory**: Per-compute-unit, 48KB default
  - Accessible by all threads in a CU
  - Lower latency than global memory
  - Coherent within warp
- **Global Memory**: Shared across all CUs
  - Higher latency
  - Requires synchronization for consistency
- **Local Memory**: Per-thread (spilled registers)
  - Private to thread
  - Not accessible by other threads

### Memory Consistency Model

**Intra-Warp Memory Consistency:**
- Memory operations within a warp are sequentially consistent
- All threads see memory updates in the same order

**Inter-Warp Memory Consistency:**
- Requires explicit barriers (FENCE)
- Memory operations are not automatically ordered across warps

### Synchronization Primitives

#### Barrier Synchronization (BARRIER)
```
All threads in warp must reach barrier before proceeding.
- Semantics: All memory operations before barrier complete
- Semantics: All threads wait until all reach barrier
- Then: Barrier is cleared and execution continues
```

#### Memory Ordering (FENCE)
```
Ensures all memory operations before FENCE complete.
- Synchronizes shared and global memory
- Does not synchronize thread execution (only memory)
```

## Exceptions and Error Handling

### Supported Exceptions
- Invalid memory access (out of bounds)
- Illegal instruction
- Divide by zero

### Exception Handling
- Exceptions are warp-level (affect entire warp)
- Exception handler sets completion flag with error status
- Host is notified of kernel failure

## Performance Characteristics

### Instruction Latencies (in cycles)

| Instruction Type | Latency | Throughput |
|---|---|---|
| Integer ALU | 1 | 1 per cycle |
| Multiplication | 3 | 1 per 3 cycles |
| Division | 16 | 1 per 16 cycles |
| Floating Point (if supported) | 4 | 1 per 4 cycles |
| Load (L1 cache hit) | 2 | 1 per cycle |
| Load (L1 cache miss) | 10-50 | Varies |
| Store | 1 | 1 per cycle |
| Branch | 1 + potential stall | Depends on divergence |

### Memory Latencies

| Operation | Latency (cycles) |
|---|---|
| Shared Memory Access (hit) | 2 |
| L1 Cache Hit | 4 |
| L2 Cache Hit | 10 |
| Global Memory Hit | 50-200 |

## Baseline Execution Example

```
// Kernel: vector add
for (thread_id in 0..31) {
    idx = thread_id
    a[idx] = load from global memory
    b[idx] = load from global memory
    result = a[idx] + b[idx]
    store result to global memory
}

Timeline:
Cycle 0: All 32 threads start
Cycle 2: All loads complete (memory latency)
Cycle 3: All ALU adds execute
Cycle 4: All stores execute
Cycle 5: Kernel complete
```

## Future ISA Extensions

- Floating point (RV32F)
- Double precision (RV32D)
- Atomic operations (RV32A)
- Compressed instructions (RV32C)
- Multiplication/Division (RV32M)

## Status

- ISA baseline defined
- Execution semantics documented
- Memory model specified
- Synchronization primitives defined
- Performance characteristics captured

This ISA definition is the golden reference for all subsequent implementation phases.
