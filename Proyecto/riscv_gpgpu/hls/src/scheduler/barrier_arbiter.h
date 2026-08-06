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

#include <hls_stream.h>
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

// ── Real, multi-CU global barrier arbiter (docs/hls/interfaces.md SS16.37) ──
// The one genuinely new piece needed for NUM_CUS>1: BarrierState is
// inherently kernel-wide (release/completion depend on every CU's warps
// together, not any one CU's own share), so it can't simply be duplicated
// per-CU the way schedulerCore's own WarpSlot[] correctly is - each CU
// deciding release "locally" would release its own warps without waiting
// for the others, silently wrong. barrierCore owns the one real
// BarrierState and is the sole source of the top-level busy/done/fault
// signals - schedulerCore no longer owns any of the three.
//
// Broadcast from barrierCore to each CU's schedulerCore, one entry per
// pending action - deliberately two independent bools, not an enum, since
// a release and a kernel-done can both be true possibilities the receiver
// needs to check every pass (though not simultaneously in practice: a
// release fires mid-kernel while warps are still resident, done fires only
// once every warp has reached COMPLETE).
struct barrier_signal_t {
    bool release;      // un-stall my resident STALLED slots
    bool kernel_done;  // whole kernel (every CU) finished - return to IDLE
};

// Free-running top-level task, same persistent-hardware model every other
// free-running kernel in this project already uses. Mirrors mem_arbiter's
// proven N:1/1:N array-of-streams shape (docs/hls/interfaces.md SS10.9) -
// NUM_CUS event-in streams (one per CU, forwarding the WarpStatusCode each
// CU's schedulerCore already extracted from its own status_in for its own
// recordResult() call - not re-deriving anything, just relaying), NUM_CUS
// signal-out streams (release/done broadcasts).
//
// Independently observes the SAME start/total_warps every CU's own
// schedulerCore also observes (matching how program_ptr/program_len are
// already broadcast to every CU, not routed, SS10.8) - no explicit
// "launch" message needed from any CU. Each CU's schedulerCore
// independently recomputes the identical launch-fault condition
// (total_warps > NUM_CUS*MAX_WARPS_PER_CU) before self-launching, so a
// faulted launch never causes a CU to dispatch warps that barrierCore
// itself refused to accept - a pure, stateless check every caller derives
// the same way, not a value that needs relaying.
//
// NUM_CUS <= 8: flat DATAFLOW - one stream per CU (N = NUM_CUS).
// NUM_CUS >  8: hierarchical DATAFLOW - one stream per cluster (N = NUM_CLUSTERS).
// In the hierarchical case clusterBarrierRelay (cu_cluster.h) multiplexes
// all per-CU events onto the single cluster stream; barrierCore's counting
// logic is identical regardless of N.
template<int N>
inline void barrierCoreN(
    warp_id_t total_warps,
    bool&     start,
    bool&     busy,
    bool&     done,
    bool&     fault,
    hls::stream<WarpStatusCode>   (&events_in)[N],
    hls::stream<barrier_signal_t> (&signal_out)[N]
) {
    BarrierState barrier;
    busy = false; done = false; fault = false;

    while (true) {
#pragma HLS PIPELINE off
        if (!busy) {
            if (!start) continue;

            barrierLaunch(barrier, total_warps);
            if (barrierLaunchFault(barrier)) { fault = true; continue; }

            busy = true;
        } else {
        POLL_CU_EVENTS:
            for (int c = 0; c < N; ++c) {
#pragma HLS UNROLL
                if (!events_in[c].empty()) {
                    WarpStatusCode code = events_in[c].read();
                    barrierOnEvent(barrier, code);
                }
            }

            if (barrierReleaseReady(barrier)) {
                barrierAcknowledgeRelease(barrier);
            SIGNAL_RELEASE:
                for (int c = 0; c < N; ++c) {
#pragma HLS UNROLL
                    barrier_signal_t sig;
                    sig.release     = true;
                    sig.kernel_done = false;
                    signal_out[c].write(sig);
                }
            }

            if (barrierKernelComplete(barrier)) {
                busy = false;
                done = true;
            SIGNAL_DONE:
                for (int c = 0; c < N; ++c) {
#pragma HLS UNROLL
                    barrier_signal_t sig;
                    sig.release     = false;
                    sig.kernel_done = true;
                    signal_out[c].write(sig);
                }
            }
        }
    }
}

// Convenience wrapper: existing flat call sites compile unchanged.
// In the hierarchical path (NUM_CUS > 8) gpgpu_top.cpp calls
// barrierCoreN<NUM_CLUSTERS>(...) directly with the cluster-level streams.
inline void barrierCore(
    warp_id_t total_warps,
    bool& start, bool& busy, bool& done, bool& fault,
    hls::stream<WarpStatusCode>   (&events_in)[NUM_CUS],
    hls::stream<barrier_signal_t> (&signal_out)[NUM_CUS]
) {
    barrierCoreN<NUM_CUS>(total_warps, start, busy, done, fault,
                          events_in, signal_out);
}

}  // namespace riscv_gpgpu_hls

#endif  // RISCV_GPGPU_HLS_BARRIER_ARBITER_H
