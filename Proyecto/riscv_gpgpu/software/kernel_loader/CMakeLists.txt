add_library(kernel_loader_lib STATIC kernel_loader.cpp)

target_include_directories(kernel_loader_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(BUILD_TESTS AND GTest_FOUND)
    add_executable(test_kernel_loader test_kernel_loader.cpp)
    target_link_libraries(test_kernel_loader PRIVATE GTest::GTest GTest::Main kernel_loader_lib llvm_backend_stub)
    add_test(NAME kernel_loader_tests COMMAND test_kernel_loader)
elseif(BUILD_TESTS)
    message(WARNING "GTest not found. Skipping kernel loader tests.")
endif()
