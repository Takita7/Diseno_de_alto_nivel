// simt_controller.cpp - SIMT divergence and reconvergence controller

#include "simt_controller.h"
#include "../common/logging.h"

namespace riscv_gpgpu {

SIMTController::SIMTController(sc_core::sc_module_name name, const Config& config)
    : sc_core::sc_module(name), config_(config),
      divergence_events_(0), wasted_cycles_(0) {
    LOG_INFO("SIMTController initialised");
}

SIMTController::~SIMTController() = default;

void SIMTController::initializeWarp(WarpID warp_id, uint32_t threads_per_warp) {
    if (warp_id >= active_masks_.size()) {
        active_masks_.resize(warp_id + 1, 0xFFFFFFFF);
        threads_per_warp_.resize(warp_id + 1, threads_per_warp);
        divergence_stacks_.resize(warp_id + 1);
    }
    // Activate all threads
    uint32_t mask = (threads_per_warp >= 32) ? 0xFFFFFFFF
                  : (1u << threads_per_warp) - 1u;
    active_masks_[warp_id] = mask;
}

void SIMTController::handleBranch(WarpID warp_id, bool* thread_conditions) {
    if (warp_id >= active_masks_.size()) return;
    pushDivergenceState(warp_id, active_masks_[warp_id]);
    computeActiveMask(warp_id, thread_conditions);
    divergence_events_++;
}

void SIMTController::handleJoin(WarpID warp_id) {
    if (warp_id >= active_masks_.size()) return;
    popDivergenceState(warp_id);
}

uint32_t SIMTController::getActiveMask(WarpID warp_id) const {
    if (warp_id >= active_masks_.size()) return 0xFFFFFFFF;
    return active_masks_[warp_id];
}

bool SIMTController::isThreadActive(WarpID warp_id, ThreadID thread_id) const {
    uint32_t mask = getActiveMask(warp_id);
    return (mask >> thread_id) & 1u;
}

void SIMTController::computeActiveMask(WarpID warp_id, const bool* conditions) {
    uint32_t new_mask = 0;
    uint32_t nthr = threads_per_warp_[warp_id];
    for (uint32_t t = 0; t < nthr && t < 32; ++t)
        if (conditions[t]) new_mask |= (1u << t);
    active_masks_[warp_id] = new_mask;
}

void SIMTController::pushDivergenceState(WarpID warp_id, uint32_t mask) {
    divergence_stacks_[warp_id].mask_stack.push(mask);
}

void SIMTController::popDivergenceState(WarpID warp_id) {
    auto& stack = divergence_stacks_[warp_id];
    if (!stack.mask_stack.empty()) {
        active_masks_[warp_id] = stack.mask_stack.top();
        stack.mask_stack.pop();
    }
}

} // namespace riscv_gpgpu
