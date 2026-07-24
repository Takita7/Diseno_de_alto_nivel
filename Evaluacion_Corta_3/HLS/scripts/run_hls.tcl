# =============================================================================
# run_hls.tcl
#
# Automates the Vitis HLS 2024.1 flow:
#   1. Create/reset the HLS project
#   2. Add the RGB-to-grayscale accelerator
#   3. Add the C++ testbench
#   4. Select the AMD Kria K26 target device
#   5. Configure a 250 MHz clock
#   6. Run C simulation
#   7. Run C synthesis
#   8. Run C/RTL co-simulation
#   9. Export the design as a Vivado IP
#
# Run this script from a Vitis HLS command prompt:
#
#   vitis_hls -f scripts/run_hls.tcl
#
# The script uses absolute paths derived from its own location, so it can be
# launched from any working directory.
# =============================================================================


# -----------------------------------------------------------------------------
# Locate the project directories
# -----------------------------------------------------------------------------

# Directory containing this Tcl script:
#   Evaluacion_3/HLS/scripts
set SCRIPT_DIR [file dirname [file normalize [info script]]]

# HLS root directory:
#   Evaluacion_3/HLS
set HLS_DIR [file dirname $SCRIPT_DIR]

set SOURCE_DIR [file join $HLS_DIR "src"]
set TB_DIR     [file join $HLS_DIR "tb"]
set DATA_DIR   [file join $HLS_DIR "data"]

# -----------------------------------------------------------------------------
# Source and testbench files
# -----------------------------------------------------------------------------

set DESIGN_SOURCE [file join $SOURCE_DIR "grayscale_accel.cpp"]
set DESIGN_HEADER [file join $SOURCE_DIR "grayscale_accel.h"]
set TESTBENCH     [file join $TB_DIR "grayscale_tb.cpp"]

set INPUT_RAW     [file join $DATA_DIR "input.raw"]

set CSIM_OUTPUT   [file join $DATA_DIR "output_hls_csim.raw"]
set COSIM_OUTPUT  [file join $DATA_DIR "output_hls_cosim.raw"]

# -----------------------------------------------------------------------------
# Project configuration
# -----------------------------------------------------------------------------

set PROJECT_DIR   [file join $HLS_DIR "grayscale_hls_project"]
set SOLUTION_NAME "solution1"
set TOP_FUNCTION  "grayscale_accel"

# AMD Kria K26 device used by the KV260 starter kit.
set TARGET_PART "xck26-sfvc784-2LV-c"

# 250 MHz corresponds to a 4 ns clock period.
set CLOCK_PERIOD_NS 4.0


# -----------------------------------------------------------------------------
# Validate required files before starting Vitis HLS
# -----------------------------------------------------------------------------

foreach required_file [list \
        $DESIGN_SOURCE \
        $DESIGN_HEADER \
        $TESTBENCH \
        $INPUT_RAW] {

    if {![file exists $required_file]} {
        puts stderr ""
        puts stderr "ERROR: Required file not found:"
        puts stderr "       $required_file"
        puts stderr ""
        exit 1
    }
}

# Check that input.raw has the expected size:
#
#   1920 x 1080 x 3 = 6,220,800 bytes
set EXPECTED_INPUT_SIZE 6220800
set ACTUAL_INPUT_SIZE [file size $INPUT_RAW]

if {$ACTUAL_INPUT_SIZE != $EXPECTED_INPUT_SIZE} {
    puts stderr ""
    puts stderr "ERROR: Invalid input.raw size."
    puts stderr "       Expected: $EXPECTED_INPUT_SIZE bytes"
    puts stderr "       Actual:   $ACTUAL_INPUT_SIZE bytes"
    puts stderr "       File:     $INPUT_RAW"
    puts stderr ""
    exit 1
}


# -----------------------------------------------------------------------------
# Display configuration
# -----------------------------------------------------------------------------

puts ""
puts "============================================================"
puts " Vitis HLS RGB-to-Grayscale Flow"
puts "============================================================"
puts "HLS directory : $HLS_DIR"
puts "Project       : $PROJECT_DIR"
puts "Solution      : $SOLUTION_NAME"
puts "Top function  : $TOP_FUNCTION"
puts "Target part   : $TARGET_PART"
puts "Clock period  : $CLOCK_PERIOD_NS ns"
puts "Input RAW     : $INPUT_RAW"
puts "Input size    : $ACTUAL_INPUT_SIZE bytes"
puts "============================================================"
puts ""


# -----------------------------------------------------------------------------
# Create/reset project
# -----------------------------------------------------------------------------

open_project -reset $PROJECT_DIR

set_top $TOP_FUNCTION


# -----------------------------------------------------------------------------
# Add synthesizable design source
#
# The -I option lets grayscale_accel.cpp find grayscale_accel.h.
# -----------------------------------------------------------------------------

add_files $DESIGN_SOURCE \
    -cflags "-I$SOURCE_DIR -std=c++17"


# -----------------------------------------------------------------------------
# Add testbench
#
# The testbench is used by both C simulation and C/RTL co-simulation.
# -----------------------------------------------------------------------------

add_files -tb $TESTBENCH \
    -cflags "-I$SOURCE_DIR -std=c++17"


# -----------------------------------------------------------------------------
# Create/reset solution
# -----------------------------------------------------------------------------

open_solution -reset $SOLUTION_NAME -flow_target vivado

set_part $TARGET_PART

create_clock \
    -period $CLOCK_PERIOD_NS \
    -name default


# -----------------------------------------------------------------------------
# Run C simulation
#
# grayscale_tb.cpp accepts:
#   argv[1] = input RAW path
#   argv[2] = output RAW path
# -----------------------------------------------------------------------------

puts ""
puts "============================================================"
puts " Running C simulation"
puts "============================================================"
puts ""

csim_design -clean


# -----------------------------------------------------------------------------
# Run C synthesis
# -----------------------------------------------------------------------------

puts ""
puts "============================================================"
puts " Running C synthesis"
puts "============================================================"
puts ""

csynth_design


# -----------------------------------------------------------------------------
# Run C/RTL co-simulation
#
# RTL is generated in Verilog. The testbench compares every output pixel
# against the software reference model.
# -----------------------------------------------------------------------------

puts ""
puts "============================================================"
puts " Running C/RTL co-simulation"
puts "============================================================"
puts ""

cosim_design -rtl verilog


# -----------------------------------------------------------------------------
# Export as a Vivado IP Catalog component
# -----------------------------------------------------------------------------

puts ""
puts "============================================================"
puts " Exporting Vivado IP"
puts "============================================================"
puts ""

config_export \
    -format ip_catalog \
    -rtl verilog

export_design


# -----------------------------------------------------------------------------
# Completion summary
# -----------------------------------------------------------------------------

puts ""
puts "============================================================"
puts " HLS flow completed successfully"
puts "============================================================"
puts "C simulation output:"
puts "  $CSIM_OUTPUT"
puts ""
puts "C/RTL co-simulation output:"
puts "  $COSIM_OUTPUT"
puts ""
puts "Synthesis report:"
puts "  [file join $PROJECT_DIR $SOLUTION_NAME syn report grayscale_accel_csynth.rpt]"
puts ""
puts "Exported IP:"
puts "  [file join $PROJECT_DIR $SOLUTION_NAME impl ip]"
puts "============================================================"
puts ""

exit