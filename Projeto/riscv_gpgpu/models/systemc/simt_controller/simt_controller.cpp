// simt_controller.cpp - SIMT controller implementation
//

#include "simt_controller.h"
#include "../common/logging.h"

namespace riscv_gpgpu {

SIMTController::SIMTController(sc_core::sc_module_name name, const Config& config)
    : sc_core::sc_module(name),
      config_(config),
      divergence_events_(0),
      wasted_cycles_(0) {
    
    LOG_INFO("SIMTController initialized");
}

SIMTController::~SIMTController() {
    LOG_INFO("SIMTController destroyed");
}

void SIMTController::initializeWarp(WarpID warp_id, uint32_t threads_per_warp) {
    threads_per_warp_.push_back(threads_per_warp);
    active_masks_.push_back((1ULL << threads_per_warp) - 1);  // All threads active
    divergence_stacks_.resize(warp_id + 1);
}

void SIMTController::handleBranch(WarpID warp_id, bool* thread_conditions) {
    if (warp_id >= active_masks_.size()) {
        return;
    }
    
    uint32_t current_mask = active_masks_[warp_id];
    uint32_t new_mask = 0;
    
    // Compute which threads take each branch
    for (uint32_t i = 0; i < threads_per_warp_[warp_id]; ++i) {
        if ((current_mask & (1 << i)) && thread_conditions[i]) {
            new_mask |= (1 << i);
        }
    }
    
    // Check for divergence
    if (new_mask != current_mask && new_mask != 0) {
        divergence_events_++;
        LOG_INFO("Divergence detected");
    }
    
    pushDivergenceState(warp_id, active_masks_[warp_id]);
    active_masks_[warp_id] = new_mask;
}

void SIMTController::handleJoin(WarpID warp_id) {
    if (warp_id >= active_masks_.size()) {
        return;
    }
    
    popDivergenceState(warp_id);
}

uint32_t SIMTController::getActiveMask(WarpID warp_id) const {
    if (warp_id >= active_masks_.size()) {
        return 0;
    }
    return active_masks_[warp_id];
}

bool SIMTController::isThreadActive(WarpID warp_id, ThreadID thread_id) const {
    if (warp_id >= active_masks_.size() || thread_id >= 32) {
        return false;
    }
    return (active_masks_[warp_id] & (1 << thread_id)) != 0;
}

void SIMTController::computeActiveMask(WarpID warp_id, const bool* conditions) {
    if (warp_id >= active_masks_.size()) {
        return;
    }
    
    uint32_t current_mask = active_masks_[warp_id];
    uint32_t new_mask = 0;
    
    for (uint32_t i = 0; i < threads_per_warp_[warp_id]; ++i) {
        if ((current_mask & (1 << i)) && conditions[i]) {
            new_mask |= (1 << i);
        }
    }
    
    active_masks_[warp_id] = new_mask;
}

void SIMTController::pushDivergenceState(WarpID warp_id, uint32_t mask) {
    if (warp_id >= divergence_stacks_.size()) {
        divergence_stacks_.resize(warp_id + 1);
    }
    
    auto& stack = divergence_stacks_[warp_id];
    if (stack.mask_stack.size() < config_.max_history_depth) {
        stack.mask_stack.push(mask);
    }
}

void SIMTController::popDivergenceState(WarpID warp_id) {
    if (warp_id >= divergence_stacks_.size()) {
        return;
    }
    
    auto& stack = divergence_stacks_[warp_id];
    if (!stack.mask_stack.empty()) {
        active_masks_[warp_id] = stack.mask_stack.top();
        stack.mask_stack.pop();
    }
}

}  // namespace riscv_gpgpu
