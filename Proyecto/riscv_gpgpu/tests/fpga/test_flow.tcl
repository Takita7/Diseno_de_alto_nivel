# test_flow.tcl - RTL/FPGA flow smoke test (T020)
#
# Confirms the HLS->RTL synthesis path actually works end to end for both
# real kernels (compute_pipeline, memory_pipeline) against every board whose
# Vivado/Vitis HLS device support is currently installed - the Phase 4
# Independent Test criterion ("A researcher can synthesize the HLS design
# and generate RTL artifacts for a representative configuration with known
# resource and timing estimates").
#
# Deliberately a smoke test, not a resource/timing regression check: it does
# NOT assert on LUT/DSP/BRAM counts or Fmax, because the L1/L2 WAYS/sizing
# are still open decisions (docs/hls/interfaces.md SS6) - a baseline to
# assert against doesn't exist yet. It only checks that csynth completes and
# produces a report. See T025 for the driver that will run against a
# settled configuration and track real numbers over time.
#
# Boards with no installed device support are SKIPPED, not failed (mirrors
# tests/hls/CMakeLists.txt's guarded-skip pattern for missing prerequisites)
# - detected empirically per board via a throwaway set_part probe, since
# `get_parts` alone was observed to hang/stall under batch/piped stdin in
# this environment. KV260 is the only board left in `$boards` - Alveo U55C
# was discarded (docs/hls/interfaces.md SS14); the per-board loop/skip
# structure is kept as-is rather than flattened, since it's what made
# validating that decision straightforward in the first place.
#
# Usage: vitis_hls -f tests/fpga/test_flow.tcl   (resolves its own location,
#        so it can be run from any working directory)

set script_dir [file dirname [file normalize [info script]]]
set repo_root  [file normalize "$script_dir/../.."]
set src_dir    "$repo_root/hls/src"
set build_dir  "$repo_root/build/fpga_smoke"

file mkdir $build_dir
# open_project rejects path separators in its name argument - it always
# creates the project relative to the current working directory. cd there
# so board/kernel names can stay bare (no risk of colliding with anything
# under $repo_root itself).
cd $build_dir

set boards {
    {kv260 RISCV_GPGPU_BOARD_KV260 xck26-sfvc784-2LV-c}
}

set kernels {
    {compute_pipeline compute_unit/compute_pipeline.cpp}
    {memory_pipeline  memory/memory_pipeline.cpp}
}

# Fast (~1s) probe: is $part installed, without the get_parts hang seen when
# piping this script via stdin/-f in this environment.
proc part_available {part} {
    file delete -force ".part_probe"
    open_project -reset ".part_probe"
    open_solution -reset solution1
    set failed [catch {set_part $part}]
    close_project
    file delete -force ".part_probe"
    return [expr {!$failed}]
}

set attempted 0
set failed_count 0
set skipped_boards {}

foreach board $boards {
    lassign $board board_name board_macro part

    if {![part_available $part]} {
        puts "SKIP: $board_name ($part) - device support not installed"
        lappend skipped_boards $board_name
        continue
    }

    foreach kernel $kernels {
        lassign $kernel top_fn rel_src
        incr attempted
        set src_file "$src_dir/$rel_src"
        set proj_dir "${board_name}_${top_fn}"
        file delete -force $proj_dir

        puts "=== csynth: $top_fn for $board_name ==="
        if {[catch {
            open_project -reset $proj_dir
            add_files $src_file -cflags "-I$src_dir -D$board_macro"
            # riscv_gpgpu_hls:: prefix required as of Vitis HLS 2026.1 -
            # unqualified set_top silently fails to resolve namespaced
            # functions there (see docs/hls/interfaces.md SS16.10). 2023.1
            # resolved the unqualified name fine, so this is a real
            # cross-version behavior difference, not a source bug.
            set_top "riscv_gpgpu_hls::$top_fn"
            open_solution -reset solution1
            source "$repo_root/hls/constraints/${board_name}.tcl"
            csynth_design
            close_project
        } err]} {
            puts "FAIL: $top_fn/$board_name - $err"
            incr failed_count
            continue
        }

        # The generic report name (not the qualified-name-derived one, e.g.
        # riscv_gpgpu_hls_compute_pipeline_csynth.rpt as of the 2026.1
        # set_top qualification above) - stable regardless of top-function
        # naming/qualification across tool versions.
        set rpt "$proj_dir/solution1/syn/report/csynth.rpt"
        if {![file exists $rpt]} {
            puts "FAIL: $top_fn/$board_name - csynth reported success but no report at $rpt"
            incr failed_count
        } else {
            puts "PASS: $top_fn/$board_name - report at $rpt"
        }
    }
}

puts "----------------------------------------------------------------------"
if {$attempted == 0} {
    puts "SKIP: no board device support installed ($boards) - nothing to synthesize"
    exit 0
} elseif {$failed_count > 0} {
    puts "FAILED: $failed_count/$attempted csynth run(s) did not complete cleanly"
    if {[llength $skipped_boards] > 0} {
        puts "(also skipped: $skipped_boards - device support not installed)"
    }
    exit 1
} else {
    puts "PASS: all $attempted csynth run(s) completed cleanly"
    if {[llength $skipped_boards] > 0} {
        puts "(skipped: $skipped_boards - device support not installed)"
    }
    exit 0
}
