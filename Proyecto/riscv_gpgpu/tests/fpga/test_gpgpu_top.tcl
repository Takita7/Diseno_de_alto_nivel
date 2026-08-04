set script_dir [file dirname [file normalize [info script]]]
set repo_root  [file normalize "$script_dir/../.."]
set src_dir    "$repo_root/hls/src"
set build_dir  "$repo_root/build/gpgpu_top"

file mkdir $build_dir
cd $build_dir

open_project -reset gpgpu_scheduler

# Full top-level
add_files "$src_dir/scheduler/gpgpu_top.cpp" \
    -cflags "-I$src_dir -DRISCV_GPGPU_BOARD_KV260"

# gpgpu_top calls compute_pipeline(), whose implementation is in a
# separate .cpp file, so it must also be added to the HLS project.
add_files "$src_dir/compute_unit/compute_pipeline.cpp" \
    -cflags "-I$src_dir -DRISCV_GPGPU_BOARD_KV260"

set_top gpgpu_scheduler

open_solution -reset solution1

set_part xck26-sfvc784-2LV-c
create_clock -period 5.0 -name clk

csynth_design

close_project

puts "=================================================="
puts "PASS: gpgpu_scheduler csynth completed"
puts "=================================================="