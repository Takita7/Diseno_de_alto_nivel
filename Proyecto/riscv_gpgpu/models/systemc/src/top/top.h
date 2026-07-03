// top.h - Top-level SystemC module
//
// Integrates all architecture components into a complete system
//

#ifndef RISCV_GPGPU_TOP_H
#define RISCV_GPGPU_TOP_H

#include <systemc>
#include <vector>
#include <memory>

namespace riscv_gpgpu {

// Forward declarations
class ComputeUnit;
class WarpScheduler;
class MemoryHierarchy;

class GPGPUTop : public sc_core::sc_module {
public:
    // Ports
    sc_core::sc_in<bool> clk{"clk"};
    sc_core::sc_in<bool> reset{"reset"};
    
    // Configuration
    struct Config {
        uint32_t num_compute_units;
        uint32_t threads_per_warp;
        uint32_t max_warps_per_cu;
        uint32_t shared_mem_size;
        uint32_t l1_cache_size;
        uint32_t l2_cache_size;
    };

    GPGPUTop(sc_core::sc_module_name name, const Config& config);
    ~GPGPUTop();

    // Public interface
    void launchKernel(uint32_t grid_x, uint32_t grid_y);
    bool isKernelComplete() const;
    
    // Statistics
    uint64_t getTotalCycles() const;
    uint64_t getTotalInstructions() const;
    uint32_t getL1CacheHits() const;
    uint32_t getL1CacheMisses() const;
    uint32_t getDivergenceEvents() const;

private:
    Config config_;
    
    // Components
    std::vector<std::unique_ptr<ComputeUnit>> compute_units_;
    std::unique_ptr<WarpScheduler> scheduler_;
    std::unique_ptr<MemoryHierarchy> memory_;
    
    // Clock signal
    sc_core::sc_clock system_clock;
    
    // Process
    SC_HAS_PROCESS(GPGPUTop);
    void simulationProcess();
};

}  // namespace riscv_gpgpu

#endif  // RISCV_GPGPU_TOP_H
