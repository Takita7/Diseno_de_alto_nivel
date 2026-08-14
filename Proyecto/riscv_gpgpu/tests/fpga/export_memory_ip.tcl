set script_dir [file dirname [file normalize [info script]]]
set repo_root  [file normalize "$script_dir/../.."]
set src_dir    "$repo_root/hls/src"
set build_dir  "$repo_root/build/ip_export/memory_pipeline"

file mkdir $build_dir
cd $build_dir

open_project -reset memory_pipeline

add_files "$src_dir/memory/memory_pipeline.cpp" \
    -cflags "-I$src_dir -DRISCV_GPGPU_BOARD_KV260"

set_top riscv_gpgpu_hls::memory_pipeline

open_solution -reset solution1

set_part xck26-sfvc784-2LV-c
create_clock -period 5.0 -name clk

csynth_design

export_design \
    -format ip_catalog \
    -rtl verilog \
    -vendor riscv_gpgpu \
    -library hls \
    -version 1.0 \
    -display_name "RISC-V GPGPU Memory Pipeline"

close_project

puts "=============================================="
puts "PASS: memory_pipeline IP exported"
puts "=============================================="