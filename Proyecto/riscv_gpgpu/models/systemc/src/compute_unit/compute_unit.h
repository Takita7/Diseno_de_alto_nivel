// compute_unit.h – Baseline compute unit model
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
    struct Config {
        uint32_t unit_id          = 0;
        uint32_t num_threads      = 32;
        uint32_t threads_per_warp = 32;
        uint32_t max_warps        = 4;
        uint32_t shared_mem_size  = 16 * 1024;
    };

    // reset, memory_ready, memory_request removed –
    // nothing connects them yet; TLM socket replaces these in Phase 4.
    sc_core::sc_in<bool> clk{"clk"};

    SC_HAS_PROCESS(ComputeUnit);
    ComputeUnit(sc_core::sc_module_name name, const Config& config);
    ~ComputeUnit();

    // Public interface
    void      launchKernel(BlockID block_id, uint32_t grid_x, uint32_t grid_y);
    WarpState getWarpState(WarpID warp_id) const;
    void      step();
    bool      isComplete() const;

    // Statistics
    CycleCount       getTotalCycles()       const { return total_cycles_;       }
    InstructionCount getTotalInstructions() const { return total_instructions_; }

private:
    void clockProcess();
    void executeProcess();

    Config        config_;
    ComputeUnitID unit_id_;

    std::vector<WarpState>           warp_states_;
    std::queue<WarpID>               ready_warps_;
    std::queue<WarpID>               stalled_warps_;
    std::vector<std::vector<uint32_t>> registers_;   // [warp][reg]
    std::vector<uint8_t>             shared_memory_;

    CycleCount       total_cycles_       = 0;
    InstructionCount total_instructions_ = 0;
    WarpID           current_executing_warp_ = 0;
    bool             is_running_         = false;

    void initializeWarp  (WarpID warp_id);
    void finalizeWarp    (WarpID warp_id);
    void scheduleWarp    ();
    void executeInstruction(WarpID warp_id);
    bool checkMemoryDependencies(WarpID warp_id);
    void updateWarpState ();
};

}  // namespace riscv_gpgpu

#endif  // RISCV_GPGPU_COMPUTE_UNIT_H