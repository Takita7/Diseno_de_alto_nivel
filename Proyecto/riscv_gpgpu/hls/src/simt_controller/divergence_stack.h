// divergence_stack.h - HLS-synthesizable SIMT divergence/reconvergence state
//
// Golden reference: models/systemc/src/simt_controller/simt_controller.{h,cpp}.
//
// Scope narrowed vs. the golden SIMTController by design, not by omission:
//
// 1. Per-WARP, not per-warp-indexed-vector. The golden SIMTController holds
//    active_masks_[warp_id]/divergence_stacks_[warp_id] etc. for every warp a
//    CU has ever seen (std::vector, grows via ensureWarpExists()) because one
//    SIMTController instance served an entire ComputeUnit across many warps
//    over time. compute_pipeline (docs/hls/interfaces.md SS2) is invoked once
//    PER WARP, so there is only ever one warp's divergence state live at a
//    time - a single DivergenceStack instance, constructed fresh (via
//    initializeWarp()) at the top of each invocation, is the correct and
//    complete port of that per-warp slice. No warp_id indexing needed here.
//
// 2. Barrier table intentionally NOT ported here. threadHitBarrier/
//    allWarpsAtBarrier/clearBarrier require cross-warp, cross-invocation
//    state (which warps, across the whole kernel, have arrived at a given
//    barrier_id) - the opposite of point 1's single-warp scope. Per
//    docs/hls/interfaces.md SS2.4 (host-orchestrated barriers, team-approved),
//    that bookkeeping lives on the host between compute_pipeline invocations,
//    not inside this per-warp structure.
//
// 3. pc_stack and thread_masks (SIMTController::DivergenceStack) are dropped.
//    grep-verified dead state in the golden model: pc_stack always pushes 0
//    and is never read after popping; thread_masks is written in
//    computeActiveMask() and never read anywhere. Porting them would only
//    cost BRAM/registers for no behavioral effect.
//
// Bounded vs. the golden model's unbounded std::stack: MAX_DIVERGENCE_DEPTH
// (hls_config.h) caps nesting. Push-when-full sets a sticky overflow_ flag
// (checkable via overflowed()) instead of corrupting state - the golden model
// had no such bound to violate, so this is a new, explicit HLS resource limit,
// not a behavior it needs to match.

#ifndef RISCV_GPGPU_HLS_DIVERGENCE_STACK_H
#define RISCV_GPGPU_HLS_DIVERGENCE_STACK_H

#include "../common/hls_config.h"
#include "../common/hls_types.h"

namespace riscv_gpgpu_hls {

class DivergenceStack {
public:
    // Golden reference: SIMTController::initializeWarp(warp_id, threads_per_warp)
    // - kept for parity with call sites that only know a lane count. NOT an
    // overload of initializeWarp(thread_mask_t) below: an `int`/literal
    // argument (e.g. `initializeWarp(32)`) converts equally well to both
    // ap_uint<8> and thread_mask_t (ap_uint<32>), which made the two
    // genuinely ambiguous as overloads (caught by tests/hls/
    // test_hls_data_structures.cpp failing to compile) - a distinct name
    // is the correct fix, not a caller-side cast.
    void initializeWarpFromThreadCount(ap_uint<8> threads_per_warp) {
        threads_per_warp_ = threads_per_warp;
        thread_mask_t mask = (threads_per_warp == MAX_THREADS_PER_WARP)
                              ? thread_mask_t(-1)
                              : thread_mask_t((1u << threads_per_warp) - 1u);
        initializeWarp(mask);
    }

    // compute_pipeline.cpp entry point: docs/hls/interfaces.md SS2.2 passes
    // active_mask_init as a mask directly (compute_pipeline is invoked once
    // per warp already, so there is no separate "lane count" to derive it
    // from - the caller/host already knows the mask). Golden model's
    // ComputeUnit::executeWarp() calls initializeWarp() at the top of EVERY
    // invocation, including barrier resumes ("after a barrier all threads
    // are synchronised" - see compute_unit.cpp's comment there), so this is
    // called once per compute_pipeline invocation too, not just on a fresh
    // warp start.
    void initializeWarp(thread_mask_t mask) {
        active_mask_ = mask;
        sp_ = 0;
        overflow_ = false;
        divergence_events_ = 0;
        wasted_cycles_ = 0;
    }

    thread_mask_t getActiveMask() const { return active_mask_; }

    bool isThreadActive(lane_id_t thread_id) const {
        return active_mask_[thread_id];
    }

    bool hasPendingDivergence() const { return sp_ != 0; }
    bool overflowed()           const { return overflow_; }
    ap_uint<32> getDivergenceEvents() const { return divergence_events_; }
    ap_uint<32> getWastedCycles()     const { return wasted_cycles_; }

    // Golden reference: SIMTController::handleBranch(). conditions[t] == true
    // means thread t's branch condition evaluated taken (mirrors
    // ComputeUnit::executeBranch's `ctx.regs[t][instr.rs1] == 0` convention -
    // caller decides what "taken" means, this class only handles the mask
    // bookkeeping).
    //
    // Three cases (only active lanes considered) - matches a bug fix pulled
    // in from upstream (models/systemc/src/simt_controller/simt_controller.cpp,
    // commit 9c4dfea "GPGPU READY"). The original two-case version below is
    // what this function used to do; case 2 was simply folded into case 3,
    // which was wrong:
    //
    //   1. taken != 0 && not_taken != 0 -> real divergence: some lanes fall
    //      through, some are masked. Push not_taken, mask = taken, count it.
    //
    //   2. taken == 0 && not_taken != 0 -> ALL active lanes take the masked
    //      path (none fall through). Correct behavior: mask = 0 (nobody runs
    //      the fall-through block), push not_taken so handleJoin() restores
    //      them. The bug this fixes: the old code's fallback branch computed
    //      mask = (taken!=0) ? taken : not_taken, which for taken==0 sets
    //      mask = not_taken - i.e. the ENTIRE current mask, since not_taken
    //      and current are the same set when nothing was taken. Net effect
    //      of the bug: every lane silently stayed active despite none of
    //      them satisfying the fall-through condition, contradicting the
    //      documented VBRANCH semantics ("rs1[t]!=0 -> masked until VJOIN").
    //      Not counted as a divergence event: every active lane agreed.
    //
    //   3. taken != 0 && not_taken == 0 -> all active lanes fall through
    //      (or mask was already 0). No masking, no stack push.
    void handleBranch(const bool conditions[MAX_THREADS_PER_WARP]) {
        thread_mask_t taken = 0, not_taken = 0;
    HANDLE_BRANCH_LANES:
        for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
#pragma HLS UNROLL
            if (active_mask_[t]) {
                if (conditions[t]) taken[t]     = 1;
                else                not_taken[t] = 1;
            }
        }

        if (taken != 0 && not_taken != 0) {
            // Case 1: real divergence.
            pushDivergence(not_taken);
            active_mask_ = taken;
            ++divergence_events_;
            wasted_cycles_ += popcount(not_taken);
        } else if (taken == 0 && not_taken != 0) {
            // Case 2: all active lanes masked - fall-through block runs with
            // mask = 0. Push so handleJoin() restores them; NOT a divergence
            // event (no disagreement among active lanes).
            pushDivergence(not_taken);
            active_mask_ = 0;
        } else {
            // Case 3: all active lanes fall through (or mask already 0).
            active_mask_ = (taken != 0) ? taken : not_taken;
        }
    }

    // Golden reference: SIMTController::handleJoin(). No-op (matches golden
    // model's LOG_WARNING-and-ignore) if the stack is empty.
    void handleJoin() {
        if (sp_ == 0) return;
        --sp_;
        active_mask_ |= not_taken_stack_[sp_];
    }

private:
    void pushDivergence(thread_mask_t not_taken_mask) {
        if (sp_ >= MAX_DIVERGENCE_DEPTH) { overflow_ = true; return; }
        not_taken_stack_[sp_] = not_taken_mask;
        ++sp_;
    }

    static ap_uint<32> popcount(thread_mask_t m) {
        ap_uint<32> c = 0;
    POPCOUNT_BITS:
        for (int t = 0; t < MAX_THREADS_PER_WARP; ++t) {
#pragma HLS UNROLL
            c += m[t];
        }
        return c;
    }

    thread_mask_t active_mask_        = 0;
    ap_uint<8>    threads_per_warp_   = MAX_THREADS_PER_WARP;

    thread_mask_t not_taken_stack_[MAX_DIVERGENCE_DEPTH];
    ap_uint<8>    sp_       = 0;   // stack pointer / depth in use
    bool          overflow_ = false;

    ap_uint<32> divergence_events_ = 0;
    ap_uint<32> wasted_cycles_     = 0;
};

}  // namespace riscv_gpgpu_hls

#endif  // RISCV_GPGPU_HLS_DIVERGENCE_STACK_H
