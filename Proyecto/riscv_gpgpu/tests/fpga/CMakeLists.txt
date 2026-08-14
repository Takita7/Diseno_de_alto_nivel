# CMakeLists.txt - RTL/FPGA flow smoke test (T020) + FpgaDriver unit tests (T051)
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

# ── FpgaDriver + ELF loader unit tests (T051/T052) ───────────────────────────
# Builds on any platform: on x86 it uses file-backed fake windows; on aarch64
# (cross-build with -DFPGA_TARGET=ON) the same binary runs against real hardware.
if(BUILD_TESTS)
    find_package(GTest QUIET)
    if(GTest_FOUND)
        add_executable(test_fpga_driver test_fpga_driver.cpp)
        target_include_directories(test_fpga_driver PRIVATE
            ${CMAKE_SOURCE_DIR}/driver/src
        )
        target_link_libraries(test_fpga_driver PRIVATE
            driver_lib
            GTest::GTest GTest::Main
        )
        if(FPGA_TARGET)
            target_compile_definitions(test_fpga_driver PRIVATE FPGA_TARGET)
        endif()
        add_test(NAME fpga_driver_tests COMMAND test_fpga_driver)
        install(TARGETS test_fpga_driver RUNTIME DESTINATION bin)
    else()
        message(STATUS "GTest not found - skipping test_fpga_driver. Install libgtest-dev to enable.")
    endif()

    # Standalone smoke test -- no GTest, no FpgaDriver, raw POSIX /dev/mem.
    add_executable(fpga_smoke_test fpga_smoke_test.cpp)
    if(FPGA_TARGET)
        target_compile_definitions(fpga_smoke_test PRIVATE FPGA_TARGET)
    endif()
    install(TARGETS fpga_smoke_test RUNTIME DESTINATION bin)

    # FPGA Rodinia benchmark -- no GTest, raw POSIX /dev/mem + ELF loader.
    # Loads RISC-V kernel ELF (built with FPGA_LINK) into DDR and times it.
    add_executable(fpga_rodinia_bench fpga_rodinia_bench.cpp)
    target_compile_options(fpga_rodinia_bench PRIVATE -std=c++17)
    # Static link: board GLIBC (2.35) is older than the cross-compiler's headers.
    target_link_options(fpga_rodinia_bench PRIVATE -static)
    target_link_libraries(fpga_rodinia_bench PRIVATE pthread)
    if(FPGA_TARGET)
        target_compile_definitions(fpga_rodinia_bench PRIVATE FPGA_TARGET)
    endif()
    install(TARGETS fpga_rodinia_bench RUNTIME DESTINATION bin)
endif()
