// warp_scheduler.h – Warp scheduler and dispatch model
//

#ifndef RISCV_GPGPU_WARP_SCHEDULER_H
#define RISCV_GPGPU_WARP_SCHEDULER_H

#include <systemc>
#include <vector>
#include <queue>
#include "../common/types.h"

namespace riscv_gpgpu {

class WarpScheduler : public sc_core::sc_module {
public:
    // Sentinel returned by selectWarp() when no warp is available
    static constexpr WarpID INVALID_WARP_ID = UINT32_MAX;

    enum class SchedulingPolicy { ROUND_ROBIN, PRIORITY, FIFO };

    struct Config {
        uint32_t         num_compute_units   = 1;
        uint32_t         max_warps_per_cu    = 4;
        SchedulingPolicy policy              = SchedulingPolicy::ROUND_ROBIN;
        bool             enable_optimization = false;
        uint32_t         batch_size          = 1;
    };

    sc_core::sc_in<bool> clk{"clk"};

    SC_HAS_PROCESS(WarpScheduler);
    WarpScheduler(sc_core::sc_module_name name, const Config& config);
    ~WarpScheduler();

    // ── Public interface ──────────────────────────────────────────────────────
    void   submitKernel     (GridID grid_id, uint32_t grid_x, uint32_t grid_y);
    WarpID selectWarp       (ComputeUnitID cu_id);
    void   markWarpComplete (ComputeUnitID cu_id, WarpID warp_id);
    void   markWarpStalled  (ComputeUnitID cu_id, WarpID warp_id);

    bool hasReadyWarps (ComputeUnitID cu_id) const;
    bool isComplete    ()                    const;

    // Statistics
    uint32_t getTotalWarpsDispatched()  const { return total_warps_dispatched_;  }
    uint32_t getTotalKernelsCompleted() const { return total_kernels_completed_; }
    uint32_t getNextWarpId()          const { return kernel_warp_counter_; }   // Phase 11

private:
    void scheduleProcess();
    void balanceLoad();

    Config config_;

    // Per-CU warp queues
    std::vector<std::queue<WarpID>> ready_queues_;
    std::vector<std::queue<WarpID>> stalled_queues_;

    std::vector<uint32_t> round_robin_indices_;
    uint32_t kernel_warp_counter_    = 0;
    uint32_t total_warps_dispatched_ = 0;
    uint32_t total_kernels_completed_= 0;

    // Policy helpers
    WarpID selectWarpRoundRobin (ComputeUnitID cu_id);
    WarpID selectWarpPriority   (ComputeUnitID cu_id);
    WarpID selectWarpFIFO       (ComputeUnitID cu_id);

    void generateWarps         (GridID grid_id, uint32_t grid_x, uint32_t grid_y);
    void balanceLoadAcrossUnits();
};

}  // riscv_gpgpu

#endif  // RISCV_GPGPU_WARP_SCHEDULER_H