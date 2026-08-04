set script_dir [file dirname [file normalize [info script]]]
set repo_root  [file normalize "$script_dir/../.."]

set project_file \
    "$repo_root/build/vivado_kv260/riscv_gpgpu_kv260.xpr"

set bd_file \
    "$repo_root/build/vivado_kv260/riscv_gpgpu_kv260.srcs/sources_1/bd/gpgpu_system/gpgpu_system.bd"

open_project $project_file
open_bd_design $bd_file

set ps [get_bd_cells zynq_ultra_ps_e_0]

puts ""
puts "======================================================"
puts "ENABLING PS AXI PORTS"
puts "======================================================"

# PS -> PL master for ARM control of AXI-Lite peripherals
set_property -dict [list \
    CONFIG.PSU__USE__M_AXI_GP0 {1} \
] $ps

# PL -> PS slave for accelerator access to DDR
set_property -dict [list \
    CONFIG.PSU__USE__S_AXI_GP0 {1} \
] $ps

save_bd_design

puts ""
puts "PS AXI configuration updated."
puts ""
puts "Enabled interfaces after reconfiguration:"

foreach intf [get_bd_intf_pins zynq_ultra_ps_e_0/*] {
    puts [get_property NAME $intf]
}

save_bd_design
close_project