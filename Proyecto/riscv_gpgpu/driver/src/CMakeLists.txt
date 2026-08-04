add_library(driver_lib STATIC loader.cpp)
target_include_directories(driver_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ── PTX→RISC-V transpiler library ─────────────────────────────────────────────
add_library(ptx_transpiler_lib STATIC
    ptx_transpiler/ptx_parser.cpp
    ptx_transpiler/rv_emitter.cpp
    ptx_transpiler/ptx_transpiler.cpp
)
target_include_directories(ptx_transpiler_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/ptx_transpiler
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_features(ptx_transpiler_lib PUBLIC cxx_std_17)

if(BUILD_TESTS AND GTest_FOUND)
    add_executable(test_driver_api test_driver_api.cpp)
    target_link_libraries(test_driver_api PRIVATE GTest::GTest GTest::Main driver_lib)
    add_test(NAME driver_api_tests COMMAND test_driver_api)
elseif(BUILD_TESTS)
    message(WARNING "GTest not found. Skipping driver API tests.")
endif()
