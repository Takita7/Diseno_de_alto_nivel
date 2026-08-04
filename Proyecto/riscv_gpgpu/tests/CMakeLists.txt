# CMakeLists.txt - Tests

# Enable testing
enable_testing()

# Subdirectories for test components
add_subdirectory(systemc)
add_subdirectory(hls)
add_subdirectory(fpga)
add_subdirectory(compiler)
add_subdirectory(fpga)
# add_subdirectory(runtime)
# add_subdirectory(hls)

message(STATUS "Tests subdirectory configured")
