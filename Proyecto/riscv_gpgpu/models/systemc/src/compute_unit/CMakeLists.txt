# CMakeLists.txt - compute_unit

add_library(compute_unit compute_unit.cpp)
target_link_libraries(compute_unit PUBLIC systemc_common memory_hierarchy simt_controller)
target_include_directories(compute_unit PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
