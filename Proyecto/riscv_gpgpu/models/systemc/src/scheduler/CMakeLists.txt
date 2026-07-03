# CMakeLists.txt - Warp Scheduler component

add_library(warp_scheduler warp_scheduler.cpp)
target_link_libraries(warp_scheduler PUBLIC systemc_common)
target_include_directories(warp_scheduler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
