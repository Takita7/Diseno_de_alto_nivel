add_library(host_api_lib STATIC host_api.cpp)

target_include_directories(host_api_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(BUILD_TESTS AND GTest_FOUND)
    add_executable(test_host_api test_host_api.cpp)
    target_link_libraries(test_host_api PRIVATE GTest::GTest GTest::Main host_api_lib)
    add_test(NAME host_api_tests COMMAND test_host_api)
elseif(BUILD_TESTS)
    message(WARNING "GTest not found. Skipping host API tests.")
endif()
