// main.cpp - SystemC simulation entry point
//

#include <systemc>
#include "top.h"
#include "../common/logging.h"
#include "../common/platform.h"

using namespace riscv_gpgpu;

int main(int argc, char* argv[]) {
    Platform::printSimulationBanner();
    
    // Create top-level module
    GPGPUTop::Config config;
    config.num_compute_units = 4;
    config.threads_per_warp = 32;
    config.max_warps_per_cu = 16;
    config.shared_mem_size = 49152;  // 48KB
    config.l1_cache_size = 16384;    // 16KB
    config.l2_cache_size = 262144;   // 256KB
    
    GPGPUTop top("gpgpu_top", config);
    
    // Configure logging
    Logger::instance().setLogLevel(LogLevel::INFO);
    Logger::instance().setLogFile("simulation.log");
    
    // Launch a simple kernel
    top.launchKernel(4, 1);
    
    // Run simulation for a fixed number of cycles
    uint64_t max_cycles = 10000;
    sc_core::sc_start(max_cycles * 10, sc_core::SC_NS);  // 10ns per cycle
    
    // Print final statistics
    LOG_INFO("Simulation complete");
    std::cout << "\nSimulation Statistics:" << std::endl;
    std::cout << "Total cycles: " << top.getTotalCycles() << std::endl;
    std::cout << "Total instructions: " << top.getTotalInstructions() << std::endl;
    std::cout << "L1 cache hits: " << top.getL1CacheHits() << std::endl;
    std::cout << "L1 cache misses: " << top.getL1CacheMisses() << std::endl;
    
    Platform::printSimulationStats(top.getTotalCycles(), top.getTotalInstructions());
    
    return 0;
}
