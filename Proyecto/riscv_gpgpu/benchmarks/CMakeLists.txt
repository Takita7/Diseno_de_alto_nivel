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

# Optional external Rodinia checkout.
#
# When RODINIA_ROOT is set, the build can selectively compile compatible
# kernels from a local Rodinia tree instead of relying only on the synthetic
# Rodinia-style workloads shipped in this repository. Individual kernel source
# overrides can be supplied with RODINIA_<KERNEL>_SOURCE if the checkout uses a
# different layout.
# Declare cache variables first so -DRODINIA_ROOT=... on the cmake command line
# always takes precedence. The auto-detect below only fires when the cache is
# empty (i.e. the user has not set it explicitly).
#
# Recommended layout (ignored by git, cloned once by hand):
#   <project_root>/external/rodinia
# Clone command:
#   git clone --depth 1 https://github.com/yuhc/gpu-rodinia.git external/rodinia
set(RODINIA_ROOT "" CACHE PATH "Optional path to a local Rodinia checkout")
set(RODINIA_KERNELS "bfs;hotspot;needle;gaussian;lavamd" CACHE STRING
    "Semicolon-separated Rodinia kernels to build when RODINIA_ROOT is set")

if(NOT RODINIA_ROOT)
    set(_RODINIA_DEFAULT_PATH "${CMAKE_SOURCE_DIR}/external/rodinia")
    if(EXISTS "${_RODINIA_DEFAULT_PATH}/cuda/bfs/kernel.cu")
        set(RODINIA_ROOT "${_RODINIA_DEFAULT_PATH}")
        message(STATUS "Benchmarks: auto-detected Rodinia at ${RODINIA_ROOT}")
    endif()
endif()

set(RODINIA_KERNELS "bfs;hotspot;needle;gaussian;lavamd" CACHE STRING
    "Semicolon-separated Rodinia kernels to build when RODINIA_ROOT is set")

set(RODINIA_BFS_SOURCE      "" CACHE FILEPATH "Override Rodinia BFS source path")
set(RODINIA_HOTSPOT_SOURCE   "" CACHE FILEPATH "Override Rodinia Hotspot source path")
set(RODINIA_NEEDLE_SOURCE    "" CACHE FILEPATH "Override Rodinia Needle source path")
set(RODINIA_GAUSSIAN_SOURCE  "" CACHE FILEPATH "Override Rodinia Gaussian source path")
set(RODINIA_LAVAMD_SOURCE    "" CACHE FILEPATH "Override Rodinia LavaMD source path")

function(add_rodinia_kernel_if_present kernel_name source_path)
    if(EXISTS "${source_path}")
        add_riscv_kernel(${kernel_name}
            SOURCE "${source_path}"
            ENTRY ${kernel_name}
        )
        message(STATUS "Benchmarks: Rodinia kernel enabled: ${kernel_name} -> ${source_path}")
    else()
        message(WARNING
            "Benchmarks: Rodinia kernel '${kernel_name}' skipped because source was not found: ${source_path}")
    endif()
endfunction()

add_subdirectory(workloads/vector_add)
add_subdirectory(workloads/saxpy)

if(RODINIA_ROOT)
    message(STATUS "Benchmarks: selective Rodinia integration enabled at ${RODINIA_ROOT}")

    set(RODINIA_CUDA_COMPAT_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/rodinia_cuda_compat.h")
    set(RODINIA_KERNEL_WRAPPER_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/rodinia_kernel_wrapper.cpp.in")
    set(RODINIA_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated/rodinia")
    file(MAKE_DIRECTORY "${RODINIA_GENERATED_DIR}")

    function(configure_rodinia_kernel_wrapper wrapper_name kernel_source extra_defines)
        set(RODINIA_COMPAT_HEADER "${RODINIA_CUDA_COMPAT_HEADER}")
        set(RODINIA_KERNEL_SOURCE "${kernel_source}")
        set(RODINIA_EXTRA_DEFINES "${extra_defines}")
        configure_file(
            "${RODINIA_KERNEL_WRAPPER_TEMPLATE}"
            "${RODINIA_GENERATED_DIR}/${wrapper_name}.cpp"
            @ONLY
        )
    endfunction()

    foreach(kernel_name IN LISTS RODINIA_KERNELS)
        string(TOLOWER "${kernel_name}" kernel_key)
        if(kernel_key STREQUAL "bfs")
            set(RODINIA_BFS_KERNEL_SOURCE  "${RODINIA_ROOT}/cuda/bfs/kernel.cu")
            set(RODINIA_BFS_KERNEL2_SOURCE "${RODINIA_ROOT}/cuda/bfs/kernel2.cu")
            if(EXISTS "${RODINIA_BFS_KERNEL_SOURCE}" AND EXISTS "${RODINIA_BFS_KERNEL2_SOURCE}")
                configure_rodinia_kernel_wrapper(
                    "rodinia_bfs_kernel_wrapper"
                    "${RODINIA_BFS_KERNEL_SOURCE}"
                    "struct Node { int starting; int no_of_edges; };\n#define MAX_THREADS_PER_BLOCK 512"
                )
                add_riscv_kernel(rodinia_bfs_kernel
                    SOURCE "${RODINIA_GENERATED_DIR}/rodinia_bfs_kernel_wrapper.cpp"
                    ENTRY Kernel
                )

                configure_rodinia_kernel_wrapper(
                    "rodinia_bfs_kernel2_wrapper"
                    "${RODINIA_BFS_KERNEL2_SOURCE}"
                    "#define MAX_THREADS_PER_BLOCK 512"
                )
                add_riscv_kernel(rodinia_bfs_kernel2
                    SOURCE "${RODINIA_GENERATED_DIR}/rodinia_bfs_kernel2_wrapper.cpp"
                    ENTRY Kernel2
                )

                set(RODINIA_REAL_BFS_AVAILABLE TRUE)
                message(STATUS "Benchmarks: Rodinia BFS integrated from ${RODINIA_ROOT}")
            else()
                message(WARNING
                    "Benchmarks: Rodinia BFS skipped because kernel.cu/kernel2.cu were not found under ${RODINIA_ROOT}")
            endif()
        elseif(kernel_key STREQUAL "hotspot")
            message(STATUS "Benchmarks: Rodinia Hotspot not yet wired for real upstream integration; keeping synthetic benchmark")
        elseif(kernel_key STREQUAL "needle")
            message(STATUS "Benchmarks: Rodinia Needle not yet wired for real upstream integration; keeping synthetic benchmark")
        elseif(kernel_key STREQUAL "gaussian")
            set(RODINIA_GAUSSIAN_SOURCE "${CMAKE_CURRENT_LIST_DIR}/rodinia_gaussian_kernels.cpp")
            if(EXISTS "${RODINIA_GAUSSIAN_SOURCE}")
                add_riscv_kernel(rodinia_gaussian_fan1
                    SOURCE "${RODINIA_GAUSSIAN_SOURCE}"
                    ENTRY Fan1
                )
                add_riscv_kernel(rodinia_gaussian_fan2
                    SOURCE "${RODINIA_GAUSSIAN_SOURCE}"
                    ENTRY Fan2
                )
                set(RODINIA_REAL_GAUSSIAN_AVAILABLE TRUE)
                message(STATUS "Benchmarks: Rodinia Gaussian integrated from ${RODINIA_GAUSSIAN_SOURCE}")
            else()
                message(WARNING
                    "Benchmarks: Rodinia Gaussian skipped because source was not found: ${RODINIA_GAUSSIAN_SOURCE}")
            endif()
        elseif(kernel_key STREQUAL "lavamd")
            message(STATUS "Benchmarks: Rodinia LavaMD not yet wired for real upstream integration; keeping synthetic benchmark")
        else()
            message(WARNING "Benchmarks: unknown Rodinia kernel '${kernel_name}' requested in RODINIA_KERNELS")
        endif()
    endforeach()
else()
    message(STATUS "Benchmarks: Rodinia integration disabled; using local Rodinia-style workloads only")
endif()

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

    if(RODINIA_REAL_BFS_AVAILABLE)
        add_gpgpu_benchmark(rodinia_real_benchmark rodinia_real_benchmark.cpp)
        add_dependencies(rodinia_real_benchmark rodinia_bfs_kernel_kernel rodinia_bfs_kernel2_kernel)
        target_compile_definitions(rodinia_real_benchmark PRIVATE
            RODINIA_BFS_KERNEL_ELF="${rodinia_bfs_kernel_ELF}"
            RODINIA_BFS_KERNEL2_ELF="${rodinia_bfs_kernel2_ELF}"
        )
        message(STATUS "Benchmarks: rodinia_real_benchmark configured (upstream BFS Kernel + Kernel2)")
    endif()

    if(RODINIA_REAL_GAUSSIAN_AVAILABLE)
        if(NOT TARGET rodinia_real_benchmark)
            add_gpgpu_benchmark(rodinia_real_benchmark rodinia_real_benchmark.cpp)
        endif()
        add_dependencies(rodinia_real_benchmark rodinia_gaussian_fan1_kernel rodinia_gaussian_fan2_kernel)
        target_compile_definitions(rodinia_real_benchmark PRIVATE
            RODINIA_GAUSSIAN_FAN1_ELF="${rodinia_gaussian_fan1_ELF}"
            RODINIA_GAUSSIAN_FAN2_ELF="${rodinia_gaussian_fan2_ELF}"
        )
        message(STATUS "Benchmarks: rodinia_real_benchmark configured with upstream Gaussian Fan1 + Fan2")
    endif()

else()
    message(STATUS "Benchmarks: benchmark harnesses skipped (requires BUILD_SYSTEMC_INTEGRATION + GTest + SystemC)")
endif()
