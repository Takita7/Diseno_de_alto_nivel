add_library(llvm_backend_stub STATIC llvm_backend.cpp)

target_include_directories(llvm_backend_stub PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(BUILD_TESTS AND GTest_FOUND)
    add_executable(test_llvm_backend test_llvm_backend.cpp)
    target_link_libraries(test_llvm_backend PRIVATE GTest::GTest GTest::Main llvm_backend_stub)
    add_test(NAME llvm_backend_tests COMMAND test_llvm_backend)
elseif(BUILD_TESTS)
    message(WARNING "GTest not found. Skipping LLVM backend tests.")
endif()
