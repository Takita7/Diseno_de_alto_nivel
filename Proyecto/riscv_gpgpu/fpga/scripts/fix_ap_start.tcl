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
puts "FIXING GPGPU AP_START"
puts "======================================================"

# Constant logic 1
create_bd_cell \
    -type ip \
    -vlnv xilinx.com:ip:xlconstant:* \
    const_ap_start

set_property -dict [list \
    CONFIG.CONST_WIDTH {1} \
    CONFIG.CONST_VAL {1} \
] [get_bd_cells const_ap_start]

connect_bd_net \
    [get_bd_pins const_ap_start/dout] \
    [get_bd_pins gpgpu_scheduler_0/ap_start]

save_bd_design

puts "gpgpu_scheduler_0/ap_start tied HIGH."

puts ""
puts "======================================================"
puts "VALIDATING"
puts "======================================================"

validate_bd_design

save_bd_design
close_project

puts ""
puts "======================================================"
puts "PASS: ap_start fixed"
puts "======================================================"