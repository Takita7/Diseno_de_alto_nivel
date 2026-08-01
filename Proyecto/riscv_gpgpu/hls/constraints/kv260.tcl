# kv260.tcl — Vitis HLS synthesis constraints for the Kria KV260 Vision AI Starter Kit
#
# Device: Zynq UltraScale+ MPSoC, XCK26 (K26 SOM), commercial grade, -2LV speed grade
# Source: Kria K26 SOM Data Sheet (DS987)
#   PL fabric: 256K system logic cells, 1248 DSP slices, 144 BRAM, 64 URAM
#   PS: quad-core Cortex-A53 (<=1.5GHz), dual-core Cortex-R5F (<=600MHz)
#   Memory: 4GB 64-bit DDR4 @ 2400 Mb/s, shared between PS and PL via AXI HP/HPC ports
#
# Deployment note: this is an EMBEDDED target (no PCIe host). Full bitstream
# generation requires a Vitis embedded platform project or a hand-built Vivado
# IP Integrator design + PetaLinux — this file only covers HLS-level part/clock
# selection, i.e. what's needed to run C-synthesis and get resource/timing
# estimates for compute_pipeline.cpp / memory_pipeline.cpp.

set_part {xck26-sfvc784-2LV-c}

# 200 MHz initial target — see hls/constraints/README.md for rationale
# (uniform cross-board target during early design-space exploration).
#
# Tried relaxing this to 6.0ns (docs/hls/interfaces.md SS16.15, Phase 2)
# to close compute_pipeline's -0.32ns executeALU violation - real,
# measured result: only partial improvement for compute_pipeline
# (-0.32ns -> -0.21ns, not fully closed) AND a real regression for
# memory_pipeline (Fmax 142.43MHz -> 129.02MHz - the scheduler used the
# extra slack to pack deeper combinational logic per stage, a known
# self-defeating HLS effect, not a free win). Reverted: this file is
# shared across every kernel, so a fix that only partially helps one
# kernel while measurably hurting another isn't a net win at the
# project-wide-constraint level. Both violations remain open, deferred
# for real source-level investigation (SS16.15) rather than papered over
# by a clock change that doesn't actually solve either one.
create_clock -period 5.0 -name clk

# Default Vitis HLS clock uncertainty (12.5%) is left at tool default;
# override here explicitly once real timing data is available:
# set_clock_uncertainty 12.5%
