// warp_scheduler.cpp - Warp scheduler implementation
//
// Manages warp dispatch and scheduling across compute units
//

#include "warp_scheduler.h"
#include "../common/logging.h"
#include <algorithm>

namespace riscv_gpgpu {

WarpScheduler::WarpScheduler(sc_core::sc_module_name name, const Config& config)
    : sc_core::sc_module(name),
      config_(config),
      kernel_warp_counter_(0),
      total_warps_dispatched_(0),
      total_kernels_completed_(0) {
    
    // Initialize per-CU queues
    ready_queues_.resize(config.num_compute_units);
    stalled_queues_.resize(config.num_compute_units);
    round_robin_indices_.resize(config.num_compute_units, 0);
    
    SC_METHOD(scheduleProcess);
    sensitive << clk.pos();
}

WarpScheduler::~WarpScheduler() {
    LOG_INFO("WarpScheduler destroyed");
}

void WarpScheduler::submitKernel(GridID grid_id, uint32_t grid_x, uint32_t grid_y) {
    LOG_INFO("Submitting kernel grid");
    generateWarps(grid_id, grid_x, grid_y);
}

WarpID WarpScheduler::selectWarp(ComputeUnitID cu_id) {
    if (cu_id >= config_.num_compute_units) {
        return 0;  // Invalid CU
    }
    
    switch (config_.policy) {
        case SchedulingPolicy::ROUND_ROBIN:
            return selectWarpRoundRobin(cu_id);
        case SchedulingPolicy::PRIORITY:
            return selectWarpPriority(cu_id);
        case SchedulingPolicy::FIFO:
            return selectWarpFIFO(cu_id);
        default:
            return selectWarpFIFO(cu_id);
    }
}

WarpID WarpScheduler::selectWarpRoundRobin(ComputeUnitID cu_id) {
    auto& queue = ready_queues_[cu_id];
    
    if (queue.empty()) {
        return 0;  // No ready warps
    }
    
    // Move to next index for round-robin
    uint32_t& index = round_robin_indices_[cu_id];
    index = (index + 1) % queue.size();
    
    // Get warp at current index
    auto it = queue.begin();
    std::advance(it, index);
    WarpID warp = *it;
    
    // Remove from queue
    queue.erase(it);
    total_warps_dispatched_++;
    
    return warp;
}

WarpID WarpScheduler::selectWarpFIFO(ComputeUnitID cu_id) {
    auto& queue = ready_queues_[cu_id];
    
    if (queue.empty()) {
        return 0;  // No ready warps
    }
    
    WarpID warp = queue.front();
    queue.pop();
    total_warps_dispatched_++;
    
    return warp;
}

WarpID WarpScheduler::selectWarpPriority(ComputeUnitID cu_id) {
    // Placeholder: currently same as FIFO
    // Could be extended with priority-based scheduling
    return selectWarpFIFO(cu_id);
}

void WarpScheduler::markWarpComplete(ComputeUnitID cu_id, WarpID warp_id) {
    LOG_INFO("Warp complete");
    total_kernels_completed_++;
}

void WarpScheduler::markWarpStalled(ComputeUnitID cu_id, WarpID warp_id) {
    if (cu_id >= config_.num_compute_units) {
        return;
    }
    
    stalled_queues_[cu_id].push(warp_id);
}

bool WarpScheduler::hasReadyWarps(ComputeUnitID cu_id) const {
    if (cu_id >= config_.num_compute_units) {
        return false;
    }
    return !ready_queues_[cu_id].empty();
}

bool WarpScheduler::isComplete() const {
    // Check if all queues are empty
    for (const auto& queue : ready_queues_) {
        if (!queue.empty()) {
            return false;
        }
    }
    for (const auto& queue : stalled_queues_) {
        if (!queue.empty()) {
            return false;
        }
    }
    return true;
}

void WarpScheduler::scheduleProcess() {
    while (true) {
        wait();  // Wait for clock edge
        
        if (config_.enable_optimization) {
            balanceLoadAcrossUnits();
        }
    }
}

void WarpScheduler::generateWarps(GridID grid_id, uint32_t grid_x, uint32_t grid_y) {
    // Generate warps from grid configuration
    // Each block generates a set of warps
    uint32_t total_blocks = grid_x * grid_y;
    uint32_t warps_per_cu = config_.max_warps_per_cu;
    
    // Distribute warps across compute units
    for (uint32_t cu = 0; cu < config_.num_compute_units; ++cu) {
        uint32_t warps_for_cu = std::min(warps_per_cu, 
                                         (total_blocks * 32) / config_.num_compute_units);
        
        for (uint32_t i = 0; i < warps_for_cu; ++i) {
            WarpID warp = kernel_warp_counter_++;
            ready_queues_[cu].push(warp);
        }
    }
}

void WarpScheduler::balanceLoadAcrossUnits() {
    // Move stalled warps back to ready queue when resources available
    for (uint32_t cu = 0; cu < config_.num_compute_units; ++cu) {
        if (!stalled_queues_[cu].empty() && ready_queues_[cu].size() < config_.batch_size) {
            WarpID warp = stalled_queues_[cu].front();
            stalled_queues_[cu].pop();
            ready_queues_[cu].push(warp);
        }
    }
}

}  // namespace riscv_gpgpu
