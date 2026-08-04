set script_dir [file dirname [file normalize [info script]]]
set repo_root  [file normalize "$script_dir/../.."]

set project_dir "$repo_root/build/vivado_kv260"
set project_name "riscv_gpgpu_kv260"

# KV260 / K26 device
set part_name "xck26-sfvc784-2LV-c"

create_project $project_name $project_dir \
    -part $part_name \
    -force

# ---------------------------------------------------------
# Add the two HLS-generated IP repositories
# ---------------------------------------------------------

set gpgpu_ip_repo \
    "$repo_root/build/ip_export/gpgpu_scheduler/gpgpu_scheduler/solution1/impl/ip"

set memory_ip_repo \
    "$repo_root/build/ip_export/memory_pipeline/memory_pipeline/solution1/impl/ip"

set_property ip_repo_paths \
    [list $gpgpu_ip_repo $memory_ip_repo] \
    [current_project]

update_ip_catalog

puts "================================================="
puts "KV260 Vivado project created"
puts "HLS IP repositories added"
puts "================================================="

puts "GPGPU IP:"
puts $gpgpu_ip_repo

puts "Memory IP:"
puts $memory_ip_repo