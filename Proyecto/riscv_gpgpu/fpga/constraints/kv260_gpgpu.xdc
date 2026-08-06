# kv260_gpgpu.xdc - Timing constraints for the GPGPU PL implementation
# Target: AMD Kria KV260/KR260 (Zynq UltraScale+ MPSoC, xck26)
#
# The GPGPU is clocked from the PS-provided PL clock (pl_clk0). No external
# I/O pins are used: all connectivity is via AXI to the PS, so no physical
# pin constraints are required for this block.

# ── Reset: PS peripheral reset, treated as asynchronous assertion ───────────
# The PLL's locked output already acts as power-on reset via proc_sys_reset.
# pl_resetn0 is a secondary supervisor reset from the PS.
set_false_path -from [get_pins -hier -filter {NAME =~ */pl_resetn0}]
