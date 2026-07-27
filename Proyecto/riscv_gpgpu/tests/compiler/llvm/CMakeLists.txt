add_executable(test_compiler_llvm_backend test_llvm_backend.cpp)
target_link_libraries(test_compiler_llvm_backend PRIVATE GTest::GTest GTest::Main)
add_test(NAME compiler_llvm_backend_tests COMMAND test_compiler_llvm_backend)
message(STATUS "Compiler LLVM backend tests configured")
