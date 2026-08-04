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
puts "PS INTERFACES"
puts "======================================================"

foreach intf [get_bd_intf_pins zynq_ultra_ps_e_0/*] {
    puts [get_property NAME $intf]
}

puts ""
puts "======================================================"
puts "PS PINS"
puts "======================================================"

foreach pin [get_bd_pins zynq_ultra_ps_e_0/*] {
    puts [get_property NAME $pin]
}

close_project