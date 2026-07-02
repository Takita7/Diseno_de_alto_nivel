# CMakeLists.txt - Common software components

add_library(riscv_config INTERFACE)
target_include_directories(riscv_config INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# The configuration parsing is currently header-only and Python scripts
# Full C++ implementation will be built in future phases
