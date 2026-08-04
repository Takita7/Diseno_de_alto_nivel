set script_dir [file dirname [file normalize [info script]]]
set repo_root  [file normalize "$script_dir/../.."]

set project_file \
    "$repo_root/build/vivado_kv260/riscv_gpgpu_kv260.xpr"

open_project $project_file

open_bd_design \
    "$repo_root/build/vivado_kv260/riscv_gpgpu_kv260.srcs/sources_1/bd/gpgpu_system/gpgpu_system.bd"

puts ""
puts "======================================================"
puts "CONNECTING GPGPU <-> MEMORY PIPELINE STREAMS"
puts "======================================================"

# GPGPU memory request -> memory pipeline request
connect_bd_intf_net \
    [get_bd_intf_pins gpgpu_scheduler_0/mem_req_out] \
    [get_bd_intf_pins memory_pipeline_0/req_in]

# Memory pipeline response -> GPGPU response
connect_bd_intf_net \
    [get_bd_intf_pins memory_pipeline_0/resp_out] \
    [get_bd_intf_pins gpgpu_scheduler_0/mem_resp_in]

save_bd_design

puts ""
puts "======================================================"
puts "AXI-STREAM CONNECTIONS CREATED"
puts "======================================================"

puts "gpgpu_scheduler_0/mem_req_out -> memory_pipeline_0/req_in"
puts "memory_pipeline_0/resp_out -> gpgpu_scheduler_0/mem_resp_in"

close_project