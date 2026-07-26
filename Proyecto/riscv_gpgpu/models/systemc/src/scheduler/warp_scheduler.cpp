// warp_scheduler.cpp – Phase 2: real scheduling logic

#include "warp_scheduler.h"
#include "../common/logging.h"

namespace riscv_gpgpu {

// ── Constructor / destructor ──────────────────────────────────────────────────

WarpScheduler::WarpScheduler(sc_core::sc_module_name name, const Config& config)
    : sc_core::sc_module(name), config_(config)
{
    ready_queues_.resize(config_.num_compute_units);
    stalled_queues_.resize(config_.num_compute_units);
    round_robin_indices_.resize(config_.num_compute_units, 0);

    LOG_INFO("WarpScheduler initialized: "
             + std::to_string(config_.num_compute_units) + " CU(s), policy="
             + (config_.policy == SchedulingPolicy::ROUND_ROBIN ? "ROUND_ROBIN"
              : config_.policy == SchedulingPolicy::FIFO         ? "FIFO"
                                                                 : "PRIORITY"));
}

WarpScheduler::~WarpScheduler() {}

// ── Kernel submission ─────────────────────────────────────────────────────────

void WarpScheduler::submitKernel(GridID grid_id,
                                  uint32_t grid_x, uint32_t grid_y) {
    LOG_INFO("WarpScheduler: submitKernel grid="
             + std::to_string(grid_x) + "x" + std::to_string(grid_y)
             + "  total=" + std::to_string(grid_x * grid_y) + " warps");
    generateWarps(grid_id, grid_x, grid_y);
    balanceLoadAcrossUnits();
}

void WarpScheduler::generateWarps(GridID /*grid_id*/,
                                   uint32_t grid_x, uint32_t grid_y) {
    uint32_t total = grid_x * grid_y;
    for (uint32_t i = 0; i < total; ++i) {
        WarpID       warp_id = kernel_warp_counter_++;
        ComputeUnitID cu_id  = i % config_.num_compute_units;
        ready_queues_[cu_id].push(warp_id);
        LOG_DEBUG("  Warp " + std::to_string(warp_id)
                  + " → CU "  + std::to_string(cu_id));
    }
}

// ── Warp selection ────────────────────────────────────────────────────────────

WarpID WarpScheduler::selectWarp(ComputeUnitID cu_id) {
    if (cu_id >= config_.num_compute_units) return INVALID_WARP_ID;

    WarpID warp_id = INVALID_WARP_ID;
    switch (config_.policy) {
        case SchedulingPolicy::ROUND_ROBIN:
            warp_id = selectWarpRoundRobin(cu_id); break;
        case SchedulingPolicy::FIFO:
            warp_id = selectWarpFIFO(cu_id);       break;
        case SchedulingPolicy::PRIORITY:
            warp_id = selectWarpPriority(cu_id);   break;
    }

    if (warp_id != INVALID_WARP_ID) {
        ++total_warps_dispatched_;
        LOG_DEBUG("WarpScheduler: dispatch warp " + std::to_string(warp_id)
                  + " from CU " + std::to_string(cu_id));
    }
    return warp_id;
}

// Round-robin and FIFO: pop from the front of the ready queue.
// The distinction between policies becomes meaningful in Phase 4 when
// warps carry priority metadata.
WarpID WarpScheduler::selectWarpRoundRobin(ComputeUnitID cu_id) {
    if (ready_queues_[cu_id].empty()) return INVALID_WARP_ID;
    WarpID id = ready_queues_[cu_id].front();
    ready_queues_[cu_id].pop();
    round_robin_indices_[cu_id] =
        (round_robin_indices_[cu_id] + 1) % config_.max_warps_per_cu;
    return id;
}

WarpID WarpScheduler::selectWarpFIFO(ComputeUnitID cu_id) {
    return selectWarpRoundRobin(cu_id);   // same queue structure
}

WarpID WarpScheduler::selectWarpPriority(ComputeUnitID cu_id) {
    // Phase 2: fall back to round-robin (no priority metadata yet)
    return selectWarpRoundRobin(cu_id);
}

// ── Warp state updates ────────────────────────────────────────────────────────

void WarpScheduler::markWarpComplete(ComputeUnitID cu_id, WarpID warp_id) {
    ++total_kernels_completed_;
    LOG_DEBUG("WarpScheduler: warp " + std::to_string(warp_id)
              + " on CU " + std::to_string(cu_id) + " COMPLETE");
}

void WarpScheduler::markWarpStalled(ComputeUnitID cu_id, WarpID warp_id) {
    if (cu_id >= config_.num_compute_units) return;
    stalled_queues_[cu_id].push(warp_id);
    LOG_DEBUG("WarpScheduler: warp " + std::to_string(warp_id)
              + " on CU " + std::to_string(cu_id) + " STALLED");
}

// ── Status queries ────────────────────────────────────────────────────────────

bool WarpScheduler::hasReadyWarps(ComputeUnitID cu_id) const {
    if (cu_id >= config_.num_compute_units) return false;
    return !ready_queues_[cu_id].empty();
}

bool WarpScheduler::isComplete() const {
    for (uint32_t i = 0; i < config_.num_compute_units; ++i) {
        if (!ready_queues_[i].empty() || !stalled_queues_[i].empty())
            return false;
    }
    return true;
}

// ── Load balancing ────────────────────────────────────────────────────────────

void WarpScheduler::balanceLoadAcrossUnits() {
    if (config_.num_compute_units < 2) return;

    // Iteratively move warps from the busiest to the most idle CU
    // until all queues differ by at most one warp.
    bool moved = true;
    while (moved) {
        moved = false;
        uint32_t max_cu = 0, min_cu = 0;
        for (uint32_t i = 1; i < config_.num_compute_units; ++i) {
            if (ready_queues_[i].size() > ready_queues_[max_cu].size()) max_cu = i;
            if (ready_queues_[i].size() < ready_queues_[min_cu].size()) min_cu = i;
        }
        if (ready_queues_[max_cu].size() > ready_queues_[min_cu].size() + 1) {
            WarpID warp = ready_queues_[max_cu].front();
            ready_queues_[max_cu].pop();
            ready_queues_[min_cu].push(warp);
            LOG_DEBUG("WarpScheduler: rebalanced warp " + std::to_string(warp)
                      + " from CU " + std::to_string(max_cu)
                      + " → CU "    + std::to_string(min_cu));
            moved = true;
        }
    }
}

void WarpScheduler::balanceLoad()    { balanceLoadAcrossUnits(); }
void WarpScheduler::scheduleProcess() { /* Phase 5: SC_THREAD registration */ }

}  // namespace riscv_gpgpu