add_library(system_top STATIC system_top.cpp)
target_link_libraries(system_top PUBLIC gpgpu_top systemc_common)
target_include_directories(system_top PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
