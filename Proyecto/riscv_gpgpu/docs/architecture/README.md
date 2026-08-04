# Architecture Docs

This directory contains architecture contracts and design references.

Scope:
- interface definitions between software, model, and FPGA control plane
- ISA and execution-semantics reference docs

## Files

| File | Purpose |
|---|---|
| `ARCHITECTURE.md` | High-level architecture narrative |
| `interfaces.md` | Cross-component interface contracts |
| `isa.md` | ISA and execution model notes |
| `axi_interface.md` | AXI4-Lite register map and AXI4 DMA contract for Kria deployment |

## Ownership Rules

- Register/DMA interface changes must be mirrored in code constants
	(`driver/src/fpga_regs.h`).
- Detailed implementation behavior belongs in component source trees, not here.
