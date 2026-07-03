// compute_unit.h - Baseline compute unit model
//
// Represents a single compute unit that executes warps
// and manages thread resources
//

#ifndef RISCV_GPGPU_COMPUTE_UNIT_H
#define RISCV_GPGPU_COMPUTE_UNIT_H

#include <systemc>
#include <vector>
#include <queue>
#include <memory>
#include "../common/types.h"

namespace riscv_gpgpu {

class ComputeUnit : public sc_core::sc_module {
public:
    // Configuration
    struct Config {
        uint32_t unit_id;
        uint32_t num_threads;
        uint32_t threads_per_warp;
        uint32_t max_warps;
        uint32_t shared_mem_size;
    };

    // Ports for memory interface
    sc_core::sc_in<bool> clk{"clk"};
    sc_core::sc_in<bool> reset{"reset"};
    
    // Memory interface (placeholder - to be extended with full interfaces)
    sc_core::sc_out<bool> memory_ready{"memory_ready"};
    sc_core::sc_in<bool> memory_request{"memory_request"};

    ComputeUnit(sc_core::sc_module_name name, const Config& config);
    ~ComputeUnit();

    // Public interface
    void launchKernel(BlockID block_id, uint32_t grid_x, uint32_t grid_y);
    WarpState getWarpState(WarpID warp_id) const;
    void step();  // Execute one cycle
    bool isComplete() const;
    
    // Statistics
    CycleCount getTotalCycles() const { return total_cycles_; }
    InstructionCount getTotalInstructions() const { return total_instructions_; }

private:
    // SC_MODULE interface
    SC_HAS_PROCESS(ComputeUnit);
    
    void clockProcess();
    void resetProcess();
    void executeProcess();

    // Internal state
    Config config_;
    ComputeUnitID unit_id_;
    
    // Warp management
    std::vector<WarpState> warp_states_;
    std::queue<WarpID> ready_warps_;
    std::queue<WarpID> stalled_warps_;
    
    // Thread register files
    std::vector<std::vector<uint32_t>> registers_;  // [warp][reg]
    
    // Shared memory
    std::vector<uint8_t> shared_memory_;
    
    // Execution state
    CycleCount total_cycles_;
    InstructionCount total_instructions_;
    WarpID current_executing_warp_;
    bool is_running_;
    
    // Helper methods
    void initializeWarp(WarpID warp_id);
    void finalizeWarp(WarpID warp_id);
    void scheduleWarp();
    void executeInstruction(WarpID warp_id);
    bool checkMemoryDependencies(WarpID warp_id);
    void updateWarpState();
};

}  // namespace riscv_gpgpu

#endif  // RISCV_GPGPU_COMPUTE_UNIT_H
