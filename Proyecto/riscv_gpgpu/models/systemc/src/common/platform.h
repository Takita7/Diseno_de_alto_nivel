// platform.h - SystemC platform utilities
//
// Provides SystemC-specific utilities and abstractions
// for the architecture model
//

#ifndef RISCV_GPGPU_PLATFORM_H
#define RISCV_GPGPU_PLATFORM_H

#include <systemc>
#include <string>
#include <iostream>

namespace riscv_gpgpu {

// SystemC module naming utilities
class Platform {
public:
    // Get current simulation time
    static sc_core::sc_time getCurrentTime() {
        return sc_core::sc_time_stamp();
    }
    
    // Get current simulation cycle (assuming 1ns time unit)
    static uint64_t getCurrentCycle() {
        return sc_core::sc_time_stamp().to_double() / 1000.0;
    }
    
    // Format module name hierarchically
    static std::string getModuleName(const std::string& base_name,
                                      uint32_t id) {
        return base_name + "_" + std::to_string(id);
    }
    
    // Print simulation banner
    static void printSimulationBanner() {
        std::cout << "=====================================" << std::endl;
        std::cout << "RISCV GPGPU SystemC Simulation" << std::endl;
        std::cout << "=====================================" << std::endl;
        std::cout << "SystemC version: " << SC_VERSION << std::endl;
        std::cout << "Starting simulation..." << std::endl;
    }
    
    // Print simulation statistics
    static void printSimulationStats(uint64_t total_cycles,
                                      uint64_t total_instructions) {
        std::cout << "=====================================" << std::endl;
        std::cout << "Simulation Complete" << std::endl;
        std::cout << "Total cycles: " << total_cycles << std::endl;
        std::cout << "Total instructions: " << total_instructions << std::endl;
        std::cout << "=====================================" << std::endl;
    }
};

}  // namespace riscv_gpgpu

#endif  // RISCV_GPGPU_PLATFORM_H
