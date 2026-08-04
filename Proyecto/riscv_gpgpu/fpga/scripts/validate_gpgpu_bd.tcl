set script_dir [file dirname [file normalize [info script]]]
set repo_root  [file normalize "$script_dir/../.."]

set project_file \
    "$repo_root/build/vivado_kv260/riscv_gpgpu_kv260.xpr"

open_project $project_file

open_bd_design \
    "$repo_root/build/vivado_kv260/riscv_gpgpu_kv260.srcs/sources_1/bd/gpgpu_system/gpgpu_system.bd"

puts ""
puts "======================================================"
puts "VALIDATING BLOCK DESIGN"
puts "======================================================"

validate_bd_design

save_bd_design
close_project