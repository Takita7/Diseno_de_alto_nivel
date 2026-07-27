#include <systemc>
#include <iostream>
#include "top.h"

int sc_main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    riscv_gpgpu::GPGPUTop::Config cfg;
    riscv_gpgpu::GPGPUTop top("gpgpu_top", cfg);

    // Keep the default simulation entry lightweight and deterministic.
    std::cout << "[systemc_simulation] GPGPUTop instantiated.\n";
    sc_core::sc_start(sc_core::SC_ZERO_TIME);
    return 0;
}
