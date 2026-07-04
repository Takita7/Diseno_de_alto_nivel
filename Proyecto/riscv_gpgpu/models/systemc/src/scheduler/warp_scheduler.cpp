// warp_scheduler.cpp – Phase 0 stub. Phase 2 adds real scheduling logic.

#include "warp_scheduler.h"
#include "../common/logging.h"

namespace riscv_gpgpu {

WarpScheduler::WarpScheduler(sc_core::sc_module_name name, const Config& config)
    : sc_core::sc_module(name), config_(config),
      kernel_warp_counter_(0),
      total_warps_dispatched_(0),
      total_kernels_completed_(0)
{
    ready_queues_.resize(config_.num_compute_units);
    stalled_queues_.resize(config_.num_compute_units);
    round_robin_indices_.resize(config_.num_compute_units, 0);
    LOG_INFO("WarpScheduler initialized (stub)");
}

WarpScheduler::~WarpScheduler() {}

void WarpScheduler::submitKernel(GridID, uint32_t, uint32_t) {}

WarpID WarpScheduler::selectWarp(ComputeUnitID)          { return 0; }
void   WarpScheduler::markWarpComplete(ComputeUnitID, WarpID) {}
void   WarpScheduler::markWarpStalled(ComputeUnitID, WarpID)  {}
bool   WarpScheduler::hasReadyWarps(ComputeUnitID) const  { return false; }
bool   WarpScheduler::isComplete()                 const  { return true;  }

void   WarpScheduler::scheduleProcess()   {}
void   WarpScheduler::balanceLoad()       {}
void   WarpScheduler::balanceLoadAcrossUnits() {}

WarpID WarpScheduler::selectWarpRoundRobin(ComputeUnitID) { return 0; }
WarpID WarpScheduler::selectWarpPriority(ComputeUnitID)   { return 0; }
WarpID WarpScheduler::selectWarpFIFO(ComputeUnitID)       { return 0; }
void   WarpScheduler::generateWarps(GridID, uint32_t, uint32_t) {}

}  // namespace riscv_gpgpu