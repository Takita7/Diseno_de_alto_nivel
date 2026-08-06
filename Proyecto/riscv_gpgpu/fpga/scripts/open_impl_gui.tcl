set script_dir [file dirname [file normalize [info script]]]
set repo_root  [file normalize "$script_dir/../.."]

set project_file \
    "$repo_root/build/vivado_kv260/riscv_gpgpu_kv260.xpr"

set util_rpt \
    "$repo_root/build/vivado_kv260/implementation_utilization.rpt"
set timing_rpt \
    "$repo_root/build/vivado_kv260/implementation_timing.rpt"
set drc_rpt \
    "$repo_root/build/vivado_kv260/implementation_drc.rpt"

if {![file exists $project_file]} {
    puts "ERROR: Vivado project not found:"
    puts "  $project_file"
    puts "Run the build first (build_all.tcl)."
    exit 1
}

open_project $project_file

set impl_status [get_property STATUS [get_runs impl_1]]
puts "impl_1 status: $impl_status"

if {[string match "*Complete*" $impl_status]} {
    open_run impl_1
    puts "Opened implemented design (impl_1)."
} else {
    puts "WARNING: impl_1 is not complete. Opening synthesized design instead."
    if {[string match "*Complete*" [get_property STATUS [get_runs synth_1]]]} {
        open_run synth_1
    }
}

# Generate interactive report views in GUI.
catch {report_utilization -name impl_util_gui}
catch {report_timing_summary -name impl_timing_gui -max_paths 20 -warn_on_violation}
catch {report_drc -name impl_drc_gui}

puts ""
puts "Project opened in GUI. Useful files:"
if {[file exists $util_rpt]} {
    puts "  $util_rpt"
}
if {[file exists $timing_rpt]} {
    puts "  $timing_rpt"
}
if {[file exists $drc_rpt]} {
    puts "  $drc_rpt"
}
puts ""
puts "Tips:"
puts "  - Open 'Implemented Design' for routing/timing inspection."
puts "  - Check Reports -> impl_util_gui / impl_timing_gui / impl_drc_gui."

# Keep Vivado GUI session open for manual inspection.
