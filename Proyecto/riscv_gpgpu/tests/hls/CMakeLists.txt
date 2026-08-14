# CMakeLists.txt - HLS regression tests (T019)
#
# Four targets, all needing only Vitis HLS's ap_int.h/hls_stream.h headers
# (csim-compatible with plain g++), not the tool itself - they build now that
# T022 and T023 both exist:
#   test_hls_data_structures  - common/hls_config.h, common/hls_types.h,
#     simt_controller/divergence_stack.h, memory/cache_bank.h (the
#     data-structure layer preceding T022/T023 proper).
#   test_compute_pipeline     - T022 against the golden kernel_programs.h
#     kernels (register parity, barrier stall/resume, threaded memory stand-in).
#   test_memory_pipeline      - T023 against golden MemoryHierarchy semantics
#     (shared-mem bypass, L1->L2->m_axi miss/fill chain, L2-hit-refills-L1,
#     write-through/no-write-allocate, m_axi transaction counts).
#   test_pipeline_integration - T022 and T023 wired together for real (no
#     mocks on either side), the capstone check that their stream contracts
#     actually agree in practice.
#
# (This file previously reserved a test_hls_pipeline target, guarded on both
# T022/T023 existing, as a GTEST_SKIP()-stubbed placeholder for exactly what
# the four targets above now cover for real - retired once superseded rather
# than left stale next to working tests.)
#
# hls/src/ is laid out to mirror models/systemc/src/ (compute_unit/,
# simt_controller/, memory/, common/ - no scheduler/top/system_top/ since
# those SystemC components were never ported, see docs/hls/interfaces.md
# SS2.4). A single hls/src include root plus subdirectory-qualified
# #include "compute_unit/compute_pipeline.h"-style paths in the test files
# mirrors how the golden model's own tests include e.g. "top/top.h" against
# one models/systemc/src root, rather than one -I flag per component.

# Locate Vitis HLS headers (ap_int.h, hls_stream.h) for csim-style compilation
# without needing the vitis_hls binary itself. Mirrors scripts/setup-env.sh's
# SystemC auto-detection pattern.
if(NOT DEFINED XILINX_HLS_INCLUDE)
    if(DEFINED ENV{XILINX_HLS_INCLUDE})
        set(XILINX_HLS_INCLUDE "$ENV{XILINX_HLS_INCLUDE}")
    else()
        # Old standalone-installer layout (2023.1 and earlier):
        # Vitis_HLS/<ver>/include. New unified-installer layout (2026.1+,
        # see docs/hls/interfaces.md SS16.10): <ver>/Vitis/include, no
        # separate Vitis_HLS tree.
        file(GLOB _hls_include_candidates
            "/tools/Xilinx/Vitis_HLS/*/include"
            "/opt/Xilinx/Vitis_HLS/*/include"
            "/tools/Xilinx/*/Vitis/include"
            "/opt/Xilinx/*/Vitis/include"
        )
        if(_hls_include_candidates)
            list(GET _hls_include_candidates -1 XILINX_HLS_INCLUDE)  # newest version sorts last
        endif()
    endif()
endif()

if(BUILD_TESTS AND GTest_FOUND AND XILINX_HLS_INCLUDE)
    add_executable(test_hls_data_structures test_hls_data_structures.cpp)
    target_include_directories(test_hls_data_structures PRIVATE
        ${CMAKE_SOURCE_DIR}/hls/src
        ${XILINX_HLS_INCLUDE}
    )
    target_link_libraries(test_hls_data_structures PRIVATE
        GTest::GTest
        GTest::Main
    )
    add_test(NAME hls_data_structures_tests COMMAND test_hls_data_structures)
else()
    message(STATUS "GTest and/or Vitis HLS headers (ap_int.h/hls_stream.h) not found - skipping test_hls_data_structures. Set XILINX_HLS_INCLUDE to the Vitis HLS include/ dir to enable.")
endif()

set(HLS_COMPUTE_PIPELINE "${CMAKE_SOURCE_DIR}/hls/src/compute_unit/compute_pipeline.cpp")
set(HLS_MEMORY_PIPELINE  "${CMAKE_SOURCE_DIR}/hls/src/memory/memory_pipeline.cpp")

if(BUILD_TESTS AND GTest_FOUND AND XILINX_HLS_INCLUDE AND EXISTS ${HLS_COMPUTE_PIPELINE})
    find_package(Threads REQUIRED)
    add_executable(test_compute_pipeline test_compute_pipeline.cpp ${HLS_COMPUTE_PIPELINE})
    target_include_directories(test_compute_pipeline PRIVATE
        ${CMAKE_SOURCE_DIR}/hls/src
        ${XILINX_HLS_INCLUDE}
    )
    target_link_libraries(test_compute_pipeline PRIVATE
        GTest::GTest
        GTest::Main
        Threads::Threads
    )
    add_test(NAME compute_pipeline_tests COMMAND test_compute_pipeline)
else()
    message(STATUS "hls/src/compute_unit/compute_pipeline.cpp not found and/or Vitis HLS headers missing - skipping test_compute_pipeline")
endif()

if(BUILD_TESTS AND GTest_FOUND AND XILINX_HLS_INCLUDE AND EXISTS ${HLS_MEMORY_PIPELINE})
    find_package(Threads REQUIRED)
    add_executable(test_memory_pipeline test_memory_pipeline.cpp ${HLS_MEMORY_PIPELINE})
    target_include_directories(test_memory_pipeline PRIVATE
        ${CMAKE_SOURCE_DIR}/hls/src
        ${XILINX_HLS_INCLUDE}
    )
    target_link_libraries(test_memory_pipeline PRIVATE
        GTest::GTest
        GTest::Main
        Threads::Threads
    )
    add_test(NAME memory_pipeline_tests COMMAND test_memory_pipeline)
else()
    message(STATUS "hls/src/memory/memory_pipeline.cpp not found and/or Vitis HLS headers missing - skipping test_memory_pipeline")
endif()

if(BUILD_TESTS AND GTest_FOUND AND XILINX_HLS_INCLUDE AND EXISTS ${HLS_COMPUTE_PIPELINE} AND EXISTS ${HLS_MEMORY_PIPELINE})
    find_package(Threads REQUIRED)
    add_executable(test_pipeline_integration test_pipeline_integration.cpp
        ${HLS_COMPUTE_PIPELINE}
        ${HLS_MEMORY_PIPELINE}
    )
    target_include_directories(test_pipeline_integration PRIVATE
        ${CMAKE_SOURCE_DIR}/hls/src
        ${CMAKE_SOURCE_DIR}/models/systemc/src
        ${XILINX_HLS_INCLUDE}
    )
    target_link_libraries(test_pipeline_integration PRIVATE
        GTest::GTest
        GTest::Main
        Threads::Threads
    )
    add_test(NAME pipeline_integration_tests COMMAND test_pipeline_integration)
else()
    message(STATUS "hls/src/{compute_unit,memory}/{compute,memory}_pipeline.cpp not found and/or Vitis HLS headers missing - skipping test_pipeline_integration")
endif()

# On-chip scheduler (docs/hls/interfaces.md SS2.5/SS10.12): CuDispatchUnit/
# BarrierArbiter/MemArbiter are header-only (no threading in their own
# tests - unlike GpgpuTop below, none of these three drive a real
# compute_pipeline/memory_pipeline instance), same simple pattern
# test_hls_data_structures uses.
set(HLS_SCHEDULER_DIR "${CMAKE_SOURCE_DIR}/hls/src/scheduler")

if(BUILD_TESTS AND GTest_FOUND AND XILINX_HLS_INCLUDE AND EXISTS "${HLS_SCHEDULER_DIR}/cu_dispatch_unit.h")
    add_executable(test_cu_dispatch_unit test_cu_dispatch_unit.cpp)
    target_include_directories(test_cu_dispatch_unit PRIVATE
        ${CMAKE_SOURCE_DIR}/hls/src
        ${XILINX_HLS_INCLUDE}
    )
    target_link_libraries(test_cu_dispatch_unit PRIVATE
        GTest::GTest
        GTest::Main
    )
    add_test(NAME cu_dispatch_unit_tests COMMAND test_cu_dispatch_unit)
else()
    message(STATUS "hls/src/scheduler/cu_dispatch_unit.h not found and/or Vitis HLS headers missing - skipping test_cu_dispatch_unit")
endif()

if(BUILD_TESTS AND GTest_FOUND AND XILINX_HLS_INCLUDE AND EXISTS "${HLS_SCHEDULER_DIR}/barrier_arbiter.h")
    add_executable(test_barrier_arbiter test_barrier_arbiter.cpp)
    target_include_directories(test_barrier_arbiter PRIVATE
        ${CMAKE_SOURCE_DIR}/hls/src
        ${XILINX_HLS_INCLUDE}
    )
    target_link_libraries(test_barrier_arbiter PRIVATE
        GTest::GTest
        GTest::Main
    )
    add_test(NAME barrier_arbiter_tests COMMAND test_barrier_arbiter)
else()
    message(STATUS "hls/src/scheduler/barrier_arbiter.h not found and/or Vitis HLS headers missing - skipping test_barrier_arbiter")
endif()

if(BUILD_TESTS AND GTest_FOUND AND XILINX_HLS_INCLUDE AND EXISTS "${HLS_SCHEDULER_DIR}/mem_arbiter.h")
    add_executable(test_mem_arbiter test_mem_arbiter.cpp)
    target_include_directories(test_mem_arbiter PRIVATE
        ${CMAKE_SOURCE_DIR}/hls/src
        ${XILINX_HLS_INCLUDE}
    )
    target_link_libraries(test_mem_arbiter PRIVATE
        GTest::GTest
        GTest::Main
    )
    add_test(NAME mem_arbiter_tests COMMAND test_mem_arbiter)
else()
    message(STATUS "hls/src/scheduler/mem_arbiter.h not found and/or Vitis HLS headers missing - skipping test_mem_arbiter")
endif()

# GpgpuTop (docs/hls/interfaces.md SS10.12) - the capstone: drives real
# compute_pipeline/memory_pipeline/mem_arbiter instances via threads, same
# pattern as test_pipeline_integration, plus mem_arbiter.h (header-only).
if(BUILD_TESTS AND GTest_FOUND AND XILINX_HLS_INCLUDE
   AND EXISTS "${HLS_SCHEDULER_DIR}/gpgpu_top.h"
   AND EXISTS ${HLS_COMPUTE_PIPELINE} AND EXISTS ${HLS_MEMORY_PIPELINE})
    find_package(Threads REQUIRED)
    add_executable(test_gpgpu_top test_gpgpu_top.cpp
        ${HLS_COMPUTE_PIPELINE}
        ${HLS_MEMORY_PIPELINE}
    )
    target_include_directories(test_gpgpu_top PRIVATE
        ${CMAKE_SOURCE_DIR}/hls/src
        ${CMAKE_SOURCE_DIR}/models/systemc/src
        ${XILINX_HLS_INCLUDE}
    )
    target_link_libraries(test_gpgpu_top PRIVATE
        GTest::GTest
        GTest::Main
        Threads::Threads
    )
    add_test(NAME gpgpu_top_tests COMMAND test_gpgpu_top)
else()
    message(STATUS "hls/src/scheduler/gpgpu_top.h and/or compute_pipeline.cpp/memory_pipeline.cpp not found, and/or Vitis HLS headers missing - skipping test_gpgpu_top")
endif()

# T077: observability counter parity - verifies warp_status_t::instr_count and
# MemorySubsystem hit/miss counters are non-zero, consistent, and monotonic.
if(BUILD_TESTS AND GTest_FOUND AND XILINX_HLS_INCLUDE
   AND EXISTS ${HLS_COMPUTE_PIPELINE} AND EXISTS ${HLS_MEMORY_PIPELINE})
    find_package(Threads REQUIRED)
    add_executable(test_hls_counters test_hls_counters.cpp
        ${HLS_COMPUTE_PIPELINE}
        ${HLS_MEMORY_PIPELINE}
    )
    target_include_directories(test_hls_counters PRIVATE
        ${CMAKE_SOURCE_DIR}/hls/src
        ${CMAKE_SOURCE_DIR}/models/systemc/src
        ${XILINX_HLS_INCLUDE}
    )
    target_link_libraries(test_hls_counters PRIVATE
        GTest::GTest
        GTest::Main
        Threads::Threads
    )
    add_test(NAME hls_counter_tests COMMAND test_hls_counters)
else()
    message(STATUS "compute_pipeline.cpp/memory_pipeline.cpp or Vitis HLS headers missing - skipping test_hls_counters")
endif()

# T075: binary execution parity - verifies that the HLS decode+execute path
# correctly handles programs built directly from raw RV32I binary words.
if(BUILD_TESTS AND GTest_FOUND AND XILINX_HLS_INCLUDE AND EXISTS ${HLS_COMPUTE_PIPELINE})
    find_package(Threads REQUIRED)
    add_executable(test_hls_binary_execution test_hls_binary_execution.cpp
        ${HLS_COMPUTE_PIPELINE}
    )
    target_include_directories(test_hls_binary_execution PRIVATE
        ${CMAKE_SOURCE_DIR}/hls/src
        ${XILINX_HLS_INCLUDE}
    )
    target_link_libraries(test_hls_binary_execution PRIVATE
        GTest::GTest
        GTest::Main
        Threads::Threads
    )
    add_test(NAME hls_binary_execution_tests COMMAND test_hls_binary_execution)
else()
    message(STATUS "compute_pipeline.cpp or Vitis HLS headers missing - skipping test_hls_binary_execution")
endif()

# T078: end-to-end model parity regression - compares HLS compute_pipeline
# against the lightweight C++ reference executor for divergent, barrier-heavy,
# and FP kernels from kernel_programs.h.
if(BUILD_TESTS AND GTest_FOUND AND XILINX_HLS_INCLUDE
   AND EXISTS ${HLS_COMPUTE_PIPELINE} AND EXISTS ${HLS_MEMORY_PIPELINE})
    find_package(Threads REQUIRED)
    add_executable(test_model_parity test_model_parity.cpp
        ${HLS_COMPUTE_PIPELINE}
        ${HLS_MEMORY_PIPELINE}
    )
    target_include_directories(test_model_parity PRIVATE
        ${CMAKE_SOURCE_DIR}/hls/src
        ${CMAKE_SOURCE_DIR}/models/systemc/src
        ${XILINX_HLS_INCLUDE}
    )
    target_link_libraries(test_model_parity PRIVATE
        GTest::GTest
        GTest::Main
        Threads::Threads
    )
    add_test(NAME model_parity_tests COMMAND test_model_parity)
else()
    message(STATUS "compute_pipeline.cpp/memory_pipeline.cpp or Vitis HLS headers missing - skipping test_model_parity")
endif()
