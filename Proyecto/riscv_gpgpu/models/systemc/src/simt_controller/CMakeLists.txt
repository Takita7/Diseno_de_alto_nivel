# CMakeLists.txt - SIMT Controller component

add_library(simt_controller simt_controller.cpp)
target_link_libraries(simt_controller PUBLIC systemc_common)
target_include_directories(simt_controller PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
