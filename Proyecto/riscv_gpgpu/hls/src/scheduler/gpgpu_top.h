// gpgpu_top.h - HLS-synthesizable top-level orchestration: ties
// CuDispatchUnit/BarrierArbiter to real compute_pipeline instances
//
// Golden reference: GPGPUTop::simulationProcess()'s per-round structure
// (models/systemc/src/top/top.cpp) - "for cu_id in 0..NUM_CUS: dispatch one
// warp" each round, then check the barrier - ported here as schedulerStep(),
// now driven by real on-chip state (CuDispatchUnit/BarrierArbiter, SS10.7)
// instead of software.
//
// Real gap found while implementing this: CuDispatchUnit has no "dispatched,
// awaiting result" slot state (only EMPTY/READY/STALLED/DONE) - calling
// nextReadySlot() twice without an intervening recordResult() would return
// the SAME slot both times. Golden simulationProcess() never hits this
// because it dispatches exactly one warp per CU per round, synchronously,
// before moving to the next round - the equivalent guard here is
// cu_busy_[NUM_CUS], tracked at this orchestration layer (not inside
// CuDispatchUnit itself, which stays decoupled per SS10.7): don't ask a CU
// for its next slot while its previous dispatch hasn't reported back yet.
//
// Scope of what's verified here vs. deferred to T025: GpgpuTop/
// schedulerStep() below are real, tested control logic - proven via actual
// execution against real compute_pipeline instances (see tests/hls/
// test_gpgpu_top.cpp). What ISN'T resolved here: in real hardware,
// compute_pipeline is a SEPARATELY synthesized top-level kernel per CU, so
// CuDispatchUnit's regs_/program_ (this file's C++ member state) and that
// kernel's regs[][]/program[] ports need to alias the same physical BRAM
// across two different synthesized blocks - a system-integration/v++
// connectivity decision, same category as m_axi board binding (SS6.4),
// deferred to T025. The csim tests here run compute_pipeline in the same
// process/scope specifically so that aliasing is trivially true for
// verification purposes; it doesn't prove the cross-kernel RTL wiring.

#ifndef RISCV_GPGPU_HLS_GPGPU_TOP_H
#define RISCV_GPGPU_HLS_GPGPU_TOP_H

#include <hls_stream.h>
#include "../common/hls_config.h"
#include "../common/hls_types.h"
#include "cu_dispatch_unit.h"
#include "barrier_arbiter.h"

namespace riscv_gpgpu_hls {

class GpgpuTop {
public:
    // Kernel launch (docs/hls/interfaces.md SS2.5.6/SS10.6): validates the
    // hazard-mitigation capacity check, and if it passes, assigns warps to
    // every CU and loads each one's program copy. Returns false (matching
    // status.fault, SS2.5.6) without touching any CU state if the kernel
    // doesn't fit - mirrors BarrierArbiter::launch()'s own fault flag.
    bool launchKernel(instr_word_t* program_ptr, uint32_t program_len,
                       warp_id_t total_warps) {
        arbiter_.launch(total_warps);
        if (arbiter_.launchFault()) return false;

    LAUNCH_EACH_CU:
        for (int i = 0; i < NUM_CUS; ++i) {
#pragma HLS UNROLL
            cus_[i].launch(cu_id_t(i), total_warps);
            cus_[i].loadProgram(program_ptr, program_len);
            cu_busy_[i] = false;
        }
        return true;
    }

    bool launchFault()    const { return arbiter_.launchFault(); }
    bool kernelComplete() const { return arbiter_.kernelComplete(); }

    CuDispatchUnit& cu(int i)      { return cus_[i]; }
    BarrierArbiter&  arbiter()      { return arbiter_; }
    bool&            busy(int i)    { return cu_busy_[i]; }

private:
    CuDispatchUnit cus_[NUM_CUS];
    BarrierArbiter arbiter_;
    bool           cu_busy_[NUM_CUS] = {};
};

// One scheduling round: mirrors simulationProcess()'s fan-out loop (one
// dispatch attempt + one result check per CU), then the barrier check.
// Call this repeatedly (the free-running gpgpu_scheduler() kernel below
// does so in a while(true)) until top.kernelComplete().
inline void schedulerStep(GpgpuTop& top,
                           hls::stream<warp_dispatch_t> dispatch_out[NUM_CUS],
                           hls::stream<warp_status_t>   status_in[NUM_CUS]) {
SCHEDULER_STEP_PER_CU:
    for (int i = 0; i < NUM_CUS; ++i) {
        if (!top.busy(i)) {
            slot_id_t slot = top.cu(i).nextReadySlot();
            if (slot != CuDispatchUnit::INVALID_SLOT) {
                dispatch_out[i].write(top.cu(i).buildDispatch(slot));
                top.busy(i) = true;
            }
        }
        if (!status_in[i].empty()) {
            warp_status_t st = status_in[i].read();
            top.cu(i).recordResult(st.slot_id, st);
            top.arbiter().onEvent(st.code);
            top.busy(i) = false;
        }
    }

    if (top.arbiter().releaseReady()) {
    SCHEDULER_STEP_RELEASE:
        for (int i = 0; i < NUM_CUS; ++i) {
#pragma HLS UNROLL
            top.cu(i).releaseBarrier();
        }
        top.arbiter().acknowledgeRelease();
    }
}

// Free-running top-level kernel (docs/hls/interfaces.md SS2.5.6's launch
// register set) - the on-chip replacement for the host's launch/poll role.
// program_ptr's cross-kernel regs[][]/program[] aliasing with each
// separately-synthesized compute_pipeline instance is T025 system-
// integration scope (see this file's header) - not resolved by this
// function alone.
inline void gpgpu_scheduler(
    instr_word_t* program_ptr,
    uint32_t      program_len,
    warp_id_t     total_warps,
    bool          start,
    bool&         busy,
    bool&         done,
    bool&         fault,
    hls::stream<warp_dispatch_t> dispatch_out[NUM_CUS],
    hls::stream<warp_status_t>   status_in[NUM_CUS]
) {
#pragma HLS INTERFACE m_axi    port=program_ptr offset=slave bundle=gmem
#pragma HLS INTERFACE s_axilite port=program_len  bundle=control
#pragma HLS INTERFACE s_axilite port=total_warps  bundle=control
#pragma HLS INTERFACE s_axilite port=start        bundle=control
#pragma HLS INTERFACE s_axilite port=busy         bundle=control
#pragma HLS INTERFACE s_axilite port=done         bundle=control
#pragma HLS INTERFACE s_axilite port=fault        bundle=control
#pragma HLS INTERFACE axis      port=dispatch_out
#pragma HLS INTERFACE axis      port=status_in

    static GpgpuTop top;
    busy = false; done = false; fault = false;

    while (true) {
        // Idle until the host pulses start (docs/hls/interfaces.md SS2.5.6
        // launch sequence, step 4) - same open synthesis-semantics caveat
        // T024's other scalar-port pragmas already carry (SS8): a
        // free-running kernel reading a scalar s_axilite port on every
        // iteration is provisional until checked against real vitis_hls,
        // not assumed correct here.
        if (!start) continue;

        bool ok = top.launchKernel(program_ptr, program_len, total_warps);
        if (!ok) { fault = true; continue; }
        busy = true;

        while (!top.kernelComplete()) {
#pragma HLS PIPELINE off
            schedulerStep(top, dispatch_out, status_in);
        }

        busy = false;
        done = true;
    }
}

}  // namespace riscv_gpgpu_hls

#endif  // RISCV_GPGPU_HLS_GPGPU_TOP_H
