// warp_scheduler.h - Warp scheduler and dispatch model
//
// Implements warp scheduling policies and resource management
//

#ifndef RISCV_GPGPU_WARP_SCHEDULER_H
#define RISCV_GPGPU_WARP_SCHEDULER_H

#include <systemc>
#include <vector>
#include <queue>
#include <memory>
#include "../common/types.h"

namespace riscv_gpgpu {

class WarpScheduler : public sc_core::sc_module {
public:
    // Scheduling policy options
    enum class SchedulingPolicy {
        ROUND_ROBIN,
        PRIORITY,
        FIFO
    };

    struct Config {
        uint32_t num_compute_units;
        uint32_t max_warps_per_cu;
        SchedulingPolicy policy;
        bool enable_optimization;
        uint32_t batch_size;
    };

    // Ports
    sc_core::sc_in<bool> clk{"clk"};
    sc_core::sc_in<bool> reset{"reset"};

    WarpScheduler(sc_core::sc_module_name name, const Config& config);
    ~WarpScheduler();

    // Public interface
    void submitKernel(GridID grid_id, uint32_t grid_x, uint32_t grid_y);
    WarpID selectWarp(ComputeUnitID cu_id);
    void markWarpComplete(ComputeUnitID cu_id, WarpID warp_id);
    void markWarpStalled(ComputeUnitID cu_id, WarpID warp_id);
    
    bool hasReadyWarps(ComputeUnitID cu_id) const;
    bool isComplete() const;
    
    // Statistics
    uint32_t getTotalWarpsDispatched() const { return total_warps_dispatched_; }
    uint32_t getTotalKernelsCompleted() const { return total_kernels_completed_; }

private:
    SC_HAS_PROCESS(WarpScheduler);
    
    void scheduleProcess();
    void balanceLoad();

    // Configuration
    Config config_;
    
    // Warp queue management - per compute unit
    std::vector<std::queue<WarpID>> ready_queues_;      // Ready warps per CU
    std::vector<std::queue<WarpID>> stalled_queues_;    // Stalled warps per CU
    
    // Current scheduling state
    std::vector<uint32_t> round_robin_indices_;         // For round-robin policy
    uint32_t kernel_warp_counter_;
    
    // Statistics
    uint32_t total_warps_dispatched_;
    uint32_t total_kernels_completed_;
    
    // Helper methods
    WarpID selectWarpRoundRobin(ComputeUnitID cu_id);
    WarpID selectWarpPriority(ComputeUnitID cu_id);
    WarpID selectWarpFIFO(ComputeUnitID cu_id);
    void generateWarps(GridID grid_id, uint32_t grid_x, uint32_t grid_y);
    void balanceLoadAcrossUnits();
};

}  // namespace riscv_gpgpu

#endif  // RISCV_GPGPU_WARP_SCHEDULER_H
