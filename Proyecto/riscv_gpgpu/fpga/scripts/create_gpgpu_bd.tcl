set script_dir [file dirname [file normalize [info script]]]
set repo_root  [file normalize "$script_dir/../.."]

set project_file \
    "$repo_root/build/vivado_kv260/riscv_gpgpu_kv260.xpr"

open_project $project_file

# ------------------------------------------------------------
# Create a fresh block design
# ------------------------------------------------------------

set bd_name "gpgpu_system"

if {[llength [get_bd_designs -quiet $bd_name]] != 0} {
    close_bd_design [get_bd_designs $bd_name]
}

create_bd_design $bd_name

# ------------------------------------------------------------
# Instantiate custom HLS IPs
# ------------------------------------------------------------

create_bd_cell \
    -type ip \
    -vlnv riscv_gpgpu:hls:gpgpu_scheduler:1.0 \
    gpgpu_scheduler_0

create_bd_cell \
    -type ip \
    -vlnv riscv_gpgpu:hls:memory_pipeline:1.0 \
    memory_pipeline_0

save_bd_design

puts ""
puts "======================================================"
puts "GPGPU SCHEDULER INTERFACES"
puts "======================================================"

foreach intf [get_bd_intf_pins gpgpu_scheduler_0/*] {
    puts [get_property NAME $intf]
}

puts ""
puts "======================================================"
puts "GPGPU SCHEDULER PINS"
puts "======================================================"

foreach pin [get_bd_pins gpgpu_scheduler_0/*] {
    puts [get_property NAME $pin]
}

puts ""
puts "======================================================"
puts "MEMORY PIPELINE INTERFACES"
puts "======================================================"

foreach intf [get_bd_intf_pins memory_pipeline_0/*] {
    puts [get_property NAME $intf]
}

puts ""
puts "======================================================"
puts "MEMORY PIPELINE PINS"
puts "======================================================"

foreach pin [get_bd_pins memory_pipeline_0/*] {
    puts [get_property NAME $pin]
}

puts ""
puts "======================================================"
puts "BLOCK DESIGN CREATED SUCCESSFULLY"
puts "======================================================"

save_bd_design
close_project