# CMakeLists.txt - RTL/FPGA flow smoke test (T020)
#
# Unlike tests/hls/ (csim against real Vitis HLS headers via plain g++), this
# drives the actual vitis_hls tool in batch mode - real C-synthesis, not a
# functional stand-in for it. Long-running (~1-2 min per kernel per board)
# and skips boards without installed device support, so it's a separate
# opt-in target rather than bundled into the default ctest run.

find_program(VITIS_HLS_EXECUTABLE vitis_hls)

if(BUILD_TESTS AND VITIS_HLS_EXECUTABLE)
    add_test(NAME fpga_flow_smoke_test
        COMMAND ${VITIS_HLS_EXECUTABLE} -f ${CMAKE_CURRENT_SOURCE_DIR}/test_flow.tcl
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
    # csynth for both kernels, per installed board - see test_flow.tcl's header.
    set_tests_properties(fpga_flow_smoke_test PROPERTIES
        TIMEOUT 1800
        LABELS "fpga;slow"
    )
else()
    message(STATUS "vitis_hls not found on PATH - skipping fpga_flow_smoke_test. source scripts/setup-env.sh to enable.")
endif()
