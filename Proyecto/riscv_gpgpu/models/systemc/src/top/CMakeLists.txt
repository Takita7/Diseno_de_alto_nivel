# CMakeLists.txt - Top-level module

# ── gpgpu_top library (without sc_main) ─────────────────────────────────────
add_library(gpgpu_top top.cpp)
target_link_libraries(gpgpu_top
    PUBLIC
    systemc_common
    compute_unit
    warp_scheduler
    simt_controller
    memory_hierarchy
    SystemC::SystemC
)
target_include_directories(gpgpu_top PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# ── standalone simulation executable (has sc_main in main.cpp) ──────────────
add_executable(systemc_simulation main.cpp)
target_link_libraries(systemc_simulation
    PRIVATE
    gpgpu_top
    SystemC::SystemC
)
target_include_directories(systemc_simulation PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

set_target_properties(systemc_simulation PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
