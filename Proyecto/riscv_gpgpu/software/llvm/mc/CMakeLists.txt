add_library(llvm_mc_stub STATIC riscv_gpgpu_mc.cpp)

target_include_directories(llvm_mc_stub PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(BUILD_TESTS AND GTest_FOUND)
    add_executable(test_llvm_mc test_riscv_gpgpu_mc.cpp)
    target_link_libraries(test_llvm_mc PRIVATE GTest::GTest GTest::Main llvm_mc_stub)
    add_test(NAME llvm_mc_tests COMMAND test_llvm_mc)
elseif(BUILD_TESTS)
    message(WARNING "GTest not found. Skipping LLVM MC tests.")
endif()
