// gpgpu_top.h - HLS-synthesizable top-level orchestration: the on-chip
// scheduler (schedulerCore) plus the real merged top-level "compute" IP
// (gpgpu_scheduler)
//
// Golden reference: GPGPUTop::simulationProcess()'s per-round structure
// (models/systemc/src/top/top.cpp) - "for cu_id in 0..NUM_CUS: dispatch one
// warp" each round, then check the barrier - ported here as schedulerCore(),
// driven by real on-chip state (cu_dispatch_unit.h's free functions +
// barrier_arbiter.h's free functions) instead of software.
//
// docs/hls/interfaces.md SS16.6: this file used to hold a GpgpuTop class
// (wrapping CuDispatchUnit/BarrierArbiter instances as members, reached via
// a persistent `top` object) plus schedulerStep()/schedulerLoop() operating
// on it. That shape failed real Vitis HLS DATAFLOW legality checking
// against gpgpu_scheduler in every one of 10 real csynth attempts
// (SS16.1-16.5). Redesigned around schedulerCore() below: ONE free-running
// process (matching compute_pipeline/mem_arbiter's already-proven shape
// exactly) that owns its own warp-slot and barrier state as genuinely
// local variables - nothing else in the program can reach them, by
// construction. CuDispatchUnit (now regs_/program_ only, cu_dispatch_unit.h)
// is still shared between schedulerCore (which calls loadProgram()) and
// compute_pipeline (which reads/writes regs_/program_ through its own
// ports) - that specific sharing pattern was never flagged in any attempt
// and is unchanged here.

#ifndef RISCV_GPGPU_HLS_GPGPU_TOP_H
#define RISCV_GPGPU_HLS_GPGPU_TOP_H

#include <hls_stream.h>
#include "../common/hls_config.h"
#include "../common/hls_types.h"
#include "cu_dispatch_unit.h"
#include "barrier_arbiter.h"
#include "mem_arbiter.h"
#include "../compute_unit/compute_pipeline.h"

namespace riscv_gpgpu_hls {

// The on-chip scheduler, one free-running instance per CU (docs/hls/
// interfaces.md SS16.37 - NUM_CUS>1 support), called once per CU from
// gpgpu_scheduler's DATAFLOW region exactly like compute_pipeline/
// mem_arbiter are. Real gap found while first building this (pre-redesign,
// still true here): a slot can only ever be "dispatched, awaiting result"
// implicitly - nextReadySlot() would return the same slot twice if asked
// again before its result comes back, so this tracks one `busy_cu` flag
// itself (SS10.7's decoupling: that bookkeeping belongs at this
// orchestration layer, not inside the slot-bookkeeping free functions).
//
// `cu` is a reference to storage owned by the caller (gpgpu_scheduler) -
// shared with compute_pipeline for regs_/program_ only (SS16.6's header
// comment has the reasoning for why that specific sharing is fine). `cu_id`
// identifies which CU this instance is - no longer hardcoded (SS16.37),
// each of the NUM_CUS real call sites in gpgpu_scheduler passes its own.
// `slots` is NOT a parameter - declared inside this function's own body,
// matching compute_pipeline's own RegFile/DivergenceStack shape exactly.
//
// SS16.37: `busy`/`done`/`fault` moved OUT to barrierCore
// (barrier_arbiter.h) - they were never really per-CU concepts (a kernel
// is "done" only once every CU's warps are, not just this one's), so
// duplicating them here for NUM_CUS>1 would be wrong the same way
// duplicating BarrierState itself would be. This CU's own completion
// events are still generated here (from status_in) but forwarded to
// barrierCore via `barrier_events_out`, and this CU learns "release my
// stalled slots" / "whole kernel done, go idle" via `barrier_signal_in` -
// see barrierCore's own header comment for the full design.
//
// `start` is `bool&`, not `bool` (SS16.6, found while testing this on its
// own std::thread): a plain by-value bool can never change for the life
// of one call, so once the kernel completes the loop immediately re-enters
// IDLE, sees the same fixed `start==true`, and relaunches forever - a
// permanently hot-spinning thread, confirmed to deterministically crash
// the whole test binary (SIGSEGV, 10/10 runs) once static destructors run
// at process exit while it's still actively touching its captured static
// objects. A reference lets a caller clear `start` once `busy` (now
// barrierCore's) is observed - matching the real host-clears-start-once-
// device-is-busy protocol (docs/hls/interfaces.md SS2.5.6) more faithfully
// than a fixed value ever could. gpgpu_scheduler's own `start` stays a
// plain `bool` parameter (its real, s_axilite-mapped hardware port shape,
// unaffected - schedulerCore is never itself `set_top`, only called from
// within gpgpu_scheduler, so this is purely an internal-helper signature
// choice).
inline void programLoader(
    instr_word_t* program_ptr,
    uint32_t      program_len,
    bool&         start,
    CuDispatchUnit (&cus)[NUM_CUS]
) {
    bool     loaded   = false;
    uint32_t load_idx = 0;

    while (true) {
#pragma HLS PIPELINE off

        if (!start) {
            loaded   = false;
            load_idx = 0;
            continue;
        }

        if (!loaded) {
            if (load_idx < MAX_PROGRAM_LEN) {
                if (load_idx < program_len) {
                    instr_word_t instr = program_ptr[load_idx];
LOAD_PROGRAM_WORDS:
                    for (int c = 0; c < NUM_CUS; ++c) {
#pragma HLS UNROLL
                        instr_word_t (&program_c)[MAX_PROGRAM_LEN] = cus[c].programArray();
                        program_c[load_idx] = instr;
                    }
                }
                ++load_idx;
            } else {
                loaded = true;
            }
        }
    }
}

#if RISCV_GPGPU_NUM_CUS > 8
// Hierarchical variant: loads program into two equal-size cluster arrays.
inline void programLoaderHier(
    instr_word_t* program_ptr,
    uint32_t      program_len,
    bool&         start,
    CuDispatchUnit (&cu_a)[CLUSTER_SIZE],
    CuDispatchUnit (&cu_b)[CLUSTER_SIZE]
) {
    bool     loaded   = false;
    uint32_t load_idx = 0;

    while (true) {
#pragma HLS PIPELINE off

        if (!start) {
            loaded   = false;
            load_idx = 0;
            continue;
        }

        if (!loaded) {
            if (load_idx < MAX_PROGRAM_LEN) {
                if (load_idx < program_len) {
                    instr_word_t instr = program_ptr[load_idx];
LOAD_WORDS_HIER:
                    for (int c = 0; c < CLUSTER_SIZE; ++c) {
#pragma HLS UNROLL
                        cu_a[c].programArray()[load_idx] = instr;
                        cu_b[c].programArray()[load_idx] = instr;
                    }
                }
                ++load_idx;
            } else {
                loaded = true;
            }
        }
    }
}
#endif  // RISCV_GPGPU_NUM_CUS > 8
inline void schedulerCore(
    CuDispatchUnit& cu,
    cu_id_t         cu_id,
    uint32_t        program_len,
    warp_id_t       total_warps,
    bool&           start,
    hls::stream<warp_dispatch_t>& dispatch_out,
    hls::stream<warp_status_t>&   status_in,
    hls::stream<WarpStatusCode>&   barrier_events_out,
    hls::stream<barrier_signal_t>& barrier_signal_in,
    warp_id_t       warp_id_offset = 0
) {
    WarpSlot      slots[MAX_WARPS_PER_CU];
    bool          busy_cu_scheduler = false;   // this CU's own IDLE/RUNNING
    bool          busy_cu           = false;   // waiting for a dispatched warp's result

    while (true) {
#pragma HLS PIPELINE off
        if (!busy_cu_scheduler) {
            // IDLE. Same open synthesis-semantics caveat T024's other
            // scalar-port pragmas already carry (SS8): a free-running
            // kernel reading a scalar s_axilite port on every iteration is
            // provisional until checked against real vitis_hls, not
            // assumed correct here.
            if (!start) continue;
            // Matches barrierCore's own barrierLaunchFault() decision
            // exactly (SS16.37's header comment) - a pure, stateless
            // function of total_warps every CU independently recomputes
            // the same way, so a faulted launch never causes this CU to
            // dispatch warps barrierCore itself refused to accept.
            if (total_warps > warp_id_t(NUM_CUS * MAX_WARPS_PER_CU)) continue;

            launchSlots(slots, cu_id, total_warps, warp_id_offset);
            busy_cu = false;

            busy_cu_scheduler = true;
        } else {
            // RUNNING: one scheduling round per pass.
            if (!busy_cu) {
                slot_id_t slot = nextReadySlot(slots);
                if (slot != INVALID_SLOT) {
                    dispatch_out.write(buildDispatch(slots, slot));
                    busy_cu = true;
                }
            }
            if (!status_in.empty()) {
                warp_status_t st = status_in.read();
                recordResult(slots, st.slot_id, st);
                barrier_events_out.write(st.code);
                busy_cu = false;
            }

            if (!barrier_signal_in.empty()) {
                barrier_signal_t sig = barrier_signal_in.read();
                if (sig.release)     releaseBarrierSlots(slots);
                if (sig.kernel_done) busy_cu_scheduler = false;
            }
        }
    }
}

// The one real top-level "compute" IP (SS15/SS16.6): schedulerCore +
// compute_pipeline + mem_arbiter, merged into a single DATAFLOW region -
// all three are now the same shape (one free-running process, own local
// state, streams for everything crossing a process boundary).
//
// Declared here, DEFINED in gpgpu_top.cpp (not `inline` in this header,
// unlike schedulerCore above) - an `inline` header-only function is only
// emitted if something ODR-uses it, and nothing does (this IS the top -
// nothing calls it). `set_top` needs a real, always-emitted symbol, same
// reason compute_pipeline/memory_pipeline have always been declared in a
// .h and defined in a .cpp rather than header-only.
//
// ── T076 Multi-device support via warp_id_offset ─────────────────────────────
// `gpgpu_scheduler` is a SINGLE-DEVICE HLS IP. It orchestrates up to
// NUM_CUS compute units within one FPGA device (KV260 target). The
// `warp_id_offset` s_axilite parameter enables host-side multi-device
// composition: device d is configured with warp_id_offset = sum of warps
// assigned to devices 0..d-1, so global warp IDs are non-overlapping
// across devices. `total_warps` is device-local (warps assigned to THIS
// device only). See hls/src/system_top/system_top.h for SystemTopHLS,
// the C++ coordinator that implements the warp split algorithm and
// mirrors models/systemc/src/system_top/system_top.h API.
//
// `initial_regs_ptr` is still global-warp-id-indexed (SS16): the host
// allocates a single buffer covering all global warps; each device reads
// only its own subset (identified by warp_id including offset).
// See docs/hls/interfaces.md §17.1 for the complete scope contract.
// ─────────────────────────────────────────────────────────────────────────────
void gpgpu_scheduler(
    instr_word_t* program_ptr,

    reg_t* initial_regs_ptr0,
    reg_t* initial_regs_ptr1,

    uint32_t      program_len,
    warp_id_t     total_warps,
    warp_id_t     warp_id_offset,
    bool          start,
    bool&         busy,
    bool&         done,
    bool&         fault,
    hls::stream<mem_req_t>&  mem_req_out,
    hls::stream<mem_resp_t>& mem_resp_in
);

}  // namespace riscv_gpgpu_hls

#endif  // RISCV_GPGPU_HLS_GPGPU_TOP_H
