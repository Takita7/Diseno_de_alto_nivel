cmake_minimum_required(VERSION 3.24)
project(RISCV_GPGPU 
    VERSION 0.1.0
    DESCRIPTION "FPGA-Based Open RISC-V GPGPU Platform"
    LANGUAGES CXX C
)

# Project-wide settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Option for build types
set(CMAKE_CONFIGURATION_TYPES Debug Release RelWithDebInfo)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Include project CMake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Project features
option(BUILD_SYSTEMC_MODELS "Build SystemC models" ON)
option(BUILD_TESTS "Build test suites" ON)
option(BUILD_HLS "Build HLS implementation (requires Vitis HLS)" OFF)
option(BUILD_BENCHMARKS "Build benchmark utilities" ON)
option(BUILD_DOCUMENTATION "Build documentation" OFF)

# Find required packages
find_package(SystemC REQUIRED)

# Optional packages
if(BUILD_TESTS)
    enable_testing()
    find_package(GTest QUIET)
endif()

if(BUILD_HLS)
    # HLS toolchain detection
    # This is typically handled through environment or separate build system
endif()

# Add subdirectories
add_subdirectory(models)
add_subdirectory(software)
add_subdirectory(runtime)
add_subdirectory(driver)

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# Print configuration summary
message(STATUS "RISCV_GPGPU Build Configuration:")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  SystemC Models: ${BUILD_SYSTEMC_MODELS}")
message(STATUS "  Tests: ${BUILD_TESTS}")
message(STATUS "  HLS: ${BUILD_HLS}")
message(STATUS "  Benchmarks: ${BUILD_BENCHMARKS}")
message(STATUS "  Documentation: ${BUILD_DOCUMENTATION}")
