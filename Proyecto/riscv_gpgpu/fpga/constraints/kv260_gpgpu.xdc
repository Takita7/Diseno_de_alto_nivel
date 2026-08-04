# kv260_gpgpu.xdc - Timing constraints for the GPGPU PL implementation
# Target: AMD Kria KV260/KR260 (Zynq UltraScale+ MPSoC, xck26)
#
# The GPGPU is clocked from the PS-provided PL clock (pl_clk0). No external
# I/O pins are used: all connectivity is via AXI to the PS, so no physical
# pin constraints are required for this block.

# ── Primary clock: 100 MHz from PS pl_clk0 ──────────────────────────────────
create_clock -period 10.000 -name pl_clk0 [get_pins -hier -filter {NAME =~ */pl_clk0}]

# ── AXI4-Lite control interface (s_axi_ctrl) is synchronous to pl_clk0 ──────
# ── AXI4 masters (m_axi_imem, m_axi_dmem) are synchronous to pl_clk0 ────────
# No clock-domain crossings inside the GPGPU block at this configuration.

# ── Reset: PS peripheral reset, treated as asynchronous assertion ───────────
set_false_path -from [get_pins -hier -filter {NAME =~ */pl_resetn0}]
