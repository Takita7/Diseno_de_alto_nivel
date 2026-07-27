# CMakeLists.txt – vector_add RISC-V GPGPU workload
#
# Registers the vector_add kernel for compilation via the add_riscv_kernel()
# helper (cmake/RiscvKernel.cmake).  Produces:
#   ${CMAKE_BINARY_DIR}/kernels/vector_add.elf
# Target: vector_add_kernel
#
# Also installs the run script so `cmake --install` or benchmark harnesses
# can locate it without a hardcoded path.

include(RiscvKernel)

add_riscv_kernel(vector_add
    SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/vector_add.cu"
    ENTRY  "vector_add"
)

# Install the run script alongside the built kernel
install(PROGRAMS "${CMAKE_CURRENT_SOURCE_DIR}/run_vector_add.sh"
        DESTINATION "benchmarks/vector_add"
        COMPONENT benchmarks
        OPTIONAL
)
