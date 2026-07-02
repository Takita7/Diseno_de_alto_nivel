# CMakeLists.txt - Top-level module

add_executable(systemc_simulation main.cpp top.cpp)
target_link_libraries(systemc_simulation 
    PUBLIC
    systemc_common
    compute_unit
    warp_scheduler
    simt_controller
    memory_hierarchy
    SystemC::SystemC
)
target_include_directories(systemc_simulation PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

set_target_properties(systemc_simulation PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
