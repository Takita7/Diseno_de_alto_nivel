set script_dir [file dirname [file normalize [info script]]]
set repo_root  [file normalize "$script_dir/../.."]

set project_file \
    "$repo_root/build/vivado_kv260/riscv_gpgpu_kv260.xpr"

set bd_file \
    "$repo_root/build/vivado_kv260/riscv_gpgpu_kv260.srcs/sources_1/bd/gpgpu_system/gpgpu_system.bd"

open_project $project_file
open_bd_design $bd_file

puts ""
puts "======================================================"
puts "ADDING ZYNQ ULTRASCALE+ PROCESSING SYSTEM"
puts "======================================================"

# Create the Zynq UltraScale+ MPSoC block.
create_bd_cell \
    -type ip \
    -vlnv xilinx.com:ip:zynq_ultra_ps_e:* \
    zynq_ultra_ps_e_0

# Apply the standard processing-system preset/automation.
# This configures the PS clocks and the basic K26 processing-system setup
# recognized by the current project/part.
apply_bd_automation \
    -rule xilinx.com:bd_rule:zynq_ultra_ps_e \
    -config {apply_board_preset "0"} \
    [get_bd_cells zynq_ultra_ps_e_0]

puts ""
puts "PS created."

# ------------------------------------------------------------
# Add a Processor System Reset block.
# ------------------------------------------------------------

create_bd_cell \
    -type ip \
    -vlnv xilinx.com:ip:proc_sys_reset:* \
    rst_pl_clk0

# ------------------------------------------------------------
# Connect PL clock
# ------------------------------------------------------------

connect_bd_net \
    [get_bd_pins zynq_ultra_ps_e_0/pl_clk0] \
    [get_bd_pins gpgpu_scheduler_0/ap_clk] \
    [get_bd_pins memory_pipeline_0/ap_clk] \
    [get_bd_pins rst_pl_clk0/slowest_sync_clk]

# ------------------------------------------------------------
# Connect PS reset into proc_sys_reset
# ------------------------------------------------------------

connect_bd_net \
    [get_bd_pins zynq_ultra_ps_e_0/pl_resetn0] \
    [get_bd_pins rst_pl_clk0/ext_reset_in]

# ------------------------------------------------------------
# Connect synchronized peripheral resets
# ------------------------------------------------------------

connect_bd_net \
    [get_bd_pins rst_pl_clk0/peripheral_aresetn] \
    [get_bd_pins gpgpu_scheduler_0/ap_rst_n] \
    [get_bd_pins memory_pipeline_0/ap_rst_n]

save_bd_design

puts ""
puts "======================================================"
puts "PS + CLOCK + RESET CONNECTIONS CREATED"
puts "======================================================"

close_project