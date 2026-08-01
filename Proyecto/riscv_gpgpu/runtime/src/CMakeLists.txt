add_library(runtime_lib STATIC host_runtime.cpp)

target_include_directories(runtime_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(runtime_lib
    PUBLIC kernel_loader_lib driver_lib
    PRIVATE host_api_lib
)

if(BUILD_TESTS AND GTest_FOUND)
    add_executable(test_runtime_api test_runtime_api.cpp)
    target_link_libraries(test_runtime_api PRIVATE GTest::GTest GTest::Main runtime_lib llvm_backend_stub kernel_loader_lib driver_lib)
    add_test(NAME runtime_api_tests COMMAND test_runtime_api)
elseif(BUILD_TESTS)
    message(WARNING "GTest not found. Skipping runtime API tests.")
endif()
