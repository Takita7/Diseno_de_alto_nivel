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
puts "CREATING AXI FABRICS"
puts "======================================================"

# ------------------------------------------------------------
# CONTROL SMARTCONNECT
# PS master -> 3 AXI-Lite slaves
# ------------------------------------------------------------

create_bd_cell \
    -type ip \
    -vlnv xilinx.com:ip:smartconnect:* \
    smartconnect_control

set_property -dict [list \
    CONFIG.NUM_SI {1} \
    CONFIG.NUM_MI {3} \
] [get_bd_cells smartconnect_control]

# PS control master -> SmartConnect slave
connect_bd_intf_net \
    [get_bd_intf_pins zynq_ultra_ps_e_0/M_AXI_HPM0_FPD] \
    [get_bd_intf_pins smartconnect_control/S00_AXI]

# SmartConnect -> scheduler control
connect_bd_intf_net \
    [get_bd_intf_pins smartconnect_control/M00_AXI] \
    [get_bd_intf_pins gpgpu_scheduler_0/s_axi_control]

# SmartConnect -> scheduler secondary control bank
connect_bd_intf_net \
    [get_bd_intf_pins smartconnect_control/M01_AXI] \
    [get_bd_intf_pins gpgpu_scheduler_0/s_axi_control_r]

# SmartConnect -> memory pipeline control
connect_bd_intf_net \
    [get_bd_intf_pins smartconnect_control/M02_AXI] \
    [get_bd_intf_pins memory_pipeline_0/s_axi_control]

# ------------------------------------------------------------
# DDR SMARTCONNECT
# 4 PL masters -> one PS DDR slave
# ------------------------------------------------------------

create_bd_cell \
    -type ip \
    -vlnv xilinx.com:ip:smartconnect:* \
    smartconnect_ddr

set_property -dict [list \
    CONFIG.NUM_SI {4} \
    CONFIG.NUM_MI {1} \
] [get_bd_cells smartconnect_ddr]

connect_bd_intf_net \
    [get_bd_intf_pins gpgpu_scheduler_0/m_axi_gmem0] \
    [get_bd_intf_pins smartconnect_ddr/S00_AXI]

connect_bd_intf_net \
    [get_bd_intf_pins gpgpu_scheduler_0/m_axi_gmem1] \
    [get_bd_intf_pins smartconnect_ddr/S01_AXI]

connect_bd_intf_net \
    [get_bd_intf_pins gpgpu_scheduler_0/m_axi_gmem2] \
    [get_bd_intf_pins smartconnect_ddr/S02_AXI]

connect_bd_intf_net \
    [get_bd_intf_pins memory_pipeline_0/m_axi_gmem] \
    [get_bd_intf_pins smartconnect_ddr/S03_AXI]

connect_bd_intf_net \
    [get_bd_intf_pins smartconnect_ddr/M00_AXI] \
    [get_bd_intf_pins zynq_ultra_ps_e_0/S_AXI_HPC0_FPD]

# ------------------------------------------------------------
# CLOCKS
# ------------------------------------------------------------

connect_bd_net \
    [get_bd_pins zynq_ultra_ps_e_0/pl_clk0] \
    [get_bd_pins smartconnect_control/aclk] \
    [get_bd_pins smartconnect_ddr/aclk]

# HPM clock input
connect_bd_net \
    [get_bd_pins zynq_ultra_ps_e_0/pl_clk0] \
    [get_bd_pins zynq_ultra_ps_e_0/maxihpm0_fpd_aclk]

# HPC clock input
connect_bd_net \
    [get_bd_pins zynq_ultra_ps_e_0/pl_clk0] \
    [get_bd_pins zynq_ultra_ps_e_0/saxihpc0_fpd_aclk]

# ------------------------------------------------------------
# RESETS
# ------------------------------------------------------------

connect_bd_net \
    [get_bd_pins rst_pl_clk0/interconnect_aresetn] \
    [get_bd_pins smartconnect_control/aresetn] \
    [get_bd_pins smartconnect_ddr/aresetn]

save_bd_design

puts ""
puts "======================================================"
puts "AXI FABRICS CONNECTED"
puts "======================================================"

close_project