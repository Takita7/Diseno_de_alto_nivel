# ============================================================================
# build_all.tcl
#
# Complete reproducible FPGA build flow for the RISC-V GPGPU
#
# Target platform : AMD Kria KV260
# FPGA/SOM device : AMD Kria K26
# Device family   : Zynq UltraScale+ MPSoC
# Vivado version  : 2024.1
#
# Usage from Proyecto/riscv_gpgpu:
#
#   vivado -mode batch -source fpga/scripts/build_all.tcl
#
# Prerequisites:
#
#   vitis_hls -f tests/fpga/export_gpgpu_ip.tcl
#   vitis_hls -f tests/fpga/export_memory_ip.tcl
#
# This script:
#
#   1. Checks exported HLS IPs
#   2. Creates a clean Vivado project
#   3. Instantiates the GPGPU HLS IPs
#   4. Connects scheduler <-> memory pipeline
#   5. Adds the Zynq UltraScale+ Processing System
#   6. Enables PS AXI interfaces
#   7. Connects clock/reset
#   8. Creates the ARM control AXI fabric
#   9. Creates the high-performance DDR AXI fabric
#  10. Assigns AXI addresses
#  11. Validates the block design
#  12. Creates the HDL wrapper
#  13. Runs synthesis
#  14. Runs implementation
#  15. Generates the bitstream
#  16. Generates utilization/timing/DRC reports
#
# ============================================================================


# ============================================================================
# Configuration
# ============================================================================

set script_dir [file dirname [file normalize [info script]]]
set repo_root  [file normalize "$script_dir/../.."]

set project_name "riscv_gpgpu_kv260"
set project_dir  "$repo_root/build/vivado_kv260"

# Kria K26 device used by the KV260 platform.
# Device family: Zynq UltraScale+ MPSoC
set part_name "xck26-sfvc784-2LV-c"

set bd_name "gpgpu_system"

set gpgpu_ip_repo \
    "$repo_root/build/ip_export/gpgpu_scheduler/gpgpu_scheduler/solution1/impl/ip"

set memory_ip_repo \
    "$repo_root/build/ip_export/memory_pipeline/memory_pipeline/solution1/impl/ip"

set gpgpu_component "$gpgpu_ip_repo/component.xml"
set memory_component "$memory_ip_repo/component.xml"


puts ""
puts "======================================================================"
puts " RISC-V GPGPU - ZYNQ ULTRASCALE+ MPSOC / KRIA KV260 BUILD"
puts "======================================================================"
puts "Repository      : $repo_root"
puts "Target platform : AMD Kria KV260"
puts "Device          : AMD Kria K26"
puts "Family          : Zynq UltraScale+ MPSoC"
puts "Part            : $part_name"
puts "Vivado          : [version -short]"
puts "======================================================================"
puts ""


# ============================================================================
# STEP 1 - Check exported HLS IPs
# ============================================================================

puts "---------------------------------------------------------------------"
puts "STEP 1: Checking exported HLS IPs"
puts "---------------------------------------------------------------------"

if {![file exists $gpgpu_component]} {
    puts "ERROR: gpgpu_scheduler IP was not found."
    puts ""
    puts "Expected:"
    puts "  $gpgpu_component"
    puts ""
    puts "Generate it first with:"
    puts "  vitis_hls -f tests/fpga/export_gpgpu_ip.tcl"
    exit 1
}

if {![file exists $memory_component]} {
    puts "ERROR: memory_pipeline IP was not found."
    puts ""
    puts "Expected:"
    puts "  $memory_component"
    puts ""
    puts "Generate it first with:"
    puts "  vitis_hls -f tests/fpga/export_memory_ip.tcl"
    exit 1
}

puts "PASS: gpgpu_scheduler IP found"
puts "PASS: memory_pipeline IP found"


# ============================================================================
# STEP 2 - Clean previous Vivado build
# ============================================================================

puts ""
puts "---------------------------------------------------------------------"
puts "STEP 2: Cleaning previous Vivado build"
puts "---------------------------------------------------------------------"

if {[file exists $project_dir]} {
    puts "Removing previous build:"
    puts "  $project_dir"
    file delete -force $project_dir
}

file mkdir $project_dir

puts "PASS: Clean build directory prepared"


# ============================================================================
# STEP 3 - Create Vivado project
# ============================================================================

puts ""
puts "---------------------------------------------------------------------"
puts "STEP 3: Creating Vivado project"
puts "---------------------------------------------------------------------"

create_project \
    $project_name \
    $project_dir \
    -part $part_name \
    -force

set_property ip_repo_paths \
    [list $gpgpu_ip_repo $memory_ip_repo] \
    [current_project]

update_ip_catalog

puts "PASS: Vivado project created"
puts "PASS: HLS IP repositories loaded"


# ============================================================================
# STEP 4 - Create block design and instantiate HLS IPs
# ============================================================================

puts ""
puts "---------------------------------------------------------------------"
puts "STEP 4: Creating GPGPU block design"
puts "---------------------------------------------------------------------"

create_bd_design $bd_name

create_bd_cell \
    -type ip \
    -vlnv riscv_gpgpu:hls:gpgpu_scheduler:1.0 \
    gpgpu_scheduler_0

create_bd_cell \
    -type ip \
    -vlnv riscv_gpgpu:hls:memory_pipeline:1.0 \
    memory_pipeline_0

puts "PASS: gpgpu_scheduler instantiated"
puts "PASS: memory_pipeline instantiated"


# ============================================================================
# STEP 5 - Connect scheduler <-> memory pipeline AXI streams
# ============================================================================

puts ""
puts "---------------------------------------------------------------------"
puts "STEP 5: Connecting GPGPU AXI-Stream datapath"
puts "---------------------------------------------------------------------"

connect_bd_intf_net \
    [get_bd_intf_pins gpgpu_scheduler_0/mem_req_out] \
    [get_bd_intf_pins memory_pipeline_0/req_in]

connect_bd_intf_net \
    [get_bd_intf_pins memory_pipeline_0/resp_out] \
    [get_bd_intf_pins gpgpu_scheduler_0/mem_resp_in]

puts "PASS: mem_req_out -> req_in"
puts "PASS: resp_out -> mem_resp_in"


# ============================================================================
# STEP 6 - Add Zynq UltraScale+ MPSoC Processing System
# ============================================================================

puts ""
puts "---------------------------------------------------------------------"
puts "STEP 6: Adding Zynq UltraScale+ Processing System"
puts "---------------------------------------------------------------------"

create_bd_cell \
    -type ip \
    -vlnv xilinx.com:ip:zynq_ultra_ps_e:* \
    zynq_ultra_ps_e_0

apply_bd_automation \
    -rule xilinx.com:bd_rule:zynq_ultra_ps_e \
    -config {apply_board_preset "0"} \
    [get_bd_cells zynq_ultra_ps_e_0]

set ps [get_bd_cells zynq_ultra_ps_e_0]

puts "PASS: Zynq UltraScale+ MPSoC Processing System instantiated"


# ============================================================================
# STEP 7 - Configure PS AXI interfaces
# ============================================================================

puts ""
puts "---------------------------------------------------------------------"
puts "STEP 7: Configuring PS AXI interfaces"
puts "---------------------------------------------------------------------"

# PS -> PL master.
# Used by the ARM Cortex-A53 to access accelerator control registers.
set_property -dict [list \
    CONFIG.PSU__USE__M_AXI_GP0 {1} \
] $ps

# PL -> PS high-performance slave.
# Used by accelerator AXI masters to access PS DDR.
set_property -dict [list \
    CONFIG.PSU__USE__S_AXI_GP0 {1} \
] $ps

# Disable unused LPD AXI master.
set_property -dict [list \
    CONFIG.PSU__USE__M_AXI_GP2 {0} \
] $ps

puts "Enabled : M_AXI_HPM0_FPD"
puts "Enabled : S_AXI_HPC0_FPD"
puts "Disabled: M_AXI_HPM0_LPD"


# ============================================================================
# STEP 8 - Clock and reset
# ============================================================================

puts ""
puts "---------------------------------------------------------------------"
puts "STEP 8: Connecting clock and reset"
puts "---------------------------------------------------------------------"

create_bd_cell \
    -type ip \
    -vlnv xilinx.com:ip:proc_sys_reset:* \
    rst_pl_clk0

# Accelerator and reset-controller clock.
connect_bd_net \
    [get_bd_pins zynq_ultra_ps_e_0/pl_clk0] \
    [get_bd_pins gpgpu_scheduler_0/ap_clk] \
    [get_bd_pins memory_pipeline_0/ap_clk] \
    [get_bd_pins rst_pl_clk0/slowest_sync_clk]

# PS reset.
connect_bd_net \
    [get_bd_pins zynq_ultra_ps_e_0/pl_resetn0] \
    [get_bd_pins rst_pl_clk0/ext_reset_in]

# Accelerator resets.
connect_bd_net \
    [get_bd_pins rst_pl_clk0/peripheral_aresetn] \
    [get_bd_pins gpgpu_scheduler_0/ap_rst_n] \
    [get_bd_pins memory_pipeline_0/ap_rst_n]

# PS AXI interface clocks.
connect_bd_net \
    [get_bd_pins zynq_ultra_ps_e_0/pl_clk0] \
    [get_bd_pins zynq_ultra_ps_e_0/maxihpm0_fpd_aclk]

connect_bd_net \
    [get_bd_pins zynq_ultra_ps_e_0/pl_clk0] \
    [get_bd_pins zynq_ultra_ps_e_0/saxihpc0_fpd_aclk]

puts "PASS: Clock/reset network connected"


# ============================================================================
# STEP 9 - ARM -> accelerator AXI control fabric
# ============================================================================

puts ""
puts "---------------------------------------------------------------------"
puts "STEP 9: Creating ARM control AXI fabric"
puts "---------------------------------------------------------------------"

create_bd_cell \
    -type ip \
    -vlnv xilinx.com:ip:smartconnect:* \
    smartconnect_control

set_property -dict [list \
    CONFIG.NUM_SI {1} \
    CONFIG.NUM_MI {3} \
] [get_bd_cells smartconnect_control]

# ARM/PS -> control SmartConnect.
connect_bd_intf_net \
    [get_bd_intf_pins zynq_ultra_ps_e_0/M_AXI_HPM0_FPD] \
    [get_bd_intf_pins smartconnect_control/S00_AXI]

# Scheduler primary control.
connect_bd_intf_net \
    [get_bd_intf_pins smartconnect_control/M00_AXI] \
    [get_bd_intf_pins gpgpu_scheduler_0/s_axi_control]

# Scheduler secondary control.
connect_bd_intf_net \
    [get_bd_intf_pins smartconnect_control/M01_AXI] \
    [get_bd_intf_pins gpgpu_scheduler_0/s_axi_control_r]

# Memory pipeline control.
connect_bd_intf_net \
    [get_bd_intf_pins smartconnect_control/M02_AXI] \
    [get_bd_intf_pins memory_pipeline_0/s_axi_control]

puts "PASS: ARM control fabric connected"


# ============================================================================
# STEP 10 - Accelerator -> DDR AXI fabric
# ============================================================================

puts ""
puts "---------------------------------------------------------------------"
puts "STEP 10: Creating high-performance DDR AXI fabric"
puts "---------------------------------------------------------------------"

create_bd_cell \
    -type ip \
    -vlnv xilinx.com:ip:smartconnect:* \
    smartconnect_ddr

set_property -dict [list \
    CONFIG.NUM_SI {4} \
    CONFIG.NUM_MI {1} \
] [get_bd_cells smartconnect_ddr]

# Program memory.
connect_bd_intf_net \
    [get_bd_intf_pins gpgpu_scheduler_0/m_axi_gmem0] \
    [get_bd_intf_pins smartconnect_ddr/S00_AXI]

# Initial register memory for CU0.
connect_bd_intf_net \
    [get_bd_intf_pins gpgpu_scheduler_0/m_axi_gmem1] \
    [get_bd_intf_pins smartconnect_ddr/S01_AXI]

# Initial register memory for CU1.
connect_bd_intf_net \
    [get_bd_intf_pins gpgpu_scheduler_0/m_axi_gmem2] \
    [get_bd_intf_pins smartconnect_ddr/S02_AXI]

# Runtime GPGPU load/store path.
connect_bd_intf_net \
    [get_bd_intf_pins memory_pipeline_0/m_axi_gmem] \
    [get_bd_intf_pins smartconnect_ddr/S03_AXI]

# High-performance connection into PS DDR.
connect_bd_intf_net \
    [get_bd_intf_pins smartconnect_ddr/M00_AXI] \
    [get_bd_intf_pins zynq_ultra_ps_e_0/S_AXI_HPC0_FPD]

puts "PASS: DDR fabric connected"


# ============================================================================
# STEP 11 - SmartConnect clocks and resets
# ============================================================================

puts ""
puts "---------------------------------------------------------------------"
puts "STEP 11: Connecting AXI fabric clock/reset"
puts "---------------------------------------------------------------------"

connect_bd_net \
    [get_bd_pins zynq_ultra_ps_e_0/pl_clk0] \
    [get_bd_pins smartconnect_control/aclk] \
    [get_bd_pins smartconnect_ddr/aclk]

connect_bd_net \
    [get_bd_pins rst_pl_clk0/interconnect_aresetn] \
    [get_bd_pins smartconnect_control/aresetn] \
    [get_bd_pins smartconnect_ddr/aresetn]

puts "PASS: AXI fabric clock/reset connected"


# ============================================================================
# STEP 12 - Keep HLS wrapper active
# ============================================================================

puts ""
puts "---------------------------------------------------------------------"
puts "STEP 12: Enabling GPGPU HLS wrapper"
puts "---------------------------------------------------------------------"

create_bd_cell \
    -type ip \
    -vlnv xilinx.com:ip:xlconstant:* \
    const_ap_start

set_property -dict [list \
    CONFIG.CONST_WIDTH {1} \
    CONFIG.CONST_VAL   {1} \
] [get_bd_cells const_ap_start]

connect_bd_net \
    [get_bd_pins const_ap_start/dout] \
    [get_bd_pins gpgpu_scheduler_0/ap_start]

puts "PASS: gpgpu_scheduler ap_start tied HIGH"


# ============================================================================
# STEP 13 - Assign AXI addresses
# ============================================================================

puts ""
puts "---------------------------------------------------------------------"
puts "STEP 13: Assigning AXI addresses"
puts "---------------------------------------------------------------------"

assign_bd_address
save_bd_design

puts ""
puts "Expected control address map:"
puts "  0xA0000000 : gpgpu_scheduler / s_axi_control"
puts "  0xA0010000 : gpgpu_scheduler / s_axi_control_r"
puts "  0xA0020000 : memory_pipeline / s_axi_control"
puts ""
puts "Accelerator DDR window:"
puts "  0x00000000 - 0x7FFFFFFF"


# ============================================================================
# STEP 14 - Validate block design
# ============================================================================

puts ""
puts "---------------------------------------------------------------------"
puts "STEP 14: Validating complete block design"
puts "---------------------------------------------------------------------"

if {[catch {validate_bd_design} validation_error]} {
    puts ""
    puts "ERROR: Block design validation failed."
    puts $validation_error
    exit 1
}

save_bd_design

puts "PASS: Block design validation completed"


# ============================================================================
# STEP 15 - Generate block-design products
# ============================================================================

puts ""
puts "---------------------------------------------------------------------"
puts "STEP 15: Generating block-design output products"
puts "---------------------------------------------------------------------"

set bd_file \
    "$project_dir/${project_name}.srcs/sources_1/bd/${bd_name}/${bd_name}.bd"

generate_target all [get_files $bd_file]

puts "PASS: Block-design output products generated"


# ============================================================================
# STEP 16 - Create HDL wrapper
# ============================================================================

puts ""
puts "---------------------------------------------------------------------"
puts "STEP 16: Creating HDL wrapper"
puts "---------------------------------------------------------------------"

set wrapper_file [make_wrapper \
    -files [get_files $bd_file] \
    -top]

add_files -norecurse $wrapper_file

set_property top ${bd_name}_wrapper [current_fileset]

update_compile_order -fileset sources_1

puts "Top module: [get_property top [current_fileset]]"
puts "PASS: HDL wrapper created"


# ============================================================================
# STEP 17 - Vivado synthesis
# ============================================================================

puts ""
puts "======================================================================"
puts " STEP 17: VIVADO SYNTHESIS"
puts "======================================================================"

reset_run synth_1

launch_runs synth_1 -jobs 8
wait_on_run synth_1

set synth_status [get_property STATUS [get_runs synth_1]]

puts ""
puts "Synthesis status:"
puts "  $synth_status"

if {![string match "*Complete*" $synth_status]} {
    puts ""
    puts "ERROR: Vivado synthesis failed."
    exit 1
}

open_run synth_1

report_utilization \
    -file "$project_dir/synthesis_utilization.rpt"

report_timing_summary \
    -file "$project_dir/synthesis_timing.rpt"

close_design

puts "PASS: Vivado synthesis completed"


# ============================================================================
# STEP 18 - Implementation + bitstream
# ============================================================================

puts ""
puts "======================================================================"
puts " STEP 18: IMPLEMENTATION + BITSTREAM"
puts "======================================================================"

reset_run impl_1

launch_runs impl_1 \
    -to_step write_bitstream \
    -jobs 8

wait_on_run impl_1

set impl_status [get_property STATUS [get_runs impl_1]]

puts ""
puts "Implementation status:"
puts "  $impl_status"

if {![string match "*Complete*" $impl_status]} {
    puts ""
    puts "ERROR: Implementation or bitstream generation failed."
    exit 1
}

open_run impl_1

puts "PASS: Implementation and bitstream generation completed"


# ============================================================================
# STEP 19 - Generate final reports
# ============================================================================

puts ""
puts "---------------------------------------------------------------------"
puts "STEP 19: Generating implementation reports"
puts "---------------------------------------------------------------------"

report_utilization \
    -file "$project_dir/implementation_utilization.rpt"

report_timing_summary \
    -file "$project_dir/implementation_timing.rpt"

report_drc \
    -file "$project_dir/implementation_drc.rpt"

puts "PASS: Final reports generated"


# ============================================================================
# STEP 20 - Locate final bitstream
# ============================================================================

puts ""
puts "---------------------------------------------------------------------"
puts "STEP 20: Locating final bitstream"
puts "---------------------------------------------------------------------"

set bitfiles [glob -nocomplain \
    "$project_dir/${project_name}.runs/impl_1/*.bit"]

if {[llength $bitfiles] == 0} {
    puts ""
    puts "ERROR: No bitstream was found."
    exit 1
}

set final_bitstream [lindex $bitfiles 0]

puts "BITSTREAM:"
puts "  $final_bitstream"


# ============================================================================
# STEP 21 - Check final timing
# ============================================================================

puts ""
puts "---------------------------------------------------------------------"
puts "STEP 21: Checking final timing"
puts "---------------------------------------------------------------------"

set timing_paths [get_timing_paths \
    -delay_type max \
    -max_paths 1 \
    -nworst 1]

if {[llength $timing_paths] > 0} {

    set wns [get_property SLACK [lindex $timing_paths 0]]

    puts "Worst setup slack:"
    puts "  WNS = $wns ns"

    if {$wns < 0} {
        puts "WARNING: Bitstream generated, but setup timing is violated."
    } else {
        puts "PASS: Setup timing met."
    }
}


# ============================================================================
# Final summary
# ============================================================================

puts ""
puts "======================================================================"
puts " COMPLETE BUILD SUMMARY"
puts "======================================================================"
puts ""
puts "Platform:"
puts "  AMD Kria KV260"
puts ""
puts "Device:"
puts "  AMD Kria K26"
puts ""
puts "FPGA/SoC family:"
puts "  Zynq UltraScale+ MPSoC"
puts ""
puts "Vivado part:"
puts "  $part_name"
puts ""
puts "Custom HLS IP:"
puts "  gpgpu_scheduler"
puts "  memory_pipeline"
puts ""
puts "Control address map:"
puts "  0xA0000000 : gpgpu_scheduler / s_axi_control"
puts "  0xA0010000 : gpgpu_scheduler / s_axi_control_r"
puts "  0xA0020000 : memory_pipeline / s_axi_control"
puts ""
puts "DDR address window:"
puts "  0x00000000 - 0x7FFFFFFF"
puts ""
puts "Bitstream:"
puts "  $final_bitstream"
puts ""
puts "Reports:"
puts "  $project_dir/implementation_utilization.rpt"
puts "  $project_dir/implementation_timing.rpt"
puts "  $project_dir/implementation_drc.rpt"
puts ""
puts "======================================================================"
puts " PASS: COMPLETE ZYNQ ULTRASCALE+ MPSOC / KV260 FPGA BUILD"
puts "======================================================================"
puts ""

close_project