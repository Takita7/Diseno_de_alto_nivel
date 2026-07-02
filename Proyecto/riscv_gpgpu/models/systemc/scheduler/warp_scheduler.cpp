// warp_scheduler.cpp - WarpScheduler implementation

#include "warp_scheduler.h"
#include "../common/logging.h"
#include <sstream>

namespace riscv_gpgpu {

WarpScheduler::WarpScheduler(sc_core::sc_module_name name, const Config& config)
    : sc_core::sc_module(name), config_(config),
      kernel_warp_counter_(0),
      total_warps_dispatched_(0),
      total_kernels_completed_(0) {

    ready_queues_.resize(config.num_compute_units);
    stalled_queues_.resize(config.num_compute_units);
    round_robin_indices_.resize(config.num_compute_units, 0);

    SC_METHOD(scheduleProcess);
    sensitive << clk.pos();

    LOG_INFO("WarpScheduler initialised (" +
             std::to_string(config.num_compute_units) + " CUs, policy=" +
             (config.policy == SchedulingPolicy::ROUND_ROBIN ? "RR" :
              config.policy == SchedulingPolicy::PRIORITY    ? "PRI" : "FIFO") + ")");
}

WarpScheduler::~WarpScheduler() = default;

void WarpScheduler::submitKernel(GridID grid_id, uint32_t grid_x, uint32_t grid_y) {
    generateWarps(grid_id, grid_x, grid_y);
}

WarpID WarpScheduler::selectWarp(ComputeUnitID cu_id) {
    switch (config_.policy) {
    case SchedulingPolicy::PRIORITY: return selectWarpPriority(cu_id);
    case SchedulingPolicy::FIFO:     return selectWarpFIFO(cu_id);
    default:                         return selectWarpRoundRobin(cu_id);
    }
}

void WarpScheduler::markWarpComplete(ComputeUnitID cu_id, WarpID warp_id) {
    total_warps_dispatched_++;
}

void WarpScheduler::markWarpStalled(ComputeUnitID cu_id, WarpID warp_id) {
    if (cu_id < stalled_queues_.size()) stalled_queues_[cu_id].push(warp_id);
}

bool WarpScheduler::hasReadyWarps(ComputeUnitID cu_id) const {
    if (cu_id >= ready_queues_.size()) return false;
    return !ready_queues_[cu_id].empty();
}

bool WarpScheduler::isComplete() const {
    for (const auto& q : ready_queues_)  if (!q.empty()) return false;
    for (const auto& q : stalled_queues_) if (!q.empty()) return false;
    return true;
}

void WarpScheduler::scheduleProcess() { balanceLoad(); }

void WarpScheduler::balanceLoad() { balanceLoadAcrossUnits(); }

WarpID WarpScheduler::selectWarpRoundRobin(ComputeUnitID cu_id) {
    if (cu_id >= ready_queues_.size() || ready_queues_[cu_id].empty()) return 0;
    WarpID w = ready_queues_[cu_id].front();
    ready_queues_[cu_id].pop();
    return w;
}

WarpID WarpScheduler::selectWarpPriority(ComputeUnitID cu_id) {
    return selectWarpRoundRobin(cu_id);
}

WarpID WarpScheduler::selectWarpFIFO(ComputeUnitID cu_id) {
    return selectWarpRoundRobin(cu_id);
}

void WarpScheduler::generateWarps(GridID grid_id, uint32_t grid_x, uint32_t grid_y) {
    uint32_t total_blocks = grid_x * grid_y;
    for (uint32_t b = 0; b < total_blocks; ++b) {
        ComputeUnitID cu = b % config_.num_compute_units;
        ready_queues_[cu].push(static_cast<WarpID>(kernel_warp_counter_++));
    }
}

void WarpScheduler::balanceLoadAcrossUnits() {
    // Simple load balancing: move warps from overloaded to underloaded CUs
    for (size_t i = 0; i < ready_queues_.size(); ++i) {
        if (ready_queues_[i].size() > 4) {
            for (size_t j = 0; j < ready_queues_.size(); ++j) {
                if (ready_queues_[j].size() < 2 && !ready_queues_[i].empty()) {
                    ready_queues_[j].push(ready_queues_[i].front());
                    ready_queues_[i].pop();
                }
            }
        }
    }
}

} // namespace riscv_gpgpu
