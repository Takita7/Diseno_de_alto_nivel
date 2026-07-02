# CMakeLists.txt - SystemC models

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/CommonSettings.cmake)

# SystemC common utilities library
add_library(systemc_common INTERFACE)
target_include_directories(systemc_common INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_link_libraries(systemc_common INTERFACE SystemC::SystemC)

# Build all component libraries
add_subdirectory(compute_unit)
add_subdirectory(scheduler)
add_subdirectory(simt_controller)
add_subdirectory(memory)

# Build top-level module and executable
if(BUILD_SYSTEMC_MODELS)
    add_subdirectory(top)
endif()

message(STATUS "SystemC models configured successfully")
