// simt_controller.cpp – Phase 3: divergence, IPDOM stack, reconvergence
//
// Divergence model (Ventus / IPDOM style):
//   handleBranch → splits active mask into taken / not-taken paths.
//                  Pushes the not-taken mask onto the IPDOM stack,
//                  sets active_mask to the taken path.
//   handleJoin   → pops the not-taken mask and ORs it back into
//                  active_mask (all threads reunite at the IPDOM).
//
// "Wasted cycles" are approximated as the number of masked-off thread
// lanes per divergence event (popcount of the not-taken mask).
//

#include "simt_controller.h"
#include "../common/logging.h"
#include <bitset>
#include <sstream>

namespace riscv_gpgpu {

// ── Constructor / destructor ──────────────────────────────────────────────────

SIMTController::SIMTController(sc_core::sc_module_name name, const Config& config)
    : sc_core::sc_module(name), config_(config)
{
    LOG_INFO("SIMTController initialized");
}

SIMTController::~SIMTController() {}

// ── Internal helper ───────────────────────────────────────────────────────────

void SIMTController::ensureWarpExists(WarpID warp_id) {
    if (warp_id >= active_masks_.size()) {
        active_masks_.resize(warp_id + 1, 0);
        threads_per_warp_.resize(warp_id + 1, 0);
        divergence_stacks_.resize(warp_id + 1);
    }
}

// ── Public interface ──────────────────────────────────────────────────────────

void SIMTController::initializeWarp(WarpID warp_id, uint32_t threads_per_warp) {
    ensureWarpExists(warp_id);
    threads_per_warp_[warp_id] = threads_per_warp;
    // All threads active: set the lowest threads_per_warp bits
    active_masks_[warp_id] = (threads_per_warp == 32)
                              ? 0xFFFFFFFFu
                              : (1u << threads_per_warp) - 1u;

    LOG_DEBUG("SIMTController: warp " + std::to_string(warp_id)
              + " initialized, " + std::to_string(threads_per_warp)
              + " threads, mask=0x" + [&]{
                  std::ostringstream ss;
                  ss << std::hex << active_masks_[warp_id];
                  return ss.str(); }());
}

void SIMTController::handleBranch(WarpID warp_id, bool* thread_conditions) {
    ensureWarpExists(warp_id);

    uint32_t current  = active_masks_[warp_id];
    uint32_t tpw      = threads_per_warp_[warp_id];
    uint32_t taken     = 0;
    uint32_t not_taken = 0;

    // Only consider threads that are currently active
    for (uint32_t t = 0; t < tpw; ++t) {
        if ((current >> t) & 1u) {
            if (thread_conditions[t]) taken     |= (1u << t);
            else                       not_taken |= (1u << t);
        }
    }

    if (taken != 0 && not_taken != 0) {
        // Real divergence: execute taken path first, save not-taken for later
        pushDivergenceState(warp_id, not_taken);
        computeActiveMask(warp_id, thread_conditions);
        active_masks_[warp_id] = taken;

        ++divergence_events_;
        wasted_cycles_ += static_cast<uint32_t>(
            std::bitset<32>(not_taken).count());

        LOG_DEBUG("SIMTController: warp " + std::to_string(warp_id)
                  + " DIVERGED – taken=0x" + [&]{
                      std::ostringstream ss; ss << std::hex << taken; return ss.str(); }()
                  + " not_taken=0x" + [&]{
                      std::ostringstream ss; ss << std::hex << not_taken; return ss.str(); }());
    } else {
        // All active threads agree → no divergence, no stack push
        active_masks_[warp_id] = (taken != 0) ? taken : not_taken;
        LOG_DEBUG("SIMTController: warp " + std::to_string(warp_id)
                  + " branch – no divergence");
    }
}

void SIMTController::handleJoin(WarpID warp_id) {
    ensureWarpExists(warp_id);

    auto& ds = divergence_stacks_[warp_id];
    if (ds.mask_stack.empty()) {
        LOG_WARNING("SIMTController: handleJoin on warp "
                    + std::to_string(warp_id) + " with empty stack – ignored");
        return;
    }

    uint32_t not_taken = ds.mask_stack.top();
    popDivergenceState(warp_id);

    // Reunite: OR the not-taken threads back into the active mask
    active_masks_[warp_id] |= not_taken;

    LOG_DEBUG("SIMTController: warp " + std::to_string(warp_id)
              + " RECONVERGED – mask=0x" + [&]{
                  std::ostringstream ss;
                  ss << std::hex << active_masks_[warp_id];
                  return ss.str(); }());
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

// ── Private helpers ───────────────────────────────────────────────────────────

void SIMTController::computeActiveMask(WarpID warp_id, const bool* conditions) {
    uint32_t tpw = threads_per_warp_[warp_id];
    auto& ds = divergence_stacks_[warp_id];
    ds.thread_masks.resize(tpw);
    for (uint32_t t = 0; t < tpw; ++t) {
        ds.thread_masks[t] = conditions[t] ? 1u : 0u;
    }
}

void SIMTController::pushDivergenceState(WarpID warp_id, uint32_t not_taken_mask) {
    divergence_stacks_[warp_id].mask_stack.push(not_taken_mask);
    divergence_stacks_[warp_id].pc_stack.push(0);  // IPDOM PC – Phase 4
}

void SIMTController::popDivergenceState(WarpID warp_id) {
    auto& ds = divergence_stacks_[warp_id];
    if (!ds.mask_stack.empty()) {
        ds.mask_stack.pop();
        ds.pc_stack.pop();
    }
}

}  // namespace riscv_gpgpu