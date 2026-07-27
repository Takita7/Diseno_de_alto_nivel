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
