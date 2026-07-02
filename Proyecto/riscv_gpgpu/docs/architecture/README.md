# Architecture

This directory contains the architecture specification and design documentation for the RISCV GPGPU platform.

## Contents

### interfaces.md
Architecture interface contracts defining the boundaries between components:
- Compute unit interfaces
- Memory hierarchy interfaces
- Scheduler and dispatch interfaces
- Configuration and parameter interfaces

### isa.md
RISC-V ISA extensions and execution semantics:
- Base instruction set
- Custom extensions for SIMT/parallel execution
- Memory access semantics
- Synchronization and barrier semantics

### components/
Individual component specifications:
- Compute Unit
- Warp Scheduler
- SIMT Controller
- Memory Hierarchy
- Synchronization and Barriers

## Status
- Created as placeholder - to be populated during design phase
