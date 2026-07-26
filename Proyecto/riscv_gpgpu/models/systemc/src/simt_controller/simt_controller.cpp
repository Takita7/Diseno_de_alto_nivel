// simt_controller.cpp
//

#include "simt_controller.h"
#include "../common/logging.h"
#include <bitset>
#include <sstream>

namespace riscv_gpgpu {

SIMTController::SIMTController(sc_core::sc_module_name name, const Config& config)
    : sc_core::sc_module(name), config_(config)
{
    LOG_INFO("SIMTController initialized");
}

SIMTController::~SIMTController() {}

void SIMTController::ensureWarpExists(WarpID warp_id) {
    if (warp_id >= active_masks_.size()) {
        active_masks_.resize(warp_id + 1, 0);
        threads_per_warp_.resize(warp_id + 1, 0);
        divergence_stacks_.resize(warp_id + 1);
    }
}

void SIMTController::initializeWarp(WarpID warp_id, uint32_t threads_per_warp) {
    ensureWarpExists(warp_id);
    threads_per_warp_[warp_id] = threads_per_warp;
    active_masks_[warp_id]     = (threads_per_warp == 32)
                                  ? 0xFFFFFFFFu
                                  : (1u << threads_per_warp) - 1u;
    LOG_DEBUG("SIMTController: warp " + std::to_string(warp_id)
              + " initialized, " + std::to_string(threads_per_warp) + " threads");
}

// ── handleBranch ──────────────────────────────────────────────────────────────
//
// thread_conditions[t]:
//   true  → thread t falls through (stays active in fall-through block)
//   false → thread t jumps (masked until VJOIN)
//
// Three cases (only active threads considered):
//
//   1. taken != 0 && not_taken != 0 → real divergence: some fall through,
//      some masked.  Push not_taken to IPDOM stack, set mask to taken.
//
//   2. taken == 0 && not_taken != 0 → all threads jump (none fall through).
//      Fall-through block runs with mask = 0 (no side effects).
//      Push not_taken to stack so VJOIN restores them.
//      NOT counted as a divergence event (all threads agreed).   ← BUG FIX
//
//   3. taken != 0 && not_taken == 0 → all threads fall through.
//      No masking, no stack push, just update mask.
//
void SIMTController::handleBranch(WarpID warp_id, bool* thread_conditions) {
    ensureWarpExists(warp_id);

    uint32_t current   = active_masks_[warp_id];
    uint32_t tpw       = threads_per_warp_[warp_id];
    uint32_t taken     = 0;
    uint32_t not_taken = 0;

    for (uint32_t t = 0; t < tpw; ++t) {
        if ((current >> t) & 1u) {
            if (thread_conditions[t]) taken     |= (1u << t);
            else                      not_taken |= (1u << t);
        }
    }

    if (taken != 0 && not_taken != 0) {
        // Case 1: real divergence
        pushDivergenceState(warp_id, not_taken);
        computeActiveMask(warp_id, thread_conditions);
        active_masks_[warp_id] = taken;
        ++divergence_events_;
        wasted_cycles_ += static_cast<uint32_t>(std::bitset<32>(not_taken).count());
        LOG_DEBUG("SIMTController: warp " + std::to_string(warp_id) + " DIVERGED");

    } else if (taken == 0 && not_taken != 0) {
        // Case 2: all threads jump – fall-through block runs with mask = 0.
        // Push all threads to IPDOM stack; VJOIN will restore them.
        // Not counted as divergence_events_ (no disagreement between threads).
        pushDivergenceState(warp_id, not_taken);
        active_masks_[warp_id] = 0;
        LOG_DEBUG("SIMTController: warp " + std::to_string(warp_id)
                  + " all-jump (fall-through masked)");

    } else {
        // Case 3: all threads fall through (or mask already 0)
        active_masks_[warp_id] = (taken != 0) ? taken : not_taken;
        LOG_DEBUG("SIMTController: warp " + std::to_string(warp_id)
                  + " branch – no divergence");
    }
}

void SIMTController::handleJoin(WarpID warp_id) {
    ensureWarpExists(warp_id);
    auto& ds = divergence_stacks_[warp_id];
    if (ds.mask_stack.empty()) {
        LOG_DEBUG("SIMTController: handleJoin on warp "
                    + std::to_string(warp_id) + " with empty stack – ignored");
        return;
    }
    uint32_t not_taken = ds.mask_stack.top();
    popDivergenceState(warp_id);
    active_masks_[warp_id] |= not_taken;
    LOG_DEBUG("SIMTController: warp " + std::to_string(warp_id) + " RECONVERGED");
}

uint32_t SIMTController::getActiveMask(WarpID warp_id) const {
    if (warp_id >= active_masks_.size()) return 0;
    return active_masks_[warp_id];
}

bool SIMTController::isThreadActive(WarpID warp_id, ThreadID thread_id) const {
    if (warp_id >= active_masks_.size()) return false;
    return (active_masks_[warp_id] >> thread_id) & 1u;
}

bool SIMTController::hasPendingDivergence(WarpID warp_id) const {
    if (warp_id >= divergence_stacks_.size()) return false;
    return !divergence_stacks_[warp_id].mask_stack.empty();
}

void SIMTController::threadHitBarrier(WarpID warp_id, uint32_t barrier_id) {
    barrier_table_[barrier_id].insert(warp_id);
    LOG_DEBUG("SIMTController: warp " + std::to_string(warp_id)
              + " hit barrier " + std::to_string(barrier_id)
              + "  (" + std::to_string(barrier_table_[barrier_id].size())
              + " warps waiting)");
}

bool SIMTController::allWarpsAtBarrier(uint32_t barrier_id,
                                        uint32_t total_warps) const {
    auto it = barrier_table_.find(barrier_id);
    if (it == barrier_table_.end()) return false;
    return it->second.size() >= static_cast<size_t>(total_warps);
}

void SIMTController::clearBarrier(uint32_t barrier_id) {
    barrier_table_.erase(barrier_id);
    LOG_DEBUG("SIMTController: barrier " + std::to_string(barrier_id) + " cleared");
}

void SIMTController::computeActiveMask(WarpID warp_id, const bool* conditions) {
    uint32_t tpw = threads_per_warp_[warp_id];
    auto& ds = divergence_stacks_[warp_id];
    ds.thread_masks.resize(tpw);
    for (uint32_t t = 0; t < tpw; ++t)
        ds.thread_masks[t] = conditions[t] ? 1u : 0u;
}

void SIMTController::pushDivergenceState(WarpID warp_id, uint32_t not_taken_mask) {
    divergence_stacks_[warp_id].mask_stack.push(not_taken_mask);
    divergence_stacks_[warp_id].pc_stack.push(0);
}

void SIMTController::popDivergenceState(WarpID warp_id) {
    auto& ds = divergence_stacks_[warp_id];
    if (!ds.mask_stack.empty()) {
        ds.mask_stack.pop();
        ds.pc_stack.pop();
    }
}

}  // riscv_gpgpu
