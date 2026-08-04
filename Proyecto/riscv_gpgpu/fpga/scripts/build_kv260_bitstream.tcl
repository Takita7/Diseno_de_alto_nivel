set script_dir [file dirname [file normalize [info script]]]
set repo_root  [file normalize "$script_dir/../.."]

set project_file \
    "$repo_root/build/vivado_kv260/riscv_gpgpu_kv260.xpr"

open_project $project_file

puts ""
puts "======================================================"
puts "CHECKING SYNTHESIS"
puts "======================================================"

set synth_status [get_property STATUS [get_runs synth_1]]
puts "synth_1 status: $synth_status"

if {![string match "*Complete*" $synth_status]} {
    puts "ERROR: synthesis is not complete."
    exit 1
}

puts ""
puts "======================================================"
puts "STARTING IMPLEMENTATION"
puts "======================================================"

reset_run impl_1

launch_runs impl_1 \
    -to_step write_bitstream \
    -jobs 8

wait_on_run impl_1

set impl_status [get_property STATUS [get_runs impl_1]]

puts ""
puts "======================================================"
puts "IMPLEMENTATION STATUS"
puts "======================================================"

puts $impl_status

if {![string match "*Complete*" $impl_status]} {
    puts ""
    puts "ERROR: Implementation/bitstream generation failed."
    exit 1
}

open_run impl_1

puts ""
puts "======================================================"
puts "POST-IMPLEMENTATION UTILIZATION"
puts "======================================================"

report_utilization \
    -file "$repo_root/build/vivado_kv260/implementation_utilization.rpt"

puts ""
puts "======================================================"
puts "POST-IMPLEMENTATION TIMING"
puts "======================================================"

report_timing_summary \
    -file "$repo_root/build/vivado_kv260/implementation_timing.rpt"

puts ""
puts "======================================================"
puts "DRC REPORT"
puts "======================================================"

report_drc \
    -file "$repo_root/build/vivado_kv260/implementation_drc.rpt"

puts ""
puts "======================================================"
puts "LOCATING BITSTREAM"
puts "======================================================"

set bitfiles [glob -nocomplain \
    "$repo_root/build/vivado_kv260/*.runs/impl_1/*.bit"]

foreach bit $bitfiles {
    puts "BITSTREAM: $bit"
}

if {[llength $bitfiles] == 0} {
    puts "ERROR: no .bit file found."
    exit 1
}

puts ""
puts "======================================================"
puts "PASS: IMPLEMENTATION + BITSTREAM COMPLETED"
puts "======================================================"

close_project