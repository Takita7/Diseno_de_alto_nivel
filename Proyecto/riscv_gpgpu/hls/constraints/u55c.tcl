# u55c.tcl — Vitis HLS synthesis constraints for the Alveo U55C
#
# Device: Virtex UltraScale+, XCU55C (3 SLRs, stacked silicon interconnect)
# Memory: 16 GB HBM2 co-located on SLR0, 460 GB/s aggregate bandwidth,
#         up to 32 pseudo-channels (256MB each) — memory_pipeline.cpp's
#         `global_mem` m_axi port should be bound to a specific HBM PC at
#         link time (v++ --connectivity.sp), not decided at HLS C-synthesis time.
# Host link: PCIe Gen3 x16 or dual Gen4 x8; single-slot, 150W max.
#
# Deployment note: this is the Vitis UNIFIED/XRT flow. HLS C-synthesis here
# only produces the IP; T025/T026 additionally need `v++ --link` against an
# Alveo platform (confirm exact installed platform string via `platforminfo -l`)
# to produce a .xclbin, plus an XRT/OpenCL host application.

set_part {xcu55c-fsvh2892-2L-e}

# 200 MHz initial target — see hls/constraints/README.md for rationale.
# The U55C has headroom to run kernels faster (300+ MHz is common for
# well-pipelined Alveo kernels) — revisit after functional co-simulation passes.
create_clock -period 5.0 -name clk

# set_clock_uncertainty 12.5%
