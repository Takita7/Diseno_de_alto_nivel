# CMakeLists.txt — PTX transpiler tests (T056, T057, T058)

if(BUILD_TESTS AND GTest_FOUND)
    # T056: PTX parser unit tests
    add_executable(test_ptx_parser test_ptx_parser.cpp)
    target_link_libraries(test_ptx_parser PRIVATE GTest::GTest GTest::Main ptx_transpiler_lib)
    add_test(NAME ptx_parser_tests COMMAND test_ptx_parser)

    # T057: RISC-V emitter unit tests
    add_executable(test_rv_emitter test_rv_emitter.cpp)
    target_link_libraries(test_rv_emitter PRIVATE GTest::GTest GTest::Main ptx_transpiler_lib)
    add_test(NAME rv_emitter_tests COMMAND test_rv_emitter)

    # T058: End-to-end transpiler integration test (requires clang riscv32 target)
    add_executable(test_ptx_transpiler test_ptx_transpiler.cpp)
    target_link_libraries(test_ptx_transpiler PRIVATE GTest::GTest GTest::Main ptx_transpiler_lib)
    add_test(NAME ptx_transpiler_tests COMMAND test_ptx_transpiler)

    message(STATUS "PTX transpiler tests configured")
else()
    message(STATUS "Skipping PTX transpiler tests (BUILD_TESTS or GTest not available)")
endif()
