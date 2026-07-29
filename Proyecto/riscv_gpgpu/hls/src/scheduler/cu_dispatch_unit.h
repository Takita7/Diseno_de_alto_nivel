// cu_dispatch_unit.h - HLS-synthesizable per-CU resident-warp dispatch state
//
// Golden reference: models/systemc/src/scheduler/warp_scheduler.{h,cpp}'s
// generateWarps()/selectWarp() (the exercised, real subset - per
// docs/hls/interfaces.md SS10.3 Finding A, WarpScheduler::stalled_queues_/
// markWarpStalled() are dead code in the golden model's own live
// simulationProcess() path and are not ported here) and
// GPGPUTop::simulationProcess()'s per-CU slice of its barrier_queue
// bookkeeping (models/systemc/src/top/top.cpp).
//
// Design departs from both in one deliberate way, per docs/hls/interfaces.md
// SS10.6/SS10.7: warps are NOT held in a dynamic queue. Both
// generateWarps()'s round-robin CU assignment and selectWarp()'s FIFO order
// are fully deterministic given (cu_id, total_warps, NUM_CUS), so this CU's
// resident state is MAX_WARPS_PER_CU fixed slots, computed once at launch()
// - no queue-shaped state anywhere. See SS10.5/SS10.7 for the full
// reasoning.
//
// Precondition this class assumes but does NOT itself re-check: the caller
// (BarrierArbiter's launch sequencer, docs/hls/interfaces.md SS2.5.5) has
// already validated total_warps <= NUM_CUS*MAX_WARPS_PER_CU (the SS10.6
// hazard mitigation) before calling launch(). A CU whose share of an
// over-capacity kernel exceeds MAX_WARPS_PER_CU would silently lose the
// overflow warps otherwise - checked once, centrally, not duplicated in
// every CU instance.
//
// Register storage: NOT held inside WarpSlot (see hls_types.h's comment on
// WarpSlot) - compute_pipeline's regs[][] port (docs/hls/interfaces.md
// SS2.5.3) needs one flat reg_t[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][32]
// array to alias, so it lives here as CuDispatchUnit's own separate member
// instead (struct-of-arrays, not array-of-structs).

#ifndef RISCV_GPGPU_HLS_CU_DISPATCH_UNIT_H
#define RISCV_GPGPU_HLS_CU_DISPATCH_UNIT_H

#include "../common/hls_config.h"
#include "../common/hls_types.h"

namespace riscv_gpgpu_hls {

class CuDispatchUnit {
public:
    // Plain int, not slot_id_t: ap_uint<N> has no constexpr constructor
    // (found while compiling this against the real Vitis HLS headers - a
    // static constexpr ap_uint member is not a literal type), so the
    // sentinel is declared as int and relies on the implicit ap_uint
    // conversion at each comparison/return site instead.
    static constexpr int INVALID_SLOT = MAX_WARPS_PER_CU;

    // Called once per kernel launch. Computes this CU's warp assignment with
    // the same round-robin arithmetic WarpScheduler::generateWarps() uses
    // (warp w belongs to CU w % NUM_CUS) - evaluated directly per slot
    // instead of iteratively pushed into a queue.
    void launch(cu_id_t cu_id, warp_id_t total_warps) {
    LAUNCH_SLOTS:
        for (int slot = 0; slot < MAX_WARPS_PER_CU; ++slot) {
#pragma HLS UNROLL
            warp_id_t w = warp_id_t(cu_id) + warp_id_t(slot) * warp_id_t(NUM_CUS);
            if (w < total_warps) {
                slots_[slot].warp_id    = w;
                slots_[slot].resume_pc  = 0;
                slots_[slot].barrier_id = 0;
                slots_[slot].state      = WarpSlot::State::READY;
            } else {
                slots_[slot].state      = WarpSlot::State::EMPTY;
            }
        }
    }

    // Next READY slot in ascending order, or INVALID_SLOT if none - mirrors
    // WarpScheduler::selectWarp()'s FIFO order: generateWarps() pushes warps
    // in increasing warp_id order, and ascending slot order reproduces that
    // pop order exactly, since slot index and relative warp_id order match
    // by construction in launch().
    slot_id_t nextReadySlot() const {
    FIND_READY_SLOT:
        for (int slot = 0; slot < MAX_WARPS_PER_CU; ++slot) {
#pragma HLS UNROLL
            if (slots_[slot].state == WarpSlot::State::READY) return slot_id_t(slot);
        }
        return INVALID_SLOT;
    }

    // Packages a slot's state into a warp_dispatch_t for compute_pipeline
    // (docs/hls/interfaces.md SS2.5.3). active_mask_init is always "all
    // lanes active" - matches DivergenceStack::initializeWarp() being
    // called fresh on every compute_pipeline invocation, fresh warp or
    // barrier-resumed alike (divergence_stack.h: "after a barrier all
    // threads are synchronised").
    warp_dispatch_t buildDispatch(slot_id_t slot) const {
        warp_dispatch_t d;
        d.slot_id          = slot;
        d.warp_id          = slots_[slot].warp_id;
        d.active_mask_init = thread_mask_t(-1);   // all MAX_THREADS_PER_WARP lanes
        d.resume_pc        = slots_[slot].resume_pc;
        return d;
    }

    // Applies a compute_pipeline result to a slot. Takes the whole
    // warp_status_t (not separate code/bid args, an earlier shape this
    // method had) because it now also needs resume_pc - compute_pipeline
    // reads a shared program[] array by an internal index, so it has to
    // report where to resume; the old per-invocation host contract never
    // needed this (the host tracked instruction-stream position itself).
    void recordResult(slot_id_t slot, const warp_status_t& status) {
        if (status.code == WarpStatusCode::COMPLETE) {
            slots_[slot].state = WarpSlot::State::DONE;
        } else {   // STALLED_AT_BARRIER
            slots_[slot].state      = WarpSlot::State::STALLED;
            slots_[slot].barrier_id = status.barrier_id;
            slots_[slot].resume_pc  = status.resume_pc;
        }
    }

    // BarrierArbiter calls this on every CU once a global release fires
    // (docs/hls/interfaces.md SS2.5.5) - STALLED -> READY, saved regs/
    // resume_pc untouched, matching simulationProcess()'s resume-unchanged
    // contract.
    void releaseBarrier() {
    RELEASE_SLOTS:
        for (int slot = 0; slot < MAX_WARPS_PER_CU; ++slot) {
#pragma HLS UNROLL
            if (slots_[slot].state == WarpSlot::State::STALLED)
                slots_[slot].state = WarpSlot::State::READY;
        }
    }

    // True once every non-EMPTY slot is DONE - this CU's share of the
    // kernel is finished.
    bool allDone() const {
    CHECK_ALL_DONE:
        for (int slot = 0; slot < MAX_WARPS_PER_CU; ++slot) {
#pragma HLS UNROLL
            WarpSlot::State s = slots_[slot].state;
            if (s == WarpSlot::State::READY || s == WarpSlot::State::STALLED) return false;
        }
        return true;
    }

    WarpSlot::State stateOf(slot_id_t slot)    const { return slots_[slot].state; }
    warp_id_t       warpIdOf(slot_id_t slot)   const { return slots_[slot].warp_id; }
    barrier_id_t    barrierIdOf(slot_id_t slot) const { return slots_[slot].barrier_id; }

    // compute_pipeline's regs[][] port aliases this directly (top-level
    // wiring detail, docs/hls/interfaces.md SS2.5.3 - deferred to T025 same
    // as m_axi board binding).
    reg_t (&regsArray())[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD] {
        return regs_;
    }

    // Per-CU program store (docs/hls/interfaces.md SS10.8). Owned here, not
    // by BarrierArbiter as SS10.8's original draft sketched - found while
    // implementing this: folding the load into BarrierArbiter would mean it
    // reaching into every CU's internals, breaking the CuDispatchUnit/
    // BarrierArbiter decoupling SS10.7 deliberately established (so both
    // stay independently testable). Consolidated with regs_ here instead,
    // same "per-CU state compute_pipeline's ports alias" role.
    //
    // Loads this CU's local copy from the host's DRAM program buffer -
    // called once per kernel launch, before any warp dispatches. A plain
    // copy loop, not a class of its own: "broadcast, not a routed
    // transfer" (SS10.8) - every CU gets an independent copy of the same
    // source, no arbitration needed. The top-level wiring (SS10.4 step 7)
    // calls this once per CU; program_ptr's actual `#pragma HLS INTERFACE
    // m_axi` belongs at whichever top-level kernel function owns that
    // port, not here - CuDispatchUnit is a plain class, never a synthesis
    // top level itself, same as DivergenceStack/BlockScheduler before it.
    void loadProgram(instr_word_t* program_ptr, uint32_t program_len) {
    LOAD_PROGRAM_WORDS:
        for (uint32_t i = 0; i < program_len; ++i) {
#pragma HLS LOOP_TRIPCOUNT max=MAX_PROGRAM_LEN
            if (i >= uint32_t(MAX_PROGRAM_LEN)) break;   // static bound, same as
                                                          // executeOneWarp()'s guard
            program_[i] = program_ptr[i];
        }
    }

    // compute_pipeline's program[] port aliases this directly - same role
    // as regsArray() above.
    instr_word_t (&programArray())[MAX_PROGRAM_LEN] { return program_; }

private:
    WarpSlot     slots_[MAX_WARPS_PER_CU];
    reg_t        regs_[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    instr_word_t program_[MAX_PROGRAM_LEN];
};

}  // namespace riscv_gpgpu_hls

#endif  // RISCV_GPGPU_HLS_CU_DISPATCH_UNIT_H
