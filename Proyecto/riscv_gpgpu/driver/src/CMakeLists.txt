add_library(driver_lib STATIC loader.cpp)

target_include_directories(driver_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(BUILD_TESTS AND GTest_FOUND)
    add_executable(test_driver_api test_driver_api.cpp)
    target_link_libraries(test_driver_api PRIVATE GTest::GTest GTest::Main driver_lib)
    add_test(NAME driver_api_tests COMMAND test_driver_api)
elseif(BUILD_TESTS)
    message(WARNING "GTest not found. Skipping driver API tests.")
endif()
