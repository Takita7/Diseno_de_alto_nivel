# CMakeLists.txt - SystemC models

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/CommonSettings.cmake)

# SystemC common utilities library
add_library(systemc_common INTERFACE)
target_include_directories(systemc_common INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)
target_link_libraries(systemc_common INTERFACE SystemC::SystemC)

# Build all component libraries
add_subdirectory(src/memory)
add_subdirectory(src/simt_controller)
add_subdirectory(src/compute_unit)
add_subdirectory(src/scheduler)
if(BUILD_SYSTEMC_INTEGRATION)
    add_subdirectory(integration)
endif()

# Build top-level module and executable
if(BUILD_SYSTEMC_MODELS)
    add_subdirectory(src/top)
endif()

message(STATUS "SystemC models configured successfully")
