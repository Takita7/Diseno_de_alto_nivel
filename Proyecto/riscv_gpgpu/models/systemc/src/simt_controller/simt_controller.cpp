// simt_controller.cpp – Phase 0 stub. Phase 3 adds divergence/barrier logic.

#include "simt_controller.h"
#include "../common/logging.h"

namespace riscv_gpgpu {

SIMTController::SIMTController(sc_core::sc_module_name name, const Config& config)
    : sc_core::sc_module(name), config_(config)
{
    LOG_INFO("SIMTController initialized (stub)");
}

SIMTController::~SIMTController() {}

void SIMTController::initializeWarp(WarpID warp_id, uint32_t threads_per_warp) {
    // Grow internal vectors to fit this warp_id if needed
    if (warp_id >= active_masks_.size()) {
        active_masks_.resize(warp_id + 1, 0);
        threads_per_warp_.resize(warp_id + 1, 0);
        divergence_stacks_.resize(warp_id + 1);
    }
    threads_per_warp_[warp_id] = threads_per_warp;
    // All threads active by default: set the lowest N bits
    active_masks_[warp_id] = (threads_per_warp == 32)
                             ? 0xFFFFFFFF
                             : (1u << threads_per_warp) - 1u;
}

void SIMTController::handleBranch(WarpID, bool*) {
    // Phase 3: split active mask into taken/not-taken paths
}

void SIMTController::handleJoin(WarpID) {
    // Phase 3: pop divergence stack and merge masks
}

uint32_t SIMTController::getActiveMask(WarpID warp_id) const {
    if (warp_id >= active_masks_.size()) return 0;
    return active_masks_[warp_id];
}

bool SIMTController::isThreadActive(WarpID warp_id, ThreadID thread_id) const {
    if (warp_id >= active_masks_.size()) return false;
    return (active_masks_[warp_id] >> thread_id) & 1u;
}

void SIMTController::computeActiveMask(WarpID, const bool*) {}
void SIMTController::pushDivergenceState(WarpID, uint32_t)  {}
void SIMTController::popDivergenceState(WarpID)             {}

}  // namespace riscv_gpgpu