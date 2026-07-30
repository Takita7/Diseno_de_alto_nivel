# CMakeLists.txt – Benchmarks
#
# Registers all RISC-V GPGPU workloads via the RiscvKernel.cmake helper.
# Each workload subdirectory calls add_riscv_kernel() which:
#   - Compiles source to ${CMAKE_BINARY_DIR}/kernels/<name>.elf
#   - Creates target <name>_kernel
#   - Appends to the meta-target compile_kernels
#
# Build all kernels at once:
#   cmake --build <build_dir> --target compile_kernels

include(RiscvKernel)

message(STATUS "Benchmarks: registering RISC-V GPGPU workloads")

add_subdirectory(workloads/vector_add)
add_subdirectory(workloads/saxpy)

# ── PTX benchmark harness (Phases 5d + 5e + 5f) ──────────────────────────────
# Requires: BUILD_SYSTEMC_INTEGRATION (KernelBridge) + GTest
# Exercises full stack: PTX → ELF → KernelBridge → ComputeUnit(RV32F) → verify
if(BUILD_TESTS AND BUILD_SYSTEMC_INTEGRATION AND GTest_FOUND AND SystemC_FOUND)
    # Helper macro to add a benchmark executable linked against the full stack
    macro(add_gpgpu_benchmark target_name src_file)
        add_executable(${target_name}
            ${src_file}
            ${CMAKE_SOURCE_DIR}/tests/systemc/sc_gtest_main.cpp
        )
        target_link_libraries(${target_name}
            PRIVATE
            GTest::GTest
            systemc_integration
            compute_unit
            memory_hierarchy
            driver_lib
            host_api_lib
            ptx_transpiler_lib
            runtime_lib
            SystemC::SystemC
        )
        target_include_directories(${target_name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/models/systemc
            ${CMAKE_SOURCE_DIR}/models/systemc/src
            ${CMAKE_SOURCE_DIR}/models/systemc/integration
            ${CMAKE_SOURCE_DIR}/driver/src
            ${CMAKE_SOURCE_DIR}/driver/src/ptx_transpiler
            ${CMAKE_SOURCE_DIR}/software/host_api
            ${CMAKE_SOURCE_DIR}/runtime/src
        )
        set_target_properties(${target_name} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        )
        add_test(NAME ${target_name} COMMAND ${target_name})
    endmacro()

    # PTX full-stack benchmark (Phases 5d + 5e + 5f)
    add_gpgpu_benchmark(ptx_kernels_benchmark ptx_kernels_benchmark.cpp)
    message(STATUS "Benchmarks: ptx_kernels_benchmark configured (Phases 5d+5e+5f)")

    # Rodinia-style benchmarks (matmul naive + hotspot 1D)
    add_gpgpu_benchmark(rodinia_benchmark rodinia_benchmark.cpp)
    message(STATUS "Benchmarks: rodinia_benchmark configured (matmul_naive + hotspot1d)")

else()
    message(STATUS "Benchmarks: benchmark harnesses skipped (requires BUILD_SYSTEMC_INTEGRATION + GTest + SystemC)")
endif()
