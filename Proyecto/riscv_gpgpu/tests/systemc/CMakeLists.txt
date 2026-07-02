# CMakeLists.txt - SystemC tests

if(BUILD_TESTS)
    if(GTest_FOUND)
        add_executable(test_scheduler test_scheduler.cpp)
        target_link_libraries(test_scheduler PRIVATE 
            GTest::GTest 
            GTest::Main
            warp_scheduler
            systemc_common
        )
        add_test(NAME scheduler_tests COMMAND test_scheduler)
        
        add_executable(test_pipeline test_pipeline.cpp)
        target_link_libraries(test_pipeline PRIVATE
            GTest::GTest
            GTest::Main
            systemc_common
        )
        add_test(NAME pipeline_integration_tests COMMAND test_pipeline)
    else()
        message(WARNING "GTest not found. Skipping C++ unit tests.")
    endif()
endif()

message(STATUS "SystemC tests configured")
