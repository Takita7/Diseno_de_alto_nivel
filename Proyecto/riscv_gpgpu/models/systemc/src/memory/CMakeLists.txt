# CMakeLists.txt - Memory Hierarchy component

add_library(memory_hierarchy memory_hierarchy.cpp)
target_link_libraries(memory_hierarchy PUBLIC systemc_common)
target_include_directories(memory_hierarchy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
