# CMakeLists.txt - Software Components

# Subdirectories for software components
add_subdirectory(common)
add_subdirectory(host_api)
add_subdirectory(kernel_loader)
add_subdirectory(llvm)

message(STATUS "Software subdirectory configured")
