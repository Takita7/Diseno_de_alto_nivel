# HLS Synthesis Constraints — Per-Board

Part of T021. Companion to `docs/hls/interfaces.md`, which defines the shared
`compute_pipeline`/`memory_pipeline` C++ interfaces. This directory holds the
per-target Tcl fragments that set device part and clock for Vitis HLS.

## Scope: two boards, on-chip caches + external memory via m_axi

**AU15P is out of scope** (dropped per team decision — using the personal board
would have limited the design to its much smaller fabric and it's not one of the
two boards the project is actually targeting).

**`memory_pipeline` uses on-chip BRAM for shared memory, L1, and L2 as caches,
and an `m_axi` port to real external memory for the global-memory tier.** See
`docs/hls/interfaces.md` §3 for the corrected design (supersedes an earlier
draft that dropped `m_axi` entirely — that was a mistake; the golden SystemC
model's own config (`config/arch_config.yaml`, `global_memory_size: 0 # ...
unlimited/external`) always treated global memory as external).

## Targets

| File | Board | Device | External memory | Flow |
|---|---|---|---|---|
| `kv260.tcl` | Kria KV260 Vision AI Starter Kit | Zynq UltraScale+ MPSoC, XCK26 | 4GB DDR4, via PS HP/HPC AXI port | Embedded (Vitis embedded platform or manual Vivado IP Integrator + PetaLinux) |
| `u55c.tcl` | Alveo U55C | Virtex UltraScale+, XCU55C | 16GB HBM2 (no DDR on this board), bound to a pseudo-channel at `v++` link time | Vitis unified/XRT (`v++` + `.xclbin`) |

## How to use

```tcl
source hls/constraints/kv260.tcl   ;# or u55c.tcl
```
Each file sets `set_part` and `create_clock` only. Kernel-specific directives
(pragmas, BRAM cache-bank `BIND_STORAGE` binds, `m_axi` burst/port config,
unroll factors) live in `hls/pragma/` (T024).

## Why one uniform clock target for now

Both boards start at the same 200 MHz (5.0 ns) target period rather than each
board's individually achievable maximum, keeping early resource/timing estimates
comparable during design-space exploration (same motivation as the Vortex paper's
own design-space table, `Proyecto/docs/Papers/vortex_micro21_final.pdf` §6.2.1).
Revisit per board once `compute_pipeline`/`memory_pipeline` pass functional
co-simulation — U55C has more headroom to go faster than KV260.

## On-chip cache budget reference (see docs/hls/interfaces.md §5 for full detail)

| | KV260 (XCK26) | Alveo U55C (XCU55C) |
|---|---|---|
| Combined on-chip BRAM+URAM | ≈ 26.6 Mb (~3.3 MB) | ≈ 43 MB |
| DSP slices | 1,248 | 9,024 |
| External memory (via `m_axi`) | 4GB DDR4 (shared w/ PS) | 16GB HBM2, 460GB/s aggregate |

On-chip budget here only needs to cover cache capacity (shared memory + L1 +
L2 banks) — not the whole addressable memory, since global memory is external
again. KV260 is still the tighter cache budget of the two; size L1/L2
`WAYS`/`SETS_PER_WAY` against it first, then decide whether U55C's larger
on-chip budget goes toward bigger caches, more compute units, or is left
largely unused in favor of leaning on HBM bandwidth directly.

## Known gaps / action items

- **`u55c.tcl`**: part number is the FPGA silicon (`xcu55c-fsvh2892-2L-e`). The
  Vitis **platform** string (needed for `v++ --platform` in T025/T026, not for
  `set_part` here) is separate and versioned by shell release — confirm the
  exact platform installed in your Vitis environment (`platforminfo -l`) when
  you get to T025. Which HBM pseudo-channel(s) `global_mem` binds to is also a
  T025/T026 link-time decision, not fixed here.
- **`kv260.tcl`**: this part serves PL-side HLS IP synthesis only. The full
  embedded bitstream build additionally needs a PetaLinux/Vitis platform project
  — out of scope for this file, tracked under T025/T026. HP vs HPC port choice
  (HPC is PS-cache-coherent, HP is not) is also open — see
  `docs/hls/interfaces.md` §6.
- **Exact L1/L2 `WAYS`/`SETS_PER_WAY` values are not yet fixed** —
  `docs/hls/interfaces.md` §5's sizing procedure needs to actually be run once
  `NUM_CUS` is chosen, before T022/T023 implementation starts in earnest.
