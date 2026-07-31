// barrier_arbiter.h - HLS-synthesizable global barrier/launch arbiter state
// and free functions
//
// Golden reference: GPGPUTop::simulationProcess()'s barrier_queue mechanics
// (models/systemc/src/top/top.cpp) - specifically the release condition
// `barrier_queue.size() == total_warps_` (top.cpp:157), global across every
// CU, ported bit-faithfully per docs/hls/interfaces.md SS10.6 (the barrier
// scope reversal - global, not block-scoped).
//
// One state instance, system-wide - counts arrivals across every CU instead
// of each CU deciding release locally (docs/hls/interfaces.md SS10.7).
// Deliberately decoupled from warp-slot bookkeeping (cu_dispatch_unit.h):
// nothing here inspects a WarpSlot directly. The top-level wiring
// (gpgpu_top.cpp's schedulerCore) is responsible for calling
// barrierOnEvent() and the matching recordResult() together, every time a
// warp_status_t comes back from compute_pipeline, and for calling
// releaseBarrierSlots() once barrierReleaseReady() is true, followed by
// barrierAcknowledgeRelease().
//
// docs/hls/interfaces.md SS16.6: rewritten from a BarrierArbiter class to a
// plain BarrierState struct plus free functions, the same treatment
// cu_dispatch_unit.h's warp-slot bookkeeping got - not because this state
// was ever observed to fail real DATAFLOW checking (it's four plain
// scalars, no arrays; every failure across 10 real csynth attempts named a
// specific ARRAY element, never a bare scalar), but for consistency with
// schedulerCore now owning all scheduling state as genuinely local
// variables, and to close out any remaining doubt rather than leave one
// piece of scheduler state on the old, failure-prone shape.
//
// Verification note on the release condition: `stalled_count_ ==
// total_warps_` only becomes true if EVERY warp in the kernel is currently
// stalled - if even one warp completes without ever reaching the barrier,
// stalled_count_ can never reach total_warps_ again (done_count_ absorbs
// it instead), so barrierReleaseReady() correctly never fires. This
// reproduces simulationProcess()'s own inherited requirement (docs/hls/
// interfaces.md SS10.7's "inherited assumption" note) - a kernel with
// non-uniform barrier participation hangs the golden model too, in
// software - as a direct consequence of the counter arithmetic, not a
// special case added here.

#ifndef RISCV_GPGPU_HLS_BARRIER_ARBITER_H
#define RISCV_GPGPU_HLS_BARRIER_ARBITER_H

#include "../common/hls_config.h"
#include "../common/hls_types.h"

namespace riscv_gpgpu_hls {

struct BarrierState {
    warp_id_t total_warps_   = 0;
    warp_id_t stalled_count_ = 0;
    warp_id_t done_count_    = 0;
    bool      launch_fault_  = false;
};

// Called once per kernel launch. Latches total_warps and performs the
// SS10.6 hazard mitigation: a kernel bigger than the hardware's declared
// resident capacity is an invalid launch, not a silent hang.
inline void barrierLaunch(BarrierState& b, warp_id_t total_warps) {
    b.total_warps_   = total_warps;
    b.stalled_count_ = 0;
    b.done_count_    = 0;
    b.launch_fault_  = (total_warps > warp_id_t(NUM_CUS * MAX_WARPS_PER_CU));
}

// Host-visible `status.fault` (docs/hls/interfaces.md SS2.5.6).
inline bool barrierLaunchFault(const BarrierState& b) { return b.launch_fault_; }

// Call once per {slot, new_state} transition, alongside the matching
// recordResult() (cu_dispatch_unit.h) - slot isn't needed here, only the
// state, since this counts, it doesn't route.
inline void barrierOnEvent(BarrierState& b, WarpStatusCode code) {
    if (code == WarpStatusCode::COMPLETE) {
        ++b.done_count_;
    } else {   // STALLED_AT_BARRIER
        ++b.stalled_count_;
    }
}

// True once every warp in the kernel is simultaneously stalled at a
// barrier - matches simulationProcess()'s barrier_queue.size() ==
// total_warps_ check exactly, global across all CUs.
inline bool barrierReleaseReady(const BarrierState& b) {
    return b.total_warps_ != 0 && b.stalled_count_ == b.total_warps_;
}

// Call after broadcasting release (releaseBarrierSlots(), cu_dispatch_unit.h)
// - resets the arrival counter for the kernel's next wave, if any.
// done_count_ is untouched; it accumulates across the whole kernel.
inline void barrierAcknowledgeRelease(BarrierState& b) {
    b.stalled_count_ = 0;
}

// True once every warp in the kernel has reached COMPLETE, across every
// wave - the whole kernel is finished.
inline bool barrierKernelComplete(const BarrierState& b) {
    return b.total_warps_ != 0 && b.done_count_ == b.total_warps_;
}

}  // namespace riscv_gpgpu_hls

#endif  // RISCV_GPGPU_HLS_BARRIER_ARBITER_H
