# HLS

This directory is the HLS implementation workspace for mapping the validated
SystemC behavior into synthesizable hardware blocks.

Scope of this README:
- what belongs in `hls/`
- expected structure and tool assumptions

Out of scope:
- functional-model behavior details (`models/systemc/`)
- board deployment scripts (`fpga/`, `scripts/deploy_kria.sh`)

## Directory Scope

| Path | Scope |
|---|---|
| `hls/src/` | Synthesizable HLS source files |
| `hls/config/` | Tool configuration and synthesis settings |
| `hls/pragma/` | Shared pragma/header definitions |

## Design Contract

- Keep software-visible register/DMA contract aligned with
    `docs/architecture/axi_interface.md`.
- Keep behavior aligned with validated SystemC semantics.
- Keep HLS-specific constraints local to this directory.

## Tooling Baseline

- Vitis HLS (2023.x or compatible)
- Target device family: Kria K26 class
- Clock and constraints are finalized in FPGA handoff stages

## Current Status

- Directory scaffold is present.
- Phase 7 software-side deployment path exists and is ready to consume HLS/RTL
    outputs once generated.
