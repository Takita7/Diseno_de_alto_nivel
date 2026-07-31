// cu_dispatch_unit.h - HLS-synthesizable per-CU register/program storage,
// plus free functions for warp-slot dispatch bookkeeping
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
// are fully deterministic given (cu_id, total_warps, NUM_CUS), so resident
// state is MAX_WARPS_PER_CU fixed slots, computed once at launch - no
// queue-shaped state anywhere. See SS10.5/SS10.7 for the full reasoning.
//
// docs/hls/interfaces.md SS16.6: warp-slot bookkeeping (WarpSlot[] +
// launch/dispatch/release logic) used to live as CuDispatchUnit methods
// operating on a `slots_` class member, reached through a persistent,
// externally-visible `GpgpuTop` object. That shape failed real Vitis HLS
// DATAFLOW legality checking against gpgpu_scheduler in every one of 10
// real csynth attempts (SS16.1-16.5), regardless of loop structure, UNROLL,
// call boundaries, or dead-code visibility - the common factor each time
// was the state being reachable via a persistent object touched from
// multiple places, not the code shape wrapped around it. Redesigned to
// match the one shape that has NEVER been flagged in any attempt:
// compute_pipeline.cpp's own executeALU(RegFile regs, ...)-style free
// functions operating on a caller-owned array by reference - the array
// itself only ever exists as a genuinely local variable of whichever
// single process owns it (schedulerCore, gpgpu_top.cpp), never as a class
// member reachable from elsewhere. CuDispatchUnit here is now ONLY
// regs_/program_/loadProgram() - the part of the old design that was
// NEVER flagged in any attempt, left completely unchanged.
//
// Precondition the free functions below assume but do NOT re-check: the
// caller (BarrierArbiter's launch sequencer, docs/hls/interfaces.md SS2.5.5)
// has already validated total_warps <= NUM_CUS*MAX_WARPS_PER_CU (the SS10.6
// hazard mitigation) before calling launchSlots(). A CU whose share of an
// over-capacity kernel exceeds MAX_WARPS_PER_CU would silently lose the
// overflow warps otherwise - checked once, centrally, not duplicated here.

#ifndef RISCV_GPGPU_HLS_CU_DISPATCH_UNIT_H
#define RISCV_GPGPU_HLS_CU_DISPATCH_UNIT_H

#include "../common/hls_config.h"
#include "../common/hls_types.h"

namespace riscv_gpgpu_hls {

// Sentinel for "no ready slot" - plain int, not slot_id_t: ap_uint<N> has
// no constexpr constructor (found while compiling this against the real
// Vitis HLS headers - a static constexpr ap_uint is not a literal type),
// so this relies on the implicit ap_uint conversion at each comparison/
// return site instead. Free constant now (was CuDispatchUnit::INVALID_SLOT)
// since nextReadySlot() below is a free function, not a class method.
constexpr int INVALID_SLOT = MAX_WARPS_PER_CU;

// ── Warp-slot bookkeeping (docs/hls/interfaces.md SS16.6) ───────────────────
// Every function below takes the resident-slot array by reference, owned
// by whichever single process calls them (schedulerCore) - never a class
// member, never reachable from anywhere else.

// Single-slot assignment - the same round-robin arithmetic
// WarpScheduler::generateWarps() uses (warp w belongs to CU w % NUM_CUS).
inline void assignSlot(WarpSlot (&slots)[MAX_WARPS_PER_CU], int slot,
                        cu_id_t cu_id, warp_id_t total_warps) {
    warp_id_t w = warp_id_t(cu_id) + warp_id_t(slot) * warp_id_t(NUM_CUS);
    if (w < total_warps) {
        slots[slot].warp_id    = w;
        slots[slot].resume_pc  = 0;
        slots[slot].barrier_id = 0;
        slots[slot].state      = WarpSlot::State::READY;
        slots[slot].fresh      = true;   // SS16: seed regs_ on first dispatch
    } else {
        slots[slot].state      = WarpSlot::State::EMPTY;
    }
}

// Called once per kernel launch - assigns every resident slot for this CU.
inline void launchSlots(WarpSlot (&slots)[MAX_WARPS_PER_CU], cu_id_t cu_id,
                         warp_id_t total_warps) {
LAUNCH_SLOTS:
    for (int slot = 0; slot < MAX_WARPS_PER_CU; ++slot) {
#pragma HLS UNROLL
        assignSlot(slots, slot, cu_id, total_warps);
    }
}

// Next READY slot in ascending order, or INVALID_SLOT if none - mirrors
// WarpScheduler::selectWarp()'s FIFO order: generateWarps() pushes warps
// in increasing warp_id order, and ascending slot order reproduces that
// pop order exactly, since slot index and relative warp_id order match
// by construction in launchSlots().
inline slot_id_t nextReadySlot(const WarpSlot (&slots)[MAX_WARPS_PER_CU]) {
FIND_READY_SLOT:
    for (int slot = 0; slot < MAX_WARPS_PER_CU; ++slot) {
#pragma HLS UNROLL
        if (slots[slot].state == WarpSlot::State::READY) return slot_id_t(slot);
    }
    return INVALID_SLOT;
}

// Packages a slot's state into a warp_dispatch_t for compute_pipeline
// (docs/hls/interfaces.md SS2.5.3). active_mask_init is always "all lanes
// active" - matches DivergenceStack::initializeWarp() being called fresh
// on every compute_pipeline invocation, fresh warp or barrier-resumed
// alike. Clears slots[slot].fresh after packaging it (SS16), so a slot's
// initial-regs seed fires exactly once, on its first dispatch, never
// again across barrier resumes.
inline warp_dispatch_t buildDispatch(WarpSlot (&slots)[MAX_WARPS_PER_CU],
                                      slot_id_t slot) {
    warp_dispatch_t d;
    d.slot_id          = slot;
    d.warp_id          = slots[slot].warp_id;
    d.active_mask_init = thread_mask_t(-1);   // all MAX_THREADS_PER_WARP lanes
    d.resume_pc        = slots[slot].resume_pc;
    d.fresh_launch      = slots[slot].fresh;
    slots[slot].fresh   = false;
    return d;
}

// Applies a compute_pipeline result to a slot. Takes the whole
// warp_status_t (not separate code/bid args) because it also needs
// resume_pc - compute_pipeline reads a shared program[] array by an
// internal index, so it has to report where to resume.
inline void recordResult(WarpSlot (&slots)[MAX_WARPS_PER_CU], slot_id_t slot,
                          const warp_status_t& status) {
    if (status.code == WarpStatusCode::COMPLETE) {
        slots[slot].state = WarpSlot::State::DONE;
    } else {   // STALLED_AT_BARRIER
        slots[slot].state      = WarpSlot::State::STALLED;
        slots[slot].barrier_id = status.barrier_id;
        slots[slot].resume_pc  = status.resume_pc;
    }
}

// Called once every CU once a global barrier release fires (docs/hls/
// interfaces.md SS2.5.5) - STALLED -> READY, saved resume_pc untouched,
// matching simulationProcess()'s resume-unchanged contract.
inline void releaseBarrierSlots(WarpSlot (&slots)[MAX_WARPS_PER_CU]) {
RELEASE_SLOTS:
    for (int slot = 0; slot < MAX_WARPS_PER_CU; ++slot) {
#pragma HLS UNROLL
        if (slots[slot].state == WarpSlot::State::STALLED)
            slots[slot].state = WarpSlot::State::READY;
    }
}

// True once every non-EMPTY slot is DONE - this CU's share of the kernel
// is finished.
inline bool allSlotsDone(const WarpSlot (&slots)[MAX_WARPS_PER_CU]) {
CHECK_ALL_DONE:
    for (int slot = 0; slot < MAX_WARPS_PER_CU; ++slot) {
#pragma HLS UNROLL
        WarpSlot::State s = slots[slot].state;
        if (s == WarpSlot::State::READY || s == WarpSlot::State::STALLED) return false;
    }
    return true;
}

// ── Per-CU register/program storage (unchanged from every prior attempt -
// this shape has never been flagged in any real csynth run) ─────────────────
class CuDispatchUnit {
public:
    // compute_pipeline's regs[][] port aliases this directly (top-level
    // wiring detail, docs/hls/interfaces.md SS2.5.3).
    reg_t (&regsArray())[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD] {
        return regs_;
    }

    // Loads this CU's local copy of the host's DRAM program buffer - called
    // once per kernel launch, before any warp dispatches. "Broadcast, not a
    // routed transfer" (SS10.8) - every CU gets an independent copy of the
    // same source, no arbitration needed.
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
    reg_t        regs_[MAX_WARPS_PER_CU][MAX_THREADS_PER_WARP][NUM_REGS_PER_THREAD];
    instr_word_t program_[MAX_PROGRAM_LEN];
};

}  // namespace riscv_gpgpu_hls

#endif  // RISCV_GPGPU_HLS_CU_DISPATCH_UNIT_H
