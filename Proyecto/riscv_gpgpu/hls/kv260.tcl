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
create_clock -period 5.0 -name clk

# Default Vitis HLS clock uncertainty (12.5%) is left at tool default;
# override here explicitly once real timing data is available:
# set_clock_uncertainty 12.5%
