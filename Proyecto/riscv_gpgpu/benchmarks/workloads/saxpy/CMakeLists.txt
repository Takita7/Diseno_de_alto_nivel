# CMakeLists.txt – saxpy RISC-V GPGPU workload
#
# Registers the saxpy kernel for compilation via add_riscv_kernel().
# Produces:
#   ${CMAKE_BINARY_DIR}/kernels/saxpy.elf
# Target: saxpy_kernel

include(RiscvKernel)

add_riscv_kernel(saxpy
    SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/saxpy.cu"
    ENTRY  "saxpy"
)

install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/run_saxpy.sh"
        DESTINATION "benchmarks/saxpy"
        COMPONENT benchmarks
        OPTIONAL
)
