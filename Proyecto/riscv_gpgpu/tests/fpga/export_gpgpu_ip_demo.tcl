set script_dir [file dirname [file normalize [info script]]]
set repo_root  [file normalize "$script_dir/../.."]
set src_dir    "$repo_root/hls/src"
set build_dir  "$repo_root/build/ip_export_demo/gpgpu_scheduler"

file mkdir $build_dir
cd $build_dir

open_project -reset gpgpu_scheduler_demo

add_files "$src_dir/scheduler/gpgpu_top.cpp" \
    -cflags "-I$src_dir -DRISCV_GPGPU_BOARD_KV260 -DRISCV_GPGPU_SHARED_MEM_SIZE_BYTES=16384 -DRISCV_GPGPU_MAX_WARPS_PER_CU=4"

add_files "$src_dir/compute_unit/compute_pipeline.cpp" \
    -cflags "-I$src_dir -DRISCV_GPGPU_BOARD_KV260 -DRISCV_GPGPU_SHARED_MEM_SIZE_BYTES=16384 -DRISCV_GPGPU_MAX_WARPS_PER_CU=4"

set_top riscv_gpgpu_hls::gpgpu_scheduler

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
    -display_name "RISC-V GPGPU Scheduler (Demo)"

close_project

puts "=============================================="
puts "PASS: gpgpu_scheduler_demo IP exported"
puts "=============================================="
