# CMakeLists.txt - SystemC tests

# CMakeLists.txt - SystemC tests

if(BUILD_TESTS AND BUILD_SYSTEMC_MODELS AND SystemC_FOUND)
    if(GTest_FOUND)
        # sc_gtest_main.cpp provides sc_main() that calls RUN_ALL_TESTS().
        # Tests must NOT link GTest::Main (which would conflict with sc_main).

        add_executable(test_scheduler test_scheduler.cpp sc_gtest_main.cpp)
        target_link_libraries(test_scheduler PRIVATE
            GTest::GTest
            warp_scheduler
            systemc_common
            SystemC::SystemC
        )
        add_test(NAME scheduler_tests COMMAND test_scheduler)

        add_executable(test_pipeline test_pipeline.cpp sc_gtest_main.cpp)
        target_link_libraries(test_pipeline PRIVATE
            GTest::GTest
            gpgpu_top
            systemc_common
            SystemC::SystemC
        )
        add_test(NAME pipeline_integration_tests COMMAND test_pipeline)

        add_executable(test_simt_controller test_simt_controller.cpp sc_gtest_main.cpp)
        target_link_libraries(test_simt_controller PRIVATE
            GTest::GTest
            simt_controller
            SystemC::SystemC
        )
        add_test(NAME simt_controller_tests COMMAND test_simt_controller)

        add_executable(test_simt_compute_unit test_simt_compute_unit.cpp sc_gtest_main.cpp)
        target_link_libraries(test_simt_compute_unit PRIVATE
            GTest::GTest
            compute_unit
            simt_controller
            memory_hierarchy
            SystemC::SystemC
        )
        add_test(NAME simt_compute_unit_tests COMMAND test_simt_compute_unit)
    else()
        message(WARNING "GTest not found. Skipping C++ unit tests.")
    endif()
elseif(BUILD_TESTS)
    message(STATUS "BUILD_SYSTEMC_MODELS is OFF; skipping SystemC unit tests.")
endif()

# ── SystemC ↔ Software integration tests ─────────────────────────────────────
# These do NOT require BUILD_SYSTEMC_MODELS — they use the SystemC integration
# library which can be built independently when SystemC is available.
if(BUILD_TESTS AND BUILD_SYSTEMC_INTEGRATION AND GTest_FOUND AND SystemC_FOUND)
    add_executable(test_systemc_integration test_systemc_integration.cpp sc_gtest_main.cpp)
    target_link_libraries(test_systemc_integration
        PRIVATE
        GTest::GTest
        systemc_integration
        compute_unit
        memory_hierarchy
        driver_lib
        kernel_loader_lib
        host_api_lib
        llvm_backend_stub
        SystemC::SystemC
    )
    target_include_directories(test_systemc_integration PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../../models/systemc
        ${CMAKE_CURRENT_SOURCE_DIR}/../../models/systemc/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../../models/systemc/integration
        ${CMAKE_CURRENT_SOURCE_DIR}/../../driver/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../../software/host_api
        ${CMAKE_CURRENT_SOURCE_DIR}/../../software/kernel_loader
    )
    set_target_properties(test_systemc_integration PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    add_test(NAME systemc_integration_tests COMMAND test_systemc_integration)
    message(STATUS "SystemC integration tests configured")
elseif(BUILD_TESTS)
    message(STATUS "SystemC not found — skipping systemc_integration tests")
endif()

message(STATUS "SystemC tests configured")
