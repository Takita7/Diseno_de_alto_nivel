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
puts "DISABLING UNUSED PS AXI PORT"
puts "======================================================"

set ps [get_bd_cells zynq_ultra_ps_e_0]

# M_AXI_HPM0_LPD corresponds to the GP2 interface in the PS configuration.
# We are using M_AXI_HPM0_FPD for ARM -> PL control, so LPD is unnecessary.
set_property -dict [list \
    CONFIG.PSU__USE__M_AXI_GP2 {0} \
] $ps

puts "Disabled unused M_AXI_HPM0_LPD."

puts ""
puts "======================================================"
puts "ASSIGNING AXI ADDRESSES"
puts "======================================================"

# Let Vivado assign valid address regions for all currently connected
# AXI masters/slaves.
assign_bd_address

save_bd_design

puts ""
puts "======================================================"
puts "ADDRESS MAP"
puts "======================================================"

foreach seg [get_bd_addr_segs] {
    puts "[get_property NAME $seg]  OFFSET=[get_property OFFSET $seg]  RANGE=[get_property RANGE $seg]"
}

puts ""
puts "======================================================"
puts "VALIDATING COMPLETE BLOCK DESIGN"
puts "======================================================"

validate_bd_design

save_bd_design
close_project

puts ""
puts "======================================================"
puts "PASS: PS configuration and address assignment complete"
puts "======================================================"