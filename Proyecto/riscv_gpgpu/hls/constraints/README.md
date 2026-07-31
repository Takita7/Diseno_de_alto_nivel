# HLS Synthesis Constraints — KV260

Part of T021. Companion to `docs/hls/interfaces.md`, which defines the shared
`compute_pipeline`/`memory_pipeline` C++ interfaces. This directory holds the
per-target Tcl fragments that set device part and clock for Vitis HLS.

## Scope: KV260 only, on-chip caches + external memory via m_axi

**AU15P is out of scope** (dropped per team decision — using the personal board
would have limited the design to its much smaller fabric and it's not the
board the project is actually targeting).

**Alveo U55C was also dropped** (T025, `docs/hls/interfaces.md` §14) — an
explicit team decision, not a technical dead end. `u55c.tcl`/`hls/config/u55c.h`
existed briefly and were removed; KV260 is now the sole target board.

**`memory_pipeline` uses on-chip BRAM for shared memory, L1, and L2 as caches,
and an `m_axi` port to real external memory for the global-memory tier.** See
`docs/hls/interfaces.md` §3 for the corrected design (supersedes an earlier
draft that dropped `m_axi` entirely — that was a mistake; the golden SystemC
model's own config (`config/arch_config.yaml`, `global_memory_size: 0 # ...
unlimited/external`) always treated global memory as external).

## Target

| File | Board | Device | External memory | Flow |
|---|---|---|---|---|
| `kv260.tcl` | Kria KV260 Vision AI Starter Kit | Zynq UltraScale+ MPSoC, XCK26 | 4GB DDR4, via PS HP AXI port | Embedded (Vitis embedded platform or manual Vivado IP Integrator + PetaLinux) |

## How to use

```tcl
source hls/constraints/kv260.tcl
```
Sets `set_part` and `create_clock` only. Kernel-specific directives
(pragmas, BRAM cache-bank `BIND_STORAGE` binds, `m_axi` burst/port config,
unroll factors) live inline next to the code they apply to (T024) — see
`docs/hls/interfaces.md` §8.

## Why 200 MHz for now

200 MHz (5.0 ns period) is an initial target, not KV260's individually
achievable maximum — chosen to keep early resource/timing estimates legible
during design-space exploration (same motivation as the Vortex paper's own
design-space table, `Proyecto/docs/Papers/vortex_micro21_final.pdf` §6.2.1).
Revisit once `compute_pipeline`/`memory_pipeline` pass functional
co-simulation.

## On-chip cache budget reference (see docs/hls/interfaces.md §5 for full detail)

| | KV260 (XCK26) |
|---|---|
| Combined on-chip BRAM+URAM | ≈ 26.6 Mb (~3.3 MB) |
| DSP slices | 1,248 |
| External memory (via `m_axi`) | 4GB DDR4 (shared w/ PS, via HP port) |

On-chip budget here only needs to cover cache capacity (shared memory + L1 +
L2 banks) — not the whole addressable memory, since global memory is external
again.

## Known gaps / action items

- **This part serves PL-side HLS IP synthesis only.** The full embedded
  bitstream build additionally needs a PetaLinux/Vitis platform project —
  out of scope for this file, tracked under T025/T026.
- **`m_axi` port: decided — HP**, not HPC (`docs/hls/interfaces.md` §10.11
  has the reasoning: the on-chip scheduler design sequences host/device
  memory access rather than overlapping it, so PS-cache-coherency isn't
  needed).
- **Exact L1/L2 `WAYS`/`SETS_PER_WAY` values are not yet fixed** —
  `docs/hls/interfaces.md` §5's sizing procedure needs to actually be run
  against real resource reports (now obtainable — a real Vitis 2023.1
  install is configured, see `docs/hls/interfaces.md` §14).
