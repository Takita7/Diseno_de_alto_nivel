# CMakeLists.txt - integration layer (ELF loader, RV32I decoder, KernelBridge)

add_library(systemc_integration
    elf_loader.cpp
    kernel_bridge.cpp
)

target_link_libraries(systemc_integration
    PUBLIC
    compute_unit
    memory_hierarchy
    driver_lib
    SystemC::SystemC
)

target_include_directories(systemc_integration PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../driver/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../software/kernel_loader
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../software/host_api
)

add_library(systemc_host_backend STATIC
    systemc_host_backend.cpp
)
target_link_libraries(systemc_host_backend PUBLIC
    host_api_lib
    systemc_integration
)
target_include_directories(systemc_host_backend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
