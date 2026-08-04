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
puts "VALIDATING BLOCK DESIGN"
puts "======================================================"

validate_bd_design
save_bd_design

puts ""
puts "======================================================"
puts "GENERATING BLOCK DESIGN OUTPUT PRODUCTS"
puts "======================================================"

generate_target all [get_files $bd_file]

puts ""
puts "======================================================"
puts "CREATING HDL WRAPPER"
puts "======================================================"

set wrapper_file [make_wrapper \
    -files [get_files $bd_file] \
    -top]

add_files -norecurse $wrapper_file

set_property top gpgpu_system_wrapper [current_fileset]
update_compile_order -fileset sources_1

puts ""
puts "Top module:"
puts [get_property top [current_fileset]]

puts ""
puts "======================================================"
puts "STARTING VIVADO SYNTHESIS"
puts "======================================================"

reset_run synth_1
launch_runs synth_1 -jobs 8
wait_on_run synth_1

set synth_status [get_property STATUS [get_runs synth_1]]

puts ""
puts "======================================================"
puts "SYNTHESIS STATUS"
puts "======================================================"
puts $synth_status

if {[string match "*Complete*" $synth_status]} {

    open_run synth_1

    puts ""
    puts "======================================================"
    puts "RESOURCE UTILIZATION"
    puts "======================================================"

    report_utilization \
        -file "$repo_root/build/vivado_kv260/synthesis_utilization.rpt"

    puts ""
    puts "======================================================"
    puts "TIMING SUMMARY"
    puts "======================================================"

    report_timing_summary \
        -file "$repo_root/build/vivado_kv260/synthesis_timing.rpt"

    puts ""
    puts "PASS: Vivado synthesis completed."

} else {

    puts ""
    puts "ERROR: Vivado synthesis did not complete successfully."
    exit 1
}

close_project