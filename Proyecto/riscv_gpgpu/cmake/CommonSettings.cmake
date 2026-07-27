# CommonSettings.cmake - Common build settings for all targets

# Compiler flags for different build types
set(COMMON_CXX_FLAGS "-Wall -Wextra -Wpedantic")

# Debug flags
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG=1")

# Release flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# RelWithDebInfo flags
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")

# Apply common flags to all configurations
foreach(LANG C CXX)
    string(APPEND CMAKE_${LANG}_FLAGS " ${COMMON_CXX_FLAGS}")
endforeach()

# Function to add a model library (used by subprojects)
function(add_model_library target_name)
    add_library(${target_name} ${ARGN})
    target_link_libraries(${target_name} PUBLIC SystemC::SystemC)
    target_compile_options(${target_name} PRIVATE -fPIC)
endfunction()

# Function to add a test executable
function(add_model_test test_name test_file)
    if(BUILD_TESTS)
        add_executable(${test_name} ${test_file} ${ARGN})
        target_link_libraries(${test_name} PRIVATE SystemC::SystemC)
        if(GTest_FOUND)
            target_link_libraries(${test_name} PRIVATE GTest::GTest GTest::Main)
        endif()
        add_test(NAME ${test_name} COMMAND ${test_name})
    endif()
endfunction()

# Output useful build configuration
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build flags (Debug): ${CMAKE_CXX_FLAGS_DEBUG}")
message(STATUS "Build flags (Release): ${CMAKE_CXX_FLAGS_RELEASE}")
