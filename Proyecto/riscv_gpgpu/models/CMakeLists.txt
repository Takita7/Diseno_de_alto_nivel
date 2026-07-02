# CMakeLists.txt - SystemC Models

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/CommonSettings.cmake)

# SystemC models — built when SystemC is found (regardless of BUILD_SYSTEMC_MODELS
# so that the integration libraries are always available for the test suite).
if(SystemC_FOUND)
    add_subdirectory(systemc)
    message(STATUS "SystemC models enabled (SystemC found at ${SystemC_LIBRARY})")
else()
    message(STATUS "Models subdirectory (ready for implementation)")
endif()
